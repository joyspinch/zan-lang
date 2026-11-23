'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const repo = path.join(__dirname, '../..');
const adminJs = fs.readFileSync(path.join(repo, 'packages/Zan.Mvc/wwwroot/js/admin.js'), 'utf8');

class ClassList {
  constructor(el) {
    this.el = el;
    this.set = new Set();
  }
  _syncFrom(str) {
    this.set = new Set(String(str || '').trim().split(/\s+/).filter(Boolean));
  }
  _syncTo() {
    this.el._className = Array.from(this.set).join(' ');
  }
  add(...tokens) { tokens.forEach((t) => t && this.set.add(t)); this._syncTo(); }
  remove(...tokens) { tokens.forEach((t) => this.set.delete(t)); this._syncTo(); }
  contains(t) { return this.set.has(t); }
  toggle(t, force) {
    if (force === undefined) { force = !this.set.has(t); }
    if (force) { this.set.add(t); } else { this.set.delete(t); }
    this._syncTo();
    return force;
  }
}

function splitSelector(sel) {
  return sel.split(',').map((s) => s.trim()).filter(Boolean);
}

function matchesSimple(el, sel) {
  if (!el || el.nodeType !== 1) { return false; }
  if (sel === '*') { return true; }
  // Handle simple compound selector like tag#id.cls[attr="val"]:not(...)
  let rest = sel;
  const notMatch = rest.match(/:not\(([^)]+)\)/g);
  if (notMatch) {
    for (const nm of notMatch) {
      const inner = nm.slice(5, -1);
      if (matchesSimple(el, inner)) { return false; }
      rest = rest.replace(nm, '');
    }
  }
  const tagMatch = rest.match(/^([a-zA-Z0-9_-]+)/);
  if (tagMatch) {
    if (el.tagName !== tagMatch[1].toUpperCase()) { return false; }
    rest = rest.slice(tagMatch[1].length);
  }
  const idMatches = rest.match(/#[a-zA-Z0-9_-]+/g) || [];
  for (const idm of idMatches) {
    if (el.id !== idm.slice(1)) { return false; }
    rest = rest.replace(idm, '');
  }
  const attrMatches = rest.match(/\[[^\]]+\]/g) || [];
  for (const am of attrMatches) {
    const body = am.slice(1, -1);
    const eq = body.indexOf('=');
    if (eq < 0) {
      if (!el.hasAttribute(body)) { return false; }
    } else {
      const k = body.slice(0, eq).trim();
      const v = body.slice(eq + 1).trim().replace(/^["']|["']$/g, '');
      if (el.getAttribute(k) !== v) { return false; }
    }
    rest = rest.replace(am, '');
  }
  const clsMatches = rest.match(/\.[a-zA-Z0-9_-]+/g) || [];
  for (const cm of clsMatches) {
    if (!el.classList.contains(cm.slice(1))) { return false; }
    rest = rest.replace(cm, '');
  }
  return true;
}

function matchesSelector(el, selector) {
  for (const part of splitSelector(selector)) {
    const tokens = part.trim().split(/\s+/);
    if (tokens.length === 1) {
      if (matchesSimple(el, tokens[0])) { return true; }
      continue;
    }
    // Support ancestor/direct-child chains like "form[data-submit][data-dirty="1"]" or ".ad-tab.active"
    if (tokens.includes('>')) {
      const segs = part.split('>').map((s) => s.trim());
      let cur = el;
      let ok = true;
      for (let i = segs.length - 1; i >= 0; i--) {
        if (!cur || !matchesSimple(cur, segs[i])) { ok = false; break; }
        cur = cur.parentNode;
      }
      if (ok) { return true; }
    } else {
      let cur = el;
      if (!matchesSimple(cur, tokens[tokens.length - 1])) { continue; }
      let idx = tokens.length - 2;
      cur = cur.parentNode;
      while (cur && idx >= 0) {
        if (matchesSimple(cur, tokens[idx])) { idx--; }
        cur = cur.parentNode;
      }
      if (idx < 0) { return true; }
    }
  }
  return false;
}

class MockElement {
  constructor(tagName, ownerDocument) {
    this.nodeType = 1;
    this.tagName = tagName.toUpperCase();
    this.ownerDocument = ownerDocument;
    this.children = [];
    this.parentNode = null;
    this.attributes = new Map();
    this.style = {};
    this.listeners = new Map();
    this.classList = new ClassList(this);
    this._className = '';
    this._innerHTML = '';
    this._textContent = '';
    this.hidden = false;
    this.scrollTop = 0;
    this.scrollLeft = 0;
    this.offsetWidth = 100;
    this.offsetHeight = 32;
    this.clientWidth = 800;
    this.offsetLeft = 0;
    this.checked = false;
    this.value = '';
    this.disabled = false;
  }
  get className() { return this._className; }
  set className(v) {
    this._className = String(v || '');
    this.classList._syncFrom(this._className);
  }
  get id() { return this.attributes.get('id') || ''; }
  set id(v) { this.setAttribute('id', v); }
  get firstChild() { return this.children[0] || null; }
  get isConnected() {
    let p = this;
    while (p) {
      if (p === this.ownerDocument.body || p === this.ownerDocument.documentElement) { return true; }
      p = p.parentNode;
    }
    return false;
  }
  get textContent() {
    if (this.children.length) {
      return this.children.map((c) => c.textContent).join('');
    }
    return this._textContent;
  }
  set textContent(v) {
    this.children = [];
    this._textContent = String(v ?? '');
  }
  get innerHTML() { return this._innerHTML; }
  set innerHTML(html) {
    this._innerHTML = String(html || '');
    this.children = [];
    if (!this._innerHTML) { return; }
    // Lightweight parser for test fragments (supports nested tags, attributes, text)
    const stack = [this];
    const tokenRe = /<(\/?)([a-zA-Z0-9_-]+)([^>]*)>|([^<]+)/g;
    let m;
    while ((m = tokenRe.exec(this._innerHTML))) {
      if (m[4]) {
        const cur = stack[stack.length - 1];
        if (!cur.children.length) { cur._textContent += m[4]; }
        continue;
      }
      const closing = m[1] === '/';
      const tag = m[2];
      const attrStr = m[3] || '';
      if (closing) {
        if (stack.length > 1) { stack.pop(); }
        continue;
      }
      const child = this.ownerDocument.createElement(tag);
      const attrRe = /([a-zA-Z0-9_:-]+)(?:="([^"]*)")?/g;
      let am;
      while ((am = attrRe.exec(attrStr))) {
        if (am[1] === '/') { continue; }
        child.setAttribute(am[1], am[2] !== undefined ? am[2] : '');
      }
      stack[stack.length - 1].appendChild(child);
      if (!/\/\s*$/.test(attrStr) && !/^(input|br|hr|img)$/i.test(tag)) {
        stack.push(child);
      }
    }
  }
  setAttribute(k, v) {
    const val = String(v);
    this.attributes.set(k, val);
    if (k === 'class') { this.className = val; }
    if (k === 'hidden') { this.hidden = true; }
    if (k === 'value') { this.value = val; }
  }
  getAttribute(k) { return this.attributes.has(k) ? this.attributes.get(k) : null; }
  hasAttribute(k) { return this.attributes.has(k); }
  removeAttribute(k) {
    this.attributes.delete(k);
    if (k === 'hidden') { this.hidden = false; }
  }
  appendChild(child) {
    if (child.parentNode) { child.remove(); }
    child.parentNode = this;
    this.children.push(child);
    return child;
  }
  insertBefore(child, ref) {
    if (!ref) { return this.appendChild(child); }
    if (child.parentNode) { child.remove(); }
    const idx = this.children.indexOf(ref);
    if (idx < 0) { return this.appendChild(child); }
    child.parentNode = this;
    this.children.splice(idx, 0, child);
    return child;
  }
  replaceWith(node) {
    if (!this.parentNode) { return; }
    const idx = this.parentNode.children.indexOf(this);
    if (idx >= 0) {
      node.parentNode = this.parentNode;
      this.parentNode.children.splice(idx, 1, node);
      this.parentNode = null;
    }
  }
  remove() {
    if (!this.parentNode) { return; }
    const idx = this.parentNode.children.indexOf(this);
    if (idx >= 0) { this.parentNode.children.splice(idx, 1); }
    this.parentNode = null;
  }
  get nextElementSibling() {
    if (!this.parentNode) { return null; }
    const i = this.parentNode.children.indexOf(this);
    return i >= 0 ? (this.parentNode.children[i + 1] || null) : null;
  }
  get previousElementSibling() {
    if (!this.parentNode) { return null; }
    const i = this.parentNode.children.indexOf(this);
    return i > 0 ? this.parentNode.children[i - 1] : null;
  }
  getClientRects() { return this.hidden ? [] : [{}]; }
  getBoundingClientRect() { return { left: 0, top: 0, width: 100, height: 32 }; }
  focus() { this.ownerDocument.activeElement = this; }
  select() {}
  contains(node) {
    let p = node;
    while (p) {
      if (p === this) { return true; }
      p = p.parentNode;
    }
    return false;
  }
  matches(sel) { return matchesSelector(this, sel); }
  closest(sel) {
    let p = this;
    while (p && p.nodeType === 1) {
      if (matchesSelector(p, sel)) { return p; }
      p = p.parentNode;
    }
    return null;
  }
  querySelectorAll(sel) {
    const out = [];
    const walk = (node) => {
      for (const c of node.children) {
        if (matchesSelector(c, sel)) { out.push(c); }
        walk(c);
      }
    };
    walk(this);
    return out;
  }
  querySelector(sel) {
    return this.querySelectorAll(sel)[0] || null;
  }
  addEventListener(type, fn) {
    if (!this.listeners.has(type)) { this.listeners.set(type, []); }
    this.listeners.get(type).push(fn);
  }
  removeEventListener(type, fn) {
    const list = this.listeners.get(type);
    if (!list) { return; }
    const idx = list.indexOf(fn);
    if (idx >= 0) { list.splice(idx, 1); }
  }
  dispatchEvent(ev) {
    ev.target = ev.target || this;
    ev.preventDefault = ev.preventDefault || (() => { ev.defaultPrevented = true; });
    ev.stopPropagation = ev.stopPropagation || (() => { ev.cancelBubble = true; });
    let cur = this;
    while (cur) {
      const list = cur.listeners.get(ev.type);
      if (list) {
        for (const fn of list.slice()) { fn.call(cur, ev); }
      }
      if (ev.cancelBubble) { break; }
      cur = cur.parentNode || (cur === this.ownerDocument.documentElement ? this.ownerDocument : null);
    }
  }
  click() {
    if (typeof this.onclick === 'function') {
      this.onclick({ target: this, preventDefault() {}, stopPropagation() {} });
    }
    this.dispatchEvent({ type: 'click', target: this });
  }
}

