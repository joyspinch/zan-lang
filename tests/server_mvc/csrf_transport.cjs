'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

for (const layout of ['layout.html', 'Admin/layout.html', 'Account/layout.html']) {
  const source = fs.readFileSync(path.join(__dirname, '../../packages/Zan.Mvc/views', layout), 'utf8');
  assert.ok(source.includes('/static/js/csrf.js'), layout + ' wires CSRF transport');
}

const events = {};
const requests = [];
const document = {
  cookie: 'unrelated=one; csrf_token=current-token',
  addEventListener(name, fn) { events[name] = fn; },
  createElement() { return {}; }
};
const window = { fetch(input, options) { requests.push({ input, options }); return Promise.resolve({}); } };
vm.runInNewContext(fs.readFileSync(path.join(__dirname,
  '../../packages/Zan.Mvc/wwwroot/js/csrf.js'), 'utf8'), {
  window, document, location: { href: 'https://app.test/edit', origin: 'https://app.test' },
  URL, Headers, Request
});

function form(action, method, dataSubmit) {
  const inputs = [];
  return {
    tagName: 'FORM', action, inputs,
    getAttribute(name) { return name === 'method' ? method : null; },
    hasAttribute(name) { return name === 'data-submit' && dataSubmit; },
    querySelector() { return inputs[0] || null; },
    appendChild(input) { inputs.push(input); }
  };
}
function submit(target) { events.submit({ target }); }
const local = form('https://app.test/save', 'post');
submit(local);
assert.equal(local.inputs.length, 1);
assert.equal(local.inputs[0].name, '_csrf');
assert.equal(local.inputs[0].value, 'current-token');
document.cookie = 'csrf_token=rotated-token';
submit(local);
assert.equal(local.inputs.length, 1);
assert.equal(local.inputs[0].value, 'rotated-token');
const fragment = form('https://app.test/admin/save', null, true);
submit(fragment);
assert.equal(fragment.inputs[0].value, 'rotated-token');
for (const target of [form('https://other.test/save', 'post'), form('https://app.test/search', 'get')]) {
  submit(target);
  assert.equal(target.inputs.length, 0);
}
const hx = { detail: { verb: 'patch', path: '/edit', headers: {} } };
events['htmx:configRequest'](hx);
assert.equal(hx.detail.headers['X-CSRF-Token'], 'rotated-token');
const foreignHx = { detail: { verb: 'post', path: 'https://other.test/', headers: {} } };
events['htmx:configRequest'](foreignHx);
assert.equal(foreignHx.detail.headers['X-CSRF-Token'], undefined);

const original = { method: 'PUT', headers: { 'X-Fragment': '1' } };
window.fetch('/save', original);
assert.equal(requests.at(-1).options.headers.get('X-CSRF-Token'), 'rotated-token');
assert.equal(requests.at(-1).options.headers.get('X-Fragment'), '1');
assert.equal(original.headers['X-CSRF-Token'], undefined);
window.fetch(new Request('https://app.test/save', { method: 'PATCH', headers: { 'X-Custom': 'yes' } }));
assert.equal(requests.at(-1).options.headers.get('X-CSRF-Token'), 'rotated-token');
assert.equal(requests.at(-1).options.headers.get('X-Custom'), 'yes');
for (const [target, init] of [
  ['https://other.test/save', { method: 'POST' }],
  ['//other.test/save', { method: 'DELETE' }],
  ['/read', {}],
  ['/save', { method: 'POST', credentials: 'omit' }]
]) {
  window.fetch(target, init);
  assert.equal(requests.at(-1).options, init);
  assert.equal(init.headers, undefined);
}
document.cookie = '';
window.fetch('/save', { method: 'POST' });
assert.equal(requests.at(-1).options.headers, undefined);
console.log('CSRF transport: forms, fragments, htmx, fetch, rotation and cross-origin exclusions passed');
