/* Admin 外壳内核：标签工作区、碎片面板加载、Layer 弹层、通用写操作与生命周期总线。
   业务部件（表单/表格挂件、实时监控图表、AI 助手）通过 window.ZanAdmin 扩展总线接入。 */
(function () {
  var panelHost, panel, tabs, toasts;
  var panes = {};
  var activePane = null;
  var state = { tabs: [], active: '' };
  var KEY = 'zanweb.admin.tabs.v2';
  var loadSeq = 0;
  var loadController = null;
  var ctxMenu = null;
  var paneMemo = {};

  // ---- lifecycle & command bus (window.ZanAdmin) --------------------------
  var initHooks = [];
  var mountHooks = [];
  var unmountHooks = [];
  var activateHooks = [];
  var deactivateHooks = [];
  var dirtyGuards = [];
  var moduleRegistry = [];
  var tabKeyRules = [{ prefix: '/admin/monitor/data', param: 't' }];
  var commandProviders = [];
  var initialized = false;

  var pre = window.ZanAdmin && window.ZanAdmin._q ? window.ZanAdmin._q : null;

  function emitInit(ctx) {
    initialized = true;
    for (var i = 0; i < initHooks.length; i++) {
      try { initHooks[i](ctx); } catch (e) { /* isolate plugin errors */ }
    }
  }

  function emitMount(root, ctx) {
    if (!root) { return; }
    for (var i = 0; i < mountHooks.length; i++) {
      try { mountHooks[i](root, ctx); } catch (e) { /* isolate plugin errors */ }
    }
  }

  function emitUnmount(root) {
    if (!root) { return; }
    for (var i = 0; i < unmountHooks.length; i++) {
      try { unmountHooks[i](root); } catch (e) { /* isolate plugin errors */ }
    }
  }

  function emitActivate(root, ctx) {
    if (!root) { return; }
    for (var i = 0; i < activateHooks.length; i++) {
      try { activateHooks[i](root, ctx || {}); } catch (e) { /* isolate plugin errors */ }
    }
  }

  function emitDeactivate(root, ctx) {
    if (!root) { return; }
    for (var i = 0; i < deactivateHooks.length; i++) {
      try { deactivateHooks[i](root, ctx || {}); } catch (e) { /* isolate plugin errors */ }
    }
  }

  function el(id) { return document.getElementById(id); }

  function panelEl() { return activePane ? activePane.el : (panel || document.getElementById('ad-panel')); }

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  function toast(msg, kind) {
    if (!toasts) { return; } /* SPA host without the shell: nowhere to show */
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

  function pathOf(path, keepHash) {
    var u = new URL(path || '/admin', location.origin);
    var q = Array.from(u.searchParams.entries()).sort(function (a, b) {
      return a[0] === b[0] ? a[1].localeCompare(b[1]) : a[0].localeCompare(b[0]);
    });
    u.search = '';
    q.forEach(function (p) { u.searchParams.append(p[0], p[1]); });
    return u.pathname + (u.search ? u.search : '') +
      (keepHash ? (u.hash || '') : '');
  }

  function normalize(path) { return pathOf(path, false); }

  function rememberPane(path) {
    var u = new URL(path || '/admin', location.origin);
    if (u.hash && u.hash.length > 1) {
      paneMemo[normalize(path)] = decodeURIComponent(u.hash.slice(1));
    }
  }

  function restore() {
    try {
      var raw = localStorage.getItem(KEY);
      if (raw) { state = JSON.parse(raw); }
    } catch (e) { state = { tabs: [], active: '' }; }
    if (!state || !Array.isArray(state.tabs)) { state = { tabs: [], active: '' }; }
    state.tabs = state.tabs.map(function (t) {
      return { path: normalize(t.path), title: t.title || t.path };
    });
    state.tabs = state.tabs.filter(function (t) {
      return base(t.path).indexOf('/admin') === 0 &&
        normalize(t.path) !== '/admin/monitor/data';
    });
    var byScreen = {};
    var merged = [];
    for (var i = 0; i < state.tabs.length; i++) {
      var key = tabKey(state.tabs[i].path);
      if (!byScreen[key]) { merged.push(byScreen[key] = state.tabs[i]); }
      else { byScreen[key].path = state.tabs[i].path; }
    }
    state.tabs = merged;
    state.active = state.active ? normalize(state.active) : '';
    if (state.active && find(state.active) < 0) {
      var activeKey = tabKey(state.active);
      for (var j = 0; j < state.tabs.length; j++) {
        if (tabKey(state.tabs[j].path) === activeKey) { state.active = state.tabs[j].path; break; }
      }
    }
    if (find(state.active) < 0) { state.active = ''; }
  }

  function find(path) {
    path = normalize(path);
    for (var i = 0; i < state.tabs.length; i++) {
      if (state.tabs[i].path === path) { return i; }
    }
    return -1;
  }

  function paint() {
    if (!tabs) { return; }
    tabs.innerHTML = '';
    state.tabs.forEach(function (t) {
      var key = tabKey(t.path);
      var b = document.createElement('span');
      b.className = 'ad-tab' + (t.path === state.active ? ' active' : '');
      b.setAttribute('data-tab-key', key);
      if (hasPaneDirty(panes[key])) { b.classList.add('dirty'); }
      b.title = t.path;
      var label = document.createElement('button');
      label.type = 'button';
      label.className = 'ad-tab-label';
      if (t.path === state.active) { label.setAttribute('aria-current', 'page'); }
      label.textContent = t.title || t.path;
      b.appendChild(label);
      if (state.tabs.length > 1) {
        var x = document.createElement('button');
        x.type = 'button';
        x.className = 'x';
        x.textContent = '×';
        x.setAttribute('aria-label', '关闭 ' + (t.title || t.path));
        x.onclick = function (ev) { ev.stopPropagation(); close(t.path); };
        b.appendChild(x);
      }
      label.onclick = function () { open(t.path, t.title); };
      b.oncontextmenu = function (ev) { ev.preventDefault(); tabMenu(ev, t); };
      tabs.appendChild(b);
    });
    document.querySelectorAll('.ad-side a.mi').forEach(function (a) {
      var href = a.getAttribute('href');
      a.classList.toggle('active', tabKey(href) === tabKey(state.active));
    });
    if (window._ensureActiveMenuGroupOpen) { window._ensureActiveMenuGroupOpen(); }
    save();
    showActiveTab();
    paintTabNav();
  }

  function paintTabDirty() {
    if (!tabs) { return; }
    var nodes = tabs.querySelectorAll('.ad-tab[data-tab-key]');
    for (var i = 0; i < nodes.length; i++) {
      var key = nodes[i].getAttribute('data-tab-key');
      nodes[i].classList.toggle('dirty', hasPaneDirty(panes[key]));
    }
  }

  function paintTabNav() {
    if (!tabs) { return; }
    var bar = tabs.parentNode;
    if (!bar) { return; }
    var arrows = bar.querySelectorAll('[data-tabnav]');
    var over = tabs.scrollWidth > tabs.clientWidth + 1;
    for (var i = 0; i < arrows.length; i++) { arrows[i].hidden = !over; }
  }

  function showActiveTab() {
    if (!tabs) { return; }
    var a = tabs.querySelector('.ad-tab.active');
    if (!a) { return; }
    var left = a.offsetLeft;
    var right = left + a.offsetWidth;
    if (left < tabs.scrollLeft) { tabs.scrollLeft = left - 8; }
    else if (right > tabs.scrollLeft + tabs.clientWidth) {
      tabs.scrollLeft = right - tabs.clientWidth + 8;
    }
  }

  function base(path) {
    var u = new URL(path || '/admin', location.origin);
    return u.pathname;
  }

  function tabKey(path) {
    var u = new URL(path || '/admin', location.origin);
    for (var i = tabKeyRules.length - 1; i >= 0; i--) {
      var r = tabKeyRules[i];
      if (r.fn) {
        try {
          var custom = r.fn(u);
          if (custom) { return String(custom); }
        } catch (e) { /* isolate plugin errors */ }
        continue;
      }
      if (u.pathname === r.prefix || u.pathname.indexOf(r.prefix + '/') === 0) {
        return r.param
          ? (u.pathname + '?' + r.param + '=' + encodeURIComponent(u.searchParams.get(r.param) || ''))
          : u.pathname;
      }
    }
    return u.pathname;
  }

  function registerTabKey(prefixOrFn, paramName) {
    if (typeof prefixOrFn === 'function') {
      tabKeyRules.push({ fn: prefixOrFn });
      return;
    }
    if (typeof prefixOrFn === 'string' && prefixOrFn) {
      tabKeyRules.push({ prefix: prefixOrFn, param: paramName || '' });
    }
  }

  function tabMenu(ev, t) {
    hideTabMenu();
    var i = find(t.path);
    var m = document.createElement('div');
    m.className = 'ad-ctx';
    var items = [
      ['刷新', function () { open(t.path, t.title, { forceLoad: true }); }, false],
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

  function hasAnyDirtyTab(list) {
    for (var i = 0; i < list.length; i++) {
      if (hasPaneDirty(panes[tabKey(list[i].path)])) { return true; }
    }
    return false;
  }

  function destroyTabPanes(list) {
    for (var i = 0; i < list.length; i++) {
      destroyPane(tabKey(list[i].path));
    }
  }

  function closeOthers(path, skipDirty) {
    var removed = state.tabs.filter(function (o) { return o.path !== path; });
    if (!skipDirty && hasAnyDirtyTab(removed)) {
      Layer.confirm('其他标签页中有未保存的修改，确定全部关闭并放弃吗？', function () {
        closeOthers(path, true);
      }, '未保存修改', { danger: true, okText: '确认关闭' });
      return;
    }
    destroyTabPanes(removed);
    state.tabs = state.tabs.filter(function (o) { return o.path === path; });
    var keep = state.tabs[0];
    if (keep) { open(keep.path, keep.title, { skipDirty: true }); }
  }

  function closeRight(path, skipDirty) {
    var i = find(path);
    if (i < 0) { return; }
    var removed = state.tabs.slice(i + 1);
    if (!skipDirty && hasAnyDirtyTab(removed)) {
      Layer.confirm('右侧标签页中有未保存的修改，确定关闭并放弃吗？', function () {
        closeRight(path, true);
      }, '未保存修改', { danger: true, okText: '确认关闭' });
      return;
    }
    destroyTabPanes(removed);
    state.tabs = state.tabs.slice(0, i + 1);
    if (find(state.active) < 0) { open(path, state.tabs[i].title, { skipDirty: true }); return; }
    paint();
  }

  function closeAllTabs(skipDirty) {
    var removed = state.tabs.filter(function (o) { return tabKey(o.path) !== '/admin'; });
    if (!skipDirty && hasAnyDirtyTab(removed)) {
      Layer.confirm('标签页中有未保存的修改，确定全部关闭并放弃吗？', function () {
        closeAllTabs(true);
      }, '未保存修改', { danger: true, okText: '确认关闭' });
      return;
    }
    destroyTabPanes(removed);
    var existingHome = null;
    for (var i = 0; i < state.tabs.length; i++) {
      if (tabKey(state.tabs[i].path) === '/admin') { existingHome = state.tabs[i]; break; }
    }
    var home = existingHome || { path: '/admin', title: '仪表盘' };
    state.tabs = [home];
    open(home.path, home.title, { skipDirty: true });
  }

  function close(path, skipDirty) {
    var i = find(path);
    if (i < 0) { return; }
    var key = tabKey(path);
    if (!skipDirty && hasPaneDirty(panes[key])) {
      Layer.confirm('标签“' + (state.tabs[i].title || path) + '”有未保存的修改，确定关闭并放弃吗？', function () {
        close(path, true);
      }, '未保存修改', { danger: true, okText: '确认关闭' });
      return;
    }
    state.tabs.splice(i, 1);
    destroyPane(key);
    if (state.active === path) {
      var next = state.tabs[Math.min(i, state.tabs.length - 1)];
      if (next) { open(next.path, next.title, { skipDirty: true }); return; }
      state.active = '';
    }
    paint();
  }

  function hasDirtyForm(root) {
    return !!(root && root.querySelector('form[data-submit][data-dirty="1"], [data-dirty="1"]'));
  }

  function hasPaneDirty(pane) {
    if (!pane || !pane.el || !pane.loaded) { return false; }
    if (hasDirtyForm(pane.el)) { return true; }
    for (var i = 0; i < dirtyGuards.length; i++) {
      try {
        if (dirtyGuards[i](pane.el, pane.path)) { return true; }
      } catch (e) { /* isolate plugin errors */ }
    }
    return false;
  }

  function clearDirtyForms(root) {
    if (!root) { return; }
    var dirty = root.querySelectorAll('[data-dirty]');
    for (var i = 0; i < dirty.length; i++) { dirty[i].removeAttribute('data-dirty'); }
    paintTabDirty();
  }

  function ensurePane(key, path) {
    var pane = panes[key];
    if (pane) { return pane; }
    var node = document.createElement('div');
    node.className = 'ad-pane';
    node.setAttribute('data-pane-key', key);
    node.hidden = true;
    if (panelHost) { panelHost.appendChild(node); }
    pane = {
      key: key,
      path: path,
      el: node,
      loaded: false,
      loading: false,
      seq: 0,
      controller: null,
      scrollTop: 0,
      scrollLeft: 0
    };
    panes[key] = pane;
    return pane;
  }

  function deactivatePane(pane) {
    if (!pane || !pane.el) { return; }
    pane.scrollTop = pane.el.scrollTop || 0;
    pane.scrollLeft = pane.el.scrollLeft || 0;
    if (pane.loaded) {
      emitDeactivate(pane.el, { kind: 'panel', path: pane.path, key: pane.key });
    }
    pane.el.classList.remove('active');
    pane.el.hidden = true;
  }

  function activatePane(pane) {
    if (!pane || !pane.el) { return; }
    if (activePane && activePane !== pane) { deactivatePane(activePane); }
    activePane = pane;
    panel = pane.el;
    pane.el.hidden = false;
    pane.el.classList.add('active');
    pane.el.scrollTop = pane.scrollTop || 0;
    pane.el.scrollLeft = pane.scrollLeft || 0;
    if (pane.loaded) {
      emitActivate(pane.el, { kind: 'panel', path: pane.path, key: pane.key });
    }
  }

  function destroyPane(key) {
    var pane = panes[key];
    if (!pane) { return; }
    pane.seq++;
    if (pane.controller) {
      try { pane.controller.abort(); } catch (e) {}
      pane.controller = null;
    }
    if (activePane === pane) {
      if (pane.loaded) {
        emitDeactivate(pane.el, { kind: 'panel', path: pane.path, key: pane.key });
      }
      activePane = null;
      panel = panelHost;
    }
    if (pane.loaded) {
      emitUnmount(pane.el);
    }
    if (pane.el && pane.el.parentNode) { pane.el.remove(); }
    delete panes[key];
  }

  function isDangerConfirm(el, text) {
    if (el && (el.classList.contains('danger') || el.hasAttribute('data-danger'))) {
      return true;
    }
    return /(删除|清空|移除|注销|销毁)/.test(text || '');
  }

  function focusablesIn(container) {
    if (!container) { return []; }
    var all = container.querySelectorAll(
      'a[href], button:not([disabled]), input:not([disabled]):not([type="hidden"]), ' +
      'select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])'
    );
    var out = [];
    for (var i = 0; i < all.length; i++) {
      var n = all[i];
      if (n.offsetParent !== null || n.getClientRects().length > 0) { out.push(n); }
    }
    return out;
  }

  function trapFocus(container, ev) {
    if (!container || ev.key !== 'Tab') { return false; }
    var list = focusablesIn(container);
    if (!list.length) {
      ev.preventDefault();
      if (container.focus) { container.focus(); }
      return true;
    }
    var first = list[0];
    var last = list[list.length - 1];
    var cur = document.activeElement;
    if (ev.shiftKey) {
      if (cur === first || !container.contains(cur)) {
        ev.preventDefault();
        last.focus();
      }
    } else if (cur === last || !container.contains(cur)) {
      ev.preventDefault();
      first.focus();
    }
    return true;
  }

  function open(path, title, optsOrReplace, skipDirty) {
    var opts = (optsOrReplace && typeof optsOrReplace === 'object')
      ? optsOrReplace
      : { replace: !!optsOrReplace, skipDirty: !!skipDirty };
    if (skipDirty) { opts.skipDirty = true; }

    var rawPath = path || '/admin';
    rememberPane(rawPath);
    var normPath = normalize(rawPath);
    var key = tabKey(normPath);

    var existingIdx = find(normPath);
    if (existingIdx < 0) {
      for (var j = 0; j < state.tabs.length; j++) {
        if (tabKey(state.tabs[j].path) === key) {
          existingIdx = j;
          break;
        }
      }
    }

    if (opts.fromTabLink && existingIdx >= 0 && normPath === normalize(key)) {
      normPath = state.tabs[existingIdx].path;
    }

    var pane = tabs ? ensurePane(key, normPath) : null;
    var needLoad = !pane || !pane.loaded || pane.path !== normPath || !!opts.forceLoad;

    if (needLoad && !opts.skipDirty && pane && hasPaneDirty(pane)) {
      Layer.confirm('当前页面有未保存的表单修改，确定离开吗？', function () {
        clearDirtyForms(pane.el);
        open(normPath, title, {
          replace: !!opts.replace,
          skipDirty: true,
          forceLoad: !!opts.forceLoad,
          quiet: !!opts.quiet
        });
      }, '未保存修改', { danger: true, okText: '确认离开' });
      return;
    }
    if (!pane && !opts.skipDirty && panel && hasDirtyForm(panel) && normPath !== state.active) {
      Layer.confirm('当前页面有未保存的表单修改，确定离开吗？', function () {
        clearDirtyForms(panel);
        open(normPath, title, { replace: !!opts.replace, skipDirty: true, forceLoad: !!opts.forceLoad });
      }, '未保存修改', { danger: true, okText: '确认离开' });
      return;
    }

    if (existingIdx >= 0) {
      state.tabs[existingIdx].path = normPath;
      if (title && !opts.fromTabLink) { state.tabs[existingIdx].title = title; }
      else if (title && !state.tabs[existingIdx].title) { state.tabs[existingIdx].title = title; }
    } else {
      state.tabs.push({ path: normPath, title: title || normPath });
    }

    var same = state.active === normPath;
    state.active = normPath;
    paint();
    if (opts.replace || same) { history.replaceState({ path: normPath }, '', normPath); }
    else { history.pushState({ path: normPath }, '', normPath); }

    if (pane) {
      var wasLoaded = pane.loaded;
      var wasActive = activePane === pane;
      activatePane(pane);
      if (needLoad) {
        loadPane(pane, normPath, !!opts.quiet);
      } else if (wasLoaded && !wasActive) {
        /* activatePane already emitted onActivate when switching back to cached pane */
      }
    } else {
      load(normPath, !!opts.quiet);
    }
  }

  function restoreHistory() {
    rememberPane(location.pathname + location.search + location.hash);
    var path = normalize(location.pathname + location.search + location.hash);
    var i = find(path);
    var title = i >= 0 ? state.tabs[i].title : path;
    open(path, title, { replace: true });
  }

  // ---- panel --------------------------------------------------------------

  function markRequired(root) {
    root.querySelectorAll('.form input[required], .form select[required], .form textarea[required]').forEach(function (inp) {
      var prev = inp.previousElementSibling;
      while (prev && prev.tagName !== 'LABEL') {
        prev = prev.previousElementSibling;
      }
      if (prev && prev.tagName === 'LABEL') {
        prev.classList.add('req');
      }
    });
  }

  /* innerHTML does not execute <script>, and a screen may carry one. */
  function runScripts(root) {
    root.querySelectorAll('script').forEach(function (old) {
      var s = document.createElement('script');
      s.textContent = old.textContent;
      old.replaceWith(s);
    });
  }

  function ensureStyle(href) {
    if (!href || !document.head) { return; }
    var links = document.head.querySelectorAll('link[rel="stylesheet"]');
    for (var i = 0; i < links.length; i++) {
      if (links[i].getAttribute('href') === href) { return; }
    }
    var lk = document.createElement('link');
    lk.setAttribute('rel', 'stylesheet');
    lk.setAttribute('href', href);
    document.head.appendChild(lk);
  }

  function matchesModule(root, selector) {
    if (!root || !selector) { return false; }
    if (root.matches && root.matches(selector)) { return true; }
    return !!(root.querySelector && root.querySelector(selector));
  }

  function registerModule(selector, spec) {
    if (!selector || typeof selector !== 'string') { return; }
    var entry = typeof spec === 'string' ? { script: spec }
      : (typeof spec === 'function' ? { loader: spec } : (spec || {}));
    for (var i = 0; i < moduleRegistry.length; i++) {
      if (moduleRegistry[i].selector === selector) {
        if (entry.script) { moduleRegistry[i].script = entry.script; }
        if (entry.style) { moduleRegistry[i].style = entry.style; }
        if (entry.loader) { moduleRegistry[i].loader = entry.loader; }
        return;
      }
    }
    var mod = {
      selector: selector,
      script: entry.script || '',
      style: entry.style || '',
      loader: typeof entry.loader === 'function' ? entry.loader : null,
      hooks: {
        mount: typeof entry.mount === 'function' ? entry.mount : null,
        unmount: typeof entry.unmount === 'function' ? entry.unmount : null,
        activate: typeof entry.activate === 'function' ? entry.activate : null,
        deactivate: typeof entry.deactivate === 'function' ? entry.deactivate : null
      },
      wired: false,
      loaded: false,
      promise: null
    };
    moduleRegistry.push(mod);
  }

  function wireModuleHooks(mod) {
    if (mod.wired) { return; }
    mod.wired = true;
    var h = mod.hooks;
    if (h.mount) {
      mountHooks.push(function (root, ctx) {
        if (matchesModule(root, mod.selector)) { h.mount(root, ctx); }
      });
    }
    if (h.unmount) {
      unmountHooks.push(function (root) {
        if (matchesModule(root, mod.selector)) { h.unmount(root); }
      });
    }
    if (h.activate) {
      activateHooks.push(function (root, ctx) {
        if (matchesModule(root, mod.selector)) { h.activate(root, ctx); }
      });
    }
    if (h.deactivate) {
      deactivateHooks.push(function (root, ctx) {
        if (matchesModule(root, mod.selector)) { h.deactivate(root, ctx); }
      });
    }
  }

  function ensureModulesFor(root, ctx) {
    if (!root || !moduleRegistry.length) { return; }
    for (var i = 0; i < moduleRegistry.length; i++) {
      (function (mod) {
        if (!matchesModule(root, mod.selector)) { return; }
        if (mod.style) { ensureStyle(mod.style); }
        if (mod.loaded) { return; }
        function runInitial() {
          if (root.isConnected !== false && matchesModule(root, mod.selector)) {
            if (mod.hooks.mount) {
              try { mod.hooks.mount(root, ctx || {}); } catch (e) {}
            }
          }
        }
        if (!mod.promise) {
          var p = mod.loader ? Promise.resolve().then(mod.loader)
            : (mod.script ? import(mod.script) : Promise.resolve(null));
          mod.promise = p.then(function (ns) {
            mod.loaded = true;
            var api = (ns && ns.default && typeof ns.default === 'object') ? ns.default : ns;
            if (api && typeof api === 'object') {
              if (!mod.hooks.mount && typeof api.mount === 'function') { mod.hooks.mount = api.mount; }
              if (!mod.hooks.unmount && typeof api.unmount === 'function') { mod.hooks.unmount = api.unmount; }
              if (!mod.hooks.activate && typeof api.activate === 'function') { mod.hooks.activate = api.activate; }
              if (!mod.hooks.deactivate && typeof api.deactivate === 'function') { mod.hooks.deactivate = api.deactivate; }
            }
            wireModuleHooks(mod);
            return api;
          }).catch(function () {
            mod.promise = null;
          });
        }
        mod.promise.then(function () { if (mod.loaded) { runInitial(); } });
      })(moduleRegistry[i]);
    }
  }

  function bootShellModules() {
    var host = el('ad-shell-modules');
    if (!host) { return; }
    var raw = host.getAttribute('data-modules');
    if (!raw) { return; }
    try {
      var list = JSON.parse(raw);
      if (!Array.isArray(list)) { return; }
      for (var i = 0; i < list.length; i++) {
        var item = list[i];
        if (!item) { continue; }
        if (item.tabPrefix) { registerTabKey(item.tabPrefix, item.tabParam || ''); }
        if (item.selector && (item.script || item.style)) {
          registerModule(item.selector, { script: item.script || '', style: item.style || '' });
        }
      }
    } catch (e) { /* ignore malformed module manifest */ }
  }

  function isPaneInteractive(target, ev) {
    if (Layer.any() || document.querySelector('.cmdk-mask')) { return false; }
    var cur = panelEl();
    if (!cur) { return false; }
    if (target && target !== cur && (!cur.contains || !cur.contains(target))) { return false; }
    var act = (ev && ev.target) || document.activeElement;
    if (act && act.closest && act.closest('input, textarea, select, [contenteditable="true"], [contenteditable=""]')) {
      return false;
    }
    return true;
  }

  function mountFragment(root, ctx) {
    if (!root) { return; }
    markRequired(root);
    emitMount(root, ctx || {});
    ensureModulesFor(root, ctx || {});
    runScripts(root);
  }

  function loadPane(pane, path, quiet) {
    path = normalize(path);
    var seq = ++pane.seq;
    if (pane.controller) { pane.controller.abort(); }
    pane.controller = window.AbortController ? new AbortController() : null;
    pane.path = path;
    pane.loading = true;
    pane.el.setAttribute('aria-busy', 'true');
    if (!pane.loaded && !pane.el.innerHTML) {
      pane.el.innerHTML = '<div class="panel-loading muted">加载中…</div>';
    }
    var opts = { headers: { 'X-Fragment': '1' }, credentials: 'same-origin' };
    if (pane.controller) { opts.signal = pane.controller.signal; }
    return fetch(path, opts)
      .then(function (r) {
        if (seq !== pane.seq) { return ''; }
        if (r.status === 401) { location.href = '/admin/login'; return ''; }
        if (!r.ok) { throw new Error('http ' + r.status); }
        var t = r.headers.get('X-Tab-Title');
        if (t) {
          var i = find(path);
          if (i >= 0) { state.tabs[i].title = decodeURIComponent(t); paint(); }
        }
        return r.text();
      })
      .then(function (html) {
        if (seq !== pane.seq) { return; }
        if (html === '') { throw new Error('empty fragment'); }
        if (pane.loaded) { emitUnmount(pane.el); }
        pane.el.innerHTML = html;
        pane.el.removeAttribute('aria-busy');
        pane.loading = false;
        pane.loaded = true;
        if (!quiet) {
          pane.scrollTop = 0;
          pane.scrollLeft = 0;
          pane.el.scrollTop = 0;
          pane.el.scrollLeft = 0;
        } else {
          pane.el.scrollTop = pane.scrollTop || 0;
          pane.el.scrollLeft = pane.scrollLeft || 0;
        }
        mountFragment(pane.el, {
          kind: 'panel',
          path: path,
          key: pane.key,
          quiet: !!quiet,
          active: activePane === pane
        });
        paintTabDirty();
      })
      .catch(function (err) {
        if (seq !== pane.seq || (err && err.name === 'AbortError')) { return; }
        pane.loading = false;
        pane.el.removeAttribute('aria-busy');
        if (pane.loaded) { emitUnmount(pane.el); }
        pane.loaded = false;
        pane.el.innerHTML = '<div class="panel-error" role="alert"><h2>页面加载失败</h2>' +
          '<p>当前页面暂时无法显示，请检查网络或稍后重试。</p>' +
          '<button type="button" class="btn primary" data-refresh>重新加载</button></div>';
        pane.scrollTop = 0;
        pane.el.scrollTop = 0;
        paintTabDirty();
      });
  }

  function load(path, quiet) {
    path = normalize(path);
    var seq = ++loadSeq;
    if (loadController) { loadController.abort(); }
    loadController = window.AbortController ? new AbortController() : null;
    if (panel) { emitUnmount(panel); }
    if (!panel) { return Promise.resolve(''); }
    panel.setAttribute('aria-busy', 'true');
    var opts = { headers: { 'X-Fragment': '1' }, credentials: 'same-origin' };
    if (loadController) { opts.signal = loadController.signal; }
    return fetch(path, opts)
      .then(function (r) {
        if (seq !== loadSeq) { return ''; }
        if (r.status === 401) { location.href = '/admin/login'; return ''; }
        if (!r.ok) { throw new Error('http ' + r.status); }
        var t = r.headers.get('X-Tab-Title');
        if (t) {
          var i = find(path);
          if (i >= 0) { state.tabs[i].title = decodeURIComponent(t); paint(); }
        }
        return r.text();
      })
      .then(function (html) {
        if (seq !== loadSeq) { return; }
        if (html === '') { throw new Error('empty fragment'); }
        panel.innerHTML = html;
        panel.removeAttribute('aria-busy');
        if (!quiet) { panel.scrollTop = 0; }
        mountFragment(panel, { kind: 'panel', path: path, quiet: !!quiet, active: true });
      })
      .catch(function (err) {
        if (seq !== loadSeq || (err && err.name === 'AbortError')) { return; }
        panel.removeAttribute('aria-busy');
        panel.innerHTML = '<div class="panel-error" role="alert"><h2>页面加载失败</h2>' +
          '<p>当前页面暂时无法显示，请检查网络或稍后重试。</p>' +
          '<button type="button" class="btn primary" data-refresh>重新加载</button></div>';
        panel.scrollTop = 0;
      });
  }

  function reload(quiet) {
    if (activePane) {
      if (quiet) {
        activePane.scrollTop = activePane.el.scrollTop || 0;
        activePane.scrollLeft = activePane.el.scrollLeft || 0;
      }
      return loadPane(activePane, activePane.path, quiet);
    }
    if (state.active) { return load(state.active, quiet); }
  }

  // ---- layer: windows, drawers, prompts and notices ------------------------
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
      var opener = document.activeElement;
      var mask = node('div', 'lay-mask');
      var box = node('div', 'lay-box' + (opts.drawer ? ' drawer' : '')
        + (opts.wide ? ' wide' : '')
        + (opts.kind ? ' ' + opts.kind : ''));
      box.setAttribute('role', 'dialog');
      box.setAttribute('aria-modal', 'true');
      box.setAttribute('tabindex', '-1');
      box.style.zIndex = ++zTop;
      mask.style.zIndex = zTop;
      if (opts.width && window.innerWidth > 768) {
        var want = String(opts.width).trim();
        box.style.width = /^\d+(?:\.\d+)?px$/.test(want)
          ? Math.min(parseFloat(want), window.innerWidth - 24) + 'px'
          : want;
      }

      var head = node('header');
      head.appendChild(node('span', 'title', opts.title || ''));
      var ops = node('span', 'ops');
      if (!opts.drawer && !opts.bare) {
        var max = node('button', 'wbtn');
        max.type = 'button';
        max.title = '最大化 / 还原';
        max.setAttribute('aria-label', '最大化或还原');
        max.innerHTML = '<svg width="12" height="12" viewBox="0 0 12 12" fill="none"'
          + ' stroke="currentColor" stroke-width="1.3" stroke-linejoin="round">'
          + '<rect x="1.6" y="1.6" width="6.6" height="6.6" rx="1"/>'
          + '<path d="M4 10.4h5.4a1 1 0 0 0 1-1V4"/></svg>';
        max.onclick = function () {
          box.classList.toggle('max');
          if (!box.classList.contains('max')) { place(box, opts); }
        };
        ops.appendChild(max);
      }
      var x = node('button', 'wbtn', '×');
      x.type = 'button';
      x.title = '关闭';
      x.setAttribute('aria-label', '关闭弹窗');
      x.onclick = function () { Layer.requestClose(id); };
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
        mask.onclick = function () { Layer.requestClose(id); };
      }
      stack.push({ id: id, box: box, mask: mask, opener: opener });
      return { id: id, box: box, body: body, head: head };
    }

    function place(box, opts) {
      if (opts.drawer || window.innerWidth <= 768) { return; }
      var w = box.offsetWidth;
      var h = box.offsetHeight;
      var left = Math.max(16, (window.innerWidth - w) / 2);
      var top = Math.max(24, (window.innerHeight - h) / 2.4);
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
      var entry = stack[stack.length - 1];
      if (opts.content !== undefined) {
        w.body.innerHTML = opts.content;
        ready(w, opts);
        return Promise.resolve(w.id);
      }
      entry.url = opts.url;
      entry.body = w.body;
      entry.head = w.head;
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
      mountFragment(w.body, { kind: 'dialog', url: opts.url || '' });
      var ft = w.body.querySelector('footer, form > footer');
      if (ft) { ft.classList.add('lay-footer'); }
      if (!opts.drawer) {
        requestAnimationFrame(function () { place(w.box, opts); });
      }
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
      confirm: function (text, onOk, title, opts) {
        opts = opts || {};
        var w = shell({ title: title || '确认', bare: true,
                        maskClose: false });
        w.box.classList.add('ask');
        w.body.innerHTML = '';
        w.body.appendChild(node('p', 'ask-text', text));
        var foot = node('div', 'ask-ops');
        var no = node('button', 'btn', opts.cancelText || '取消');
        no.type = 'button';
        no.onclick = function () { Layer.close(w.id); };
        var yes = node('button', 'btn ' + (opts.danger ? 'danger solid' : 'primary'),
                       opts.okText || '确定');
        yes.type = 'button';
        yes.onclick = function () { Layer.close(w.id); onOk(); };
        foot.appendChild(no);
        foot.appendChild(yes);
        w.body.appendChild(foot);
        if (opts.danger) { no.focus(); } else { yes.focus(); }
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
        var x = node('button', 'x', '×');
        x.type = 'button';
        x.setAttribute('aria-label', '关闭通知');
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
          var entry = stack[i];
          var body = entry.box.querySelector('.body');
          if (body) { emitUnmount(body); }
          entry.box.remove();
          entry.mask.remove();
          stack.splice(i, 1);
          if (entry.opener && entry.opener.isConnected && entry.opener.focus) {
            try { entry.opener.focus(); } catch (e) {}
          }
          return;
        }
      },
      requestClose: function (id) {
        for (var i = stack.length - 1; i >= 0; i--) {
          if (stack[i].id !== id) { continue; }
          var entry = stack[i];
          if (!entry.box.classList.contains('ask') && !entry.forceClose && hasDirtyForm(entry.box)) {
            Layer.confirm('当前表单有未保存的修改，确定放弃并关闭吗？', function () {
              entry.forceClose = true;
              Layer.close(id);
            }, '未保存修改', { danger: true, okText: '放弃修改' });
            return false;
          }
          Layer.close(id);
          return true;
        }
        return false;
      },
      topBox: function () {
        return stack.length ? stack[stack.length - 1].box : null;
      },
      topUrl: function () {
        for (var i = stack.length - 1; i >= 0; i--) {
          if (stack[i].url) { return stack[i]; }
        }
        return null;
      },
      img: function (src, title) {
        var w = shell({ title: title || '预览', width: 'min(90vw, 860px)',
                        bare: true, maskClose: true });
        w.box.classList.add('photo');
        w.body.innerHTML = '';
        var im = node('img', 'photo-img');
        im.src = src;
        im.onclick = function () { Layer.close(w.id); };
        w.body.appendChild(im);
        return w.id;
      },
      urlEntries: function () {
        var out = [];
        for (var i = 0; i < stack.length; i++) {
          if (stack[i].url) { out.push(stack[i]); }
        }
        return out;
      },
      closeTop: function () {
        if (!stack.length) { return false; }
        var top = stack[stack.length - 1];
        Layer.close(top.id);
        return true;
      },
      requestCloseTop: function () {
        if (!stack.length) { return false; }
        var top = stack[stack.length - 1];
        Layer.requestClose(top.id);
        return true;
      },
      any: function () { return stack.length > 0; }
    };
  })();

  window.Layer = Layer;

  // ---- dialogs ------------------------------------------------------------

  function dialog(url, wide, title, width) {
    return Layer.open({ url: url, title: title || '编辑',
                        wide: !!wide,
                        width: width || undefined });
  }

  function closeDialog() { Layer.closeTop(); }
  function requestCloseDialog() { Layer.requestCloseTop(); }

  function dialogLoad(entry, url) {
    if (url) { entry.url = url; }
    if (entry.body) { emitUnmount(entry.body); }
    return fetch(entry.url, {
      headers: { 'X-Fragment': '1' }, credentials: 'same-origin'
    }).then(function (r) {
      if (r.ok) { return r.text(); }
      return r.json().then(function (j) { throw new Error(j.msg || '加载失败'); });
    }).then(function (html) {
      entry.body.innerHTML = html;
      var t = entry.body.querySelector('[data-title]');
      if (t) { entry.head.querySelector('.title').textContent = t.getAttribute('data-title'); }
      mountFragment(entry.body, { kind: 'dialog', url: entry.url || '' });
    }).catch(function (e) {
      Layer.msg(e.message || '加载失败', 'bad');
    });
  }

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
      return r.text().then(function (raw) {
        var j = null;
        if (raw) {
          try { j = JSON.parse(raw); } catch (e) { j = null; }
        }
        if (!r.ok) {
          var detail = j && j.msg ? j.msg : (raw || '服务器未返回错误详情');
          if (detail.length > 240) { detail = detail.slice(0, 240) + '…'; }
          throw new Error('HTTP ' + r.status + '：' + detail);
        }
        if (!j) { throw new Error('服务器返回了无法解析的响应'); }
        return j;
      });
    });
  }

  function afterWrite() {
    var subs = Layer.urlEntries();
    for (var i = 0; i < subs.length; i++) { dialogLoad(subs[i]); }
    reload(true);
  }

  function submit(form) {
    var data = new FormData(form);
    var parts = [];
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
    var originText = '';
    if (btn) {
      originText = btn.textContent;
      btn.disabled = true;
      btn.textContent = '保存中…';
    }
    post(form.getAttribute('action'), parts.join('&')).then(function (j) {
      if (btn) { btn.disabled = false; btn.textContent = originText || '保存'; }
      if (!j) { return; }
      if (j.code === '0000') {
        form.removeAttribute('data-dirty');
        paintTabDirty();
        toast(j.msg || '已保存', 'ok');
        closeDialog();
        afterWrite();
        return;
      }
      toast(j.msg || '保存失败', 'bad');
    }).catch(function (e) {
      if (btn) { btn.disabled = false; btn.textContent = originText || '保存'; }
      toast(e && e.message ? e.message : '请求失败', 'bad');
    });
  }

  // ---- wiring -------------------------------------------------------------

  document.addEventListener('click', function (ev) {
    hideTabMenu();
    var top = Layer.topUrl();
    var inBox = top && top.body.contains(ev.target);
    var a = ev.target.closest('[data-tab]');
    if (a && !inBox) {
      ev.preventDefault();
      open(a.getAttribute('href'), a.getAttribute('data-title') || a.textContent.trim(), { fromTabLink: true });
      document.body.classList.remove('side-open');
      return;
    }
    var l = ev.target.closest('[data-load]');
    if (l) {
      ev.preventDefault();
      var href = l.getAttribute('href');
      if (inBox) {
        var target = href.charAt(0) === '?' ? base(top.url) + href : href;
        dialogLoad(top, target);
        return;
      }
      var path = href.charAt(0) === '?' ? base(state.active) + href : href;
      open(path, l.getAttribute('data-title') || l.textContent.trim());
      return;
    }
    var preview = ev.target.closest('[data-sql-preview], [data-text-preview]');
    if (preview) {
      ev.preventDefault();
      var text = preview.getAttribute('data-sql-preview') ||
                 preview.getAttribute('data-text-preview') || '';
      var content = '<div class="text-preview"><pre class="sql-detail-code mono">' +
                    esc(text) + '</pre><p class="hint">内容仅来自当前已脱敏的监控快照，不含请求参数。</p></div>';
      Layer.open({ title: preview.hasAttribute('data-text-preview') ? '错误详情' : 'SQL 模板详情',
                   content: content, wide: true });
      return;
    }
    var d = ev.target.closest('[data-dialog]');
    if (d) {
      ev.preventDefault();
      dialog(d.getAttribute('href') || d.getAttribute('data-dialog'),
             d.hasAttribute('data-wide'),
             d.getAttribute('data-title') || d.textContent.trim(),
             d.getAttribute('data-width'));
      return;
    }
    var dr = ev.target.closest('[data-drawer]');
    if (dr) {
      ev.preventDefault();
      Layer.drawer({ url: dr.getAttribute('href') || dr.getAttribute('data-drawer'),
                     title: dr.getAttribute('data-title') || dr.textContent.trim(),
                     width: dr.getAttribute('data-width') || undefined });
      return;
    }
    var ft = ev.target.closest('.form-tab');
    if (ft) {
      ev.preventDefault();
      var fbox = ft.closest('form');
      if (fbox) {
        var ftabs = fbox.querySelectorAll('.form-tab');
        for (var fi = 0; fi < ftabs.length; fi++) {
          ftabs[fi].classList.toggle('active', ftabs[fi] === ft);
        }
        var fpanes = fbox.querySelectorAll('.form-pane');
        for (var fp = 0; fp < fpanes.length; fp++) {
          fpanes[fp].classList.toggle('active',
            fpanes[fp].getAttribute('data-pane') === ft.getAttribute('data-pane'));
        }
      }
      return;
    }
    var gt = ev.target.closest('.gen-tab');
    if (gt) {
      ev.preventDefault();
      var bar = gt.closest('.gen-tabbar');
      var col = bar ? bar.getAttribute('data-tab-col') : '';
      if (col) {
        var u = new URL(state.active || location.href, location.origin);
        var val = gt.getAttribute('data-tab-val') || '';
        if (val) { u.searchParams.set(col, val); } else { u.searchParams.delete(col); }
        u.searchParams.delete('page');
        open(u.pathname + (u.search ? u.search : ''), gt.textContent.trim());
      }
      return;
    }
    var x = ev.target.closest('[data-export]');
    if (x) {
      ev.preventDefault();
      var runExport = function () {
        Layer.msg('正在导出…');
        fetch(x.getAttribute('href') || x.getAttribute('data-export'),
              { credentials: 'same-origin' })
          .then(function (r) {
            if (r.status === 401) { location.href = '/admin/login'; return null; }
            if (!r.ok) { throw new Error('http ' + r.status); }
            return r.blob().then(function (b) {
              var cd = r.headers.get('Content-Disposition') || '';
              var m = /filename\*=UTF-8''([^;]+)/.exec(cd) ||
                      /filename="?([^";]+)"?/.exec(cd);
              var name = m ? decodeURIComponent(m[1])
                : (x.getAttribute('data-filename') || 'export');
              var a = document.createElement('a');
              a.href = URL.createObjectURL(b);
              a.download = name;
              document.body.appendChild(a);
              a.click();
              setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 4000);
              toast('已导出 ' + name, 'ok');
            });
          })
          .catch(function () { toast('导出失败', 'bad'); });
      };
      var exportConfirm = x.getAttribute('data-confirm');
      if (exportConfirm) {
        Layer.confirm(exportConfirm, runExport, '确认', { danger: isDangerConfirm(x, exportConfirm) });
      } else { runExport(); }
      return;
    }
    var up = ev.target.closest('[data-upload]');
    if (up) {
      ev.preventDefault();
      var pick = document.createElement('input');
      pick.type = 'file';
      pick.hidden = true;
      document.body.appendChild(pick);
      pick.addEventListener('change', function () {
        var f = pick.files && pick.files[0];
        pick.remove();
        if (!f) { return; }
        fetch(up.getAttribute('data-upload') + '?name=' + encodeURIComponent(f.name), {
          method: 'POST',
          credentials: 'same-origin',
          body: f
        }).then(function (r) {
          if (r.status === 401) { location.href = '/admin/login'; return null; }
          return r.json();
        }).then(function (j) {
          if (!j) { return; }
          var okFlag = j.code === '0000';
          toast(j.msg || (okFlag ? '已上传' : '上传失败'), okFlag ? 'ok' : 'bad');
          if (okFlag) { reload(true); }
        }).catch(function () { toast('上传失败', 'bad'); });
      });
      pick.click();
      return;
    }
    var bt = ev.target.closest('[data-batch]');
    if (bt) {
      ev.preventDefault();
      var scope = bt.closest('.card') || document;
      var ids = [];
      var checked = scope.querySelectorAll('tr[data-id] td.zt-cell input[type="checkbox"]');
      for (var bi = 0; bi < checked.length; bi++) {
        if (checked[bi].checked) {
          var tr = checked[bi].closest('tr[data-id]');
          if (tr) { ids.push(tr.getAttribute('data-id')); }
        }
      }
      if (!ids.length) { toast('请先选择数据', 'bad'); return; }
      var runBatch = function () {
        var args = 'ids=' + encodeURIComponent(ids.join(','));
        var val = bt.getAttribute('data-value');
        if (val !== null && val !== '') { args += '&value=' + encodeURIComponent(val); }
        post(bt.getAttribute('data-batch'), args)
          .then(function (j) {
            if (!j) { return; }
            toast(j.msg || '完成', j.code === '0000' ? 'ok' : 'bad');
            if (j.code !== '0000') { return; }
            closeDialog();
            reload(true);
          })
          .catch(function (e) { toast(e && e.message ? e.message : '请求失败', 'bad'); });
      };
      var batchConfirm = bt.getAttribute('data-confirm');
      if (batchConfirm) {
        Layer.confirm(batchConfirm, runBatch, '确认', { danger: isDangerConfirm(bt, batchConfirm) });
      } else { runBatch(); }
      return;
    }
    var pc = ev.target.closest('[data-pick-clear]');
    if (pc) {
      ev.preventDefault();
      var wrap = pc.closest('.pick');
      if (wrap) {
        var hid = wrap.querySelector('input[type="hidden"]');
        var txt = wrap.querySelector('input[type="search"]');
        if (hid) { hid.value = ''; }
        if (txt) { txt.value = ''; txt.focus(); }
      }
      return;
    }
    var p = ev.target.closest('[data-post]');
    if (p) {
      ev.preventDefault();
      var run = function () {
        var overDialog = Layer.topUrl() != null;
        post(p.getAttribute('data-post'), p.getAttribute('data-args') || '')
          .then(function (j) {
            if (!j) { return; }
            toast(j.msg || '完成', j.code === '0000' ? 'ok' : 'bad');
            if (j.code !== '0000') { return; }
            if (overDialog) { afterWrite(); return; }
            closeDialog();
            reload(true);
          })
          .catch(function (e) { toast(e && e.message ? e.message : '请求失败', 'bad'); });
      };
      var confirmText = p.getAttribute('data-confirm');
      if (confirmText) {
        Layer.confirm(confirmText, run, '确认', { danger: isDangerConfirm(p, confirmText) });
      } else { run(); }
      return;
    }
    var nav = ev.target.closest('[data-tabnav]');
    if (nav) {
      ev.preventDefault();
      var dir = parseInt(nav.getAttribute('data-tabnav'), 10) || 1;
      tabs.scrollLeft = tabs.scrollLeft + dir * Math.max(120, tabs.clientWidth * 0.7);
      return;
    }
    if (ev.target.closest('[data-close]')) { ev.preventDefault(); requestCloseDialog(); }
    if (ev.target.closest('[data-refresh]')) {
      ev.preventDefault();
      var dirtyRefresh = activePane ? hasPaneDirty(activePane) : (panel && hasDirtyForm(panel));
      if (dirtyRefresh) {
        Layer.confirm('当前页面有未保存的表单修改，确定刷新并丢弃吗？', function () {
          clearDirtyForms(panelEl());
          reload();
        }, '未保存修改', { danger: true, okText: '确认刷新' });
      } else {
        reload();
      }
    }
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
      ev.preventDefault();
      var parts = [];
      new FormData(search).forEach(function (v, k) {
        if (String(v).length) {
          parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(v));
        }
      });
      var top = Layer.topUrl();
      if (top && top.body.contains(search)) {
        var sbase = top.url.split('?')[0];
        var spath = search.getAttribute('action') || sbase;
        if (spath.charAt(0) !== '/') { spath = sbase.split('?')[0]; }
        if (parts.length) { spath = spath.split('?')[0] + '?' + parts.join('&'); }
        dialogLoad(top, spath);
        return;
      }
      var path = search.getAttribute('action') || base(state.active);
      if (parts.length) { path = path + '?' + parts.join('&'); }
      open(path, search.getAttribute('data-title') || path);
      return;
    }
    var form = ev.target.closest('[data-submit]');
    if (!form) { return; }
    ev.preventDefault();
    submit(form);
  });

  document.addEventListener('mouseover', function (ev) {
    var a = ev.target.closest('[data-tip]');
    if (a) { Layer.tip(a, a.getAttribute('data-tip')); }
  });
  document.addEventListener('mouseout', function (ev) {
    if (ev.target.closest('[data-tip]')) { Layer.untip(); }
  });

  document.addEventListener('click', function (ev) {
    var im = ev.target.closest('[data-img]');
    if (!im) { return; }
    ev.preventDefault();
    Layer.img(im.getAttribute('data-img'), im.getAttribute('data-title'));
  });

  function markDirtyEvent(ev) {
    var t = ev.target;
    if (!t || (t.getAttribute && t.getAttribute('data-quick-field'))) { return; }
    var form = t.closest ? t.closest('form[data-submit]') : null;
    if (form) {
      form.setAttribute('data-dirty', '1');
      paintTabDirty();
    }
  }
  document.addEventListener('input', markDirtyEvent);
  document.addEventListener('change', markDirtyEvent);

  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Tab') {
      var cmdk = document.querySelector('.cmdk-mask');
      if (cmdk) { trapFocus(cmdk, ev); return; }
      var top = Layer.topBox();
      if (top) { trapFocus(top, ev); }
      return;
    }
    if (ev.key === 'Escape') {
      if (window._closeCmdK && window._closeCmdK()) { return; }
      Layer.untip(); hideTabMenu(); Layer.requestCloseTop();
    }
  });

  // ---- sidebar icons -------------------------------------------------------
  var ICONS = {
    'mdi:account': '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
    'mdi:account-circle': '<circle cx="12" cy="12" r="10"/><circle cx="12" cy="10" r="3"/><path d="M7 20.66V19a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v1.66"/>',
    'mdi:alert-circle-outline': '<circle cx="12" cy="12" r="10"/><line x1="12" x2="12" y1="8" y2="12"/><line x1="12" x2="12.01" y1="16" y2="16"/>',
    'mdi:antenna': '<path d="M2 12 7 2"/><path d="m7 12 5-10"/><path d="m12 12 5-10"/><path d="m17 12 5-10"/><path d="M4.5 7h15"/><path d="M12 16v6"/>',
    'mdi:menu': '<line x1="4" x2="20" y1="6" y2="6"/><line x1="4" x2="20" y1="12" y2="12"/><line x1="4" x2="20" y1="18" y2="18"/>',
    'mdi:package-variant': '<path d="m12 2 9 5v10l-9 5-9-5V7l9-5Z"/><path d="m3 7 9 5 9-5"/><path d="M12 12v10"/>',
    'mdi:store': '<path d="M3 9h18l-1.5-4.5h-15L3 9Z"/><path d="M4 9v11h16V9"/><path d="M9 20v-6h6v6"/>',
    'mdi:api': '<polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/>',
    'mdi:book-multiple': '<path d="m12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83Z"/><path d="m22 17.65-9.17 4.16a2 2 0 0 1-1.66 0L2 17.65"/><path d="m22 12.65-9.17 4.16a2 2 0 0 1-1.66 0L2 12.65"/>',
    'mdi:book-open': '<path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"/><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"/>',
    'mdi:chart-line': '<path d="M3 3v18h18"/><path d="M18 17V9"/><path d="M13 17V5"/><path d="M8 17v-3"/>',
    'mdi:chart-bar': '<line x1="6" x2="6" y1="20" y2="14"/><line x1="12" x2="12" y1="20" y2="8"/><line x1="18" x2="18" y1="20" y2="4"/>',
    'mdi:chart-timeline-variant': '<path d="M22 12h-4l-3 9L9 3l-3 9H2"/>',
    'mdi:clock-outline': '<circle cx="12" cy="12" r="10"/><polyline points="12 6 12 12 16 14"/>',
    'mdi:cog': '<line x1="21" x2="14" y1="4" y2="4"/><line x1="10" x2="3" y1="4" y2="4"/><line x1="21" x2="12" y1="12" y2="12"/><line x1="8" x2="3" y1="12" y2="12"/><line x1="21" x2="16" y1="20" y2="20"/><line x1="12" x2="3" y1="20" y2="20"/><line x1="14" x2="14" y1="2" y2="6"/><line x1="8" x2="8" y1="10" y2="14"/><line x1="16" x2="16" y1="18" y2="22"/>',
    'mdi:comment-multiple': '<path d="M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"/>',
    'mdi:database-search': '<ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M3 5V19A9 3 0 0 0 21 19V5"/><path d="M3 12A9 3 0 0 0 21 12"/>',
    'mdi:file-document': '<path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7z"/><path d="M14 2v4a2 2 0 0 0 2 2h4"/><path d="M10 9H8"/><path d="M16 13H8"/><path d="M16 17H8"/>',
    'mdi:gauge': '<path d="m12 14 4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/>',
    'mdi:history': '<path d="M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8"/><path d="M3 3v5h5"/><path d="M12 7v5l4 2"/>',
    'mdi:image-area': '<rect width="18" height="18" x="3" y="3" rx="2" ry="2"/><circle cx="9" cy="9" r="2"/><path d="m21 15-3.086-3.086a2 2 0 0 0-2.828 0L6 21"/>',
    'mdi:login': '<path d="M15 3h4a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-4"/><polyline points="10 17 15 12 10 7"/><line x1="15" x2="3" y1="12" y2="12"/>',
    'mdi:memory': '<rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M15 2v2"/><path d="M15 20v2"/><path d="M2 15h2"/><path d="M2 9h2"/><path d="M20 15h2"/><path d="M20 9h2"/><path d="M9 2v2"/><path d="M9 20v2"/>',
    'mdi:shield-account': '<path d="M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z"/><path d="m9 12 2 2 4-4"/>',
    'mdi:sitemap': '<rect x="16" y="16" width="6" height="6" rx="1"/><rect x="2" y="16" width="6" height="6" rx="1"/><rect x="9" y="2" width="6" height="6" rx="1"/><path d="M5 16v-3a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1v3"/><path d="M12 12V8"/>',
    'mdi:table-cog': '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18"/><path d="M9 21V9"/>',
    'mdi:table': '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18"/><path d="M3 15h18"/><path d="M12 3v18"/>',
    'mdi:tag': '<path d="M12.586 2.586A2 2 0 0 0 11.172 2H4a2 2 0 0 0-2 2v7.172a2 2 0 0 0 .586 1.414l8.704 8.704a2.426 2.426 0 0 0 3.42 0l6.58-6.58a2.426 2.426 0 0 0 0-3.42z"/><circle cx="7.5" cy="7.5" r=".5" fill="currentColor"/>',
    'mdi:web': '<circle cx="12" cy="12" r="10"/><path d="M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20"/><path d="M2 12h20"/>',
    '_': '<circle cx="12" cy="12" r="3.5"/>'
  };

  function renderIcons(root) {
    var nodes = (root || document).querySelectorAll('[data-icon]');
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      if (node.querySelector('.mi-ic')) { continue; }
      var body = ICONS[node.getAttribute('data-icon')] || ICONS['_'];
      var ic = document.createElement('span');
      ic.className = 'mi-ic';
      ic.setAttribute('aria-hidden', 'true');
      ic.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16" fill="none"'
        + ' stroke="currentColor" stroke-width="2" stroke-linecap="round"'
        + ' stroke-linejoin="round">' + body + '</svg>';
      node.insertBefore(ic, node.firstChild);
    }
  }

  // ---- account menu --------------------------------------------------------
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

  // ---- collapsible sidebar menu groups -------------------------------------
  (function sideMenuGroups() {
    var side = document.querySelector('.ad-side');
    if (!side) { return; }
    var groups = side.querySelectorAll('.ad-group');
    if (!groups.length) { return; }

    var STORAGE_KEY = 'zanweb.admin.menu.collapsed.v1';
    var savedCollapsed = {};
    var hasSaved = false;
    try {
      var raw = localStorage.getItem(STORAGE_KEY);
      if (raw) {
        savedCollapsed = JSON.parse(raw) || {};
        hasSaved = Object.keys(savedCollapsed).length > 0;
      }
    } catch (e) {
      savedCollapsed = {};
    }

    var groupList = [];
    for (var g = 0; g < groups.length; g++) {
      var grp = groups[g];
      var name = grp.textContent.trim();
      var items = [];
      var n = grp.nextElementSibling;
      while (n && !n.classList.contains('ad-group') && !n.classList.contains('ad-foot')) {
        if (n.classList.contains('mi')) { items.push(n); }
        n = n.nextElementSibling;
      }
      groupList.push({ el: grp, name: name, items: items });
    }

    function saveState() {
      try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(savedCollapsed));
      } catch (e) {}
    }

    function applyState(info) {
      var isCollapsed = !!savedCollapsed[info.name];
      info.el.classList.toggle('collapsed', isCollapsed);
      info.el.setAttribute('aria-expanded', isCollapsed ? 'false' : 'true');
      for (var i = 0; i < info.items.length; i++) {
        info.items[i].classList.toggle('collapsed-by-group', isCollapsed);
      }
    }

    function findActiveGroup() {
      for (var i = 0; i < groupList.length; i++) {
        var hasActive = groupList[i].items.some(function (item) {
          return item.classList.contains('active');
        });
        if (hasActive) { return groupList[i]; }
      }
      return null;
    }

    var activeGrp = findActiveGroup();
    if (!hasSaved && groupList.length > 2) {
      var defaultOpen = activeGrp || groupList[0];
      for (var j = 0; j < groupList.length; j++) {
        if (groupList[j] !== defaultOpen) {
          savedCollapsed[groupList[j].name] = true;
        }
      }
      saveState();
    } else if (activeGrp && savedCollapsed[activeGrp.name]) {
      delete savedCollapsed[activeGrp.name];
      saveState();
    }

    groupList.forEach(function (info) {
      applyState(info);
      info.el.setAttribute('role', 'button');
      info.el.setAttribute('tabindex', '0');
      info.el.title = '点击折叠/展开';
      info.el.addEventListener('click', function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        var willCollapse = !info.el.classList.contains('collapsed');
        if (willCollapse) {
          savedCollapsed[info.name] = true;
        } else {
          delete savedCollapsed[info.name];
        }
        saveState();
        applyState(info);
      });
      info.el.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' || ev.key === ' ') {
          ev.preventDefault();
          info.el.click();
        }
      });
    });

    window._ensureActiveMenuGroupOpen = function () {
      var cur = findActiveGroup();
      if (cur && cur.el.classList.contains('collapsed')) {
        delete savedCollapsed[cur.name];
        saveState();
        applyState(cur);
      }
    };
  })();

  // ---- Ctrl+K sidebar filter & command palette -----------------------------
  (function menuFilter() {
    var box = document.getElementById('ad-find');
    if (!box) { return; }
    var side = document.querySelector('.ad-side');
    var bounds = '阿八嚓咑妸发旮铪丌咔垃妈拏噢妑七呥仨他屲夕丫帀';
    var letters = 'abcdefghjklmnopqrstwxyz';
    var cache = {};

    var kbd = document.querySelector('.ad-find-kbd');
    if (kbd && /Mac|iPod|iPhone|iPad/.test(navigator.platform || '')) {
      kbd.textContent = '⌘K';
    }

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
      if (side) { side.classList.toggle('filtering', !!q); }
      var items = side.querySelectorAll('a.mi');
      var first = null;
      for (var i = 0; i < items.length; i++) {
        var item = items[i];
        var text = item.textContent.trim();
        var hit = !q || text.toLowerCase().indexOf(q) >= 0 || initials(text).indexOf(q) >= 0;
        item.classList.toggle('filtered', !hit);
        if (hit && !first) { first = item; }
      }
      var groups = side.querySelectorAll('.ad-group');
      for (var g = 0; g < groups.length; g++) {
        var any = false;
        var n = groups[g].nextElementSibling;
        while (n && !n.classList.contains('ad-group') && !n.classList.contains('ad-foot')) {
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

    var cmdkMask = null;
    var cmdkOpener = null;
    function collectCommands() {
      var list = [];
      var links = side ? side.querySelectorAll('a.mi[href]') : [];
      var group = '';
      var nodes = side ? side.children : [];
      for (var i = 0; i < nodes.length; i++) {
        var nd = nodes[i];
        if (nd.classList.contains('ad-group')) { group = nd.textContent.trim(); continue; }
        if (nd.matches && nd.matches('a.mi[href]')) {
          var title = (nd.getAttribute('data-title') || nd.textContent || '').trim();
          var href = nd.getAttribute('href') || '';
          var icon = nd.getAttribute('data-icon') || '_';
          list.push({
            title: title,
            meta: group || href,
            icon: icon,
            search: (title + ' ' + initials(title) + ' ' + group + ' ' + href).toLowerCase(),
            run: (function (link) { return function () { link.click(); }; })(nd)
          });
        }
      }
      if (!list.length && links.length) {
        for (var j = 0; j < links.length; j++) {
          var lk = links[j];
          var t = (lk.getAttribute('data-title') || lk.textContent || '').trim();
          list.push({
            title: t,
            meta: lk.getAttribute('href') || '',
            icon: lk.getAttribute('data-icon') || '_',
            search: (t + ' ' + initials(t)).toLowerCase(),
            run: (function (link) { return function () { link.click(); }; })(lk)
          });
        }
      }
      list.push({
        title: '刷新当前页面',
        meta: '操作',
        icon: 'mdi:history',
        search: '刷新 reload refresh sx',
        run: function () { reload(); }
      });
      var themeBtn = document.querySelector('[data-theme-toggle]');
      if (themeBtn) {
        list.push({
          title: '切换深浅色主题',
          meta: '外观',
          icon: 'mdi:cog',
          search: '主题 深色 浅色 theme dark light zt',
          run: function () { themeBtn.click(); }
        });
      }
      var densityBtn = document.querySelector('[data-density-toggle]');
      if (densityBtn) {
        list.push({
          title: '切换界面密度（紧凑/舒适）',
          meta: '外观',
          icon: 'mdi:table',
          search: '密度 紧凑 舒适 density compact comfortable md',
          run: function () { densityBtn.click(); }
        });
      }
      for (var p = 0; p < commandProviders.length; p++) {
        try {
          var extra = commandProviders[p]();
          if (Array.isArray(extra)) {
            for (var k = 0; k < extra.length; k++) { list.push(extra[k]); }
          }
        } catch (e) { /* isolate plugin errors */ }
      }
      return list;
    }

    function closeCmdK() {
      if (!cmdkMask) { return false; }
      cmdkMask.remove();
      cmdkMask = null;
      if (cmdkOpener && cmdkOpener.isConnected && cmdkOpener.focus) {
        try { cmdkOpener.focus(); } catch (e) {}
      }
      cmdkOpener = null;
      return true;
    }
    window._closeCmdK = closeCmdK;

    function openCmdK() {
      if (cmdkMask) {
        var inp0 = cmdkMask.querySelector('.cmdk-input');
        if (inp0) { inp0.focus(); inp0.select(); }
        return;
      }
      cmdkOpener = document.activeElement;
      var all = collectCommands();
      var activeIdx = 0;
      var shown = all.slice(0, 12);

      var mask = document.createElement('div');
      mask.className = 'cmdk-mask';
      mask.setAttribute('role', 'dialog');
      mask.setAttribute('aria-modal', 'true');
      mask.setAttribute('aria-label', '快速跳转与命令面板');

      var modal = document.createElement('div');
      modal.className = 'cmdk-box';

      var head = document.createElement('div');
      head.className = 'cmdk-head';
      var inp = document.createElement('input');
      inp.type = 'search';
      inp.className = 'cmdk-input';
      inp.placeholder = '搜索菜单、拼音首字母（如 cdgl）或快捷命令…';
      inp.autocomplete = 'off';
      var escKbd = document.createElement('span');
      escKbd.className = 'cmdk-kbd';
      escKbd.textContent = 'ESC';
      head.appendChild(inp);
      head.appendChild(escKbd);

      var listEl = document.createElement('div');
      listEl.className = 'cmdk-list';

      var foot = document.createElement('div');
      foot.className = 'cmdk-foot';
      foot.innerHTML = '<span>↑↓ 选择</span><span>↵ 执行</span><span>Esc 关闭</span>';

      modal.appendChild(head);
      modal.appendChild(listEl);
      modal.appendChild(foot);
      mask.appendChild(modal);
      document.body.appendChild(mask);
      cmdkMask = mask;

      function paintList() {
        listEl.innerHTML = '';
        if (!shown.length) {
          var emp = document.createElement('div');
          emp.className = 'cmdk-empty';
          emp.textContent = '没有匹配的页面或命令';
          listEl.appendChild(emp);
          return;
        }
        if (activeIdx >= shown.length) { activeIdx = 0; }
        if (activeIdx < 0) { activeIdx = shown.length - 1; }
        for (var i = 0; i < shown.length; i++) {
          (function (it, idx) {
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'cmdk-item' + (idx === activeIdx ? ' active' : '');
            var ic = document.createElement('span');
            ic.className = 'mi-ic';
            ic.setAttribute('aria-hidden', 'true');
            ic.innerHTML = '<svg viewBox="0 0 24 24" width="16" height="16" fill="none"'
              + ' stroke="currentColor" stroke-width="2" stroke-linecap="round"'
              + ' stroke-linejoin="round">' + (ICONS[it.icon] || ICONS['_']) + '</svg>';
            var ttl = document.createElement('span');
            ttl.className = 'cmdk-item-title';
            ttl.textContent = it.title;
            var meta = document.createElement('span');
            meta.className = 'cmdk-item-meta';
            meta.textContent = it.meta || '';
            btn.appendChild(ic);
            btn.appendChild(ttl);
            btn.appendChild(meta);
            btn.addEventListener('mouseenter', function () {
              activeIdx = idx;
              var cur = listEl.querySelector('.cmdk-item.active');
              if (cur) { cur.classList.remove('active'); }
              btn.classList.add('active');
            });
            btn.addEventListener('click', function () {
              closeCmdK();
              it.run();
            });
            listEl.appendChild(btn);
          })(shown[i], i);
        }
        var actEl = listEl.querySelector('.cmdk-item.active');
        if (actEl && actEl.scrollIntoView) { actEl.scrollIntoView({ block: 'nearest' }); }
      }

      inp.addEventListener('input', function () {
        var q = inp.value.trim().toLowerCase();
        shown = !q ? all.slice(0, 12) : all.filter(function (it) {
          return (it.search || '').indexOf(q) >= 0;
        }).slice(0, 20);
        activeIdx = 0;
        paintList();
      });

      inp.addEventListener('keydown', function (ev) {
        if (ev.key === 'ArrowDown') {
          ev.preventDefault();
          if (shown.length) { activeIdx = (activeIdx + 1) % shown.length; paintList(); }
        } else if (ev.key === 'ArrowUp') {
          ev.preventDefault();
          if (shown.length) { activeIdx = (activeIdx - 1 + shown.length) % shown.length; paintList(); }
        } else if (ev.key === 'Enter') {
          ev.preventDefault();
          if (shown[activeIdx]) {
            var target = shown[activeIdx];
            closeCmdK();
            target.run();
          }
        }
      });

      mask.addEventListener('click', function (ev) {
        if (ev.target === mask) { closeCmdK(); }
      });

      paintList();
      inp.focus();
    }

    if (kbd) {
      kbd.title = '打开命令面板';
      kbd.addEventListener('click', function (ev) {
        ev.preventDefault();
        ev.stopPropagation();
        openCmdK();
      });
    }

    document.addEventListener('keydown', function (ev) {
      if ((ev.ctrlKey || ev.metaKey) && (ev.key === 'k' || ev.key === 'K')) {
        ev.preventDefault();
        openCmdK();
      }
    });
  })();

  // ---- expose ZanAdmin extension bus --------------------------------------
  var ZanAdmin = {
    Layer: Layer,
    panel: panelEl,
    panelHost: function () { return panelHost || el('ad-panel'); },
    activePath: function () { return state.active; },
    openTab: open,
    navigate: function (path, title, anchor) {
      var top = Layer.topUrl();
      if (top && anchor && top.body.contains(anchor)) {
        dialogLoad(top, path);
        return;
      }
      open(path, title);
    },
    reload: reload,
    post: post,
    toast: toast,
    base: base,
    esc: esc,
    getPaneMemo: function (basePath) { return paneMemo[basePath] || ''; },
    setPaneMemo: function (basePath, name) { if (basePath && name) { paneMemo[basePath] = name; } },
    onInit: function (fn) {
      if (typeof fn !== 'function') { return; }
      initHooks.push(fn);
      if (initialized) { try { fn({ panel: panelEl(), shell: ZanAdmin }); } catch (e) {} }
    },
    onMount: function (fn) {
      if (typeof fn !== 'function') { return; }
      mountHooks.push(fn);
    },
    onUnmount: function (fn) {
      if (typeof fn !== 'function') { return; }
      unmountHooks.push(fn);
    },
    onActivate: function (fn) {
      if (typeof fn !== 'function') { return; }
      activateHooks.push(fn);
    },
    onDeactivate: function (fn) {
      if (typeof fn !== 'function') { return; }
      deactivateHooks.push(fn);
    },
    registerDirtyGuard: function (fn) {
      if (typeof fn === 'function') { dirtyGuards.push(fn); }
    },
    registerModule: registerModule,
    registerTabKey: registerTabKey,
    isPaneInteractive: isPaneInteractive,
    refreshTabDirty: paintTabDirty,
    registerCommand: function (provider) {
      if (typeof provider === 'function') { commandProviders.push(provider); }
    },
    mount: mountFragment,
    unmount: emitUnmount
  };
  window.ZanAdmin = ZanAdmin;

  if (pre && Array.isArray(pre)) {
    for (var qi = 0; qi < pre.length; qi++) {
      var item = pre[qi];
      if (item && ZanAdmin[item[0]]) { ZanAdmin[item[0]](item[1], item[2]); }
    }
  }

  document.addEventListener('DOMContentLoaded', function () {
    panelHost = el('ad-panel');
    panel = panelHost;
    tabs = el('ad-tabs');
    toasts = el('ad-toasts');
    bootShellModules();
    renderIcons(document);
    if (tabs) { restore(); }
    if (tabs) {
      var here = normalize(location.pathname + location.search + location.hash);
      rememberPane(location.pathname + location.search + location.hash);
      var title = document.body.getAttribute('data-tab-title') || here;
      if (find(here) < 0) { state.tabs.push({ path: here, title: title }); }
      state.active = here;
      if (panelHost) {
        panelHost.classList.add('has-panes');
        var initPane = ensurePane(tabKey(here), here);
        while (panelHost.firstChild && panelHost.firstChild !== initPane.el) {
          initPane.el.appendChild(panelHost.firstChild);
        }
        if (initPane.el.parentNode !== panelHost) { panelHost.appendChild(initPane.el); }
        activatePane(initPane);
        initPane.loaded = true;
      }
      history.replaceState({ path: here }, '', here);
      paint();
      window.addEventListener('resize', paintTabNav);
      tabs.addEventListener('wheel', function (ev) {
        if (tabs.scrollWidth <= tabs.clientWidth) { return; }
        ev.preventDefault();
        tabs.scrollLeft = tabs.scrollLeft + (ev.deltaY || ev.deltaX);
      }, { passive: false });
    }
    emitInit({ panel: panelEl(), shell: ZanAdmin });
    var initRoot = panelEl();
    if (initRoot) {
      mountFragment(initRoot, { kind: 'initial', path: state.active, active: true });
    }
    if (tabs) { window.addEventListener('popstate', restoreHistory); }
  });
})();