class MockDocument {
  constructor() {
    this.listeners = new Map();
    this.documentElement = new MockElement('html', this);
    this.head = new MockElement('head', this);
    this.body = new MockElement('body', this);
    this.documentElement.appendChild(this.head);
    this.documentElement.appendChild(this.body);
    this.activeElement = this.body;
  }
  createElement(tag) { return new MockElement(tag, this); }
  getElementById(id) { return this.documentElement.querySelector('#' + id); }
  querySelectorAll(sel) { return this.documentElement.querySelectorAll(sel); }
  querySelector(sel) { return this.documentElement.querySelector(sel); }
  addEventListener(type, fn) {
    if (!this.listeners.has(type)) { this.listeners.set(type, []); }
    this.listeners.get(type).push(fn);
  }
  removeEventListener(type, fn) {
    const list = this.listeners.get(type);
    if (!list) { return; }
    const idx = list.indexOf(fn);
    if (idx >= 0) { list.splice(idx, 1); }
  }
  dispatchEvent(ev) {
    const list = this.listeners.get(ev.type);
    if (!list) { return; }
    for (const fn of list.slice()) { fn.call(this, ev); }
  }
}

function createEnv(options) {
  const opts = options || {};
  const doc = new MockDocument();
  doc.body.className = 'admin';
  doc.body.setAttribute('data-tab-title', '用户管理');

  const side = doc.createElement('aside');
  side.className = 'ad-side';
  side.innerHTML =
    '<a class="mi" href="/admin/system/users" data-tab data-title="用户管理">用户管理</a>' +
    '<a class="mi" href="/admin/system/roles" data-tab data-title="角色管理">角色管理</a>' +
    '<a class="mi" href="/admin/canvas?id=1" data-tab data-title="画布 #1">画布 #1</a>' +
    '<a class="mi" href="/admin/canvas?id=2" data-tab data-title="画布 #2">画布 #2</a>';
  doc.body.appendChild(side);

  const top = doc.createElement('header');
  top.className = 'ad-top';
  top.innerHTML = '<button class="btn sm" type="button" data-refresh>刷新</button>';
  doc.body.appendChild(top);

  const tabbar = doc.createElement('div');
  tabbar.className = 'ad-tabbar';
  const tabsEl = doc.createElement('div');
  tabsEl.className = 'ad-tabs';
  tabsEl.id = 'ad-tabs';
  tabbar.appendChild(tabsEl);
  doc.body.appendChild(tabbar);

  const panelHost = doc.createElement('div');
  panelHost.className = 'ad-panel';
  panelHost.id = 'ad-panel';
  panelHost.innerHTML =
    '<div class="card"><table class="table"><tbody><tr><td><input type="checkbox" id="u1-box"></td><td>Alice</td></tr></tbody></table>' +
    '<a href="/admin/system/users?page=2" data-load id="next-page">下一页</a></div>';
  doc.body.appendChild(panelHost);

  const toasts = doc.createElement('div');
  toasts.id = 'ad-toasts';
  doc.body.appendChild(toasts);

  if (opts.shellModules) {
    const manifest = doc.createElement('div');
    manifest.id = 'ad-shell-modules';
    manifest.hidden = true;
    manifest.setAttribute('hidden', '');
    manifest.setAttribute('data-modules', JSON.stringify(opts.shellModules));
    doc.body.appendChild(manifest);
  }

  const fetches = [];
  const fragments = {
    '/admin/system/users?page=2':
      '<div class="card"><table class="table"><tbody><tr><td><input type="checkbox" id="u2-box"></td><td>Bob (Page 2)</td></tr></tbody></table>' +
      '<form data-submit action="/admin/system/users/save"><input class="input" name="note" id="u2-note" value=""></form>' +
      '<a href="/admin/system/users?page=1" data-load id="prev-page">上一页</a></div>',
    '/admin/system/roles':
      '<div class="card"><div id="roles-view">Roles List</div></div>',
    '/admin/system/users?page=1':
      '<div class="card"><div id="users-p1">Users Page 1</div></div>',
    '/admin/canvas?id=1':
      '<div data-fullbleed data-canvas-app="1"><input id="canvas-1-input" type="text"><div id="canvas-1-stage" tabindex="0"></div></div>',
    '/admin/canvas?id=2':
      '<div data-fullbleed data-canvas-app="2"><div id="canvas-2-stage" tabindex="0"></div></div>'
  };

  const storage = new Map();
  const winListeners = new Map();
  const locationObj = {
    origin: 'http://localhost',
    pathname: '/admin/system/users',
    search: '',
    hash: '',
    href: 'http://localhost/admin/system/users'
  };
  const historyObj = {
    states: [],
    replaceState(s, _t, u) { this.states.push({ op: 'replace', u }); updateLoc(u); },
    pushState(s, _t, u) { this.states.push({ op: 'push', u }); updateLoc(u); }
  };
  function updateLoc(u) {
    const parsed = new URL(u, 'http://localhost');
    locationObj.pathname = parsed.pathname;
    locationObj.search = parsed.search;
    locationObj.hash = parsed.hash;
    locationObj.href = parsed.href;
  }

  const windowObj = {
    innerWidth: 1280,
    innerHeight: 800,
    location: locationObj,
    history: historyObj,
    AbortController,
    addEventListener(t, fn) {
      if (!winListeners.has(t)) { winListeners.set(t, []); }
      winListeners.get(t).push(fn);
    },
    dispatchWindow(t, ev) {
      for (const fn of (winListeners.get(t) || [])) { fn(ev || {}); }
    }
  };

  const sandbox = {
    window: windowObj,
    document: doc,
    location: locationObj,
    history: historyObj,
    URL,
    AbortController,
    FormData: class {},
    localStorage: {
      getItem(k) { return storage.has(k) ? storage.get(k) : null; },
      setItem(k, v) { storage.set(k, String(v)); }
    },
    navigator: { platform: 'Win32' },
    requestAnimationFrame(fn) { fn(); },
    setTimeout,
    clearTimeout,
    fetch(url, opts) {
      fetches.push({ url, opts });
      const html = fragments[url];
      if (!html) { return Promise.resolve({ status: 404, ok: false, headers: { get: () => null }, text: async () => '' }); }
      return Promise.resolve({
        status: 200,
        ok: true,
        headers: { get: () => null },
        text: async () => html
      });
    }
  };
  windowObj.fetch = sandbox.fetch;
  vm.runInNewContext(adminJs, sandbox);
  doc.dispatchEvent({ type: 'DOMContentLoaded' });
  return { doc, windowObj, sandbox, fetches, panelHost, tabsEl, side, top };
}

