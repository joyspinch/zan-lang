/* chat.js — IM 实时点亮（T13 + 已读/送达回执）：监听 bell.js 在 WS/SSE
   onmessage 处广播的 zan-rt 自定义事件。消费三类帧：
   * type="message"——Chat 页对端来信追加消息行并滚到底，且打开中的会话
     即调 /read 标已读（服务端顺带置送达位）；在其他页面/其他会话收到则
     调 /ack 确认送达（回执端点只按收件人维度落库，幂等）。
   * type="ack"/"read"——我发消息的回执：对端确认送达点亮「已送达」，
     对端已读点亮「已读」（read 帧带「读到为止」的 msgId，≤ 该 id 全点亮）。
   发送链路不碰（Send 仍走表单 Saved→reload，服务端落库为准）；状态最终
   以落库为准，实时帧只是在线期间的增量点亮。
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

  /* ---- 回执打点（POST 端点均服务端校验收件人维度，重复调用幂等） ---- */

  function post(url, params) {
    try {
      fetch(url, {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams(params).toString()
      }).catch(function () { /* 离线等失败忽略，落库状态兜底 */ });
    } catch (e) { /* 老浏览器无 fetch 时静默降级 */ }
  }

  /* 送达：收帧即确认（不依赖打开会话）。 */
  function ack(id) {
    post('/admin/oa/messages/ack', { id: String(id == null ? '' : id) });
  }

  /* 已读：打开中的会话收到新消息（服务端把该会话未读全清并置送达位）。 */
  function readUp(peerId) {
    post('/admin/oa/messages/read', { peer: String(peerId) });
  }

  /* 点亮我某条消息的回执标记。 */
  function markBubble(box, msgId, text) {
    var row = box.querySelector('[data-msg="' + String(msgId) + '"][data-mine="1"] [data-mark]');
    if (row) { row.textContent = text; }
  }

  /* 点亮 ≤ upTo 的我方消息（read 帧「读到为止」语义）。 */
  function markUpTo(box, upTo, text) {
    var rows = box.querySelectorAll('[data-msg][data-mine="1"]');
    var to = parseInt(upTo, 10) || 0;
    for (var i = 0; i < rows.length; i++) {
      var id = parseInt(rows[i].getAttribute('data-msg'), 10) || 0;
      if (id > 0 && id <= to) {
        var el = rows[i].querySelector('[data-mark]');
        if (el) { el.textContent = text; }
      }
    }
  }

  function onFrame(m) {
    if (!m) { return; }
    if (m.type === 'message') {
      var from = String(m.fromId == null ? '' : m.fromId);
      if (!from) { return; }
      var box = document.querySelector('[data-chat-peer]');
      if (box && from === box.getAttribute('data-chat-peer')) {
        appendRow(box, m);
        readUp(from);
        return;
      }
      if (document.querySelector('tr[data-peer]')) { bumpRow(m); }
      ack(m.id);
      return;
    }
    if (m.type === 'ack') {
      var b1 = document.querySelector('[data-chat-peer]');
      if (b1) { markBubble(b1, m.msgId, ' · 已送达'); }
      return;
    }
    if (m.type === 'read') {
      var b2 = document.querySelector('[data-chat-peer]');
      if (b2 && String(m.by) === b2.getAttribute('data-chat-peer')) {
        markUpTo(b2, m.msgId, ' · 已读');
      }
    }
  }

  document.addEventListener('zan-rt', function (ev) {
    try { onFrame(ev.detail); } catch (e) { /* 单帧异常不碍通道 */ }
  });
})();
