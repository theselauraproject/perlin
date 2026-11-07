import json,re
def load(path):
    t=open(path,encoding='utf8').read()
    out=[];i=0;n=len(t);instr=False
    while i<n:
        c=t[i]
        if instr:
            out.append(c)
            if c=='\\': out.append(t[i+1]); i+=1
            elif c=='"': instr=False
        elif c=='"': instr=True; out.append(c)
        elif c=='/' and i+1<n and t[i+1]=='/':
            while i<n and t[i]!='\n': i+=1
            continue
        else: out.append(c)
        i+=1
    s=''.join(out)
    s=re.sub(r',(\s*[}\]])',r'\1',s)
    return json.loads(s)
