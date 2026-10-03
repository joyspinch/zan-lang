let ws;
function log(s){const l=document.getElementById('log');l.textContent+=s+"\n";l.scrollTop=l.scrollHeight;}
function conn(){ws=new WebSocket(document.getElementById('url').value);
  ws.onopen=()=>log('[open]');ws.onclose=()=>log('[close]');
  ws.onmessage=e=>log(e.data);ws.onerror=()=>log('[error]');}
function send(){const m=document.getElementById('msg');if(ws&&ws.readyState===1){ws.send(m.value);m.value='';}}