const flush = () => new Promise((r) => setImmediate(r));

(async function run() {
  const { doc, windowObj, fetches, panelHost, tabsEl, side, top } = createEnv();
  const bus = windowObj.ZanAdmin;
  const events = [];
  bus.onInit(() => events.push('init'));
  bus.onMount((root, ctx) => events.push('mount:' + ctx.path));
  bus.onUnmount((root) => events.push('unmount:' + root.getAttribute('data-pane-key')));
  bus.onActivate((root, ctx) => events.push('activate:' + ctx.path));
  bus.onDeactivate((root, ctx) => events.push('deactivate:' + ctx.path));

  // Wait: events registered after DOMContentLoaded? Let's test live hooks from here forward,
  // and verify initial wrapping right away.
  assert.equal(panelHost.classList.contains('has-panes'), true);
  assert.equal(panelHost.children.length, 1);
  assert.equal(panelHost.children[0].classList.contains('ad-pane'), true);
  assert.equal(panelHost.children[0].classList.contains('active'), true);
  assert.equal(bus.panel(), panelHost.children[0]);

  // 1. Navigate to Page 2 inside Tab 1 via [data-load]
  doc.getElementById('next-page').click();
  await flush();
  assert.equal(fetches.length, 1);
  assert.equal(fetches[0].url, '/admin/system/users?page=2');
  assert.equal(bus.activePath(), '/admin/system/users?page=2');
  assert.deepEqual(events.slice(-2), [
    'unmount:/admin/system/users',
    'mount:/admin/system/users?page=2'
  ]);

  // 2. Mutate Page 2 state: check row checkbox, edit form input (dirty), and scroll pane
  const usersPane = bus.panel();
  const u2Box = usersPane.querySelector('#u2-box');
  const u2Note = usersPane.querySelector('#u2-note');
  assert.ok(u2Box && u2Note);
  u2Box.checked = true;
  u2Note.value = 'draft-edit-page2';
  u2Note.dispatchEvent({ type: 'input', target: u2Note });
  usersPane.scrollTop = 340;

  let usersTab = tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"]');
  assert.equal(usersTab.classList.contains('dirty'), true);

  // 3. Switch to Tab 2 (Roles) -> NO dirty prompt, Tab 1 stays alive in background
  side.querySelector('a[href="/admin/system/roles"]').click();
  await flush();
  assert.equal(doc.querySelector('.lay-box.ask'), null, 'switching tabs must not prompt on dirty keep-alive tab');
  assert.equal(fetches.length, 2);
  assert.equal(fetches[1].url, '/admin/system/roles');
  assert.equal(usersPane.hidden, true);
  assert.equal(usersTab.classList.contains('dirty'), true);
  assert.equal(bus.activePath(), '/admin/system/roles');
  assert.deepEqual(events.slice(-2), [
    'deactivate:/admin/system/users?page=2',
    'mount:/admin/system/roles'
  ]);

  const rolesPane = bus.panel();
  assert.notEqual(rolesPane, usersPane);
  rolesPane.scrollTop = 120;

  // 4. Switch back via tab button -> ZERO fetch, ZERO unmount, exact state & scroll restored
  usersTab.querySelector('.ad-tab-label').click();
  await flush();
  assert.equal(fetches.length, 2, 'switching back to cached tab must not re-fetch');
  assert.equal(bus.panel(), usersPane);
  assert.equal(usersPane.hidden, false);
  assert.equal(rolesPane.hidden, true);
  assert.equal(usersPane.scrollTop, 340);
  assert.equal(usersPane.querySelector('#u2-box').checked, true);
  assert.equal(usersPane.querySelector('#u2-note').value, 'draft-edit-page2');
  assert.deepEqual(events.slice(-2), [
    'deactivate:/admin/system/roles',
    'activate:/admin/system/users?page=2'
  ]);

  // 5. Switch to Roles, then click sidebar bare link /admin/system/users -> keeps ?page=2 and live DOM!
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/roles"] .ad-tab-label').click();
  await flush();
  assert.equal(fetches.length, 2, 'Roles tab is also cached');
  assert.equal(rolesPane.scrollTop, 120);

  side.querySelector('a[href="/admin/system/users"]').click();
  await flush();
  assert.equal(fetches.length, 2, 'clicking sidebar base link must activate cached ?page=2 tab without resetting');
  assert.equal(bus.activePath(), '/admin/system/users?page=2');
  assert.equal(usersPane.querySelector('#u2-box').checked, true);
  assert.equal(usersPane.querySelector('#u2-note').value, 'draft-edit-page2');
  assert.equal(usersPane.scrollTop, 340);

  // 6. Browser popstate back to Roles restores Roles pane without fetch
  windowObj.location.pathname = '/admin/system/roles';
  windowObj.location.search = '';
  windowObj.dispatchWindow('popstate');
  await flush();
  assert.equal(fetches.length, 2, 'popstate back to cached tab must not fetch');
  assert.equal(bus.panel(), rolesPane);

  // 7. Closing dirty background Users tab: cancel keeps tab & state; confirm destroys pane
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"] .x').click();
  await flush();
  const askBoxCancel = doc.querySelector('.lay-box.ask');
  assert.ok(askBoxCancel, 'closing a dirty tab must ask for confirmation');
  askBoxCancel.querySelectorAll('button').find((b) => b.textContent === '取消').click();
  await flush();
  assert.ok(tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"]'), 'cancel must keep dirty tab');
  assert.equal(usersPane.querySelector('#u2-note').value, 'draft-edit-page2');

  // Navigating inside dirty pane via [data-load] also asks before replacing pane
  usersTab = tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"]');
  usersTab.querySelector('.ad-tab-label').click();
  await flush();
  usersPane.querySelector('#prev-page').click();
  await flush();
  const navAsk = doc.querySelector('.lay-box.ask');
  assert.ok(navAsk, 'in-tab [data-load] on dirty pane must ask for confirmation');
  navAsk.querySelectorAll('button').find((b) => b.textContent === '取消').click();
  await flush();
  assert.equal(fetches.length, 2, 'canceling in-tab navigation must not fetch');

  // Switch back to Roles and confirm close on dirty background Users tab
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/roles"] .ad-tab-label').click();
  await flush();
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"] .x').click();
  await flush();
  const askBox = doc.querySelector('.lay-box.ask');
  assert.ok(askBox, 'closing a dirty tab must ask for confirmation');
  const confirmBtn = askBox.querySelector('.btn.danger.solid');
  assert.ok(confirmBtn);
  confirmBtn.click();
  await flush();
  assert.equal(tabsEl.querySelector('.ad-tab[data-tab-key="/admin/system/users"]'), null);
  assert.equal(events.at(-1), 'unmount:/admin/system/users');

  // 8. Custom dirty guard via registerDirtyGuard protects tab on explicit refresh
  let customDirty = true;
  bus.registerDirtyGuard((root) => root === rolesPane && customDirty);
  top.querySelector('[data-refresh]').click();
  await flush();
  const refreshAsk = doc.querySelector('.lay-box.ask');
  assert.ok(refreshAsk, 'registerDirtyGuard must block refresh until confirmed');
  refreshAsk.querySelector('.btn.danger.solid').click();
  await flush();
  assert.equal(fetches.length, 3);
  assert.equal(fetches[2].url, '/admin/system/roles');
  assert.ok(events.slice(-2).includes('unmount:/admin/system/roles'));
  assert.ok(events.slice(-2).includes('mount:/admin/system/roles'));

  // 9. Large-module infrastructure: registerTabKey, registerModule (lazy load + CSS dedup), isPaneInteractive
  let moduleLoadCount = 0;
  const modEvents = [];
  bus.registerTabKey('/admin/canvas', 'id');
  bus.registerModule('[data-canvas-app]', {
    style: '/static/css/canvas-lab.css?v=1',
    loader: async () => {
      moduleLoadCount++;
      return {
        mount(root, ctx) { modEvents.push('mod-mount:' + ctx.path + ':' + root.querySelector('[data-canvas-app]').getAttribute('data-canvas-app')); },
        activate(root, ctx) { modEvents.push('mod-activate:' + ctx.path); },
        deactivate(root, ctx) { modEvents.push('mod-deactivate:' + ctx.path); },
        unmount(root) { modEvents.push('mod-unmount:' + root.getAttribute('data-pane-key')); }
      };
    }
  });

  // Not yet loaded when no matching pane exists
  assert.equal(moduleLoadCount, 0, 'lazy module must not load before matching pane mounts');
  assert.equal(doc.head.querySelector('link[href="/static/css/canvas-lab.css?v=1"]'), null);

  // Open Canvas #1 -> triggers lazy load + style injection + mount
  side.querySelector('a[href="/admin/canvas?id=1"]').click();
  await flush();
  assert.equal(moduleLoadCount, 1, 'lazy module loads once when first matching pane mounts');
  assert.equal(doc.head.querySelectorAll('link[href="/static/css/canvas-lab.css?v=1"]').length, 1);
  assert.ok(tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=1"]'), 'registerTabKey isolates ?id=1 tab');
  const canvas1Pane = bus.panel();
  assert.deepEqual(modEvents, [
    'mod-mount:/admin/canvas?id=1:1'
  ]);

  // Open Canvas #2 -> distinct tab key, zero duplicate JS/CSS load, deactivates #1 and mounts #2
  side.querySelector('a[href="/admin/canvas?id=2"]').click();
  await flush();
  assert.equal(moduleLoadCount, 1, 'already loaded module must not reload on second matching tab');
  assert.equal(doc.head.querySelectorAll('link[href="/static/css/canvas-lab.css?v=1"]').length, 1, 'module CSS must be deduplicated');
  assert.ok(tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=1"]'));
  assert.ok(tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=2"]'));
  assert.deepEqual(modEvents.slice(-2), [
    'mod-deactivate:/admin/canvas?id=1',
    'mod-mount:/admin/canvas?id=2:2'
  ]);

  // Switch back to Canvas #1 -> deactivates #2 and activates #1
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=1"] .ad-tab-label').click();
  await flush();
  assert.deepEqual(modEvents.slice(-2), [
    'mod-deactivate:/admin/canvas?id=2',
    'mod-activate:/admin/canvas?id=1'
  ]);

  // Hotkey arbitration via isPaneInteractive
  const c1Stage = canvas1Pane.querySelector('#canvas-1-stage');
  const c1Input = canvas1Pane.querySelector('#canvas-1-input');
  c1Stage.focus();
  assert.equal(bus.isPaneInteractive(c1Stage), true, 'active pane non-input target is interactive');
  c1Input.focus();
  assert.equal(bus.isPaneInteractive(c1Input), false, 'input inside pane suppresses global canvas hotkeys');
  c1Stage.focus();
  const fakeCmdk = doc.createElement('div');
  fakeCmdk.className = 'cmdk-mask';
  doc.body.appendChild(fakeCmdk);
  assert.equal(bus.isPaneInteractive(c1Stage), false, 'open command palette suppresses pane hotkeys');
  fakeCmdk.remove();
  assert.equal(bus.isPaneInteractive(c1Stage), true);
  assert.equal(bus.isPaneInteractive(top.querySelector('[data-refresh]')), false, 'target outside active pane is not pane-interactive');

  // Close Canvas #2 -> fires module unmount
  tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=2"] .x').click();
  await flush();
  assert.equal(modEvents.at(-1), 'mod-unmount:/admin/canvas?id=2');

  // 10. Declarative #ad-shell-modules bootstrap registers tabKey + style without inline scripts
  const bootEnv = createEnv({
    shellModules: [{
      selector: '[data-canvas-app]',
      script: '/static/js/canvas-lab.js?v=1',
      style: '/static/css/canvas-manifest.css?v=1',
      tabPrefix: '/admin/canvas',
      tabParam: 'id'
    }]
  });
  // Before opening /admin/canvas, manifest CSS is still uninjected
  assert.equal(bootEnv.doc.head.querySelector('link[href="/static/css/canvas-manifest.css?v=1"]'), null);
  bootEnv.side.querySelector('a[href="/admin/canvas?id=1"]').click();
  await flush();
  bootEnv.side.querySelector('a[href="/admin/canvas?id=2"]').click();
  await flush();
  assert.ok(bootEnv.tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=1"]'));
  assert.ok(bootEnv.tabsEl.querySelector('.ad-tab[data-tab-key="/admin/canvas?id=2"]'));
  assert.equal(bootEnv.doc.head.querySelectorAll('link[href="/static/css/canvas-manifest.css?v=1"]').length, 1);

  console.log('Admin tabs keep-alive & large-module shell hooks passed');
})().catch((err) => {
  console.error(err);
  process.exit(1);
});
