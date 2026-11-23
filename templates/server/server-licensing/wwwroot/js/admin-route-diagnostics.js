(function () {
  'use strict';

  const windows = [[1, '最近 1 小时（实时）'], [6, '最近 6 小时'], [24, '最近 24 小时'],
    [168, '最近 7 天'], [720, '最近 30 天']];
  const pollMs = 15000;
  const stageLabels = new Map([
    ['unattributed', '未覆盖耗时'], ['sql_query', 'SQL 查询'],
    ['sql_execute', 'SQL 执行'], ['sql_scalar', 'SQL 标量查询']
  ]);
  const coverageLabels = new Map([
    ['read-loop-to-cleanup', 'HTTP/1 · 请求读取循环开始至清理结束'],
    ['header-to-cleanup', 'HTTP/1 · 请求头至清理结束'],
    ['application', 'HTTP/2 · 应用处理阶段']
  ]);

  function duration(us) {
    if (!Number.isFinite(us)) { return '—'; }
    if (us < 1000) { return Number(us.toFixed(1)) + ' µs'; }
    if (us < 1000000) { return (us / 1000).toFixed(3) + ' ms'; }
    return (us / 1000000).toFixed(3) + ' s';
  }

  function average(us, count) {
    return duration(count > 0 ? us / count : NaN);
  }

  function utcMinute(minute) {
    const date = new Date(minute * 60000);
    return Number.isFinite(date.getTime()) ? date.toISOString().slice(0, 16).replace('T', ' ') : '—';
  }

  function resourceRows(resources) {
    if (!Array.isArray(resources)) { return []; }
    return resources.map((row) => {
      const coverage = coverageLabels.get(row.coverage) || '未知';
      const unit = row.cpu_unit === 'cycles' || row.cpu_unit === 'ns' ? row.cpu_unit : '—';
      const cpu = unit !== '—' && row.cpu_samples > 0 && Number.isFinite(row.cpu) && row.cpu >= 0 ?
        row.cpu.toLocaleString('zh-CN') + ' ' + unit : '—';
      return [coverage, unit, row.samples, row.cpu_samples, cpu, row.allocated_bytes, row.slices, row.unfinished_tasks];
    });
  }

  function routeKey(route) {
    return JSON.stringify([route.method, route.route]);
  }

  function setRows(body, rows, emptyMessage) {
    const fragment = document.createDocumentFragment();
    for (const [label, ...values] of rows) {
      const row = document.createElement('tr');
      row.insertCell().textContent = label;
      for (const value of values) {
        const cell = row.insertCell();
        cell.className = 'num';
        cell.textContent = value;
      }
      fragment.append(row);
    }
    if (!rows.length) {
      const row = document.createElement('tr');
      const cell = row.insertCell();
      cell.colSpan = body.parentElement.tHead.rows[0].cells.length;
      cell.className = 'muted';
      cell.textContent = emptyMessage;
      fragment.append(row);
    }
    body.replaceChildren(fragment);
  }

  class RouteDiagnostics extends HTMLElement {
    connectedCallback() {
      const toolbar = this.previousElementSibling;
      this.toggle = toolbar.querySelector('[data-route-diagnostics-toggle]');
      this.onToggle = () => {
        this.hidden = !this.hidden;
        this.toggle.setAttribute('aria-expanded', String(!this.hidden));
        if (this.hidden) {
          this.cancel();
        } else {
          this.load();
          this.hours.focus();
        }
      };
      this.toggle.addEventListener('click', this.onToggle);
      if (!this.hours) { this.setup(toolbar); }
      if (!this.hidden) { this.load(); }
    }

    disconnectedCallback() {
      this.toggle.removeEventListener('click', this.onToggle);
      this.cancel();
    }

    setup(toolbar) {
      this.innerHTML = `<header>接口诊断</header>
        <div class="body">
          <div class="toolbar trace-controls">
            <label>时间窗口 <select class="input" data-diag-hours></select></label>
            <label>接口 <select class="input w160" data-diag-route disabled><option>等待读取接口</option></select></label>
            <button class="btn sm" type="button" data-diag-refresh>刷新</button>
          </div>
          <p class="hint" role="status" data-diag-status></p>
          <div data-diag-details hidden>
            <h3 data-diag-title></h3>
            <p class="hint">请求耗时按样本统计，失败包括 HTTP 4xx / 5xx 和传输或处理流程异常结束。阶段缺失或有调用未结束的样本计为不完整。</p>
            <div class="table-scroll">
              <table class="table" aria-label="接口样本与 SQL 汇总">
                <thead><tr><th>指标</th><th class="num">数量</th><th class="num">计入耗时</th>
                  <th class="num">失败</th><th class="num">不完整 / 未结束</th><th class="num">平均耗时</th>
                  <th class="num">最大耗时</th><th class="num">累计耗时</th></tr></thead>
                <tbody data-diag-summary></tbody>
              </table>
            </div>
            <h3>请求资源汇总</h3>
            <p class="hint">资源统计覆盖归属于请求的 Zan 任务。每行按采集范围和 CPU 单位汇总，CPU 合计只含测得 CPU 的样本。累计分配为采集期间已结算的分配量。有未结束子任务的样本可能缺少其最后的运行片段。</p>
            <div class="table-scroll">
              <table class="table" aria-label="请求资源汇总">
                <thead><tr><th>采集范围</th><th class="num">CPU 单位</th><th class="num">资源样本</th>
                  <th class="num">CPU 样本</th><th class="num">CPU 合计</th><th class="num">累计分配（B）</th>
                  <th class="num">调度片段</th><th class="num">未结束子任务</th></tr></thead>
                <tbody data-diag-resources></tbody>
              </table>
            </div>
            <h3>阶段耗时</h3>
            <p class="hint">阶段和 SQL 平均耗时按已结束的调用计算。阶段耗时包含子阶段；阶段可能重叠，不能相加得到请求总耗时。未覆盖耗时指端到端耗时中落在已记录、已结束阶段时间区间并集之外的部分。SQL 调用单独计数，SQL 最大耗时按已记录的阶段类型列出。</p>
            <div class="table-scroll">
              <table class="table" aria-label="阶段耗时">
                <thead><tr><th>阶段</th><th class="num">调用</th><th class="num">已结束</th>
                  <th class="num">失败</th><th class="num">平均耗时</th><th class="num">最大耗时</th>
                  <th class="num">累计耗时</th></tr></thead>
                <tbody data-diag-stages></tbody>
              </table>
            </div>
            <h3>样本趋势</h3>
            <p class="hint" data-diag-buckets></p>
            <div class="table-scroll">
              <table class="table" aria-label="样本趋势">
                <thead><tr><th>时段起点（UTC）</th><th class="num">样本</th><th class="num">失败</th>
                  <th class="num">不完整</th><th class="num">平均耗时</th><th class="num">最大耗时</th>
                  <th class="num">累计耗时</th><th class="num">SQL 调用</th><th class="num">SQL 已结束</th>
                  <th class="num">SQL 失败</th><th class="num">SQL 平均耗时</th><th class="num">SQL 累计耗时</th></tr></thead>
                <tbody data-diag-series></tbody>
              </table>
            </div>
            <h3>资源样本趋势</h3>
            <div class="table-scroll">
              <table class="table" aria-label="资源样本趋势">
                <thead><tr><th>时段起点（UTC）</th><th>采集范围</th><th class="num">CPU 单位</th>
                  <th class="num">资源样本</th><th class="num">CPU 样本</th><th class="num">CPU 合计</th>
                  <th class="num">累计分配（B）</th><th class="num">调度片段</th><th class="num">未结束子任务</th></tr></thead>
                <tbody data-diag-resource-series></tbody>
              </table>
            </div>
          </div>
        </div>`;
      this.hours = this.querySelector('[data-diag-hours]');
      this.route = this.querySelector('[data-diag-route]');
      this.refresh = this.querySelector('[data-diag-refresh]');
      this.status = this.querySelector('[data-diag-status]');
      this.details = this.querySelector('[data-diag-details]');
      this.heading = this.querySelector('[data-diag-title]');
      this.summary = this.querySelector('[data-diag-summary]');
      this.resources = this.querySelector('[data-diag-resources]');
      this.stages = this.querySelector('[data-diag-stages]');
      this.buckets = this.querySelector('[data-diag-buckets]');
      this.series = this.querySelector('[data-diag-series]');
      this.resourceSeries = this.querySelector('[data-diag-resource-series]');
      this.hours.replaceChildren(...windows.map(([value, label]) => new Option(label, value)));
      const requestedHours = Number(this.dataset.hours);
      this.hours.value = windows.some(([value]) => value === requestedHours) ? requestedHours : this.dataset.history ? 24 : 1;
      this.historyMethod = toolbar.querySelector('[name="m"]');
      this.historyRoute = toolbar.querySelector('[name="route"]');
      this.routes = new Map();
      this.hours.addEventListener('change', () => this.load());
      this.route.addEventListener('change', () => {
        this.selection = this.routes.get(this.route.value);
        this.load({ refreshCatalog: false });
      });
      this.refresh.addEventListener('click', () => this.load());
    }

    cancel() {
      clearTimeout(this.timer);
      this.timer = null;
      if (this.pending) { this.pending.abort(); }
      this.pending = null;
      this.removeAttribute('aria-busy');
    }

    updateRoutes(routes) {
      const next = new Map();
      for (const { method, route } of routes) {
        if (typeof method !== 'string' || !method || typeof route !== 'string' || !route) { continue; }
        const entry = { method, route };
        next.set(routeKey(entry), entry);
      }
      if (!this.selection && next.size) {
        const method = this.historyMethod?.value.trim().toUpperCase();
        const route = this.historyRoute?.value.trim();
        this.selection = Array.from(next.values()).find((entry) => entry.route === route && (!method || entry.method === method))
          || next.values().next().value;
      }
      const key = this.selection ? routeKey(this.selection) : '';
      const missing = Boolean(key) && !next.has(key);
      if (missing) { next.set(key, this.selection); }
      const catalog = JSON.stringify([Array.from(next.keys()), missing]);
      this.routes = next;
      if (this.catalog !== catalog) {
        this.catalog = catalog;
        const options = Array.from(next, ([id, { method, route }]) =>
          new Option(method + ' ' + route + (missing && id === key ? '（不在窗口接口列表）' : ''), id));
        this.route.replaceChildren(...(options.length ? options : [new Option('当前窗口无接口样本', '')]));
      }
      this.route.value = key;
      this.route.disabled = !key;
    }

    async fetchPage(hours, selected, signal) {
      const url = new URL(this.dataset.source, window.location.href);
      url.searchParams.set('hours', hours);
      if (selected) {
        url.searchParams.set('method', selected.method);
        url.searchParams.set('route', selected.route);
      }
      const response = await fetch(url, { credentials: 'same-origin', cache: 'no-store', signal });
      if (response.status === 401 || response.status === 403 || response.redirected) { return null; }
      if (!response.ok) { throw new Error('HTTP ' + response.status); }
      const page = await response.json();
      if (typeof page.enabled !== 'boolean' || !Number.isSafeInteger(page.from_minute) ||
          !Number.isSafeInteger(page.to_minute) || !Array.isArray(page.routes) ||
          !Array.isArray(page.stages) || !Array.isArray(page.series)) { throw new Error('unexpected response'); }
      return page;
    }

    async load({ poll = false, refreshCatalog = true } = {}) {
      if (this.hidden) { return; }
      this.cancel();
      const pending = this.pending = new AbortController();
      const hours = Number(this.hours.value);
      let authorized = true;
      this.refresh.disabled = true;
      if (!poll) {
        this.details.hidden = true;
        this.status.textContent = '正在读取接口诊断…';
        this.setAttribute('aria-busy', 'true');
      }
      try {
        let page;
        if (refreshCatalog || !this.selection) {
          page = await this.fetchPage(hours, null, pending.signal);
          if (pending.signal.aborted) { return; }
          if (page) { this.updateRoutes(page.routes); }
        }
        const selected = this.selection;
        if (page !== null && selected) {
          page = await this.fetchPage(hours, selected, pending.signal);
          if (pending.signal.aborted) { return; }
        }
        if (page) {
          this.render(page, selected, hours);
        } else {
          authorized = false;
          this.selection = undefined;
          this.updateRoutes([]);
          this.details.hidden = true;
          this.status.textContent = '接口诊断需要登录并具有运行监控查看权限。';
        }
      } catch (err) {
        if (!pending.signal.aborted) {
          this.status.textContent = '接口诊断暂不可用，请重试。' + (this.details.hidden ? '' : ' 下方为上次读取的结果。');
        }
      } finally {
        if (this.pending === pending) {
          this.pending = null;
          this.removeAttribute('aria-busy');
          this.refresh.disabled = false;
          if (hours === 1 && authorized) { this.timer = setTimeout(() => this.load({ poll: true }), pollMs); }
        }
      }
    }

    render(page, selected, hours) {
      const info = [utcMinute(page.from_minute) + ' → ' + utcMinute(page.to_minute) + ' UTC（截止分钟除外）'];
      if (page.enabled && page.sample_every > 0) {
        info.push('响应 Worker 采样频率约 1 / ' + page.sample_every);
      } else if (!page.enabled) {
        info.push('响应 Worker 已关闭采样');
      }
      if (page.storage_readable === false) { info.push('历史存储暂不可读，仅显示响应 Worker 的待落盘样本'); }
      if (page.local_pending_samples > 0) { info.push('含响应 Worker 待落盘样本 ' + page.local_pending_samples + ' 个'); }
      if (page.local_dropped_samples > 0) {
        info.push('响应 Worker 累计丢弃 ' + page.local_dropped_samples + ' 个样本');
      }
      if (hours === 1) { info.push('每 ' + pollMs / 1000 + ' 秒刷新'); }

      const totals = selected && page.routes.find((entry) => entry.method === selected.method && entry.route === selected.route);
      if (!totals?.samples) {
        this.details.hidden = true;
        info.push(selected ? selected.method + ' ' + selected.route + ' 在此窗口内暂无采样。' : '此窗口内暂无接口采样。');
        this.status.textContent = info.join(' · ');
        return;
      }
      const status = info.join(' · ');
      if (this.status.textContent !== status) { this.status.textContent = status; }
      this.heading.textContent = selected.method + ' ' + selected.route;
      setRows(this.summary, [
        ['请求样本', totals.samples, totals.samples, totals.failed, totals.incomplete,
          average(totals.elapsed_us, totals.samples), duration(totals.max_us), duration(totals.elapsed_us)],
        ['SQL 调用', totals.sql_calls, totals.sql_completed, totals.sql_failed, totals.sql_calls - totals.sql_completed,
          average(totals.sql_us, totals.sql_completed), '—', duration(totals.sql_us)]
      ]);
      setRows(this.resources, resourceRows(totals.resources), '此窗口内暂无资源样本。');
      setRows(this.stages, page.stages.map((stage) => [stageLabels.get(stage.name) || stage.name,
        stage.calls, stage.completed, stage.failed, average(stage.elapsed_us, stage.completed),
        duration(stage.completed > 0 ? stage.max_us : NaN), duration(stage.elapsed_us)]), '此窗口内暂无阶段采样。');
      this.buckets.textContent = '每个时段 ' + (page.bucket_minutes || 1) + ' 分钟，表中列出有样本的时段。';
      const points = page.series.filter((point) => point.samples > 0).sort((a, b) => b.minute - a.minute);
      setRows(this.series, points.map((point) => [
        utcMinute(point.minute), point.samples, point.failed, point.incomplete, average(point.elapsed_us, point.samples),
        duration(point.max_us), duration(point.elapsed_us), point.sql_calls, point.sql_completed, point.sql_failed,
        average(point.sql_us, point.sql_completed), duration(point.sql_us)
      ]), '此窗口内暂无时段分布。');
      setRows(this.resourceSeries, points.flatMap((point) => resourceRows(point.resources)
        .map((row) => [utcMinute(point.minute), ...row])), '此窗口内暂无资源时段分布。');
      this.details.hidden = false;
    }
  }

  customElements.define('zan-route-diagnostics', RouteDiagnostics);
}());
