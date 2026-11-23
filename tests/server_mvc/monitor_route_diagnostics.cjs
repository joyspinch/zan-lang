'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const repo = path.join(__dirname, '../..');
const roots = ['packages/Zan.Mvc', 'templates/server/server-collab', 'templates/server/server-licensing'];
const asset = '/wwwroot/js/admin-route-diagnostics.js';
const source = '/admin/monitor/route-diagnostics';
const read = (file) => fs.readFileSync(path.join(repo, file), 'utf8');
const script = read(roots[0] + asset);

class Element {
  constructor() {
    this.nodes = [];
    this.attributes = new Map();
    this.listeners = new Map();
  }
  get title() { return this.attributes.get('title') || ''; }
  set title(value) { this.attributes.set('title', String(value)); }
  addEventListener(event, listener) { this.listeners.set(event, listener); }
  removeEventListener(event, listener) {
    if (this.listeners.get(event) === listener) { this.listeners.delete(event); }
  }
  fire(event) { this.listeners.get(event)?.(); }
  setAttribute(key, value) { this.attributes.set(key, value); }
  removeAttribute(key) { this.attributes.delete(key); }
  replaceChildren(...nodes) { this.nodes = nodes.flatMap((node) => node.fragment || [node]); }
  focus() { this.focused = true; }
}

function harness({ history = false, hours, method = '', route = '' } = {}) {
  const calls = [];
  const timers = new Map();
  let Component;
  let timerId = 0;
  vm.runInNewContext(script, {
    HTMLElement: Element,
    customElements: { define(name, ctor) { assert.equal(name, 'zan-route-diagnostics'); Component = ctor; } },
    URL, AbortController,
    Option: class { constructor(label, value) { this.text = label; this.value = String(value); } },
    window: { location: { href: 'http://localhost/admin/monitor/monitor' } },
    document: {
      createDocumentFragment() {
        return { fragment: [], append(row) { this.fragment.push(row); } };
      },
      createElement(tag) {
        assert.equal(tag, 'tr');
        return { cells: [], insertCell() { const cell = {}; this.cells.push(cell); return cell; } };
      }
    },
    setTimeout(fn, delay) { timers.set(++timerId, { fn, delay }); return timerId; },
    clearTimeout(id) { timers.delete(id); },
    fetch(url, options) {
      return new Promise((resolve) => calls.push({ url, options, resolve }));
    }
  });

  const ui = new Component();
  const toggle = new Element();
  const nodes = new Map();
  for (const key of ['hours', 'route', 'refresh', 'status', 'details', 'title', 'summary', 'resources', 'stages',
    'buckets', 'series', 'resource-series']) {
    nodes.set('[data-diag-' + key + ']', new Element());
  }
  for (const [key, columns] of [['summary', 8], ['resources', 8], ['stages', 7], ['series', 12], ['resource-series', 9]]) {
    nodes.get('[data-diag-' + key + ']').parentElement = { tHead: { rows: [{ cells: Array(columns) }] } };
  }
  const toolbar = new Map([
    ['[data-route-diagnostics-toggle]', toggle], ['[name="m"]', { value: method }], ['[name="route"]', { value: route }]
  ]);
  Object.assign(ui, {
    hidden: true,
    dataset: { source, ...history && { history: '1', hours } },
    previousElementSibling: { querySelector(selector) { return toolbar.get(selector); } },
    querySelector(selector) { return nodes.get(selector); }
  });
  ui.connectedCallback();
  assert.equal(calls.length, 0);
  return { ui, toggle, calls, timers };
}

const minute = Date.UTC(2026, 10, 21, 13) / 60000;
const totals = { samples: 4, elapsed_us: 10000, max_us: 4000, failed: 1, incomplete: 1,
  sql_calls: 5, sql_completed: 4, sql_us: 5000, sql_failed: 2 };
const readLoopCycles = { coverage: 'read-loop-to-cleanup', cpu_unit: 'cycles', samples: 2, cpu_samples: 1,
  cpu: 12000, allocated_bytes: 1024, slices: 5, unfinished_tasks: 1 };
const http1Ns = { coverage: 'header-to-cleanup', cpu_unit: 'ns', samples: 1, cpu_samples: 0,
  cpu: -1, allocated_bytes: 64, slices: 2, unfinished_tasks: 0 };
const http2Cycles = { coverage: 'application', cpu_unit: 'cycles', samples: 1, cpu_samples: 1,
  cpu: 0, allocated_bytes: 32, slices: 1, unfinished_tasks: 0 };
const get = { method: 'GET', route: '/jobs/{id}', ...totals, resources: [readLoopCycles, http1Ns, http2Cycles] };
const post = { method: 'POST', route: '/jobs/{id}', ...totals };
const second = { method: 'GET', route: '/search/{slug}&tag', ...totals };
const discovery = { enabled: true, sample_every: 64, local_dropped_samples: 2,
  from_minute: minute - 60, to_minute: minute, routes: [get, post, second], stages: [], series: [] };
