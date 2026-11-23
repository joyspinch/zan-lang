// 聊天页脚本：与 src/worker/ChatMessage.zan 定义的 JSON 协议一一对应。
// 页面由同一进程同一端口发出，所以 WebSocket 直接连当前 host；断线 2 秒后自动重连。
const $ = (id) => document.getElementById(id);
let ws = null;
let myName = '';

function connect() {
  const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
  ws = new WebSocket(proto + location.host + '/ws');
  ws.onopen = () => setOnline(true);
  ws.onclose = () => { setOnline(false); setTimeout(connect, 2000); };
  ws.onmessage = (e) => handle(JSON.parse(e.data));
}

function setOnline(on) {
  $('status').textContent = on ? '已连接' : '重连中…';
  $('status').className = 'status ' + (on ? 'on' : 'off');
  $('text').disabled = !on;
  $('send').disabled = !on;
  if (on) $('text').focus();
}

function handle(m) {
  switch (m.type) {
    case 'welcome': myName = m.name; $('nick').placeholder = m.name; break;
    case 'chat': addChat(m); break;
    case 'system': addLine('sys', m.time + ' · ' + m.text); break;
    case 'error': addLine('err', m.text); break;
    case 'users': renderUsers(m.users); break;
  }
}

function addChat(m) {
  const row = document.createElement('div');
  row.className = 'msg' + (m.from === myName ? ' mine' : '');
  const meta = document.createElement('span');
  meta.className = 'meta';
  meta.textContent = m.time + ' ' + m.from;
  const body = document.createElement('span');
  body.className = 'body';
  body.textContent = m.text;          // textContent 而非 innerHTML：防 XSS
  row.append(meta, body);
  append(row);
}

function addLine(cls, text) {
  const div = document.createElement('div');
  div.className = cls;
  div.textContent = text;
  append(div);
}

function append(el) {
  const log = $('log');
  const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 8;
  log.append(el);
  if (stick) log.scrollTop = log.scrollHeight;
}

function renderUsers(list) {
  $('count').textContent = list.length;
  $('users').replaceChildren(...list.map((n) => {
    const li = document.createElement('li');
    li.textContent = n;
    if (n === myName) li.className = 'me';
    return li;
  }));
}

function send(obj) {
  if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(obj));
}

$('sayForm').onsubmit = (e) => {
  e.preventDefault();
  const text = $('text').value.trim();
  if (text) send({ type: 'say', text });
  $('text').value = '';
};

$('nickForm').onsubmit = (e) => {
  e.preventDefault();
  const nick = $('nick').value.trim();
  if (nick) send({ type: 'nick', nick });
  $('nick').value = '';
};

connect();
