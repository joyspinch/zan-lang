const esc = s => String(s).replace(/[&<>]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
async function j(u){ const r = await fetch(u); return (await r.json()).data; }
function rows(el, arr, cols, empty){
  const b = document.getElementById(el);
  if(!arr || !arr.length){ b.innerHTML = '<tr><td colspan="'+cols.length+'" class="empty">'+empty+'</td></tr>'; return; }
  b.innerHTML = arr.map(o => '<tr>'+cols.map(c => '<td>'+esc(o[c] ?? '')+'</td>').join('')+'</tr>').join('');
}
async function tick(){
  try{
    const m = await j('/iot/metrics');
    const cards = [
      ['在线客户端', m.clients_connected], ['累计连接', m.clients_total],
      ['订阅数', m.subscriptions], ['主题数', m.topics],
      ['收到消息', m.messages_in], ['转发消息', m.messages_out],
      ['入流量B', m.bytes_in], ['出流量B', m.bytes_out],
      ['运行秒', m.uptime_sec]
    ];
    document.getElementById('metrics').innerHTML = cards.map(
      c => '<div class="card"><div class="k">'+c[0]+'</div><div class="v">'+c[1]+'</div></div>').join('');
    rows('clients', await j('/iot/clients'),
      ['id','client_id','addr','subs','msgs_in','msgs_out'], '暂无客户端');
    rows('subs', await j('/iot/subscriptions'),
      ['client_id','filter','qos'], '暂无订阅');
    rows('topics', await j('/iot/topics'),
      ['topic','messages','last'], '暂无主题');
  }catch(e){ /* broker starting */ }
}
async function publish(){
  const t = document.getElementById('pt').value.trim();
  const p = document.getElementById('pp').value;
  const tok = document.getElementById('tok').value.trim();
  const res = document.getElementById('pres');
  if(!t){ res.textContent = 'topic required'; return; }
  const h = {'Content-Type':'application/x-www-form-urlencoded'};
  if(tok) h['Authorization'] = 'Bearer '+tok;
  const r = await fetch('/iot/publish', {method:'POST', headers:h,
    body:'topic='+encodeURIComponent(t)+'&payload='+encodeURIComponent(p)});
  const d = await r.json();
  res.textContent = r.ok ? ('delivered '+(d.data ? d.data.delivered : 0)) : (d.msg || 'error');
  tick();
}
tick(); setInterval(tick, 2000);