const detail = { ...discovery, routes: [get],
  stages: [
    { name: 'unattributed', calls: 4, completed: 4, elapsed_us: 1900, max_us: 700, failed: 0 },
    { name: 'sql_query', calls: 4, completed: 3, elapsed_us: 4800, max_us: 2400, failed: 1 },
    { name: 'cleanup', calls: 1, completed: 0, elapsed_us: 0, max_us: 0, failed: 1 },
    { name: '<img src=x>', calls: 1, completed: 1, elapsed_us: 0, max_us: 0, failed: 0 }
  ],
  series: [
    { minute: minute - 3, ...totals, resources: [http2Cycles] },
    { minute: minute - 1, ...totals, resources: [readLoopCycles, http1Ns] },
    { minute, samples: 0 }
  ]
};
const flush = () => new Promise((resolve) => setImmediate(resolve));
async function respond(call, page, status = 200) {
  call.resolve({ status, ok: status === 200, json: async () => page });
  await flush();
}
function query(call, expected) {
  assert.equal(call.url.pathname, source);
  assert.deepEqual(Object.fromEntries(call.url.searchParams), expected);
  assert.equal(call.options.credentials, 'same-origin');
  assert.equal(call.options.cache, 'no-store');
}
const values = (body, row = 0) => body.nodes[row].cells.map((cell) => cell.textContent);

