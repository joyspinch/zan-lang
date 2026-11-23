/* Admin 表单与表格挂件：表格增强、远程外键检索、条件显隐、行内编辑与系统设置页签。
   通过 window.ZanAdmin.onMount 挂载到任意新渲染的面板与弹窗碎片，同时保留
   window.applyFragmentWidgets 供外部分片脚本直接调用。 */
(function () {
  var bus = window.ZanAdmin;

  function layerMsg(text, kind) {
    if (window.Layer) { window.Layer.msg(text, kind); }
  }

  function layerNotify(title, text, kind) {
    if (window.Layer) { window.Layer.notify(title, text, kind); }
  }

  function syncSettingExtras(root) {
    if (!root) { return; }
    var active = root.querySelector('[data-setting-tabs] [data-pane].on');
    if (!active) { return; }
    var name = active.getAttribute('data-pane');
    root.querySelectorAll('[data-pane-extra]').forEach(function (card) {
      card.hidden = card.getAttribute('data-pane-extra') !== name;
    });
  }

  // 刷新或重绘设置面板后，切回之前停留在的子页签。
  function restorePane(root, path) {
    if (!root || !bus || !bus.getPaneMemo) { return; }
    var name = bus.getPaneMemo(bus.base(path));
    if (!name) { return; }
    var btn = root.querySelector('[data-setting-tabs] [data-pane="' + name + '"]');
    if (btn) { btn.click(); }
  }

  // ---- inline cell editing -------------------------------------------------
  function wireInlineEdit(root) {
    var tables = root.querySelectorAll('table[data-quick]');
    for (var qi = 0; qi < tables.length; qi++) { wireQuickTable(tables[qi]); }
  }

  function wireQuickTable(table) {
    if (table._quickWired) { return; }
    table._quickWired = true;
    var url = table.getAttribute('data-quick');
    table.addEventListener('keydown', function (ev) {
      var el = ev.target;
      if (el.getAttribute && el.getAttribute('data-quick-field')
          && ev.key === 'Enter') { el.blur(); }
    });
    table.addEventListener('change', function (ev) {
      var el = ev.target;
      if (!el.getAttribute || !el.getAttribute('data-quick-field')
          || !el.getAttribute('data-id')) { return; }
      if (el.disabled) { return; }
      var value = el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value;
      var committed = el.getAttribute('data-prev');
      if (committed === null) { committed = value; }
      if (value === committed) { return; }
      el.disabled = true;
      fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({
          id: el.getAttribute('data-id'),
          field: el.getAttribute('data-quick-field'),
          value: value
        }).toString()
      }).then(function (r) { if (!r.ok) { throw new Error('保存失败'); } return r.json(); }).then(function (j) {
        if (j && j.code === '0000') { el.setAttribute('data-prev', value); return; }
        revertQuick(el);
        layerMsg((j && j.msg) || '保存失败', 'bad');
      }).catch(function () {
        revertQuick(el);
        layerMsg('网络错误，修改未保存', 'bad');
      }).then(function () { el.disabled = false; });
    });
  }

  function revertQuick(el) {
    var prev = el.getAttribute('data-prev');
    if (el.type === 'checkbox') { el.checked = prev === '1'; return; }
    if (prev !== null) { el.value = prev; }
  }

  // ---- conditional fields --------------------------------------------------
  function wireShowWhen(root) {
    if (!root) { return; }
    var targets = root.querySelectorAll('[data-show-field]');
    if (!targets.length) { return; }
    var forms = [];
    for (var i = 0; i < targets.length; i++) {
      var host = targets[i].closest('form') || root;
      if (forms.indexOf(host) < 0) { forms.push(host); }
    }
    forms.forEach(function (host) {
      function sync() {
        var nodes = host.querySelectorAll('[data-show-field]');
        for (var j = 0; j < nodes.length; j++) {
          var el = nodes[j];
          var fname = el.getAttribute('data-show-field');
          var raw = el.getAttribute('data-show-values') || '';
          var ctrl = host.querySelector('[name="' + fname + '"]');
          if (!ctrl) { continue; }
          var val = ctrl.type === 'checkbox' ? (ctrl.checked ? '1' : '0') : String(ctrl.value || '').trim();
          var allowed = raw === '' ? true : raw.split(',').some(function (v) { return v.trim() === val; });
          el.classList.toggle('show-when-hidden', !allowed);
          if (el.matches('input, select, textarea')) {
            if (!allowed) {
              if (el.hasAttribute('required')) {
                el.setAttribute('data-sw-req', '1');
                el.removeAttribute('required');
              }
            } else if (el.getAttribute('data-sw-req') === '1') {
              el.setAttribute('required', '');
              el.removeAttribute('data-sw-req');
            }
          }
        }
      }
      if (!host._showWhenWired) {
        host._showWhenWired = true;
        host.addEventListener('change', sync);
        host.addEventListener('input', sync);
      }
      sync();
    });
  }

  // ---- remote foreign-key picker -------------------------------------------
  function wirePick(root) {
    if (!root) { return; }
    var picks = root.matches && root.matches('.pick[data-pick]')
      ? [root] : root.querySelectorAll('.pick[data-pick]');
    for (var i = 0; i < picks.length; i++) {
      (function (pick) {
        if (pick.getAttribute('data-pick-wired')) { return; }
        pick.setAttribute('data-pick-wired', '1');
        var hidden = pick.querySelector('input[type="hidden"]');
        var box = pick.querySelector('input[type="search"]');
        var url = pick.getAttribute('data-pick');
        if (!hidden || !box || !url) { return; }
        var menu = document.createElement('div');
        menu.className = 'pick-menu';
        menu.setAttribute('role', 'listbox');
        menu.hidden = true;
        pick.appendChild(menu);
        var timer = null;
        var querySeq = 0;
        var currentItems = [];
        var activeIdx = -1;
        function close() { menu.hidden = true; activeIdx = -1; }
        function fetchUrl(params) {
          return url + (url.indexOf('?') >= 0 ? '&' : '?') + params;
        }
        function choose(it) {
          hidden.value = it.id;
          box.value = it.label;
          close();
          hidden.dispatchEvent(new Event('change', { bubbles: true }));
        }
        function highlight(idx) {
          var opts = menu.querySelectorAll('.pick-opt');
          if (!opts.length) { activeIdx = -1; return; }
          if (idx < 0) { idx = opts.length - 1; }
          if (idx >= opts.length) { idx = 0; }
          activeIdx = idx;
          for (var oi = 0; oi < opts.length; oi++) {
            opts[oi].classList.toggle('active', oi === activeIdx);
          }
          if (opts[activeIdx] && opts[activeIdx].scrollIntoView) {
            opts[activeIdx].scrollIntoView({ block: 'nearest' });
          }
        }
        function render(items) {
          menu.innerHTML = '';
          currentItems = items || [];
          activeIdx = currentItems.length ? 0 : -1;
          if (!currentItems.length) {
            var none = document.createElement('div');
            none.className = 'pick-none';
            none.textContent = '无匹配结果';
            menu.appendChild(none);
          }
          for (var k = 0; k < currentItems.length; k++) {
            (function (it, idx) {
              var opt = document.createElement('div');
              opt.className = 'pick-opt' + (idx === activeIdx ? ' active' : '');
              opt.setAttribute('role', 'option');
              opt.textContent = it.label;
              opt.addEventListener('mouseenter', function () { highlight(idx); });
              opt.addEventListener('mousedown', function (ev) {
                ev.preventDefault();
                choose(it);
              });
              menu.appendChild(opt);
            })(currentItems[k], k);
          }
          menu.hidden = false;
        }
        function showError() {
          currentItems = [];
          activeIdx = -1;
          menu.innerHTML = '<div class="pick-none" role="status">加载失败，请重新输入搜索词重试</div>';
          menu.hidden = false;
        }
        function apply(j, many) {
          if (!j || j.code !== '0000') { showError(); return; }
          var parsed = j.data;
          if (typeof parsed === 'string') {
            try { parsed = JSON.parse(parsed); } catch (e) { showError(); return; }
          }
          render(many ? (parsed || []) : (parsed && parsed.id ? [parsed] : []));
        }
        function query(kw) {
          var seq = ++querySeq;
          menu.innerHTML = '<div class="pick-none" role="status">加载中…</div>';
          menu.hidden = false;
          fetch(fetchUrl('kw=' + encodeURIComponent(kw)),
                { credentials: 'same-origin' })
            .then(function (r) { if (!r.ok) { throw new Error('pick'); } return r.json(); })
            .then(function (j) { if (seq === querySeq) { apply(j, true); } })
            .catch(function () { if (seq === querySeq) { showError(); } });
        }
        box.addEventListener('input', function () {
          hidden.value = '';
          clearTimeout(timer);
          timer = setTimeout(function () { query(box.value.trim()); }, 250);
        });
        box.addEventListener('focus', function () {
          if (box.value.trim()) { query(box.value.trim()); }
        });
        box.addEventListener('keydown', function (ev) {
          if (menu.hidden) {
            if (ev.key === 'ArrowDown') {
              ev.preventDefault();
              query(box.value.trim());
            }
            return;
          }
          if (ev.key === 'ArrowDown') {
            ev.preventDefault();
            highlight(activeIdx + 1);
          } else if (ev.key === 'ArrowUp') {
            ev.preventDefault();
            highlight(activeIdx - 1);
          } else if (ev.key === 'Enter' && activeIdx >= 0 && currentItems[activeIdx]) {
            ev.preventDefault();
            choose(currentItems[activeIdx]);
          } else if (ev.key === 'Escape') {
            ev.preventDefault();
            close();
          }
        });
        box.addEventListener('blur', function () { setTimeout(close, 150); });
        if (hidden.value && !box.value) {
          fetch(fetchUrl('id=' + encodeURIComponent(hidden.value)),
                { credentials: 'same-origin' })
            .then(function (r) { return r.ok ? r.json() : null; })
            .then(function (j) {
              if (!j || j.code !== '0000' || !j.data) { return; }
              var it = j.data;
              if (typeof it === 'string') {
                try { it = JSON.parse(it); } catch (e) { return; }
              }
              if (it && it.label) { box.value = it.label; }
            })
            .catch(function () { /* 静默：id 仍在，条件有效 */ });
        }
      })(picks[i]);
    }
  }

  function applyFragmentWidgets(root) {
    if (!root) { return; }
    if (window.ZanTable) { ZanTable.wire(root); }
    wirePick(root);
    var sels = root.querySelectorAll('select[data-value]');
    for (var i = 0; i < sels.length; i++) {
      var v = sels[i].getAttribute('data-value');
      if (v) { sels[i].value = v; }
    }
    wireShowWhen(root);
    root.querySelectorAll('table[data-quick] [data-quick-field]').forEach(function (control) {
      control.setAttribute('data-prev', control.type === 'checkbox'
        ? (control.checked ? '1' : '0') : control.value);
    });
    wireInlineEdit(root);
    var tabs = root.querySelectorAll('[data-code]');
    for (var t = 0; t < tabs.length; t++) {
      if (tabs[t]._codeWired) { continue; }
      tabs[t]._codeWired = true;
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

  // ---- settings panes, AI vendor presets & SMTP test -----------------------
  document.addEventListener('click', function (ev) {
    var paneBtn = ev.target.closest('[data-setting-tabs] [data-pane]');
    if (paneBtn) {
      var name = paneBtn.getAttribute('data-pane');
      if (bus && bus.setPaneMemo) {
        bus.setPaneMemo(bus.base(bus.activePath()), name);
      }
      var scope = paneBtn.closest('.ad-pane, .lay-box') || document;
      var tabs = paneBtn.closest('[data-setting-tabs]');
      var all = tabs.querySelectorAll('[data-pane]');
      for (var i = 0; i < all.length; i++) { all[i].classList.toggle('on', all[i] === paneBtn); }
      var panes = scope.querySelectorAll('[data-pane-body]');
      for (var p = 0; p < panes.length; p++) {
        panes[p].classList.toggle('open', panes[p].getAttribute('data-pane-body') === name);
      }
      var extras = scope.querySelectorAll('[data-pane-extra]');
      for (var e = 0; e < extras.length; e++) {
        extras[e].hidden = extras[e].getAttribute('data-pane-extra') !== name;
      }
      return;
    }

    var preset = ev.target.closest('[data-preset]');
    if (preset) {
      var presetScope = preset.closest('.ad-pane, .lay-box') || document;
      var set = function (n, v) {
        var f = presetScope.querySelector('[name="' + n + '"]');
        if (f) { f.value = v; }
      };
      set('ai.provider', preset.getAttribute('data-preset'));
      set('ai.baseUrl', preset.getAttribute('data-url'));
      set('ai.model', preset.getAttribute('data-model'));
      var aiTab = presetScope.querySelector('[data-setting-tabs] [data-pane="AI 助手"]');
      if (aiTab) { aiTab.click(); }
      var key = presetScope.querySelector('[name="ai.apiKey"]');
      if (key) { key.focus(); }
      var vName = preset.parentNode && preset.parentNode.parentNode
        ? preset.parentNode.parentNode.querySelector('.vendor-name') : null;
      layerMsg('已填入 ' + (vName ? vName.textContent : '预设'));
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
        if (j && j.code === '0000') { layerMsg(j.msg || '测试邮件已发送'); }
        else { layerNotify('发送失败', (j && j.msg) || '请检查邮件配置', 'bad'); }
      }).catch(function () {
        layerNotify('发送失败', '请求未完成', 'bad');
      }).then(function () { test.disabled = false; });
    }
  });

  // ---- pager limit & page-jump controls -----------------------------------
  document.addEventListener('change', function (ev) {
    var sel = ev.target.closest ? ev.target.closest('[data-pager-limit]') : null;
    if (!sel) { return; }
    var rawBase = sel.getAttribute('data-pager-base') || (bus ? bus.activePath() : location.pathname);
    var u = new URL(rawBase, location.origin);
    u.searchParams.set('page', '1');
    u.searchParams.set('limit', sel.value);
    var target = u.pathname + u.search;
    if (bus && bus.navigate) { bus.navigate(target, '', sel); }
    else { location.href = target; }
  });

  document.addEventListener('submit', function (ev) {
    var form = ev.target.closest ? ev.target.closest('[data-pager-jump]') : null;
    if (!form) { return; }
    ev.preventDefault();
    var inp = form.querySelector('input[name="page"]');
    if (!inp) { return; }
    var max = parseInt(inp.getAttribute('max'), 10) || 1;
    var pg = parseInt(inp.value, 10);
    if (!pg || pg < 1) { pg = 1; }
    if (pg > max) { pg = max; }
    var rawBase = form.getAttribute('data-pager-base') || form.getAttribute('action')
      || (bus ? bus.activePath() : location.pathname);
    var u = new URL(rawBase, location.origin);
    u.searchParams.set('page', String(pg));
    var lim = form.getAttribute('data-pager-cur-limit');
    if (lim) { u.searchParams.set('limit', lim); }
    var target = u.pathname + u.search;
    if (bus && bus.navigate) { bus.navigate(target, '', form); }
    else { location.href = target; }
  });

  if (bus && bus.onMount) {
    bus.onMount(function (root, ctx) {
      applyFragmentWidgets(root);
      syncSettingExtras(root);
      if (ctx && (ctx.kind === 'panel' || ctx.kind === 'initial') && ctx.path) {
        restorePane(root, ctx.path);
      }
    });
  }

  if (bus && bus.onActivate) {
    bus.onActivate(function (root) {
      if (window.ZanTable && ZanTable.reflow) { ZanTable.reflow(root); }
    });
  }

  if (bus && bus.onUnmount) {
    bus.onUnmount(function (root) {
      if (window.ZanTable && ZanTable.unwire) { ZanTable.unwire(root); }
    });
  }
})();
