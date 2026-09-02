/* flow-bell.js — 顶栏通知铃铛 + 实时通知流。
   数据通道：WebSocket 长连接 /admin/flow/notifies/stream（20s 文本心跳续期，
   断开自动重连，连续失败回落 EventSource/SSE）；菜单内点击「前往」走既有 data-tab 标签加载。 */
(function () {
  'use strict';

  var bell = document.querySelector('[data-bell]');
  if (!bell) { return; }
  var btn = bell.querySelector('[data-bell-btn]');
  var menu = bell.querySelector('[data-bell-menu]');
  var badge = bell.querySelector('[data-bell-badge]');
  var list = bell.querySelector('[data-bell-list]');

  function esc(s) {
    return String(s == null ? '' : s)
      .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }

  function setBadge(n) {
    if (!badge) { return; }
    if (n > 0) {
      badge.textContent = n > 99 ? '99+' : String(n);
      badge.hidden = false;
    } else {
      badge.hidden = true;
    }
  }

  function renderItems(items) {
    if (!list) { return; }
    if (!items || !items.length) {
      list.innerHTML = '<div class="ad-bell-empty">暂无未读</div>';
      return;
    }
    var html = '';
    for (var i = 0; i < items.length; i++) {
      html += '<a class="ad-bell-item" href="' + esc(items[i].link || '/admin/flow/notifies')
        + '" data-tab data-title="通知">' + esc(items[i].content) + '</a>';
    }
    list.innerHTML = html;
  }

  function open() {
    if (!menu) { return; }
    menu.hidden = false;
    fetch('/admin/flow/notifies/unread', { credentials: 'same-origin' })
      .then(function (r) { return r.json(); })
      .then(function (j) {
        renderItems(j.items || []);
        setBadge(j.count || 0);
      }).catch(function () {});
  }

  function close() { if (menu) { menu.hidden = true; } }

  btn.addEventListener('click', function (ev) {
    ev.stopPropagation();
    if (menu.hidden) { open(); } else { close(); }
  });
  document.addEventListener('click', function (ev) {
    if (!bell.contains(ev.target)) { close(); }
  });

  /* 新通知立即入列并让角标 +1——WS 与 SSE 两条通道共用。 */
  function onNotify(n) {
    var cur = badge && !badge.hidden ? parseInt(badge.textContent, 10) || 0 : 0;
    setBadge(cur + 1);
    if (menu && !menu.hidden && list) {
      var empty = list.querySelector('.ad-bell-empty');
      if (empty) { empty.remove(); }
      var a = document.createElement('a');
      a.className = 'ad-bell-item';
      a.href = esc(n.link || '/admin/flow/notifies');
      a.setAttribute('data-tab', '');
      a.setAttribute('data-title', '通知');
      a.textContent = n.content || '';
      list.insertBefore(a, list.firstChild);
    }
  }

  /* SSE 回退：EventSource 自连，按事件名分派。 */
  function listenSse() {
    try {
      var es = new EventSource('/admin/flow/notifies/stream');
      es.addEventListener('notify', function (ev) {
        try { onNotify(JSON.parse(ev.data)); } catch (e) { /* 忽略单帧格式错误 */ }
      });
      es.addEventListener('unread', function (ev) {
        try { setBadge(JSON.parse(ev.data).count || 0); } catch (e) {}
      });
      es.onerror = function () { /* EventSource 自连 */ };
    } catch (e) { /* 浏览器过旧时静默降级为手动刷新 */ }
  }

  /* WS 主通道：type 字段分派（notify/unread）；20s 文本心跳让服务端 Recv
     唤醒即续期（不发会被 30s 连接扫描器掐断）；断开 3s 后重连，连续 3 次
     连不上则永久回落 SSE。服务端 30 分钟收口（Close 1000）属正常换线。 */
  function listenWs() {
    var ws = null;
    var hb = null;
    var fails = 0;

    function stop() {
      if (hb) { clearInterval(hb); hb = null; }
      if (ws) { try { ws.close(); } catch (e) {} ws = null; }
    }

    function connect() {
      try {
        var proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
        ws = new WebSocket(proto + location.host + '/admin/flow/notifies/stream');
      } catch (e) { return listenSse(); }
      ws.onopen = function () { fails = 0; };
      ws.onmessage = function (ev) {
        try {
          var m = JSON.parse(ev.data);
          if (m.type === 'notify') { onNotify(m); }
          else if (m.type === 'unread') { setBadge(m.count || 0); }
        } catch (e) { /* 忽略单帧格式错误 */ }
      };
      ws.onclose = function () {
        if (hb) { clearInterval(hb); hb = null; }
        if (fails >= 3) { return listenSse(); }
        fails = fails + 1;
        setTimeout(connect, 3000);
      };
      ws.onerror = function () { try { ws.close(); } catch (e) {} };
      hb = setInterval(function () {
        if (ws && ws.readyState === 1) { ws.send('ping'); }
      }, 20000);
    }

    connect();
  }

  listenWs();
})();
