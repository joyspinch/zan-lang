(function () {
  'use strict';

  function esc(value) {
    return String(value == null ? '' : value).replace(/[&<>\"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  function duration(us) {
    if (!Number.isFinite(us)) { return '—'; }
    if (us < 1000) { return us + ' µs'; }
    if (us < 1000000) { return (us / 1000).toFixed(3) + ' ms'; }
    return (us / 1000000).toFixed(3) + ' s';
  }

  function cpuTime(cpu, unit) {
    return Number.isFinite(cpu) && cpu >= 0 && (unit === 'cycles' || unit === 'ns') ?
      esc(cpu.toLocaleString('zh-CN') + ' ' + unit) : '未测得';
  }

  function allocatedBytes(bytes) {
    return Number.isFinite(bytes) && bytes >= 0 ? bytes + ' B' : '未测得';
  }

  function resourceCoverage(row) {
    var coverage = row.resources.coverage;
    if (coverage == null) {
      coverage = row.protocol === 'HTTP/1' ? 'header-to-cleanup' : row.protocol === 'HTTP/2' ? 'application' : '';
    }
    switch (coverage) {
      case 'read-loop-to-cleanup': return '请求读取循环开始至清理结束';
      case 'header-to-cleanup': return '请求头至清理结束';
      case 'application': return '应用处理阶段';
      default: return '未知';
    }
  }

  function resourceDetails(row) {
    var resources = row.resources;
    if (!resources) { return '<p class="hint">这条请求没有资源样本。</p>'; }
    return '<p><strong>请求资源</strong> · CPU ' + cpuTime(resources.cpu, resources.cpu_unit) + ' · 累计分配 ' +
      allocatedBytes(resources.allocated_bytes) + ' · 调度片段 ' + esc(resources.slices) +
      ' · 采集截止时未结束的子任务 ' + (resources.unfinished_tasks == null ? '—' : esc(resources.unfinished_tasks)) +
      '</p><p class="hint">采集范围 ' + resourceCoverage(row) + '。CPU 和分配统计覆盖归属于本请求的 Zan 任务。' +
      (resources.unfinished_tasks > 0 ? '仍在其他线程运行的子任务可能有片段未结算，资源合计可能偏低。' : '') + '</p>' +
      (resources.stage_resources === true ? '<p><strong>未归入阶段的资源</strong> · CPU ' +
        cpuTime(resources.residual_cpu, resources.cpu_unit) + ' · 累计分配 ' +
        allocatedBytes(resources.residual_allocated_bytes) + '</p>' : '');
  }

  function waitDetails(row) {
    var omitted = Number.isInteger(row.omitted_waits) && row.omitted_waits > 0 ?
      '<p class="hint">超出容量的等待 ' + esc(row.omitted_waits) + ' 个。</p>' : '';
    if (!Array.isArray(row.waits) && !omitted) { return ''; }
    var waits = Array.isArray(row.waits) ? row.waits.filter(function (wait) {
      return wait && Number.isFinite(wait.offset_us);
    }) : [];
    var heading = '<h4>任务等待</h4>' + omitted;
    if (!waits.length) { return heading + '<p class="hint">这条请求没有保留的等待记录。</p>'; }
    waits.sort(function (a, b) { return a.offset_us - b.offset_us; });
    return heading + '<p class="hint">开始偏移从请求起点计算。等待可与处理阶段或其他任务的等待重叠，' +
      '各条等待的耗时相加可能超过请求总耗时。</p>' +
      '<div class="table-scroll trace-waits" role="region" aria-label="任务等待" tabindex="0">' +
      '<table class="table"><thead><tr><th>类型</th><th>文件</th><th class="num">行</th>' +
      '<th class="num">列</th><th class="num">开始偏移</th><th class="num">等待耗时</th></tr></thead><tbody>' +
      waits.map(function (wait) {
        return '<tr><td class="mono">' + (typeof wait.kind === 'string' && wait.kind ? esc(wait.kind) : '—') +
          '</td><td class="mono">' + (typeof wait.file === 'string' && wait.file ? esc(wait.file) : '—') +
          '</td><td class="num">' + (Number.isInteger(wait.line) ? esc(wait.line) : '—') +
          '</td><td class="num">' + (Number.isInteger(wait.column) ? esc(wait.column) : '—') +
          '</td><td class="num">' + duration(wait.offset_us) + '</td><td class="num">' +
          (wait.elapsed_us == null ? '采集截止时未结束' : duration(wait.elapsed_us)) + '</td></tr>';
      }).join('') + '</tbody></table></div>';
  }

  function executionDetails(row) {
    var slices = Array.isArray(row.execution_slices) ? row.execution_slices.slice() : [];
    var omitted = row.omitted_execution_slices;
    if (!slices.length && !omitted) { return ''; }
    var unit = row.resources && row.resources.cpu_unit;
    slices.sort(function (a, b) { return a.start_us - b.start_us; });
    return '<details><summary>线程执行片段（' + slices.length + ' 条）</summary>' +
      (omitted > 0 ? '<p class="hint">超出容量的片段 ' + esc(omitted) + ' 条，仍计入请求资源总量。</p>' : '') +
      (slices.length ? '<div class="table-scroll" role="region" aria-label="线程执行片段" tabindex="0">' +
        '<table class="table"><thead><tr><th class="num">线程 ID</th><th class="num">开始偏移</th>' +
        '<th class="num">经过时间</th><th class="num">CPU</th><th class="num">分配</th></tr></thead><tbody>' +
        slices.map(function (slice) {
          return '<tr><td class="num">' + esc(slice.tid) + '</td><td class="num">' +
            duration(slice.start_us - row.start_us) + '</td><td class="num">' + duration(slice.elapsed_us) +
            '</td><td class="num">' + cpuTime(slice.cpu, unit) + '</td><td class="num">' +
            allocatedBytes(slice.allocated_bytes) + '</td></tr>';
        }).join('') + '</tbody></table></div>' : '') + '</details>';
  }

  function isIssue(row) {
    return row.status >= 400 || (row.outcome !== 'sent' && row.outcome !== 'stream_handled') ||
      row.stages.some(function (s) { return s.failure || s.elapsed_us == null; });
  }

  function capture(page) {
    return page.clock_mono_us + ':' + page.pid;
  }

  function processStart(page) {
    var start = page.process_start_unix_ms;
    return Number.isSafeInteger(start) && start > 0 ? start : 0;
  }

  class RequestTraces extends HTMLElement {
    connectedCallback() {
      if (!this.status) {
        this.status = this.querySelector('[data-trace-status]');
        this.summary = this.querySelector('[data-trace-summary]');
        this.detail = this.querySelector('[data-trace-detail]');
        this.list = this.querySelector('[data-trace-list]');
        this.rows = this.querySelector('[data-trace-rows]');
        this.search = this.querySelector('[data-trace-search]');
        this.result = this.querySelector('[data-trace-result]');
        this.order = this.querySelector('[data-trace-order]');
        this.refresh = this.querySelector('[data-trace-refresh]');
        this.older = this.querySelector('[data-trace-older]');
        var host = this.closest('[data-stream]');
        var jump = host && host.querySelector('[data-trace-jump]');
        if (jump) {
          jump.addEventListener('click', () => {
            this.scrollIntoView({ block: 'start' });
            this.search.focus({ preventScroll: true });
          });
        }
        this.search.addEventListener('input', () => this.renderRows());
        this.result.addEventListener('change', () => this.renderRows());
        this.order.addEventListener('change', () => this.renderRows());
        this.refresh.addEventListener('click', () => this.load(0));
        this.older.addEventListener('click', () => this.load(this.page.next_before));
        this.rows.addEventListener('click', (ev) => {
          var button = ev.target.closest('[data-trace-id]');
          if (button) { this.openTrace(Number(button.dataset.traceId)); }
        });
        this.detail.addEventListener('click', (ev) => {
          var button = ev.target.closest('[data-trace-close]');
          if (!button) { return; }
          this.detail.hidden = true;
          var trigger = this.rows.querySelector('[data-trace-id="' + Number(button.dataset.traceClose) + '"]');
          if (trigger) { trigger.focus(); }
        });
      }
      this.load(0);
    }

    disconnectedCallback() {
      if (this.pending) { this.pending.abort(); }
      this.pending = null;
    }

    async load(before) {
      var source = this.dataset.source;
      if (!source || this.pending) { return; }
      var pending = this.pending = new AbortController();
      this.refresh.disabled = this.older.disabled = true;
      this.status.textContent = '正在读取请求诊断…';
      this.setAttribute('aria-busy', 'true');
      var blockOlder = false;
      try {
        var url = new URL(source, window.location.href);
        url.searchParams.set('limit', '100');
        if (before) { url.searchParams.set('before', before); }
        var response = await fetch(url, { credentials: 'same-origin', cache: 'no-store', signal: pending.signal });
        if (pending.signal.aborted) { return; }
        if (response.status === 503) {
          var error = await response.json();
          if (pending.signal.aborted) { return; }
          if (!error || error.code !== '0503') { throw new Error('HTTP 503'); }
          if (before) {
            blockOlder = true;
            this.status.textContent = '本次响应的 Worker 未开启采集。已保留当前页，请读取最新记录。';
          } else {
            this.page = null;
            this.renderRows();
            this.status.textContent = '接收请求的 Worker 已关闭采集。可设置 ZAN_REQUEST_PROFILE_CAPACITY 后重启服务。';
          }
          return;
        }
        if (response.status === 401 || response.status === 403 || response.redirected) {
          this.page = null;
          this.renderRows();
          this.status.textContent = '请求诊断需要登录并具有运行监控查看权限。';
          return;
        }
        if (!response.ok) { throw new Error('HTTP ' + response.status); }
        var page = await response.json();
        if (pending.signal.aborted) { return; }
        if (!Array.isArray(page.requests) || !Number.isSafeInteger(page.pid) ||
            !Number.isSafeInteger(page.clock_mono_us) || !Number.isSafeInteger(page.clock_unix_ms)) { throw new Error('unexpected response'); }
        if (before && (capture(this.page) !== capture(page) || processStart(this.page) !== processStart(page))) {
          blockOlder = true;
          this.status.textContent = '响应来自其他 Worker 或采集已重启。已保留当前页，请读取最新记录。';
          return;
        }
        this.page = page;
        this.renderRows();
        this.status.textContent = 'Worker PID ' + page.pid + ' · 累计完成 ' + page.completed +
          ' 条 · 历史覆盖 ' + page.overwritten + ' 条 · 每页最多 100 条，筛选仅作用于当前页';
      } catch (err) {
        if (!pending.signal.aborted) {
          this.status.textContent = '请求诊断暂不可用，请重试。' + (this.page ? ' 下方为上次读取的记录。' : '');
        }
      } finally {
        if (this.pending === pending) {
          this.pending = null;
          this.removeAttribute('aria-busy');
          this.refresh.disabled = false;
          this.older.disabled = blockOlder || !this.page || !this.page.next_before;
        }
      }
    }

    renderRows() {
      this.detail.hidden = true;
      this.summary.hidden = this.list.hidden = !this.page;
      if (!this.page) {
        this.rows.replaceChildren();
        this.detail.replaceChildren();
        return;
      }
      var page = this.page;
      var term = this.search.value.trim().toLowerCase();
      var result = this.result.value;
      var rows = page.requests.filter(function (r) {
        if (!(r.method + ' ' + r.route + ' ' + r.action).toLowerCase().includes(term)) { return false; }
        if (result === 'issue') { return isIssue(r); }
        if (result === '4xx') { return r.status >= 400 && r.status < 500; }
        return result !== '5xx' || r.status >= 500;
      });
      if (this.order.value === 'slow') { rows.sort(function (a, b) { return b.elapsed_us - a.elapsed_us; }); }

      var failures = new Map(), issues = 0, http4xx = 0, http5xx = 0, slowest = 0;
      rows.forEach(function (r) {
        if (isIssue(r)) { issues++; }
        if (r.status >= 500) { http5xx++; }
        else if (r.status >= 400) { http4xx++; }
        slowest = Math.max(slowest, r.elapsed_us);
        r.stages.forEach(function (s) {
          if (!s.failure && s.elapsed_us != null) { return; }
          var key = s.name + ' / ' + (s.failure || '未结束');
          failures.set(key, (failures.get(key) || 0) + 1);
        });
      });
      var common = Array.from(failures).sort(function (a, b) { return b[1] - a[1]; }).slice(0, 5);
      this.summary.textContent = '匹配 ' + rows.length + ' / ' + page.requests.length + ' 条 · 异常 ' + issues +
        ' 条 · HTTP 4xx ' + http4xx + ' · HTTP 5xx ' + http5xx + ' · 最慢 ' +
        (rows.length ? duration(slowest) : '—') + (common.length ?
          ' · 常见异常阶段（出现次数） ' + common.map(function (item) { return item[0] + ' × ' + item[1]; }).join('、') : '');
      this.rows.innerHTML = rows.length ? rows.map(function (r) {
        var date = new Date(page.clock_unix_ms + (r.start_us - page.clock_mono_us) / 1000).toISOString();
        return '<tr><td class="mono">' + date.slice(0, -1).replace('T', ' ') + '</td><td>' +
          esc(r.method + ' ' + (r.route || '未匹配路由')) + (r.action ? '<div class="hint">' + esc(r.action) + '</div>' : '') +
          '</td><td>' + esc(r.protocol) + '</td><td>' + esc(r.status || '—') + '</td><td' +
          (isIssue(r) ? ' class="trace-issue"' : '') + '>' + esc(r.outcome) + '</td><td class="num">' +
          duration(r.elapsed_us) + '</td><td><button class="btn sm" type="button" data-trace-id="' + esc(r.id) +
          '">查看 #' + esc(r.id) + '</button></td></tr>';
      }).join('') : '<tr><td colspan="7" class="muted">' + (page.requests.length ?
        '当前页没有匹配的请求。' : '当前没有保留记录。有业务请求完成后可读取最新记录。') + '</td></tr>';
    }

    openTrace(id) {
      var page = this.page;
      var row = page.requests.find(function (r) { return r.id === id; });
      if (!row) { return; }
      var stages = row.stages.slice().sort(function (a, b) { return a.offset_us - b.offset_us; });
      var stageResources = row.resources && row.resources.stage_resources === true;
      var extent = Math.max(row.elapsed_us, 1);
      var start = processStart(page);
      var key = capture(page) + ':' + row.id + (start ? ':' + start : '');
      this.detail.innerHTML = '<div class="trace-detail-head"><h3 tabindex="-1">请求 #' + esc(row.id) + ' · ' +
        esc(row.method + ' ' + (row.route || '未匹配路由')) + '</h3><button class="btn sm" type="button" data-trace-close="' +
        esc(row.id) + '">关闭时间线</button></div>' +
        '<p class="hint">采集标识 <code>' + esc(key) + '</code> · ' + esc(row.protocol) +
        ' · HTTP ' + esc(row.status || '—') + ' · ' + esc(row.outcome) + ' · 总耗时 ' + duration(row.elapsed_us) +
        (row.omitted_stages ? ' · 超出容量的阶段 ' + esc(row.omitted_stages) + ' 个' : '') + '</p>' +
        resourceDetails(row) + (stages.length ? '<p class="hint">所有阶段共用请求起点和终点。重叠时段可并行或嵌套，横轴为经过时间。' +
          (stageResources ? '阶段 CPU 和分配量按任务当时的活跃阶段归属。' : '') + '</p>' +
          '<div class="table-scroll"><table class="table trace-timeline"><thead><tr><th>阶段</th>' +
          '<th class="num">开始偏移</th><th class="num">阶段耗时</th>' +
          (stageResources ? '<th class="num">CPU</th><th class="num">分配</th>' : '') + '<th>结果</th>' +
          '<th><div class="trace-axis"><span>0</span><span>' + duration(row.elapsed_us) + '</span></div></th></tr></thead><tbody>' +
          stages.map(function (s) {
            var open = s.elapsed_us == null;
            return '<tr><td class="mono">' + esc(s.name) + '</td><td class="num">' + duration(s.offset_us) +
              '</td><td class="num">' + (open ? '—' : duration(s.elapsed_us)) + '</td>' +
              (stageResources ? '<td class="num">' + cpuTime(s.cpu, row.resources.cpu_unit) +
                '</td><td class="num">' + allocatedBytes(s.allocated_bytes) + '</td>' : '') +
              '<td>' + esc(s.failure || (open ? '未结束' : '已结束')) + '</td><td><div class="trace-track" aria-hidden="true">' +
              '<span class="trace-bar' + (open || s.failure ? ' trace-bar-issue' : '') + '"></span></div></td></tr>';
          }).join('') + '</tbody></table></div>' : '<p class="hint">这条请求没有阶段记录。</p>') +
        waitDetails(row) + executionDetails(row);
      this.detail.querySelectorAll('.trace-bar').forEach(function (bar, index) {
        var s = stages[index];
        var start = Math.max(0, Math.min(1, s.offset_us / extent));
        var width = Math.max(0, Math.min(1 - start, (s.elapsed_us || 0) / extent));
        bar.style.left = (start * 100) + '%';
        bar.style.width = (width * 100) + '%';
      });
      this.detail.hidden = false;
      this.detail.querySelector('h3').focus();
    }
  }

  customElements.define('zan-request-traces', RequestTraces);
}());
