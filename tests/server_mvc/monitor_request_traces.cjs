'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const repo = path.join(__dirname, '../..');
const mvc = 'packages/Zan.Mvc';
const assets = ['/wwwroot/js/admin-request-traces.js', '/wwwroot/css/admin-request-traces.css'];
function read(file) { return fs.readFileSync(path.join(repo, file), 'utf8'); }
for (const root of [mvc, 'templates/server/server-collab', 'templates/server/server-licensing']) {
  const layout = read(root + '/views/Admin/layout.html');
  assert.match(layout, /\/static\/js\/admin-request-traces\.js\?v=6" defer/);
  assert.match(layout, /\/static\/css\/admin-request-traces\.css\?v=4"/);
  const view = read(root + '/views/Admin/Monitor/MonitorController.Index.html');
  assert.match(view, /<zan-request-traces[^>]*data-source="\/admin\/monitor\/request-traces"/);
  assert.match(view, /data-stream="\/admin\/monitor\/monitor\/stream"/);
  assert.match(view, /data-day="\/admin\/monitor\/monitor\/today\?/);
  for (const asset of assets) { assert.equal(read(root + asset), read(mvc + asset)); }
}

const calls = [];
let Component;
vm.runInNewContext(read(mvc + assets[0]), {
  HTMLElement: class {},
  customElements: { define(name, ctor) { assert.equal(name, 'zan-request-traces'); Component = ctor; } },
  URL, AbortController,
  window: { location: { href: 'http://localhost/admin/monitor' } },
  fetch(url, options) {
    return new Promise((resolve, reject) => { calls.push({ url, options, resolve, reject }); });
  }
});

const stage = (name, start, elapsed, failure) => ({ name, offset_us: start, elapsed_us: elapsed, failure });
const page = {
  pid: 42, process_start_unix_ms: 1767225599000, clock_mono_us: 3000000, clock_unix_ms: Date.UTC(2026, 0, 1),
  completed: 4, overwritten: 0, next_before: 3,
  requests: [
    { id: 4, method: 'GET', route: '/ok', action: 'Example.Ok', protocol: 'HTTP/1', start_us: 3010000,
      status: 200, outcome: 'sent', elapsed_us: 100, stages: [] },
    { id: 3, method: 'POST', route: '/example', action: 'Example.Save', protocol: 'HTTP/2', start_us: 3005000,
      status: 500, outcome: 'sent', elapsed_us: 400, omitted_stages: 2,
      resources: { cpu: 1536, cpu_unit: 'ns', allocated_bytes: 500, slices: 9, unfinished_tasks: 1 },
      omitted_execution_slices: 2,
      execution_slices: [
        { tid: 180, start_us: 3005170, elapsed_us: 14, cpu: -1, allocated_bytes: 0 },
        { tid: 160, start_us: 3005050, elapsed_us: 9, cpu: 1234, allocated_bytes: 64 }
      ],
      omitted_waits: 3,
      waits: [
        { kind: '<img src="x">', file: 'src/<worker>&".zan', line: 15, column: 4, offset_us: 110, elapsed_us: 125 },
        { kind: 'await', file: 'src/main.zan', line: 10, column: 2, offset_us: 65, elapsed_us: 80 },
        { kind: 'await', file: 'src/child.zan', line: 5, column: 8, offset_us: 250 },
        { kind: 'await', file: 'src/instant.zan', line: 1, column: 1, offset_us: 270, elapsed_us: 0 }
      ],
      stages: [stage('handler', 40, 200), stage('sql_execute', 100, 80, 'returned_error'), stage('cleanup', 300)] }
  ]
};
const respond = (status, data) => calls.at(-1).resolve({ status, ok: status === 200, json: async () => data });

(async function () {
  const ui = new Component();
  Object.assign(ui, {
    dataset: { source: '/admin/monitor/request-traces' },
    setAttribute() {}, removeAttribute() {},
    status: {}, summary: {}, list: {}, search: { value: '' }, result: { value: 'all' },
    order: { value: 'recent' }, refresh: {}, older: {}, rows: { replaceChildren() { this.innerHTML = ''; } },
    detail: {
      querySelectorAll(selector) {
        assert.equal(selector, '.trace-bar');
        this.bars = Array.from(this.innerHTML.matchAll(/class="trace-bar(?: trace-bar-issue)?"/g), () => ({ style: {} }));
        return this.bars;
      },
      querySelector() { return { focus() {} }; },
      replaceChildren() {}
    }
  });

  let loading = ui.load(0);
  assert.equal(ui.older.disabled, true);
  assert.equal(calls.at(-1).url.search, '?limit=100');
  respond(200, page);
  await loading;
  assert.equal(ui.older.disabled, false);
  assert.match(ui.rows.innerHTML, /2026-01-01 00:00:00\.005/);
  assert.match(ui.summary.textContent, /异常 1 条.*sql_execute \/ returned_error × 1/);
  ui.order.value = 'slow';
  ui.renderRows();
  assert.ok(ui.rows.innerHTML.indexOf('查看 #3') < ui.rows.innerHTML.indexOf('查看 #4'));
  ui.search.value = 'example.save';
  ui.result.value = '5xx';
  ui.renderRows();
  assert.match(ui.summary.textContent, /匹配 1 \/ 2 条/);
  ui.openTrace(3);
  assert.match(ui.detail.innerHTML, /3000000:42:3:1767225599000.*超出容量的阶段 2 个/);
  assert.match(ui.detail.innerHTML, /未结束/);
  assert.deepEqual(ui.detail.bars.map((b) => [b.style.left, b.style.width]), [['10%', '50%'], ['25%', '20%'], ['75%', '0%']]);
  assert.match(ui.detail.innerHTML, /请求资源.*CPU 1,536 ns.*累计分配 500 B.*调度片段 9.*子任务 1/);
  assert.match(ui.detail.innerHTML, /采集范围 应用处理阶段/);
  const waits = ui.detail.innerHTML.split('<h4>任务等待</h4>')[1];
  assert.match(waits, /超出容量的等待 3 个/);
  assert.match(waits, /开始偏移从请求起点计算.*重叠.*耗时相加可能超过请求总耗时/);
  assert.match(waits, /&lt;img src=&quot;x&quot;&gt;<\/td><td class="mono">src\/&lt;worker&gt;&amp;&quot;\.zan<\/td><td class="num">15<\/td><td class="num">4<\/td><td class="num">110 µs<\/td><td class="num">125 µs/);
  assert.doesNotMatch(waits, /<img|<worker>/);
  assert.ok(waits.indexOf('src/main.zan') < waits.indexOf('src/&lt;worker'));
  assert.equal(page.requests[1].waits[0].offset_us, 110);
  assert.match(waits, /src\/child\.zan.*250 µs<\/td><td class="num">采集截止时未结束<\/td><\/tr>/);
  assert.match(waits, /src\/instant\.zan.*270 µs<\/td><td class="num">0 µs<\/td><\/tr>/);

  const execution = ui.detail.innerHTML.split('<summary>线程执行片段（2 条）</summary>')[1];
  assert.match(execution, /超出容量的片段 2 条/);
  assert.match(execution, /160<\/td><td class="num">50 µs<\/td><td class="num">9 µs<\/td><td class="num">1,234 ns<\/td><td class="num">64 B/);
  assert.match(execution, /180<\/td><td class="num">170 µs<\/td><td class="num">14 µs<\/td><td class="num">未测得/);
  assert.ok(execution.indexOf('160</td>') < execution.indexOf('180</td>'));
  assert.equal(page.requests[1].execution_slices[0].tid, 180);
  const legacyTimeline = ui.detail.innerHTML.split('trace-timeline')[1].split('</table>')[0];
  assert.doesNotMatch(legacyTimeline, /<th class="num">(?:CPU|分配)<\/th>/);
  assert.doesNotMatch(ui.detail.innerHTML, /未归入阶段的资源/);

  const profiled = {
    ...page.requests[1], protocol: 'HTTP/1',
    resources: { ...page.requests[1].resources, coverage: 'read-loop-to-cleanup', cpu_unit: 'cycles',
      stage_resources: true, residual_cpu: 0, residual_allocated_bytes: 0 },
    stages: [
      { ...stage('receive_head', 0, 40), cpu: 1536, allocated_bytes: 96 },
      { ...stage('handler', 40, 200), cpu: 0, allocated_bytes: 0 },
      stage('cleanup', 300)
    ]
  };
  ui.page = { ...page, requests: [profiled] };
  ui.openTrace(3);
  assert.match(ui.detail.innerHTML, /采集范围 请求读取循环开始至清理结束/);
  assert.match(ui.detail.innerHTML, /未归入阶段的资源<\/strong> · CPU 0 cycles · 累计分配 0 B/);
  const profiledTimeline = ui.detail.innerHTML.split('trace-timeline')[1].split('</table>')[0];
  assert.match(profiledTimeline, /<th class="num">CPU<\/th><th class="num">分配<\/th>/);
  const [, receiveHead, handler, cleanup] = profiledTimeline.split('</tr>');
  assert.match(receiveHead, /receive_head.*<td class="num">1,536 cycles<\/td><td class="num">96 B<\/td>/);
  assert.match(handler, /handler.*<td class="num">0 cycles<\/td><td class="num">0 B<\/td>/);
  assert.match(cleanup, /cleanup.*<td class="num">未测得<\/td><td class="num">未测得<\/td>/);
  delete profiled.resources.residual_cpu;
  delete profiled.resources.residual_allocated_bytes;
  ui.openTrace(3);
  assert.match(ui.detail.innerHTML, /未归入阶段的资源<\/strong> · CPU 未测得 · 累计分配 未测得/);
  ui.page = page;

  ui.openTrace(4);
  assert.match(ui.detail.innerHTML, /没有资源样本/);
  assert.doesNotMatch(ui.detail.innerHTML, /任务等待|线程执行片段/);
  const unsampled = page.requests[0];
  ui.page = { ...page, requests: [{ ...unsampled, waits: [], omitted_waits: 0 }] };
  ui.openTrace(4);
  assert.match(ui.detail.innerHTML, /没有保留的等待记录/);
  assert.doesNotMatch(ui.detail.innerHTML, /超出容量的等待/);
  ui.page = { ...page, requests: [{ ...unsampled, waits: [], omitted_waits: 2 }] };
  ui.openTrace(4);
  assert.match(ui.detail.innerHTML, /超出容量的等待 2 个.*没有保留的等待记录/);
  ui.page = { ...page, process_start_unix_ms: undefined };
  ui.openTrace(4);
  assert.match(ui.detail.innerHTML, /<code>3000000:42:4<\/code>/);
  ui.page = page;

  for (const replacement of [{ ...page, pid: 43 }, { ...page, clock_mono_us: 4000000 },
      { ...page, process_start_unix_ms: 1767225599001 }]) {
    loading = ui.load(3);
    assert.equal(calls.at(-1).url.search, '?limit=100&before=3');
    respond(200, replacement);
    await loading;
    assert.equal(ui.page, page);
    assert.equal(ui.older.disabled, true);
    assert.match(ui.status.textContent, /其他 Worker 或采集已重启/);
  }
  loading = ui.load(3);
  respond(200, { ...page, next_before: 0, requests: [] });
  await loading;
  assert.match(ui.rows.innerHTML, /没有保留记录/);
  assert.equal(ui.older.disabled, true);

  for (const [status, message] of [[403, /需要登录/], [503, /已关闭采集/], [500, /暂不可用/]]) {
    loading = ui.load(0);
    respond(status, status === 503 ? { code: '0503' } : null);
    await loading;
    assert.match(ui.status.textContent, message);
    assert.equal(ui.refresh.disabled, false);
  }

  loading = ui.load(0);
  const call = calls.at(-1);
  ui.disconnectedCallback();
  assert.equal(call.options.signal.aborted, true);
  respond(200, page);
  await loading;
  assert.equal(ui.page, null);
  console.log('MVC request diagnostics: assets, filtering, UTC anchor, timelines, resources, waits, cursor identity, errors and cancellation passed');
}()).catch((err) => { console.error(err); process.exitCode = 1; });
