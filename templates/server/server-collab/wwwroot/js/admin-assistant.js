/* Admin AI 助手插件：设置页模型列表获取、右下角悬浮对话抽屉、SSE 流式回复与建表执行。
   通过 window.ZanAdmin 生命周期总线与命令注册表接入外壳，无需修改内核即可独立演进或裁剪。 */
(function () {
  var bus = window.ZanAdmin;

  function layerMsg(text, kind) {
    if (window.Layer) { window.Layer.msg(text, kind); }
  }

  function layerNotify(title, text, kind) {
    if (window.Layer) { window.Layer.notify(title, text, kind); }
  }

  function layerConfirm(text, onOk) {
    if (window.Layer) { window.Layer.confirm(text, onOk); }
  }

  function wireModelPick(root) {
    if (!root) { return; }
    var input = root.querySelector('[name="ai.model"]');
    if (!input || input.getAttribute('data-model-wired')) { return; }
    input.setAttribute('data-model-wired', '1');
    input.setAttribute('autocomplete', 'off');
    var wrap = document.createElement('span');
    wrap.className = 'ai-model-wrap';
    input.parentNode.insertBefore(wrap, input);
    wrap.appendChild(input);
    var btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'btn sm ai-model-btn';
    btn.textContent = '获取模型';
    wrap.appendChild(btn);

    var menu = document.createElement('div');
    menu.className = 'pick-menu ai-model-menu';
    menu.setAttribute('role', 'listbox');
    menu.hidden = true;
    wrap.appendChild(menu);

    var allModels = [];
    var visibleModels = [];
    var activeIdx = -1;

    function closeMenu() { menu.hidden = true; activeIdx = -1; }
    function pickModel(name) {
      input.value = name;
      closeMenu();
      input.dispatchEvent(new Event('input', { bubbles: true }));
      input.dispatchEvent(new Event('change', { bubbles: true }));
    }
    function highlightModel(idx) {
      var opts = menu.querySelectorAll('.pick-opt');
      if (!opts.length) { activeIdx = -1; return; }
      if (idx < 0) { idx = opts.length - 1; }
      if (idx >= opts.length) { idx = 0; }
      activeIdx = idx;
      for (var i = 0; i < opts.length; i++) {
        opts[i].classList.toggle('active', i === activeIdx);
      }
      if (opts[activeIdx] && opts[activeIdx].scrollIntoView) {
        opts[activeIdx].scrollIntoView({ block: 'nearest' });
      }
    }
    function renderMenu(filterText) {
      if (!allModels.length) { closeMenu(); return; }
      var q = String(filterText || '').trim().toLowerCase();
      visibleModels = q ? allModels.filter(function (m) {
        return m.toLowerCase().indexOf(q) >= 0;
      }) : allModels.slice();
      menu.innerHTML = '';
      if (!visibleModels.length) {
        var none = document.createElement('div');
        none.className = 'pick-none';
        none.textContent = '无匹配模型';
        menu.appendChild(none);
        activeIdx = -1;
        menu.hidden = false;
        return;
      }
      activeIdx = 0;
      for (var i = 0; i < visibleModels.length; i++) {
        (function (m, idx) {
          var opt = document.createElement('div');
          opt.className = 'pick-opt' + (idx === activeIdx ? ' active' : '');
          opt.setAttribute('role', 'option');
          opt.textContent = m;
          opt.addEventListener('mouseenter', function () { highlightModel(idx); });
          opt.addEventListener('mousedown', function (ev) {
            ev.preventDefault();
            pickModel(m);
          });
          menu.appendChild(opt);
        })(visibleModels[i], i);
      }
      menu.hidden = false;
    }

    input.addEventListener('focus', function () {
      if (allModels.length) { renderMenu(input.value); }
    });
    input.addEventListener('input', function () {
      if (allModels.length) { renderMenu(input.value); }
    });
    input.addEventListener('keydown', function (ev) {
      if (menu.hidden) {
        if (ev.key === 'ArrowDown' && allModels.length) {
          ev.preventDefault();
          renderMenu(input.value);
        }
        return;
      }
      if (ev.key === 'ArrowDown') {
        ev.preventDefault();
        highlightModel(activeIdx + 1);
      } else if (ev.key === 'ArrowUp') {
        ev.preventDefault();
        highlightModel(activeIdx - 1);
      } else if (ev.key === 'Enter' && activeIdx >= 0 && visibleModels[activeIdx]) {
        ev.preventDefault();
        pickModel(visibleModels[activeIdx]);
      } else if (ev.key === 'Escape') {
        ev.preventDefault();
        closeMenu();
      }
    });
    input.addEventListener('blur', function () { setTimeout(closeMenu, 150); });

    btn.addEventListener('click', function () {
      var url = root.querySelector('[name="ai.baseUrl"]');
      var key = root.querySelector('[name="ai.apiKey"]');
      if (!url || !url.value.trim()) { layerMsg('请先填写接口地址'); return; }
      btn.disabled = true;
      btn.textContent = '获取中…';
      fetch('/admin/dev/assistant/models', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ baseUrl: url.value.trim(),
                                    apiKey: key ? key.value : '' }).toString()
      }).then(function (r) { return r.json(); }).then(function (j) {
        var models = (j && j.code === '0000' && j.data && j.data.models) || [];
        if (!models.length) {
          layerNotify('没有获取到模型', '确认接口地址和 Key 是否正确，或该服务不支持模型列表', 'bad');
          return;
        }
        allModels = models.slice();
        if (!input.value) { input.value = models[0]; }
        renderMenu('');
        input.focus();
        layerMsg('已获取 ' + models.length + ' 个模型，可直接选择或输入过滤');
      }).catch(function () {
        layerNotify('获取失败', '请求未完成', 'bad');
      }).then(function () {
        btn.disabled = false; btn.textContent = '获取模型';
      });
    });
  }
  window.wireModelPick = wireModelPick;

  var STORE = 'zanweb.ai';
  var state = { ready: false, agents: [], agent: '', dock: null, built: false };
  var mem = load();

  function load() {
    try {
      var raw = localStorage.getItem(STORE);
      if (raw) { return JSON.parse(raw); }
    } catch (e) { /* private mode */ }
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

  function esc(t) {
    return String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function sqlOf(text) {
    var m = /```sql\s*([\s\S]*?)```/i.exec(text);
    if (m) { return m[1].trim(); }
    m = /```\s*([\s\S]*?)```/i.exec(text);
    if (m && /create\s+table/i.test(m[1])) { return m[1].trim(); }
    return '';
  }

  function build() {
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
      layerConfirm('清空「' + (agentOf(state.agent) || {}).name + '」的对话记录？', function () {
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
      layerConfirm('确认执行下面的建表语句？\n\n' + sql, function () {
        btn.disabled = true;
        btn.textContent = '执行中…';
        fetch('/admin/dev/assistant/apply', {
          method: 'POST', credentials: 'same-origin',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ sql: sql, confirm: '1' }).toString()
        }).then(function (r) { return r.json(); }).then(function (j) {
          if (j && j.code === '0000') { layerMsg('建表成功'); btn.textContent = '已建表'; }
          else { layerNotify('建表失败', (j && j.msg) || '请稍后再试', 'bad'); btn.disabled = false; btn.textContent = '确认建表'; }
        }).catch(function () {
          layerNotify('建表失败', '请求未完成', 'bad');
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
    stream(text, history, msgs, log, pending);
  }

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
    fetch('/admin/dev/assistant/stream', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    }).then(function (r) {
      if (!r.body || !r.body.getReader) {
        return fetch('/admin/dev/assistant/chat', {
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
    if (!document.querySelector('[data-ai-btn]')) { return Promise.resolve(); }
    return fetch('/admin/dev/assistant/state', { credentials: 'same-origin' })
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
        layerConfirm('AI 助手还没有配置，现在去填服务商和 API Key？', function () {
          var a = document.querySelector('.ad-side a[href="/admin/system/settings"]');
          if (a) { a.click(); } else { location.href = '/admin/system/settings'; }
        });
        return;
      }
      show();
    });
  });

  if (bus) {
    if (bus.onInit) { bus.onInit(refresh); }
    if (bus.onMount) { bus.onMount(function (root) { wireModelPick(root); }); }
    if (bus.registerCommand) {
      bus.registerCommand(function () {
        var aiBtn = document.querySelector('[data-ai-btn]');
        if (!aiBtn) { return []; }
        return [{
          title: '打开 AI 助手',
          meta: '工具',
          icon: 'mdi:api',
          search: 'ai 助手 assistant zs',
          run: function () { aiBtn.click(); }
        }];
      });
    }
  } else {
    document.addEventListener('DOMContentLoaded', refresh);
  }
})();
