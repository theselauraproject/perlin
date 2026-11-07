#pragma once
#include <string_view>

namespace perlin {
    inline constexpr std::string_view kClientControlPage = R"HTML(<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Perlin local control</title>
<style>
body{margin:24px;background:#14171c;color:#edf0f4;font:15px system-ui}main{max-width:1280px;margin:auto}
img{display:block;width:100%;aspect-ratio:16/9;background:#090b0e;object-fit:contain;outline:none}
img:focus{box-shadow:0 0 0 2px #69b9ff}form{display:flex;gap:8px;margin:16px 0}input{flex:1}
button,input{font:inherit;padding:8px;background:#252c35;color:inherit;border:1px solid #566170;border-radius:4px}
pre{white-space:pre-wrap;overflow-wrap:anywhere}p{color:#b9c4d1}
</style><main><h1>Perlin local control</h1>
<p>Click the game to focus. WASD moves, Space jumps, Shift sneaks, Control sprints.
Hold the right mouse button and drag to look. Left click attacks, R uses an item,
E opens inventory, Q drops, and 1–9 selects a slot. Menus accept clicks and typing.
Movement stops when the tab loses focus.</p>
<img id="game" tabindex="0" alt="Waiting for the hidden client's rendered frame">
<form id="commands"><input id="command" aria-label="Server command" placeholder="/time set day"><button>Send command</button><button type="button" id="release">Release input</button></form>
<pre id="status">Connecting…</pre><pre id="error" role="alert"></pre>
</main><script>
const game=document.querySelector('#game'),status=document.querySelector('#status'),error=document.querySelector('#error');
const bindings={KeyW:'forward',KeyS:'back',KeyA:'left',KeyD:'right',Space:'jump',ShiftLeft:'sneak',ShiftRight:'sneak',ControlLeft:'sprint',ControlRight:'sprint',KeyE:'inventory',KeyQ:'drop',KeyR:'use',MouseLeft:'attack',MouseMiddle:'pickItem'};
for(let i=1;i<=9;i++)bindings['Digit'+i]='hotbar.'+i;
const held=new Set();let state={},frameUrl,looking=false,yaw=0,pitch=0,lookingDirty=false,pointer;
let requests=Promise.resolve();
const token='__PERLIN_TOKEN__',auth={'X-Perlin-Token':token};history.replaceState(null,'','/');
function send(command){const result=requests.then(async()=>{const r=await fetch('/command',{method:'POST',headers:{...auth,'Content-Type':'application/json'},body:JSON.stringify(command)});if(!r.ok)throw Error('Command rejected: '+r.status);return r.json()});requests=result.catch(()=>{});return result}
function failure(e){error.textContent=String(e)}
async function release(){held.clear();looking=false;lookingDirty=false;await send({op:'release'})}
function refreshInput(){return send({op:'input',actions:[...new Set([...held].map(k=>bindings[k]))],ticks:6})}
function keyCode(e){if(/^Key[A-Z]$/.test(e.code))return e.code.charCodeAt(3);if(/^Digit[0-9]$/.test(e.code))return e.code.charCodeAt(5);if(/^F([1-9]|1[0-2])$/.test(e.code))return 111+Number(e.code.slice(1));return {Backspace:8,Tab:9,Enter:13,ShiftLeft:16,ShiftRight:16,ControlLeft:17,ControlRight:17,AltLeft:18,AltRight:18,Escape:27,Space:32,PageUp:33,PageDown:34,End:35,Home:36,ArrowLeft:37,ArrowUp:38,ArrowRight:39,ArrowDown:40,Delete:46,Slash:191}[e.code]}
game.addEventListener('keydown',e=>{e.preventDefault();if(!state.textInput&&bindings[e.code]){held.add(e.code);refreshInput().catch(failure)}else{const key=keyCode(e);if(key!==undefined)send({op:'key',key,down:true}).catch(failure);if(state.textInput&&[...e.key].length===1&&!e.ctrlKey&&!e.metaKey&&!e.altKey)send({op:'character',key:e.key.codePointAt(0)}).catch(failure)}});
window.addEventListener('keyup',e=>{if(held.delete(e.code))refreshInput().catch(failure);if(document.activeElement===game){const key=keyCode(e);if(key!==undefined)send({op:'key',key,down:false}).catch(failure)}});
window.addEventListener('blur',()=>release().catch(failure));
game.addEventListener('blur',()=>release().catch(failure));
document.addEventListener('visibilitychange',()=>{if(document.hidden)release().catch(failure)});
game.addEventListener('contextmenu',e=>e.preventDefault());
function point(e){const r=game.getBoundingClientRect();return {x:(e.clientX-r.left)/r.width*(game.naturalWidth||1280),y:(e.clientY-r.top)/r.height*(game.naturalHeight||720)}}
game.addEventListener('mousedown',e=>{e.preventDefault();game.focus();if(state.cursorCaptured){if(e.button===2){looking=true;yaw=state.yaw||0;pitch=state.pitch||0}else{held.add(e.button===0?'MouseLeft':'MouseMiddle');refreshInput().catch(failure)}}else send({op:'mouse',...point(e),button:[0,2,1][e.button]||0,down:true}).catch(failure)});
window.addEventListener('mouseup',e=>{looking=false;if(held.delete(e.button===0?'MouseLeft':'MouseMiddle'))refreshInput().catch(failure);if(document.activeElement===game)send({op:'mouse',...point(e),button:[0,2,1][e.button]||0,down:false}).catch(failure)});
game.addEventListener('mousemove',e=>{if(!state.cursorCaptured)pointer=point(e)});
window.addEventListener('mousemove',e=>{if(looking){yaw+=e.movementX*.15;pitch=Math.max(-89,Math.min(89,pitch-e.movementY*.15));lookingDirty=true}});
game.addEventListener('wheel',e=>{e.preventDefault();send({op:'scroll',y:-Math.sign(e.deltaY)}).catch(failure)},{passive:false});
document.querySelector('#release').onclick=()=>release().catch(failure);
document.querySelector('#commands').onsubmit=e=>{e.preventDefault();send({op:'command',text:document.querySelector('#command').value}).catch(failure)};
async function poll(){try{
 state=await fetch('/state',{headers:auth}).then(r=>{if(!r.ok)throw Error('State: '+r.status);return r.json()});
 status.textContent=JSON.stringify(state,null,2);
 if(held.size)await refreshInput();
 else if(poll.wasHeld)await send({op:'release'});
 poll.wasHeld=held.size>0;
 if(lookingDirty){lookingDirty=false;await send({op:'look',yaw,pitch})}
 if(pointer){const p=pointer;pointer=undefined;await send({op:'pointer',...p})}
 }catch(e){failure(e)}setTimeout(poll,100)}
async function frame(){try{const r=await fetch('/frame',{headers:auth});if(r.ok&&r.status!==202){const next=URL.createObjectURL(await r.blob());game.src=next;game.alt='Hidden Perlin rendered frame';if(frameUrl)URL.revokeObjectURL(frameUrl);frameUrl=next}}catch(e){failure(e)}setTimeout(frame,250)}
poll();frame();
</script></html>)HTML";
}
