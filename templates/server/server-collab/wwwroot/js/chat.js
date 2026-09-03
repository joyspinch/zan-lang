/* chat.js — IM 实时点亮（T13）：监听 bell.js 在 WS/SSE onmessage 处广播的
   zan-rt 自定义事件，只消费 type="message" 帧，不碰发送链路（Send 仍走表单
   Saved→reload，服务端落库为准）。职责边界：本文件只做「无刷新呈现」——
   Chat 页对端来信追加消息行并滚到底；会话列表页更新对端行未读徽标与摘要；
   未读数与已读状态一律以落库为准（已读回执按落库正确性非实时）。
   页面检测在每帧到达时现查 DOM（data-tab 切页不刷新文档，缓存会失联）。 */
(function () {
  'use strict';

  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  /* 服务端 Fmt.Stamp 同款口径（本地时区，秒级截到分）。 */
  function stamp(sec) {
    var d = new Date(parseInt(sec, 10) * 1000);
    if (isNaN(d.getTime())) { return ''; }
    function p(n) { return n < 10 ? '0' + n : '' + n; }
    return d.getFullYear() + '-' + p(d.getMonth() + 1) + '-' + p(d.getDate())
      + ' ' + p(d.getHours()) + ':' + p(d.getMinutes());
  }

  /* Chat 页：追加一条对端来信（DOM 结构对齐 Messages.Chat.html 服务端行）。 */
  function appendRow(box, m) {
    var scroll = box.querySelector('[data-chat-scroll]');
    if (!scroll) { return; }
    var emptyEl = scroll.querySelector('.empty');
    if (emptyEl) { emptyEl.remove(); }
    var wrap = document.createElement('div');
    wrap.style.cssText = 'display:flex;margin:4px 0';
    var bubble = document.createElement('div');
    bubble.style.cssText = 'max-width:72%;padding:6px 10px;border-radius:8px;'
      + 'border:1px solid #eef0f3';
    var meta = document.createElement('div');
    meta.className = 'muted';
    meta.style.fontSize = '11px';
    meta.textContent = (box.getAttribute('data-chat-name') || '对方')
      + ' · ' + stamp(m.createdAt);
    var body = document.createElement('div');
    body.style.cssText = 'white-space:pre-wrap;line-height:1.6';
    body.textContent = m.content || '';
    bubble.appendChild(meta);
    bubble.appendChild(body);
    wrap.appendChild(bubble);
    scroll.appendChild(wrap);
    scroll.scrollTop = scroll.scrollHeight;
  }

  /* 会话列表页：更新对端行未读徽标（+1 落库口径旁路，仅本次在线增量）
     与最近消息摘要（对齐 Messages.Index.html 服务端单元格结构）。 */
  function bumpRow(m) {
    var tr = document.querySelector('tr[data-peer="' + String(m.fromId) + '"]');
    if (!tr) { return; }
    var cell = tr.querySelector('[data-unread]');
    if (cell) {
      var n = (parseInt(cell.textContent, 10) || 0) + 1;
      cell.innerHTML = '<span class="tag bad">' + n + '</span>';
    }
    var last = tr.querySelector('[data-last]');
    if (last) {
      last.innerHTML = '<span class="muted" style="font-size:12px">'
        + esc(stamp(m.createdAt)) + '</span> ' + esc(m.excerpt || '');
    }
  }

  function onFrame(m) {
    if (!m || m.type !== 'message') { return; }
    var from = String(m.fromId == null ? '' : m.fromId);
    if (!from) { return; }
    var box = document.querySelector('[data-chat-peer]');
    if (box && from === box.getAttribute('data-chat-peer')) {
      appendRow(box, m);
      return;
    }
    if (document.querySelector('tr[data-peer]')) { bumpRow(m); }
  }

  document.addEventListener('zan-rt', function (ev) {
    try { onFrame(ev.detail); } catch (e) { /* 单帧异常不碍通道 */ }
  });
})();
