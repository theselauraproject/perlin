from pathlib import Path
import struct,json,re,collections,hashlib,uuid
class PDB:
 def __init__(self,path):
  self.path=path;self.b=Path(path).read_bytes();b=self.b
  if not b.startswith(b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\x00\x00\x00'):raise ValueError('Not MSF7 PDB')
  self.bs,_,_,ds,_,bm=struct.unpack_from('<6I',b,32);bs=self.bs
  ids=struct.unpack_from('<'+str((ds+bs-1)//bs)+'I',b,bm*bs)
  d=b''.join(b[i*bs:(i+1)*bs] for i in ids)[:ds];n=struct.unpack_from('<I',d)[0];sizes=struct.unpack_from('<'+str(n)+'I',d,4);cur=4+4*n
  self.streams=[]
  for size in sizes:
   np=0 if size==0xffffffff else (size+bs-1)//bs;pages=struct.unpack_from('<'+str(np)+'I',d,cur) if np else ();cur+=4*np
   self.streams.append(b''.join(b[i*bs:(i+1)*bs] for i in pages)[:size])
  info=self.streams[1];self.guid=str(uuid.UUID(bytes_le=info[12:28]));self.age=struct.unpack_from('<I',info,8)[0]
  tpi=self.streams[2];version,hs,first,end,recordbytes=struct.unpack_from('<5I',tpi);self.records={};pos=hs
  for ix in range(first,end):
   n,kind=struct.unpack_from('<HH',tpi,pos);self.records[ix]=(kind,tpi[pos+4:pos+2+n]);pos+=n+2
  self.classes={}
  for ix,(kind,rec) in self.records.items():
   if kind in (0x1504,0x1505,0x1506):
    count,props,fields=struct.unpack_from('<HHI',rec)
    size,cur=number(rec,8 if kind==0x1506 else 16);name,cur=cstr(rec,cur)
    if not(props&0x80):self.classes[name]={'name':name,'type_index':ix,'size':size,'fieldlist':fields,'member_count':count}
   elif kind==0x1507:
    count,props,underlying,fields=struct.unpack_from('<HHII',rec);name,_=cstr(rec,12)
    if not(props&0x80):self.classes[name]={'name':name,'type_index':ix,'fieldlist':fields,'underlying':underlying,'member_count':count,'enum':True}
 def typename(self,ix,depth=0):
  basic={0x3:'void',0x10:'char',0x20:'unsigned char',0x11:'short',0x21:'unsigned short',0x12:'long',0x22:'unsigned long',0x13:'long long',0x23:'unsigned long long',0x30:'bool',0x40:'float',0x41:'double',0x70:'char',0x71:'wchar_t',0x72:'int16_t',0x73:'uint16_t',0x74:'int32_t',0x75:'uint32_t',0x76:'int64_t',0x77:'uint64_t',0x7a:'char16_t',0x7b:'char32_t'}
  if ix<0x1000:return basic.get(ix,f'basic_0x{ix:04x}')
  if depth>6 or ix not in self.records:return f'type_0x{ix:x}'
  kind,rec=self.records[ix]
  if kind in (0x1504,0x1505,0x1506):
   _,cur=number(rec,8 if kind==0x1506 else 16);return cstr(rec,cur)[0]
  if kind==0x1507:return cstr(rec,12)[0]
  if kind==0x1001:
   ref,flags=struct.unpack_from('<IH',rec);return ('const ' if flags&1 else '')+('volatile ' if flags&2 else '')+self.typename(ref,depth+1)
  if kind==0x1002:
   ref,attrs=struct.unpack_from('<II',rec);mode=(attrs>>5)&7;return self.typename(ref,depth+1)+(' &' if mode==1 else ' &&' if mode==4 else ' *')
  if kind==0x1503:
   ref,index=struct.unpack_from('<II',rec);size,_=number(rec,8);return self.typename(ref,depth+1)+f' [bytes={size}]'
  return f'type_0x{ix:x}_kind_0x{kind:x}'
 def fields(self,ix,seen=None):
  seen=set() if seen is None else seen
  if not ix or ix in seen:return []
  seen.add(ix);kind,rec=self.records[ix]
  if kind!=0x1203:return [{'unparsed_kind':hex(kind)}]
  fields=[];cur=0
  while cur<len(rec):
   if rec[cur]>=0xf0:cur+=rec[cur]&15;continue
   at=cur;k=struct.unpack_from('<H',rec,cur)[0];cur+=2
   try:
    if k==0x150d:
     attr,t=struct.unpack_from('<HI',rec,cur);cur+=6;offset,cur=number(rec,cur);name,cur=cstr(rec,cur);fields.append({'name':name,'offset':offset,'type':self.typename(t),'type_index':hex(t),'member_kind':'instance'})
    elif k==0x150e:
     attr,t=struct.unpack_from('<HI',rec,cur);cur+=6;name,cur=cstr(rec,cur);fields.append({'name':name,'type':self.typename(t),'type_index':hex(t),'member_kind':'static'})
    elif k==0x1502:
     attr=struct.unpack_from('<H',rec,cur)[0];cur+=2;value,cur=number(rec,cur);name,cur=cstr(rec,cur);fields.append({'name':name,'value':value,'member_kind':'enumerator'})
    elif k==0x1400:
     attr,t=struct.unpack_from('<HI',rec,cur);cur+=6;offset,cur=number(rec,cur);fields.append({'offset':offset,'type':self.typename(t),'member_kind':'base'})
    elif k in (0x1401,0x1402):
     attr,t,vb=struct.unpack_from('<HII',rec,cur);cur+=10;vbptr,cur=number(rec,cur);vboff,cur=number(rec,cur);fields.append({'type':self.typename(t),'member_kind':'virtual_base','vbptr_offset':vbptr,'vbase_offset':vboff})
    elif k==0x150f:
     count,t=struct.unpack_from('<HI',rec,cur);cur+=6;name,cur=cstr(rec,cur)
    elif k==0x1511:
     attr,t=struct.unpack_from('<HI',rec,cur);cur+=6
     if (attr>>2)&7 in (4,6):cur+=4
     name,cur=cstr(rec,cur)
    elif k in (0x1510,0x1512):
     attr,t=struct.unpack_from('<HI',rec,cur);cur+=6;name,cur=cstr(rec,cur)
    elif k==0x1409:cur+=6
    elif k==0x1404:
     ix2=struct.unpack_from('<I',rec,cur+2)[0];cur+=6;fields.extend(self.fields(ix2,seen))
    else:
     fields.append({'unparsed_kind':hex(k),'fieldlist_offset':at});break
   except (IndexError,struct.error,ValueError) as e:
    fields.append({'parse_error':str(e),'fieldlist_offset':at});break
  return fields
 def symbols(self):
  dbi=self.streams[3];si=struct.unpack_from('<H',dbi,20)[0];s=self.streams[si];pos=0
  while pos+4<=len(s):
   size,kind=struct.unpack_from('<HH',s,pos)
   if size<2 or pos+size+2>len(s):raise ValueError('invalid symbol record')
   rec=s[pos+4:pos+2+size];row={'kind':hex(kind),'record_offset':pos}
   if kind in (0x110e,0x110d,0x110c):
    flags,off,seg=struct.unpack_from('<IIH',rec);name,_=cstr(rec,10);row.update(name=name,section=seg,section_offset=hex(off))
   elif kind==0x1108:
    t=struct.unpack_from('<I',rec)[0];name,_=cstr(rec,4);row.update(name=name,type_index=hex(t))
   elif kind==0x1125:
    name,_=cstr(rec,10);row.update(name=name)
   if 'name' in row:yield row
   pos+=size+2

def cstr(b,cur):
 end=b.index(0,cur);return b[cur:end].decode('utf-8','replace'),end+1

def number(b,cur):
 value=struct.unpack_from('<H',b,cur)[0];cur+=2
 if value<0x8000:return value,cur
 formats={0x8000:'b',0x8001:'h',0x8002:'H',0x8003:'i',0x8004:'I',0x8009:'q',0x800a:'Q'}
 if value not in formats:raise ValueError(f'unsupported numeric leaf {value:x}')
 fmt=formats[value];return struct.unpack_from('<'+fmt,b,cur)[0],cur+struct.calcsize(fmt)

if __name__=='__main__':
 import argparse
 ap=argparse.ArgumentParser();ap.add_argument('pdb');ap.add_argument('--out',required=True);ap.add_argument('--exe');a=ap.parse_args();p=PDB(a.pdb);out=Path(a.out);out.mkdir(parents=True,exist_ok=True)
 selected=[]
 for name,row in p.classes.items():
  if re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*Packet(?:Payload|VariantPayload)?',name) or name in ['InputData','PlayerAuthInputData','PlayerAuthInputPacketPayload::InputData','PlayerPositionModeComponent::PositionMode','MovePlayerPacketPayload::PositionMode','CorrectPlayerMovePredictionPacketPayload::PredictionType']:
   row=dict(row);row['type_index']=hex(row['type_index']);row['fieldlist']=hex(row['fieldlist']);row['fields']=p.fields(int(row['fieldlist'],16));selected.append(row)
 symbols=[x for x in p.symbols() if 'Packet' in x.get('name','') or 'NetworkProtocolVersion' in x.get('name','')]
 handlers=[x for x in symbols if x['kind']=='0x110e' and x['name'].startswith('?handle@') and ('NetworkHandler@@' in x['name'] or 'NetEventCallback@@' in x['name'])]
 meta={'filename':Path(a.pdb).name,'size':len(p.b),'sha256':hashlib.sha256(p.b).hexdigest(),'guid':p.guid,'age':p.age,'stream_count':len(p.streams),'type_record_count':len(p.records),'packet_layout_count':len(selected),'packet_symbol_count':len(symbols),'handler_symbols_count':len(handlers),'warning':'PDB GUID/age have not been compared with an executable; section offsets and layouts belong only to this PDB build.'}
 if a.exe:
  from analyze_minecraft_static import PE
  pe=PE(Path(a.exe));matched=any(x['guid']==p.guid and x['age']==p.age for x in pe.metadata['codeview']);meta['compared_executable_sha256']=pe.metadata['sha256'];meta['exe_match']=matched;meta['warning']='C++ memory layout is not wire serialization order; full handler semantics are not established.' if matched else 'PDB GUID/age do not match this executable; do not apply PDB addresses or layouts to it.'
 (out/'pdb_metadata.json').write_text(json.dumps(meta,indent=2)+'\n');(out/'pdb_packet_layouts.json').write_text(json.dumps(sorted(selected,key=lambda r:r['name']),indent=2)+'\n');(out/'pdb_packet_symbols.json').write_text(json.dumps(symbols,indent=2)+'\n');(out/'pdb_handler_symbols.json').write_text(json.dumps(handlers,indent=2)+'\n')
 print(json.dumps(meta,indent=2));print('parsing_issues',sum(any('unparsed_kind' in f or 'parse_error' in f for f in row['fields']) for row in selected))
 for n in ['PlayerAuthInputPacketPayload','MovePlayerPacketPayload','CorrectPlayerMovePredictionPacketPayload','InputData','PlayerAuthInputData']:
  if n in p.classes:
   c=p.classes[n];print(n,json.dumps(p.fields(c['fieldlist']),indent=2))
