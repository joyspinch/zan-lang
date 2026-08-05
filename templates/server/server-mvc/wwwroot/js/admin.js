/* Admin shell behaviour: tabs, dialogs, writes and the live metric stream.
 *
 * The rules are all in the markup, so a screen is a plain template:
 *   <a data-tab href="/admin/system/users">      opens (or focuses) a tab
 *   <a data-load href="?page=2">          reloads the current panel in place
 *   <a data-dialog href="/admin/system/users/form?id=3">   opens a fragment as a modal
 *   <button data-post="/admin/system/users/status" data-args="id=3&status=0">
 *   <form data-submit action="/admin/system/users/save">   posts, closes, reloads
 *
 * A panel is fetched with X-Fragment: 1, which makes the server render the
 * screen without the shell. */
(function () {
  var panel, tabs, toasts;
  var state = { tabs: [], active: '' };
  var KEY = 'zanweb.admin.tabs';
  var stream = null;
  var ctxMenu = null;
  // Which sub-pane a screen last showed, keyed by its base path. A write
  // reloads the panel in place; without this the active pane would snap back
  // to the first one and drop the operator somewhere they did not choose.
  var paneMemo = {};

  function el(id) { return document.getElementById(id); }

  function toast(msg, kind) {
    var t = document.createElement('div');
    t.className = 'toast ' + (kind || 'ok');
    t.textContent = msg;
    toasts.appendChild(t);
    setTimeout(function () { t.remove(); }, 3200);
  }

  // ---- tabs ---------------------------------------------------------------

  function save() {
    try {
      localStorage.setItem(KEY, JSON.stringify(state));
    } catch (e) { /* private mode: tabs simply do not survive a reload */ }
  }

  function restore() {
    try {
      var raw = localStorage.getItem(KEY);
      if (raw) { state = JSON.parse(raw); }
    } catch (e) { state = { tabs: [], active: '' }; }
    if (!state.tabs) { state.tabs = []; }
  }

  function find(path) {
    for (var i = 0; i < state.tabs.length; i++) {
      if (state.tabs[i].path === path) { return i; }
    }
    return -1;
  }

  function paint() {
    tabs.innerHTML = '';
    state.tabs.forEach(function (t) {
      var b = document.createElement('span');
      b.className = 'ad-tab' + (t.path === state.active ? ' active' : '');
      b.title = t.path;
      var label = document.createElement('span');
      label.textContent = t.title || t.path;
      b.appendChild(label);
      if (state.tabs.length > 1) {
        var x = document.createElement('span');
        x.className = 'x';
        x.textContent = '×';
        x.onclick = function (ev) { ev.stopPropagation(); close(t.path); };
        b.appendChild(x);
      }
      b.onclick = function () { open(t.path, t.title); };
      b.oncontextmenu = function (ev) { ev.preventDefault(); tabMenu(ev, t); };
      tabs.appendChild(b);
    });
    document.querySelectorAll('.ad-side a.mi').forEach(function (a) {
      a.classList.toggle('active', a.getAttribute('href') === base(state.active));
    });
    save();
    showActiveTab();
    paintTabNav();
  }

  // The strip scrolls under two arrows rather than a scrollbar, so a long
  // session does not squash the tabs or leave the active one off-screen.
  function paintTabNav() {
    var bar = tabs.parentNode;
    if (!bar) { return; }
    var arrows = bar.querySelectorAll('[data-tabnav]');
    var over = tabs.scrollWidth > tabs.clientWidth + 1;
    for (var i = 0; i < arrows.length; i++) { arrows[i].hidden = !over; }
  }

  function showActiveTab() {
    var a = tabs.querySelector('.ad-tab.active');
    if (!a) { return; }
    var left = a.offsetLeft;
    var right = left + a.offsetWidth;
    if (left < tabs.scrollLeft) { tabs.scrollLeft = left - 8; }
    else if (right > tabs.scrollLeft + tabs.clientWidth) {
      tabs.scrollLeft = right - tabs.clientWidth + 8;
    }
  }

  function base(path) { return (path || '').split('?')[0]; }

  // Right-click on a tab: the usual workspace menu. Closing is deliberate --
  // a left click never closes anything but the tab's own ×.
  function tabMenu(ev, t) {
    hideTabMenu();
    var i = find(t.path);
    var m = document.createElement('div');
    m.className = 'ad-ctx';
    var items = [
      ['刷新', function () { open(t.path, t.title); }, false],
      ['关闭', function () { close(t.path); }, state.tabs.length < 2],
      ['关闭其他', function () { closeOthers(t.path); }, state.tabs.length < 2],
      ['关闭右侧', function () { closeRight(t.path); }, i >= state.tabs.length - 1],
      ['全部关闭', function () { closeAllTabs(); }, state.tabs.length < 2]
    ];
    items.forEach(function (it) {
      var n = document.createElement('div');
      n.className = 'item' + (it[2] ? ' off' : '');
      n.textContent = it[0];
      if (!it[2]) {
        n.onclick = function () { hideTabMenu(); it[1](); };
      }
      m.appendChild(n);
    });
    document.body.appendChild(m);
    var x = Math.min(ev.clientX, window.innerWidth - m.offsetWidth - 6);
    var y = Math.min(ev.clientY, window.innerHeight - m.offsetHeight - 6);
    m.style.left = Math.max(4, x) + 'px';
    m.style.top = Math.max(4, y) + 'px';
    ctxMenu = m;
  }

  function hideTabMenu() {
    if (ctxMenu) { ctxMenu.remove(); ctxMenu = null; }
  }

  function closeOthers(path) {
    state.tabs = state.tabs.filter(function (o) { return o.path === path; });
    var keep = state.tabs[0];
    open(keep.path, keep.title);
  }

  function closeRight(path) {
    var i = find(path);
    if (i < 0) { return; }
    state.tabs = state.tabs.slice(0, i + 1);
    if (find(state.active) < 0) { open(path, state.tabs[i].title); return; }
    paint();
  }

  // The last tab stays: an empty workspace has nothing to show.
  function closeAllTabs() {
    var home = { path: '/admin', title: '仪表盘' };
    state.tabs = [home];
    open(home.path, home.title);
  }

  function close(path) {
    var i = find(path);
    if (i < 0) { return; }
    state.tabs.splice(i, 1);
    if (state.active === path) {
      var next = state.tabs[Math.min(i, state.tabs.length - 1)];
      if (next) { open(next.path, next.title); return; }
      state.active = '';
    }
    paint();
  }

  function open(path, title) {
    var i = find(path);
    if (i < 0) { state.tabs.push({ path: path, title: title || path }); }
    state.active = path;
    paint();
    load(path);
  }

  // ---- panel --------------------------------------------------------------

  function load(path) {
    stopStream();
    panel.setAttribute('aria-busy', 'true');
    return fetch(path, { headers: { 'X-Fragment': '1' }, credentials: 'same-origin' })
      .then(function (r) {
        if (r.status === 401) { location.href = '/admin/login'; return ''; }
        var t = r.headers.get('X-Tab-Title');
        if (t) {
          var i = find(state.active);
          if (i >= 0) { state.tabs[i].title = decodeURIComponent(t); paint(); }
        }
        return r.text();
      })
      .then(function (html) {
        if (html === '') { return; }
        panel.innerHTML = html;
        panel.removeAttribute('aria-busy');
        panel.scrollTop = 0;
        history.replaceState(null, '', path);
        startStream();
        runScripts(panel);
        restorePane(path);
        wireModelPick(panel);
      })
      .catch(function () {
        panel.removeAttribute('aria-busy');
        toast('页面加载失败', 'bad');
      });
  }

  /* innerHTML does not execute <script>, and a screen may carry one. */
  function runScripts(root) {
    if (window.applyFragmentWidgets) { window.applyFragmentWidgets(root); }
    root.querySelectorAll('script').forEach(function (old) {
      var s = document.createElement('script');
      s.textContent = old.textContent;
      old.replaceWith(s);
    });
  }

  function reload() {
    if (state.active) { load(state.active); }
  }

  // Re-open the sub-pane this screen was on before its panel was rebuilt.
  function restorePane(path) {
    var name = paneMemo[base(path)];
    if (!name) { return; }
    var btn = panel.querySelector('[data-setting-tabs] [data-pane="' + name + '"]');
    if (btn) { btn.click(); }
  }

  // The AI model is fetched, not typed: given the base URL and key already on
  // the form, ask the provider what it offers and let the operator pick. The
  // key is only posted to our own endpoint, which uses it server-side and never
  // returns it. Providers with no /models endpoint keep the plain text field.
  function wireModelPick(root) {
    var input = root.querySelector('[name="ai.model"]');
    if (!input || input.getAttribute('data-model-wired')) { return; }
    input.setAttribute('data-model-wired', '1');
    var list = document.createElement('datalist');
    list.id = 'ai-model-options';
    input.setAttribute('list', list.id);
    input.parentNode.insertBefore(list, input.nextSibling);
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'btn sm';
    btn.textContent = '获取模型';
    btn.style.marginLeft = '8px';
    input.parentNode.insertBefore(btn, list.nextSibling);
    btn.addEventListener('click', function () {
      var url = root.querySelector('[name="ai.baseUrl"]');
      var key = root.querySelector('[name="ai.apiKey"]');
      if (!url || !url.value.trim()) { Layer.msg('请先填写接口地址'); return; }
      btn.disabled = true;
      btn.textContent = '获取中…';
      fetch('/admin/dev/ai/models', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ baseUrl: url.value.trim(),
                                    apiKey: key ? key.value : '' }).toString()
      }).then(function (r) { return r.json(); }).then(function (j) {
        var models = (j && j.code === '0000' && j.data && j.data.models) || [];
        if (!models.length) { Layer.notify('没有获取到模型', '确认接口地址和 Key 是否正确，或该服务不支持模型列表', 'bad'); return; }
        list.innerHTML = '';
        for (var i = 0; i < models.length; i++) {
          var o = document.createElement('option');
          o.value = models[i];
          list.appendChild(o);
        }
        if (!input.value) { input.value = models[0]; }
        input.focus();
        Layer.msg('已获取 ' + models.length + ' 个模型，点输入框选择');
      }).catch(function () {
        Layer.notify('获取失败', '请求未完成', 'bad');
      }).then(function () {
        btn.disabled = false; btn.textContent = '获取模型';
      });
    });
  }

  // ---- layer: windows, drawers, prompts and notices ------------------------
  //
  // One primitive, five shapes. A layer is a positioned box over the shell:
  //   Layer.open({url|content})     a draggable, resizable-by-maximise window
  //   Layer.drawer({url|content})   the same panel, docked to the right edge
  //   Layer.confirm(text, onOk)     a question with two buttons
  //   Layer.msg(text, kind)         a short line in the middle of the screen
  //   Layer.notify(title, text)     a card in the corner that stacks
  //   Layer.tip(el, text)           a bubble anchored to an element
  // They stack: opening a form from a form is one more window, and Escape or
  // the close button always dismisses the topmost one.

  var Layer = (function () {
    var seq = 0;
    var zTop = 200;
    var stack = [];

    function node(tag, cls, text) {
      var n = document.createElement(tag);
      if (cls) { n.className = cls; }
      if (text !== undefined) { n.textContent = text; }
      return n;
    }

    function shell(opts) {
      var id = ++seq;
      var mask = node('div', 'lay-mask');
      var box = node('div', 'lay-box' + (opts.drawer ? ' drawer' : '')
        + (opts.kind ? ' ' + opts.kind : ''));
      box.style.zIndex = ++zTop;
      mask.style.zIndex = zTop;
      if (opts.width) { box.style.width = opts.width; }

      var head = node('header');
      head.appendChild(node('span', 'title', opts.title || ''));
      var ops = node('span', 'ops');
      if (!opts.drawer && !opts.bare) {
        var max = node('button', 'wbtn', '□');
        max.type = 'button';
        max.title = '最大化 / 还原';
        max.onclick = function () {
          box.classList.toggle('max');
          if (!box.classList.contains('max')) { place(box, opts); }
        };
        ops.appendChild(max);
      }
      var x = node('button', 'wbtn', '×');
      x.type = 'button';
      x.title = '关闭';
      x.onclick = function () { Layer.close(id); };
      ops.appendChild(x);
      head.appendChild(ops);

      var body = node('div', 'body');
      box.appendChild(head);
      box.appendChild(body);
      document.body.appendChild(mask);
      document.body.appendChild(box);
      place(box, opts);
      if (!opts.drawer) { drag(box, head); }
      box.addEventListener('mousedown', function () { box.style.zIndex = ++zTop; });
      if (opts.maskClose !== false) {
        mask.onclick = function () { Layer.close(id); };
      }
      stack.push({ id: id, box: box, mask: mask });
      return { id: id, box: box, body: body, head: head };
    }

    // Centred on first paint, then wherever the user drags it.
    function place(box, opts) {
      if (opts.drawer) { return; }
      var w = box.offsetWidth;
      var h = box.offsetHeight;
      var left = Math.max(8, (window.innerWidth - w) / 2);
      var top = Math.max(8, (window.innerHeight - h) / 3);
      box.style.left = Math.round(left) + 'px';
      box.style.top = Math.round(top) + 'px';
    }

    function drag(box, handle) {
      handle.addEventListener('mousedown', function (ev) {
        if (ev.target.closest('.wbtn')) { return; }
        if (box.classList.contains('max')) { return; }
        var sx = ev.clientX, sy = ev.clientY;
        var ox = box.offsetLeft, oy = box.offsetTop;
        document.body.classList.add('lay-dragging');
        function move(e) {
          var nx = Math.min(Math.max(0, ox + e.clientX - sx),
                            window.innerWidth - 60);
          var ny = Math.min(Math.max(0, oy + e.clientY - sy),
                            window.innerHeight - 30);
          box.style.left = nx + 'px';
          box.style.top = ny + 'px';
        }
        function up() {
          document.body.classList.remove('lay-dragging');
          document.removeEventListener('mousemove', move);
          document.removeEventListener('mouseup', up);
        }
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', up);
        ev.preventDefault();
      });
    }

    function fill(w, opts) {
      if (opts.content !== undefined) {
        w.body.innerHTML = opts.content;
        ready(w, opts);
        return Promise.resolve(w.id);
      }
      w.body.innerHTML = '<div class="lay-loading">加载中…</div>';
      return fetch(opts.url, {
        headers: { 'X-Fragment': '1' }, credentials: 'same-origin'
      }).then(function (r) {
        if (r.ok) { return r.text(); }
        return r.json().then(function (j) { throw new Error(j.msg || '打开失败'); });
      }).then(function (html) {
        w.body.innerHTML = html;
        var t = w.body.querySelector('[data-title]');
        if (t) { w.head.querySelector('.title').textContent = t.getAttribute('data-title'); }
        ready(w, opts);
        return w.id;
      }).catch(function (e) {
        Layer.close(w.id);
        Layer.msg(e.message || '打开失败', 'bad');
      });
    }

    function ready(w, opts) {
      runScripts(w.body);
      if (!opts.drawer) { place(w.box, opts); }
      var first = w.body.querySelector('input:not([type=hidden]), textarea, select');
      if (first) { first.focus(); }
    }

    return {
      open: function (opts) {
        var w = shell(opts);
        return fill(w, opts);
      },
      drawer: function (opts) {
        opts.drawer = true;
        var w = shell(opts);
        return fill(w, opts);
      },
      confirm: function (text, onOk, title) {
        var w = shell({ title: title || '确认', width: '320px', bare: true,
                        maskClose: false });
        w.box.classList.add('ask');
        w.body.innerHTML = '';
        w.body.appendChild(node('p', 'ask-text', text));
        var foot = node('div', 'ask-ops');
        var no = node('button', 'btn sm', '取消');
        no.type = 'button';
        no.onclick = function () { Layer.close(w.id); };
        var yes = node('button', 'btn sm primary', '确定');
        yes.type = 'button';
        yes.onclick = function () { Layer.close(w.id); onOk(); };
        foot.appendChild(no);
        foot.appendChild(yes);
        w.body.appendChild(foot);
        yes.focus();
        return w.id;
      },
      msg: function (text, kind) {
        var m = node('div', 'lay-msg ' + (kind || 'ok'), text);
        m.style.zIndex = ++zTop;
        document.body.appendChild(m);
        setTimeout(function () { m.remove(); }, 2200);
      },
      notify: function (title, text, kind) {
        var host = document.getElementById('ad-notices');
        if (!host) { return; }
        var card = node('div', 'notice ' + (kind || 'ok'));
        card.appendChild(node('div', 'nt', title));
        if (text) { card.appendChild(node('div', 'nb', text)); }
        var x = node('span', 'x', '×');
        x.onclick = function () { card.remove(); };
        card.appendChild(x);
        host.appendChild(card);
        setTimeout(function () { card.remove(); }, 6000);
      },
      tip: function (anchor, text) {
        Layer.untip();
        var t = node('div', 'lay-tip', text);
        t.style.zIndex = ++zTop;
        document.body.appendChild(t);
        var r = anchor.getBoundingClientRect();
        var left = r.left + r.width / 2 - t.offsetWidth / 2;
        var top = r.top - t.offsetHeight - 6;
        if (top < 4) { top = r.bottom + 6; t.classList.add('below'); }
        t.style.left = Math.max(4, Math.round(left)) + 'px';
        t.style.top = Math.round(top) + 'px';
        Layer._tip = t;
      },
      untip: function () {
        if (Layer._tip) { Layer._tip.remove(); Layer._tip = null; }
      },
      close: function (id) {
        for (var i = stack.length - 1; i >= 0; i--) {
          if (stack[i].id !== id) { continue; }
          stack[i].box.remove();
          stack[i].mask.remove();
          stack.splice(i, 1);
          return;
        }
      },
      closeTop: function () {
        if (!stack.length) { return false; }
        var top = stack[stack.length - 1];
        Layer.close(top.id);
        return true;
      },
      any: function () { return stack.length > 0; }
    };
  })();

  window.Layer = Layer;

  // ---- dialogs ------------------------------------------------------------

  function dialog(url, wide, title) {
    return Layer.open({ url: url, title: title || '编辑',
                        width: wide ? '820px' : '560px' });
  }

  function closeDialog() { Layer.closeTop(); }

  // ---- writes -------------------------------------------------------------

  function post(url, body) {
    return fetch(url, {
      method: 'POST',
      credentials: 'same-origin',
      headers: {
        'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8',
        'X-Fragment': '1'
      },
      body: body || ''
    }).then(function (r) {
      if (r.status === 401) { location.href = '/admin/login'; return null; }
      return r.json();
    });
  }

  function submit(form) {
    var data = new FormData(form);
    var parts = [];
    // Checkbox groups arrive as one comma-joined field: the server reads a
    // single value and does not have to care about repeated names.
    var groups = {};
    data.forEach(function (v, k) {
      if (k.slice(-2) === '[]') {
        var name = k.slice(0, -2);
        (groups[name] = groups[name] || []).push(v);
        return;
      }
      parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(v));
    });
    Object.keys(groups).forEach(function (name) {
      parts.push(encodeURIComponent(name) + '=' + encodeURIComponent(groups[name].join(',')));
    });
    var btn = form.querySelector('[type=submit]');
    if (btn) { btn.disabled = true; }
    post(form.getAttribute('action'), parts.join('&')).then(function (j) {
      if (btn) { btn.disabled = false; }
      if (!j) { return; }
      if (j.code === '0000') {
        toast(j.msg || '已保存', 'ok');
        closeDialog();
        reload();
        return;
      }
      toast(j.msg || '保存失败', 'bad');
    }).catch(function () {
      if (btn) { btn.disabled = false; }
      toast('请求失败', 'bad');
    });
  }

  // ---- live metrics -------------------------------------------------------

  function stopStream() {
    if (stream) { stream.close(); stream = null; }
  }

  function startStream() {
    var host = panel.querySelector('[data-stream]');
    if (!host) { return; }
    var url = host.getAttribute('data-stream');
    stream = new EventSource(url);
    stream.addEventListener('metrics', function (ev) {
      paintSnapshot(JSON.parse(ev.data));
    });
    stream.addEventListener('series', function (ev) {
      paintSeries(JSON.parse(ev.data));
    });
    stream.onerror = function () { /* EventSource retries by itself */ };
  }

  function put(name, value) {
    panel.querySelectorAll('[data-metric="' + name + '"]').forEach(function (n) {
      n.textContent = value;
    });
  }

  function bytes(v) {
    if (v < 0) { return 'n/a'; }
    if (v < 1024) { return v + ' B'; }
    if (v < 1048576) { return (v / 1024).toFixed(0) + ' KB'; }
    return (v / 1048576).toFixed(1) + ' MB';
  }

  function paintSnapshot(m) {
    var r = m.requests || {}, q = m.queries || {};
    put('requests', r.count || 0);
    put('errors', r.errors || 0);
    put('failed', r.failed || 0);
    put('rejected', r.rejected || 0);
    put('avg_ms', (r.avg_ms || 0) + ' ms');
    put('max_ms', (r.max_ms || 0) + ' ms');
    put('queries', q.count || 0);
    put('query_avg_ms', (q.avg_ms || 0) + ' ms');
    put('cpu', m.cpu_percent < 0 ? 'n/a' : m.cpu_percent + '%');
    put('mem', bytes(m.mem_rss_bytes));
    var topreq = panel.querySelector('[data-topreq]');
    if (topreq) {
      var tr = m.top_requests || [];
      topreq.innerHTML = tr.length
        ? tr.map(function (s) {
            return '<tr><td class="ellip mono" title="' + esc(s.req) + '">' +
                   esc(s.req) + '</td><td class="num">' + (s.count || 0) +
                   '</td><td class="num">' + (s.errors || 0) +
                   '</td><td class="num">' + (s.failed || 0) +
                   '</td><td class="num">' + (s.rejected || 0) +
                   '</td><td class="num">' + (s.avg_ms || 0) +
                   '</td><td class="num">' + (s.max_ms || 0) +
                   '</td><td class="num">' + (s.total_ms || 0) + '</td></tr>';
          }).join('')
        : '<tr><td colspan="8" class="muted">暂无请求记录</td></tr>';
    }
    var top = panel.querySelector('[data-topsql]');
    if (top) {
      var ts = m.top_sql || [];
      top.innerHTML = ts.length
        ? ts.map(function (s) {
            return '<tr><td class="ellip mono" title="' + esc(s.sql) + '">' +
                   esc(s.sql) + '</td><td class="num">' + (s.count || 0) +
                   '</td><td class="num">' + (s.avg_ms || 0) +
                   '</td><td class="num">' + (s.max_ms || 0) +
                   '</td><td class="num">' + (s.total_ms || 0) + '</td></tr>';
          }).join('')
        : '<tr><td colspan="5" class="muted">暂无 SQL 记录</td></tr>';
    }
  }

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  function paintSeries(s) {
    var pts = s.points || [];
    draw('chart-req', [
      { color: '#1f6feb', label: '请求', values: pts.map(function (p) { return p.req; }) },
      { color: '#b42318', label: '错误', values: pts.map(function (p) { return p.err; }) }
    ], pts, '');
    draw('chart-lat', [
      { color: '#7c3aed', label: '平均', values: pts.map(function (p) { return p.avg_ms; }) },
      { color: '#d97706', label: 'P95', values: pts.map(function (p) { return p.p95_ms; }) }
    ], pts, ' ms');
    draw('chart-db', [
      { color: '#0f766e', label: '查询数', values: pts.map(function (p) { return p.queries; }) },
      { color: '#0891b2', label: '平均 ms', values: pts.map(function (p) { return p.query_avg_ms; }) }
    ], pts, '');
    draw('chart-sys', [
      { color: '#15803d', label: 'CPU %', values: pts.map(function (p) { return Math.max(p.cpu_percent, 0); }) },
      { color: '#6b7280', label: '内存 MB', values: pts.map(function (p) {
          return Math.round(Math.max(p.mem_rss_bytes, 0) / 1048576); }) }
    ], pts, '');
  }

  // One shared floating tooltip for every chart; position:fixed so it is never
  // clipped by a card's overflow.
  function monTip() {
    var t = document.getElementById('mon-tip');
    if (!t) {
      t = document.createElement('div');
      t.id = 'mon-tip';
      t.style.cssText = 'position:fixed;z-index:9999;pointer-events:none;display:none;'
        + 'background:rgba(17,24,39,.92);color:#fff;font:12px/1.5 system-ui,sans-serif;'
        + 'padding:6px 8px;border-radius:6px;box-shadow:0 4px 12px rgba(0,0,0,.25);white-space:nowrap';
      document.body.appendChild(t);
    }
    return t;
  }

  /* A line chart is a polyline over a scaled array; a charting library would be
   * a lot of bytes for that. */
  function draw(id, series, pts, unit) {
    var c = panel.querySelector('#' + id);
    if (!c) { return; }
    var dpr = window.devicePixelRatio || 1;
    var w = c.clientWidth, h = c.clientHeight;
    c.width = w * dpr; c.height = h * dpr;
    var g = c.getContext('2d');

    var max = 1;
    series.forEach(function (s) {
      s.values.forEach(function (v) { if (v > max) { max = v; } });
    });

    // Redraws the whole chart; `hover` >= 0 overlays a guide line and a dot on
    // each series at that sample, so the tooltip has a visible anchor.
    function render(hover) {
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      g.strokeStyle = '#eef0f3';
      g.lineWidth = 1;
      for (var i = 1; i < 4; i++) {
        var gy = Math.round(h * i / 4) + .5;
        g.beginPath(); g.moveTo(0, gy); g.lineTo(w, gy); g.stroke();
      }
      series.forEach(function (s) {
        var n = s.values.length;
        if (n < 2) { return; }
        g.beginPath();
        for (var i = 0; i < n; i++) {
          var x = w * i / (n - 1);
          var y = h - (h - 4) * (s.values[i] / max);
          if (i === 0) { g.moveTo(x, y); } else { g.lineTo(x, y); }
        }
        g.strokeStyle = s.color;
        g.lineWidth = 1.5;
        g.stroke();
      });
      var nn = series[0] ? series[0].values.length : 0;
      if (hover >= 0 && nn >= 2) {
        var hx = w * hover / (nn - 1);
        g.strokeStyle = '#94a3b8';
        g.lineWidth = 1;
        g.beginPath(); g.moveTo(hx + .5, 0); g.lineTo(hx + .5, h); g.stroke();
        series.forEach(function (s) {
          var y = h - (h - 4) * (s.values[hover] / max);
          g.fillStyle = s.color;
          g.beginPath(); g.arc(hx, y, 3, 0, Math.PI * 2); g.fill();
        });
      }
    }
    render(-1);

    c._render = render;
    c._series = series;
    c._pts = pts;
    c._unit = unit || '';
    if (!c._tipBound) {
      c._tipBound = true;
      c.addEventListener('mousemove', function (ev) {
        var sv = c._series;
        if (!sv || !sv[0] || sv[0].values.length < 2) { return; }
        var rect = c.getBoundingClientRect();
        var n = sv[0].values.length;
        var idx = Math.round((ev.clientX - rect.left) / rect.width * (n - 1));
        if (idx < 0) { idx = 0; }
        if (idx > n - 1) { idx = n - 1; }
        c._render(idx);
        var sp = c._pts, un = c._unit;
        var when = (sp && sp[idx] && typeof sp[idx].t === 'number')
          ? (sp[idx].t === 0 ? '现在' : (-sp[idx].t) + ' 秒前') : '';
        var html = when ? '<b>' + when + '</b>' : '';
        sv.forEach(function (s) {
          html += '<div><i style="display:inline-block;width:8px;height:8px;'
            + 'border-radius:2px;margin-right:6px;background:' + s.color + '"></i>'
            + esc(s.label || '') + ' ' + s.values[idx] + un + '</div>';
        });
        var tip = monTip();
        tip.innerHTML = html;
        tip.style.display = 'block';
        tip.style.left = (ev.clientX + 12) + 'px';
        tip.style.top = (ev.clientY + 12) + 'px';
      });
      c.addEventListener('mouseleave', function () {
        c._render(-1);
        var tip = document.getElementById('mon-tip');
        if (tip) { tip.style.display = 'none'; }
      });
    }

    var label = panel.querySelector('[data-max="' + id + '"]');
    if (label) { label.textContent = '峰值 ' + max; }
  }


  // A template cannot write `selected` on the right <option>, so a select that
  // carries its current value in data-value applies it after the fragment is
  // in the DOM. Same for the code preview's tabs, which are markup only.
  function applyFragmentWidgets(root) {
    var sels = root.querySelectorAll('select[data-value]');
    for (var i = 0; i < sels.length; i++) {
      var v = sels[i].getAttribute('data-value');
      if (v) { sels[i].value = v; }
    }
    var tabs = root.querySelectorAll('[data-code]');
    for (var t = 0; t < tabs.length; t++) {
      tabs[t].addEventListener('click', function () {
        var box = this.closest('.code-preview');
        var name = this.getAttribute('data-code');
        var all = box.querySelectorAll('[data-code]');
        for (var k = 0; k < all.length; k++) { all[k].classList.remove('on'); }
        this.classList.add('on');
        var bodies = box.querySelectorAll('[data-code-body]');
        for (var b = 0; b < bodies.length; b++) {
          bodies[b].classList.toggle('open',
            bodies[b].getAttribute('data-code-body') === name);
        }
      });
    }
  }
  window.applyFragmentWidgets = applyFragmentWidgets;

  // ---- wiring -------------------------------------------------------------

  document.addEventListener('click', function (ev) {
    hideTabMenu();
    var a = ev.target.closest('[data-tab]');
    if (a) {
      ev.preventDefault();
      open(a.getAttribute('href'), a.getAttribute('data-title') || a.textContent.trim());
      document.body.classList.remove('side-open');
      return;
    }
    var l = ev.target.closest('[data-load]');
    if (l) {
      ev.preventDefault();
      var href = l.getAttribute('href');
      var path = href.charAt(0) === '?' ? base(state.active) + href : href;
      state.active = path;
      var i = find(path);
      load(path);
      return;
    }
    var d = ev.target.closest('[data-dialog]');
    if (d) {
      ev.preventDefault();
      dialog(d.getAttribute('href') || d.getAttribute('data-dialog'),
             d.hasAttribute('data-wide'),
             d.getAttribute('data-title') || d.textContent.trim());
      return;
    }
    var dr = ev.target.closest('[data-drawer]');
    if (dr) {
      ev.preventDefault();
      Layer.drawer({ url: dr.getAttribute('href') || dr.getAttribute('data-drawer'),
                     title: dr.getAttribute('data-title') || dr.textContent.trim(),
                     width: dr.getAttribute('data-width') || '420px' });
      return;
    }
    var p = ev.target.closest('[data-post]');
    if (p) {
      ev.preventDefault();
      var run = function () {
        post(p.getAttribute('data-post'), p.getAttribute('data-args') || '')
          .then(function (j) {
            if (!j) { return; }
            toast(j.msg || '完成', j.code === '0000' ? 'ok' : 'bad');
            if (j.code === '0000') { closeDialog(); reload(); }
          })
          .catch(function () { toast('请求失败', 'bad'); });
      };
      var confirmText = p.getAttribute('data-confirm');
      if (confirmText) { Layer.confirm(confirmText, run); } else { run(); }
      return;
    }
    var nav = ev.target.closest('[data-tabnav]');
    if (nav) {
      ev.preventDefault();
      var dir = parseInt(nav.getAttribute('data-tabnav'), 10) || 1;
      tabs.scrollLeft = tabs.scrollLeft + dir * Math.max(120, tabs.clientWidth * 0.7);
      return;
    }
    if (ev.target.closest('[data-close]')) { ev.preventDefault(); closeDialog(); }
    if (ev.target.closest('[data-refresh]')) { ev.preventDefault(); reload(); }
    // On a phone the sidebar is a drawer; anywhere else this button is hidden.
    if (ev.target.closest('[data-side]')) {
      ev.preventDefault();
      document.body.classList.toggle('side-open');
      return;
    }
    if (document.body.classList.contains('side-open')
        && !ev.target.closest('.ad-side')) {
      document.body.classList.remove('side-open');
    }
  });

  document.addEventListener('submit', function (ev) {
    var search = ev.target.closest('[data-search]');
    if (search) {
      // A filter bar is a GET: it changes which rows the panel shows, so it
      // reloads the panel in place and leaves the tab where it is.
      ev.preventDefault();
      var parts = [];
      new FormData(search).forEach(function (v, k) {
        if (String(v).length) {
          parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(v));
        }
      });
      var path = search.getAttribute('action') || base(state.active);
      if (parts.length) { path = path + '?' + parts.join('&'); }
      state.active = path;
      load(path);
      return;
    }
    var form = ev.target.closest('[data-submit]');
    if (!form) { return; }
    ev.preventDefault();
    submit(form);
  });

  // A tip is markup, not a component: data-tip on anything shows a bubble.
  document.addEventListener('mouseover', function (ev) {
    var a = ev.target.closest('[data-tip]');
    if (a) { Layer.tip(a, a.getAttribute('data-tip')); }
  });
  document.addEventListener('mouseout', function (ev) {
    if (ev.target.closest('[data-tip]')) { Layer.untip(); }
  });

  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape') { Layer.untip(); hideTabMenu(); Layer.closeTop(); }
  });

  document.addEventListener('DOMContentLoaded', function () {
    panel = el('ad-panel');
    tabs = el('ad-tabs');
    toasts = el('ad-toasts');
    restore();
    var here = location.pathname + location.search;
    var title = document.body.getAttribute('data-tab-title') || here;
    if (find(here) < 0) { state.tabs.push({ path: here, title: title }); }
    state.active = here;
    paint();
    window.addEventListener('resize', paintTabNav);
    // A wheel over the strip pages it sideways, like a browser's tab bar.
    tabs.addEventListener('wheel', function (ev) {
      if (tabs.scrollWidth <= tabs.clientWidth) { return; }
      ev.preventDefault();
      tabs.scrollLeft = tabs.scrollLeft + (ev.deltaY || ev.deltaX);
    }, { passive: false });
    // The first screen is already in the panel, server-rendered.
    startStream();
    wireModelPick(panel);
  });


  // Account menu in the top-right corner.
  (function account() {
    var box = document.querySelector('[data-account]');
    if (!box) { return; }
    var btn = box.querySelector('[data-account-btn]');
    btn.addEventListener('click', function (ev) {
      ev.stopPropagation();
      box.classList.toggle('open');
    });
    document.addEventListener('click', function () { box.classList.remove('open'); });
    document.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape') { box.classList.remove('open'); }
    });
  })();

  // Ctrl+F filters the sidebar. Chinese entries also match their pinyin
  // initials ("wzgl" -> 文章管理): the first letter of a han character is
  // found by collating it against the 26 boundary characters of the zh-CN
  // order, which needs no lookup table.
  (function menuFilter() {
    var box = document.getElementById('ad-find');
    if (!box) { return; }
    var side = document.querySelector('.ad-side');
    var bounds = '阿八嚓咑妸发旮铪丌咔垃妈拏噢妑七呥仨他屲夕丫帀';
    var letters = 'abcdefghjklmnopqrstwxyz';
    var cache = {};

    function initial(ch) {
      if (cache[ch] !== undefined) { return cache[ch]; }
      var out = ch.toLowerCase();
      if (ch.charCodeAt(0) > 0x3fff) {
        out = '';
        for (var i = bounds.length - 1; i >= 0; i--) {
          if (ch.localeCompare(bounds.charAt(i), 'zh-CN') >= 0) { out = letters.charAt(i); break; }
        }
      }
      cache[ch] = out;
      return out;
    }

    function initials(text) {
      var s = '';
      for (var i = 0; i < text.length; i++) { s += initial(text.charAt(i)); }
      return s;
    }

    function apply() {
      var q = box.value.trim().toLowerCase();
      var items = side.querySelectorAll('a.mi');
      var first = null;
      for (var i = 0; i < items.length; i++) {
        var el = items[i];
        var text = el.textContent.trim();
        var hit = !q || text.toLowerCase().indexOf(q) >= 0 || initials(text).indexOf(q) >= 0;
        el.classList.toggle('filtered', !hit);
        if (hit && !first) { first = el; }
      }
      var groups = side.querySelectorAll('.ad-group');
      for (var g = 0; g < groups.length; g++) {
        var any = false;
        var n = groups[g].nextElementSibling;
        while (n && !n.classList.contains('ad-group')) {
          if (n.classList.contains('mi') && !n.classList.contains('filtered')) { any = true; }
          n = n.nextElementSibling;
        }
        groups[g].classList.toggle('filtered', !any);
      }
      return first;
    }

    box.addEventListener('input', apply);
    box.addEventListener('keydown', function (ev) {
      if (ev.key === 'Escape') { box.value = ''; apply(); box.blur(); }
      if (ev.key === 'Enter') {
        var first = apply();
        if (first) { first.click(); }
      }
    });
    document.addEventListener('keydown', function (ev) {
      if ((ev.ctrlKey || ev.metaKey) && (ev.key === 'f' || ev.key === 'F')) {
        ev.preventDefault();
        document.body.classList.add('side-open');
        box.focus();
        box.select();
      }
    });
  })();


  // Settings screen: one pane per group, the SMTP test button and the AI
  // presets. The screen arrives as a fragment, so every handler is delegated
  // from the document instead of bound to the elements at load time.
  document.addEventListener('click', function (ev) {
    var paneBtn = ev.target.closest('[data-setting-tabs] [data-pane]');
    if (paneBtn) {
      var name = paneBtn.getAttribute('data-pane');
      paneMemo[base(state.active)] = name;
      var tabs = paneBtn.closest('[data-setting-tabs]');
      var all = tabs.querySelectorAll('[data-pane]');
      for (var i = 0; i < all.length; i++) { all[i].classList.toggle('on', all[i] === paneBtn); }
      var panes = document.querySelectorAll('[data-pane-body]');
      for (var p = 0; p < panes.length; p++) {
        panes[p].classList.toggle('open', panes[p].getAttribute('data-pane-body') === name);
      }
      // Cards that belong to one group only (the AI presets) follow the pane.
      var extras = document.querySelectorAll('[data-pane-extra]');
      for (var e = 0; e < extras.length; e++) {
        extras[e].hidden = extras[e].getAttribute('data-pane-extra') !== name;
      }
      return;
    }

    // A preset fills the two fields that differ between providers and moves
    // the form to the AI pane; the key is still typed by hand.
    var preset = ev.target.closest('[data-preset]');
    if (preset) {
      var set = function (n, v) {
        var f = document.querySelector('[name="' + n + '"]');
        if (f) { f.value = v; }
      };
      set('ai.provider', preset.getAttribute('data-preset'));
      set('ai.baseUrl', preset.getAttribute('data-url'));
      set('ai.model', preset.getAttribute('data-model'));
      var aiTab = document.querySelector('[data-setting-tabs] [data-pane="AI 助手"]');
      if (aiTab) { aiTab.click(); }
      var key = document.querySelector('[name="ai.apiKey"]');
      if (key) { key.focus(); }
      Layer.msg('已填入 ' + preset.parentNode.parentNode.querySelector('.vendor-name').textContent);
      return;
    }

    var test = ev.target.closest('[data-mail-test]');
    if (test) {
      var to = document.getElementById('mail-to');
      if (!to || !to.value) { if (to) { to.focus(); } return; }
      test.disabled = true;
      fetch('/admin/system/settings/mailtest', {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ to: to.value }).toString()
      }).then(function (r) { return r.json(); }).then(function (j) {
        if (j && j.code === '0000') { Layer.msg(j.msg || '测试邮件已发送'); }
        else { Layer.notify('发送失败', (j && j.msg) || '请检查邮件配置', 'bad'); }
      }).catch(function () {
        Layer.notify('发送失败', '请求未完成', 'bad');
      }).then(function () { test.disabled = false; });
    }
  });

  // ---- assistant ----------------------------------------------------------

  // A docked chat, WeChat-style: the agents down the left, one conversation per
  // agent on the right. The corner button toggles it; closing only hides it, so
  // the threads, the open agent and the scroll position all survive until the
  // page itself is left. Each agent keeps its own thread in localStorage and the
  // whole thread is sent back on every turn, so the model has the conversation
  // and not just the last line. An unconfigured assistant offers to open the
  // settings screen rather than a chat that could not answer.
  (function assistant() {
    var STORE = 'zanweb.ai';
    var state = { ready: false, agents: [], agent: '', dock: null, built: false };
    var mem = load();

    function load() {
      try {
        var raw = localStorage.getItem(STORE);
        if (raw) { return JSON.parse(raw); }
      } catch (e) { /* private mode: the thread just does not persist */ }
      return { agent: '', sessions: {} };
    }
    function persist() {
      try { localStorage.setItem(STORE, JSON.stringify(mem)); } catch (e) {}
    }
    function thread(code) {
      if (!mem.sessions[code]) { mem.sessions[code] = []; }
      return mem.sessions[code];
    }
    function agentOf(code) {
      for (var i = 0; i < state.agents.length; i++) {
        if (state.agents[i].code === code) { return state.agents[i]; }
      }
      return state.agents[0] || null;
    }

    // The one and only <style> and dock, injected once so nothing here depends
    // on admin.css.
    function styles() {
      if (document.getElementById('ai-dock-css')) { return; }
      var s = document.createElement('style');
      s.id = 'ai-dock-css';
      s.textContent =
        '.ai-dock{position:fixed;right:20px;bottom:84px;width:720px;max-width:calc(100vw - 40px);'
        + 'height:520px;max-height:calc(100vh - 120px);display:none;flex-direction:column;'
        + 'background:#fff;border:1px solid #e5e7eb;border-radius:12px;box-shadow:0 18px 48px rgba(0,0,0,.22);'
        + 'z-index:1200;overflow:hidden}'
        + '.ai-dock.on{display:flex}'
        + '.ai-dock-top{display:flex;align-items:center;gap:8px;padding:10px 12px;border-bottom:1px solid #eee;background:#f7f8fa}'
        + '.ai-dock-top b{font-size:14px}.ai-dock-top .sp{flex:1}'
        + '.ai-dock-top button{border:0;background:transparent;cursor:pointer;font-size:16px;color:#6b7280;padding:2px 6px;border-radius:6px}'
        + '.ai-dock-top button:hover{background:#eceef1;color:#111}'
        + '.ai-dock-body{flex:1;display:flex;min-height:0}'
        + '.ai-side{width:190px;border-right:1px solid #eee;overflow:auto;background:#fafbfc}'
        + '.ai-agent{display:flex;flex-direction:column;gap:2px;padding:10px 12px;cursor:pointer;border-bottom:1px solid #f1f2f4}'
        + '.ai-agent:hover{background:#f0f2f5}.ai-agent.on{background:#e7f0ff}'
        + '.ai-agent .n{font-size:13px;font-weight:600;color:#1f2937}'
        + '.ai-agent .h{font-size:11px;color:#8a94a6;line-height:1.4}'
        + '.ai-main{flex:1;display:flex;flex-direction:column;min-width:0}'
        + '.ai-log{flex:1;overflow:auto;padding:14px;background:#f2f3f5}'
        + '.ai-row{display:flex;margin-bottom:12px}.ai-row.me{justify-content:flex-end}'
        + '.ai-bubble{max-width:78%;padding:8px 11px;border-radius:10px;font-size:13px;line-height:1.55;'
        + 'white-space:pre-wrap;word-break:break-word;background:#fff;border:1px solid #e6e8eb;color:#1f2937}'
        + '.ai-row.me .ai-bubble{background:#95ec69;border-color:#86df5c}'
        + '.ai-bubble.bad{background:#fde8e8;border-color:#f5c2c2;color:#b42318}'
        + '.ai-apply{margin-top:8px}'
        + '.ai-apply button{border:1px solid #2563eb;background:#2563eb;color:#fff;border-radius:6px;'
        + 'padding:4px 10px;font-size:12px;cursor:pointer}'
        + '.ai-apply button:disabled{opacity:.6;cursor:default}'
        + '.ai-send{display:flex;gap:8px;padding:10px;border-top:1px solid #eee;background:#fff}'
        + '.ai-send textarea{flex:1;resize:none;border:1px solid #d7dbe0;border-radius:8px;padding:8px 10px;font-size:13px;font-family:inherit}'
        + '.ai-send button{border:0;background:#2563eb;color:#fff;border-radius:8px;padding:0 16px;cursor:pointer;font-size:13px}'
        + '.ai-empty{color:#9aa3b2;font-size:12px;text-align:center;margin-top:40px}';
      document.head.appendChild(s);
    }

    function esc(t) {
      return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }
    // The first ```sql fenced block, or the first fenced block that looks like a
    // CREATE TABLE -- the confirm button runs exactly this text.
    function sqlOf(text) {
      var m = /```sql\s*([\s\S]*?)```/i.exec(text);
      if (m) { return m[1].trim(); }
      m = /```\s*([\s\S]*?)```/i.exec(text);
      if (m && /create\s+table/i.test(m[1])) { return m[1].trim(); }
      return '';
    }

    function build() {
      styles();
      var dock = document.createElement('div');
      dock.className = 'ai-dock';
      dock.innerHTML =
        '<div class="ai-dock-top"><b>AI 助手</b><span class="sp"></span>'
        + '<button type="button" data-ai-clear title="清空当前会话">清空</button>'
        + '<button type="button" data-ai-hide title="收起">—</button></div>'
        + '<div class="ai-dock-body"><div class="ai-side" data-ai-side></div>'
        + '<div class="ai-main"><div class="ai-log" data-ai-log></div>'
        + '<div class="ai-send"><textarea rows="2" data-ai-input '
        + 'placeholder="说说你要做什么，Enter 发送，Shift+Enter 换行"></textarea>'
        + '<button type="button" data-ai-go>发送</button></div></div></div>';
      document.body.appendChild(dock);
      state.dock = dock;
      state.built = true;

      dock.querySelector('[data-ai-hide]').addEventListener('click', hide);
      dock.querySelector('[data-ai-clear]').addEventListener('click', function () {
        Layer.confirm('清空「' + (agentOf(state.agent) || {}).name + '」的对话记录？', function () {
          mem.sessions[state.agent] = [];
          persist();
          renderLog();
        });
      });
      var input = dock.querySelector('[data-ai-input]');
      dock.querySelector('[data-ai-go]').addEventListener('click', send);
      input.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' && !ev.shiftKey) { ev.preventDefault(); send(); }
      });
      dock.querySelector('[data-ai-side]').addEventListener('click', function (ev) {
        var row = ev.target.closest('[data-agent]');
        if (!row) { return; }
        pick(row.getAttribute('data-agent'));
      });
      renderSide();
      renderLog();
    }

    function renderSide() {
      if (!state.dock) { return; }
      var side = state.dock.querySelector('[data-ai-side]');
      var h = '';
      for (var i = 0; i < state.agents.length; i++) {
        var a = state.agents[i];
        h += '<div class="ai-agent' + (a.code === state.agent ? ' on' : '')
          + '" data-agent="' + a.code + '"><span class="n">' + esc(a.name)
          + '</span><span class="h">' + esc(a.hint) + '</span></div>';
      }
      side.innerHTML = h;
    }

    function renderLog() {
      if (!state.dock) { return; }
      var log = state.dock.querySelector('[data-ai-log]');
      log.innerHTML = '';
      var msgs = thread(state.agent);
      if (!msgs.length) {
        var a = agentOf(state.agent);
        log.innerHTML = '<div class="ai-empty">' + esc(a ? a.hint : '开始对话') + '</div>';
        return;
      }
      for (var i = 0; i < msgs.length; i++) { paint(log, msgs[i]); }
      log.scrollTop = log.scrollHeight;
    }

    function paint(log, msg) {
      var row = document.createElement('div');
      row.className = 'ai-row ' + (msg.who === 'me' ? 'me' : 'ai');
      var bubble = document.createElement('div');
      bubble.className = 'ai-bubble' + (msg.bad ? ' bad' : '');
      bubble.textContent = msg.text;
      row.appendChild(bubble);
      // A proposal from an agent that may act gets a confirm button; the SQL is
      // only ever run after this second, explicit click.
      var a = agentOf(state.agent);
      if (msg.who === 'ai' && !msg.bad && a && a.apply) {
        var sql = sqlOf(msg.text);
        if (sql) { bubble.appendChild(applyBtn(sql)); }
      }
      log.appendChild(row);
      return bubble;
    }

    function applyBtn(sql) {
      var wrap = document.createElement('div');
      wrap.className = 'ai-apply';
      var btn = document.createElement('button');
      btn.type = 'button';
      btn.textContent = '确认建表';
      btn.addEventListener('click', function () {
        Layer.confirm('确认执行下面的建表语句？\n\n' + sql, function () {
          btn.disabled = true;
          btn.textContent = '执行中…';
          fetch('/admin/dev/ai/apply', {
            method: 'POST', credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: new URLSearchParams({ sql: sql, confirm: '1' }).toString()
          }).then(function (r) { return r.json(); }).then(function (j) {
            if (j && j.code === '0000') { Layer.msg('建表成功'); btn.textContent = '已建表'; }
            else { Layer.notify('建表失败', (j && j.msg) || '请稍后再试', 'bad'); btn.disabled = false; btn.textContent = '确认建表'; }
          }).catch(function () {
            Layer.notify('建表失败', '请求未完成', 'bad');
            btn.disabled = false; btn.textContent = '确认建表';
          });
        });
      });
      wrap.appendChild(btn);
      return wrap;
    }

    function pick(code) {
      state.agent = code;
      mem.agent = code;
      persist();
      renderSide();
      renderLog();
      if (state.dock) { state.dock.querySelector('[data-ai-input]').focus(); }
    }

    function send() {
      var input = state.dock.querySelector('[data-ai-input]');
      var text = input.value.trim();
      if (!text) { return; }
      input.value = '';
      var msgs = thread(state.agent);
      msgs.push({ who: 'me', text: text });
      persist();
      var log = state.dock.querySelector('[data-ai-log]');
      if (log.querySelector('.ai-empty')) { log.innerHTML = ''; }
      paint(log, { who: 'me', text: text });
      var pending = paint(log, { who: 'ai', text: '思考中…' });
      log.scrollTop = log.scrollHeight;

      var history = [];
      for (var i = 0; i < msgs.length - 1; i++) {
        history.push({ role: msgs[i].who === 'me' ? 'user' : 'assistant', content: msgs[i].text });
      }
      // The answer is streamed: /admin/dev/ai/stream replies with a
      // text/event-stream and the bubble grows as the deltas land, so the
      // operator sees the model writing instead of a spinner. The stream is
      // read with fetch (not EventSource) because the request is a POST that
      // carries the thread.
      stream(text, history, msgs, log, pending);
    }

    // Reads an event stream of `delta` / `done` / `error` frames off a POST and
    // paints each delta into the pending bubble. One connection, no polling.
    function stream(text, history, msgs, log, pending) {
      var body = new URLSearchParams({ agent: state.agent, q: text,
                                       history: JSON.stringify(history) }).toString();
      var answer = '';
      var failed = '';
      function finish() {
        msgs.push({ who: 'ai', text: failed || answer || '（空回复）',
                    bad: !!failed });
        persist();
        renderLog();
      }
      fetch('/admin/dev/ai/stream', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: body
      }).then(function (r) {
        if (!r.body || !r.body.getReader) {
          // No streaming in this browser: the non-streaming endpoint answers
          // the same conversation in one shot.
          return fetch('/admin/dev/ai/chat', {
            method: 'POST', credentials: 'same-origin',
            headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
            body: body
          }).then(function (r2) { return r2.json(); }).then(function (j) {
            var ok = j && j.code === '0000' && j.data;
            if (ok) { answer = j.data.text || ''; }
            else { failed = (j && j.msg) || '请求失败'; }
            finish();
          });
        }
        var reader = r.body.getReader();
        var dec = new TextDecoder();
        var buf = '';
        function frame(block) {
          var ev = 'message';
          var data = '';
          var lines = block.split('\n');
          for (var i = 0; i < lines.length; i++) {
            var ln = lines[i];
            if (ln.indexOf('event:') === 0) { ev = ln.slice(6).trim(); }
            else if (ln.indexOf('data:') === 0) { data += ln.slice(5).trim(); }
          }
          if (!data) { return; }
          var d = null;
          try { d = JSON.parse(data); } catch (e) { return; }
          if (ev === 'delta') {
            answer += d.text || '';
            pending.textContent = answer;
            log.scrollTop = log.scrollHeight;
          } else if (ev === 'done') {
            answer = d.text || answer;
          } else if (ev === 'error') {
            failed = d.text || '请求失败';
          }
        }
        function pump() {
          return reader.read().then(function (res) {
            if (res.done) { finish(); return; }
            buf += dec.decode(res.value, { stream: true });
            var at = buf.indexOf('\n\n');
            while (at >= 0) {
              frame(buf.slice(0, at));
              buf = buf.slice(at + 2);
              at = buf.indexOf('\n\n');
            }
            return pump();
          });
        }
        return pump();
      }).catch(function () {
        pending.textContent = '请求失败';
        pending.classList.add('bad');
        msgs.push({ who: 'ai', text: '请求失败', bad: true });
        persist();
      });
    }

    function show() {
      if (!state.built) { build(); }
      if (!state.agent || !agentOf(state.agent)) {
        state.agent = (mem.agent && agentOf(mem.agent)) ? mem.agent
          : (state.agents[0] ? state.agents[0].code : '');
      }
      renderSide();
      renderLog();
      state.dock.classList.add('on');
      var input = state.dock.querySelector('[data-ai-input]');
      if (input) { input.focus(); }
    }
    function hide() { if (state.dock) { state.dock.classList.remove('on'); } }

    function refresh() {
      return fetch('/admin/dev/ai/state', { credentials: 'same-origin' })
        .then(function (r) { return r.json(); })
        .then(function (j) {
          if (j && j.code === '0000' && j.data) {
            state.ready = !!j.data.ready;
            state.agents = j.data.agents || [];
          }
        }).catch(function () {});
    }

    document.addEventListener('click', function (ev) {
      if (!ev.target.closest('[data-ai-btn]')) { return; }
      if (state.dock && state.dock.classList.contains('on')) { hide(); return; }
      refresh().then(function () {
        if (!state.ready) {
          Layer.confirm('AI 助手还没有配置，现在去填服务商和 API Key？', function () {
            var a = document.querySelector('.ad-side a[href="/admin/system/settings"]');
            if (a) { a.click(); } else { location.href = '/admin/system/settings'; }
          });
          return;
        }
        show();
      });
    });

    document.addEventListener('DOMContentLoaded', refresh);
  })();

})();