for (const root of roots) {
  test(root + ' mounts the same diagnostic asset from both monitor pages', () => {
    assert.equal(read(root + asset), script);
    assert.match(read(root + '/views/Admin/layout.html'), /\/static\/js\/admin-route-diagnostics\.js\?v=5" defer/);
    for (const page of ['Index', 'History']) {
      const view = read(root + '/views/Admin/Monitor/MonitorController.' + page + '.html');
      assert.match(view, /data-route-diagnostics-toggle aria-controls="route-diagnostics" aria-expanded="false"/);
      assert.match(view, /<\/form>\s*<zan-route-diagnostics id="route-diagnostics"[^>]+data-source="\/admin\/monitor\/route-diagnostics"[^>]+hidden>/);
    }
    assert.match(read(root + '/views/Admin/Monitor/MonitorController.Index.html'), /<zan-request-traces/);
  });
}

test('discovers route patterns before querying details and uses sample and completion denominators', async () => {
  const { ui, toggle, calls, timers } = harness();
  assert.deepEqual(Array.from(ui.hours.nodes, (node) => node.value), ['1', '6', '24', '168', '720']);
  toggle.fire('click');
  assert.equal(ui.hours.focused, true);
  assert.equal(toggle.attributes.get('aria-expanded'), 'true');
  query(calls[0], { hours: '1' });
  await respond(calls[0], discovery);
  query(calls[1], { hours: '1', method: 'GET', route: '/jobs/{id}' });
  await respond(calls[1], detail);
  assert.equal(ui.title, '');
  assert.equal(ui.heading.textContent, 'GET /jobs/{id}');
  assert.match(ui.status.textContent, /2026-11-21 12:00 → 2026-11-21 13:00 UTC（截止分钟除外）/);
  assert.match(ui.status.textContent, /1 \/ 64.*累计丢弃 2.*每 15 秒刷新/);
  assert.deepEqual(values(ui.summary), ['请求样本', 4, 4, 1, 1, '2.500 ms', '4.000 ms', '10.000 ms']);
  assert.deepEqual(values(ui.summary, 1), ['SQL 调用', 5, 4, 2, 1, '1.250 ms', '—', '5.000 ms']);
  assert.deepEqual(values(ui.stages), ['未覆盖耗时', 4, 4, 0, '475 µs', '700 µs', '1.900 ms']);
  assert.deepEqual(values(ui.stages, 1), ['SQL 查询', 4, 3, 1, '1.600 ms', '2.400 ms', '4.800 ms']);
  assert.deepEqual(values(ui.stages, 2), ['cleanup', 1, 0, 1, '—', '—', '0 µs']);
  assert.equal(values(ui.stages, 3)[0], '<img src=x>');
  assert.match(ui.innerHTML, /可能重叠.*时间区间并集之外.*SQL 调用单独计数/);
  assert.match(ui.innerHTML, /按采集范围和 CPU 单位汇总.*只含测得 CPU 的样本/);
  const resourceRows = [
    ['HTTP/1 · 请求读取循环开始至清理结束', 'cycles', 2, 1, '12,000 cycles', 1024, 5, 1],
    ['HTTP/1 · 请求头至清理结束', 'ns', 1, 0, '—', 64, 2, 0],
    ['HTTP/2 · 应用处理阶段', 'cycles', 1, 1, '0 cycles', 32, 1, 0]
  ];
  assert.deepEqual(ui.resources.nodes.map((_, i) => values(ui.resources, i)), resourceRows);
  assert.deepEqual(ui.resourceSeries.nodes.map((_, i) => values(ui.resourceSeries, i)), [
    ['2026-11-21 12:59', ...resourceRows[0]],
    ['2026-11-21 12:59', ...resourceRows[1]],
    ['2026-11-21 12:57', ...resourceRows[2]]
  ]);
  assert.equal(ui.series.nodes.length, 2);
  assert.equal(values(ui.series)[0], '2026-11-21 12:59');
  assert.match(ui.buckets.textContent, /1 分钟/);
  assert.deepEqual(Array.from(timers.values(), (timer) => timer.delay), [15000]);

  const laterRoute = { method: 'GET', route: '/reports/{year}', ...totals };
  Array.from(timers.values())[0].fn();
  query(calls[2], { hours: '1' });
  assert.equal(ui.details.hidden, false);
  await respond(calls[2], { ...discovery, routes: [post, get, second, laterRoute] });
  query(calls[3], { hours: '1', method: 'GET', route: '/jobs/{id}' });
  await respond(calls[3], detail);
  assert.equal(ui.route.value, '["GET","/jobs/{id}"]');
  assert.equal(ui.route.nodes.length, 4);

  const secondOption = ui.route.nodes.find((node) => node.text === 'GET /search/{slug}&tag');
  ui.route.value = secondOption.value;
  ui.route.fire('change');
  assert.equal(timers.size, 0);
  query(calls[4], { hours: '1', method: 'GET', route: second.route });
  ui.hours.value = '168';
  ui.hours.fire('change');
  query(calls[5], { hours: '168' });
  assert.equal(calls[4].options.signal.aborted, true);
  await respond(calls[4], detail);
  assert.equal(ui.details.hidden, true);
  await respond(calls[5], discovery);
  query(calls[6], { hours: '168', method: 'GET', route: second.route });
  await respond(calls[6], { ...detail, routes: [second], bucket_minutes: 60 });
  assert.equal(ui.heading.textContent, 'GET /search/{slug}&tag');
  assert.match(ui.buckets.textContent, /60 分钟/);
  assert.doesNotMatch(ui.status.textContent, /刷新/);
  assert.equal(timers.size, 0);
});

test('history selects an exact discovered method and pattern and shows retained data with sampling off', async () => {
  const { ui, toggle, calls, timers } = harness({ history: true, hours: '33', method: 'post', route: post.route });
  toggle.fire('click');
  query(calls[0], { hours: '24' });
  const disabled = { ...discovery, enabled: false, sample_every: 0 };
  await respond(calls[0], disabled);
  query(calls[1], { hours: '24', method: 'POST', route: post.route });
  await respond(calls[1], { ...disabled, storage_readable: false, local_pending_samples: 4,
    stages: [], series: [{ minute: minute - 1, ...totals }], routes: [post] });
  assert.equal(ui.heading.textContent, 'POST /jobs/{id}');
  assert.match(ui.status.textContent, /响应 Worker 已关闭采样.*历史存储暂不可读.*待落盘样本 4 个/);
  assert.equal(ui.route.nodes.length, 3);
  assert.deepEqual(values(ui.summary), ['请求样本', 4, 4, 1, 1, '2.500 ms', '4.000 ms', '10.000 ms']);
  assert.match(values(ui.stages)[0], /暂无阶段采样/);
  assert.equal(ui.stages.nodes[0].cells[0].colSpan, 7);
  assert.deepEqual(values(ui.resources), ['此窗口内暂无资源样本。']);
  assert.equal(ui.resources.nodes[0].cells[0].colSpan, 8);
  assert.deepEqual(values(ui.resourceSeries), ['此窗口内暂无资源时段分布。']);
  assert.equal(ui.resourceSeries.nodes[0].cells[0].colSpan, 9);
  assert.equal(ui.details.hidden, false);
  assert.equal(timers.size, 0);
});

test('empty responses keep discovery polling, closing cancels requests, and denied access stops polling', async () => {
  const { ui, toggle, calls, timers } = harness();
  toggle.fire('click');
  await respond(calls[0], { ...discovery, enabled: false, routes: [] });
  assert.match(ui.status.textContent, /已关闭采样.*暂无接口采样/);
  assert.equal(ui.route.disabled, true);
  Array.from(timers.values())[0].fn();
  query(calls[1], { hours: '1' });
  toggle.fire('click');
  assert.equal(ui.hidden, true);
  assert.equal(toggle.attributes.get('aria-expanded'), 'false');
  assert.equal(calls[1].options.signal.aborted, true);
  assert.equal(timers.size, 0);
  await respond(calls[1], discovery);
  assert.equal(calls.length, 2);

  toggle.fire('click');
  await respond(calls[2], null, 500);
  assert.match(ui.status.textContent, /暂不可用/);
  assert.equal(ui.refresh.disabled, false);
  ui.refresh.fire('click');
  await respond(calls[3], null, 403);
  assert.match(ui.status.textContent, /需要登录/);
  assert.equal(timers.size, 0);
  ui.refresh.fire('click');
  ui.disconnectedCallback();
  assert.equal(calls[4].options.signal.aborted, true);
  await respond(calls[4], discovery);
  assert.equal(timers.size, 0);
  assert.equal(toggle.listeners.size, 0);
});
