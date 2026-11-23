/* Admin 运行监控部件：SSE 实时快照流、今日累计图、接口排行、Canvas 折线绘制与顶栏告警轮询。
   通过 window.ZanAdmin 生命周期总线按面板挂载/卸载，不侵入外壳核心。 */
(function () {
  var bus = window.ZanAdmin;
  var stream = null;
  var streamGen = 0;
  var dayTimer = null;
  var dayPts = {};
  var dayDay = '';
  var topDay = {};
  var topLive = {};
  var topTotal = 0;
  var topSort = { key: 'calls', dir: -1 };

  function panelEl() {
    return (bus && bus.panel) ? bus.panel() : document.getElementById('ad-panel');
  }

  function toast(msg, kind) {
    if (bus && bus.toast) { bus.toast(msg, kind); return; }
    if (window.Layer) { window.Layer.msg(msg, kind); }
  }

  function esc(s) {
    if (bus && bus.esc) { return bus.esc(s); }
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  function stopStream() {
    streamGen++;
    if (stream) { stream.close(); stream = null; }
    if (dayTimer) { clearInterval(dayTimer); dayTimer = null; }
    dayPts = {};
    dayDay = '';
    topDay = {};
    topLive = {};
    topTotal = 0;
  }

  function syncStreamNote() {
    var p = panelEl();
    if (!p) { return; }
    var host = p.querySelector('[data-stream]');
    var s = host ? host.getAttribute('data-stream-status') : 'connected';
    var d = host ? host.getAttribute('data-day-status') : '';
    var note = p.querySelector('.stream-note');
    var bad = s === 'disconnected' || d === 'disconnected';
    if (!bad) {
      if (note && s === 'connected') { note.remove(); }
      return;
    }
    var lines = [];
    if (s === 'disconnected') { lines.push('实时连接中断，正在重连；当前数据可能已过期'); }
    if (d === 'disconnected') { lines.push('今日历史数据暂不可用，稍后自动重试'); }
    if (!note) {
      note = document.createElement('div');
      note.className = 'stream-note';
      p.insertBefore(note, p.firstChild);
    }
    note.textContent = lines.join('　');
  }

  function startStream() {
    var p = panelEl();
    if (!p) { return; }
    var host = p.querySelector('[data-stream]');
    if (!host) { return; }
    var gen = ++streamGen;
    var url = host.getAttribute('data-stream');
    host.setAttribute('data-stream-status', 'connecting');
    stream = new EventSource(url);
    stream.addEventListener('metrics', function (ev) {
      if (gen !== streamGen) { return; }
      try {
        paintSnapshot(JSON.parse(ev.data));
        host.setAttribute('data-stream-status', 'connected');
        syncStreamNote();
      } catch (e) { toast('实时数据格式错误', 'bad'); }
    });
    stream.addEventListener('series', function (ev) {
      if (gen !== streamGen) { return; }
      try { paintSeries(JSON.parse(ev.data)); } catch (e) { toast('实时序列格式错误', 'bad'); }
    });
    stream.onerror = function () {
      if (gen !== streamGen) { return; }
      host.setAttribute('data-stream-status', 'connecting');
      setTimeout(function () {
        if (gen !== streamGen || !stream) { return; }
        if (stream.readyState === 2) {
          try { stream.close(); } catch (e) { /* already closed */ }
          startStream();
        } else if (stream.readyState === 0) {
          host.setAttribute('data-stream-status', 'disconnected');
          syncStreamNote();
        }
      }, 10000);
    };
    wireTopSort();
    startDay(host.getAttribute('data-day'), gen);
  }

  function startDay(url, gen) {
    if (!url) { return; }
    var tick = function () {
      fetch(url, { credentials: 'same-origin' })
        .then(function (r) { if (!r.ok) { throw new Error('day metrics'); } return r.json(); })
        .then(function (j) {
          if (gen !== streamGen) { return; }
          paintDay(j);
          var host = panelEl() && panelEl().querySelector('[data-stream]');
          if (host) { host.setAttribute('data-day-status', 'connected'); }
          syncStreamNote();
        })
        .catch(function () {
          if (gen !== streamGen) { return; }
          var host = panelEl() && panelEl().querySelector('[data-stream]');
          if (host) { host.setAttribute('data-day-status', 'disconnected'); }
          syncStreamNote();
        });
    };
    tick();
    dayTimer = setInterval(tick, 60000);
  }

  function paintDay(d) {
    if (d.day !== dayDay) { dayPts = {}; dayDay = d.day || ''; }
    put('day_calls', d.calls || 0);
    put('day_errors', d.errors || 0);
    put('day_avg_us', dur(d.avg_us));
    put('day_max_us', dur(d.max_us));
    put('day_total_us', dur(d.total_us));
    put('day_label', d.day || '');

    (d.points || []).forEach(function (p) { dayPts[p.t] = p; });
    var pts = Object.keys(dayPts).map(function (k) { return dayPts[k]; })
      .sort(function (a, b) { return a.t - b.t; })
      .map(function (p) {
        return { t: p.t, label: clock(p.t), calls: p.calls || 0,
                 errors: p.errors || 0, avg_us: p.avg_us || 0,
                 max_us: p.max_us || 0 };
      });
    draw('chart-day-req', [
      { color: themeVar('--viz-1', '#18a058'), cls: 'c-blue', label: '请求', values: pts.map(function (p) { return p.calls; }) },
      { color: themeVar('--danger', '#d03050'), cls: 'c-red', label: '错误', values: pts.map(function (p) { return p.errors; }) }
    ], pts, '');
    draw('chart-day-lat', [
      { color: themeVar('--viz-4', '#d03050'), cls: 'c-violet', label: '平均', values: pts.map(function (p) { return ms(p.avg_us); }) },
      { color: themeVar('--viz-3', '#f0a020'), cls: 'c-amber', label: '最慢', values: pts.map(function (p) { return ms(p.max_us); }) }
    ], pts, ' ms');

    topTotal = d.calls || 0;
    topDay = {};
    (d.top || []).forEach(function (s) { topDay[s.req] = s; });
    paintTop();
  }

  function wireTopSort() {
    var root = panelEl();
    if (!root) { return; }
    root.querySelectorAll('[data-toptable] th[data-sort]').forEach(function (th) {
      if (!th.getAttribute('data-label')) {
        th.setAttribute('data-label', th.textContent);
      }
      th.style.cursor = 'pointer';
      th.setAttribute('role', 'button');
      th.tabIndex = 0;
      th.addEventListener('keydown', function (ev) {
        if (ev.key === 'Enter' || ev.key === ' ') { ev.preventDefault(); th.click(); }
      });
      th.addEventListener('click', function () {
        var key = th.getAttribute('data-sort');
        if (topSort.key === key) { topSort.dir = -topSort.dir; }
        else { topSort = { key: key, dir: key === 'req' ? 1 : -1 }; }
        paintTop();
      });
    });
    paintTopHead();
  }

  function paintTopHead() {
    var root = panelEl();
    if (!root) { return; }
    root.querySelectorAll('[data-toptable] th[data-sort]').forEach(function (th) {
      var label = th.getAttribute('data-label') || th.textContent;
      var on = th.getAttribute('data-sort') === topSort.key;
      th.textContent = label + (on ? (topSort.dir < 0 ? ' \u2193' : ' \u2191') : '');
      th.setAttribute('aria-sort', on ? (topSort.dir < 0 ? 'descending' : 'ascending') : 'none');
    });
  }

  function paintTop() {
    var root = panelEl();
    var tb = root ? root.querySelector('[data-daytop]') : null;
    if (!tb) { return; }
    var rows = [];
    var keys = {};
    Object.keys(topDay).forEach(function (k) { keys[k] = 1; });
    Object.keys(topLive).forEach(function (k) { keys[k] = 1; });
    Object.keys(keys).forEach(function (req) {
      var d = topDay[req], l = topLive[req];
      rows.push({
        req: req,
        day: !!d,
        calls: d ? (d.calls || 0) : 0,
        errors: d ? (d.errors || 0) : 0,
        avg_us: d ? (d.avg_us || 0) : 0,
        p95_us: d ? (d.p95_us || 0) : 0,
        max_us: d ? (d.max_us || 0) : 0,
        total_us: d ? (d.total_us || 0) : 0,
        failed: l ? l.failed : null,
        rejected: l ? l.rejected : null
      });
    });
    var k = topSort.key === 'share' ? 'calls' : topSort.key, dir = topSort.dir;
    rows.sort(function (a, b) {
      if (k === 'req') { return a.req < b.req ? -dir : (a.req > b.req ? dir : 0); }
      if (a[k] == null) { return b[k] == null ? 0 : 1; }
      if (b[k] == null) { return -1; }
      return (a[k] - b[k]) * dir;
    });
    paintTopHead();
    tb.innerHTML = rows.length
      ? rows.map(function (s) {
          var share = s.day && topTotal > 0
            ? (s.calls * 100 / topTotal).toFixed(1) + '%' : '—';
          var num = function (v) { return s.day ? String(v) : '—'; };
          var d2 = function (v) { return s.day ? dur(v) : '—'; };
          return '<tr><td class="ellip mono" title="' + esc(s.req) + '">' +
                 esc(s.req) + '</td><td class="num">' + num(s.calls) +
                 '</td><td class="num muted">' + share +
                 '</td><td class="num">' + num(s.errors) +
                 '</td><td class="num">' + d2(s.avg_us) +
                 '</td><td class="num">' + d2(s.p95_us) +
                 '</td><td class="num">' + d2(s.max_us) +
                 '</td><td class="num">' + d2(s.total_us) +
                 '</td><td class="num">' + (s.failed == null ? '—' : s.failed) +
                 '</td><td class="num">' + (s.rejected == null ? '—' : s.rejected) + '</td></tr>';
        }).join('')
      : '<tr><td colspan="10" class="muted">所选日期暂无接口统计</td></tr>';
  }

  function clock(sec) {
    var d = new Date(sec * 1000);
    var p2 = function (n) { return (n < 10 ? '0' : '') + n; };
    return p2(d.getUTCHours()) + ':' + p2(d.getUTCMinutes());
  }

  function put(name, value) {
    var root = panelEl();
    if (!root) { return; }
    root.querySelectorAll('[data-metric="' + name + '"]').forEach(function (n) {
      n.textContent = value;
    });
  }

  function bytes(v) {
    if (!Number.isFinite(v) || v < 0) { return '—'; }
    if (v < 1024) { return v + ' B'; }
    if (v < 1048576) { return (v / 1024).toFixed(0) + ' KiB'; }
    return (v / 1048576).toFixed(1) + ' MiB';
  }

  function dur(us) {
    us = us || 0;
    if (us < 0) { return 'n/a'; }
    if (us < 1000) { return us + ' \u00b5s'; }
    if (us < 1000000) { return (us / 1000).toFixed(2) + ' ms'; }
    return (us / 1000000).toFixed(2) + ' s';
  }

  function ms(us) { return Math.round((us || 0) / 10) / 100; }

  function upt(msv) {
    var t = Math.floor((msv || 0) / 1000);
    var p2 = function (n) { return (n < 10 ? '0' : '') + n; };
    var c = p2(Math.floor((t % 86400) / 3600)) + ':' +
            p2(Math.floor((t % 3600) / 60)) + ':' + p2(t % 60);
    var d = Math.floor(t / 86400);
    return d > 0 ? d + 'd ' + c : c;
  }

  function paintProcs(rows) {
    var root = panelEl();
    var tb = root ? root.querySelector('[data-procs]') : null;
    if (!tb) { return; }
    rows = rows || [];
    tb.innerHTML = rows.length
      ? rows.map(function (p) {
          var stale = !p.live;
          return '<tr' + (stale ? ' class="muted"' : '') + '><td class="mono">' +
                 (p.worker || 0) + '</td><td class="mono">' + (p.pid || 0) +
                 '</td><td class="mono">' + upt(p.uptime_ms) +
                 '</td><td class="num">' + (p.requests || 0) +
                 '</td><td class="num">' + (p.errors || 0) +
                 '</td><td class="num">' + dur(p.avg_us) +
                 '</td><td class="num">' + dur(p.max_us) +
                 '</td><td class="num">' + (p.concurrent || 0) +
                 '</td><td class="num">' + (p.streams || 0) +
                 '</td><td class="num">' + (p.queries || 0) +
                 '</td><td class="num">' + (Number.isFinite(p.cpu_percent) && p.cpu_percent >= 0 ? p.cpu_percent + '%' : '—') +
                 '</td><td class="num">' + bytes(p.mem_rss_bytes > 0 ? p.mem_rss_bytes : -1) +
                 '</td><td class="num">' + (stale ? (p.age || 0) + 's 未上报' : '正常') +
                 '</td></tr>';
        }).join('')
      : '<tr><td colspan="13" class="muted">等待数据…</td></tr>';
  }

  function paintSnapshot(m) {
    var r = m.requests || {}, q = m.queries || {};
    put('requests', r.count || 0);
    put('errors', r.errors || 0);
    put('failed', r.failed || 0);
    put('rejected', r.rejected || 0);
    put('avg_us', dur(r.avg_us));
    put('max_us', dur(r.max_us));
    put('queries', q.count || 0);
    put('query_avg_us', dur(q.avg_us));
    var root = panelEl();
    var live = (m.processes || []).filter(function (p) { return p.live; });
    function resource(key, unit, format, min) {
      var values = live.map(function (p) { return p[key]; }).filter(function (v) {
        return Number.isFinite(v) && v >= min;
      });
      var value = values.length ? format(values.reduce(function (sum, v) { return sum + v; }, 0)) : '—';
      if (values.length && values.length < live.length) { value += '（' + values.length + '/' + live.length + '）'; }
      put(unit, value);
      var node = root && root.querySelector('[data-metric="' + unit + '"]');
      if (node) { node.title = '在线进程采样覆盖 ' + values.length + ' / ' + live.length; }
    }
    resource('cpu_percent', 'cpu', function (v) { return v + '%'; }, 0);
    resource('mem_rss_bytes', 'mem', bytes, 1);
    paintProcs(m.processes);
    topLive = {};
    (m.top_requests || []).forEach(function (s) { topLive[s.req] = s; });
    paintTop();
    var top = root && root.querySelector('[data-topsql]');
    if (top) {
      var ts = m.top_sql || [];
      top.innerHTML = ts.length
        ? ts.map(function (s) {
            return '<tr><td class="ellip mono" title="' + esc(s.sql) + '">' +
                   esc(s.sql) + '</td><td class="ops"><button class="link-btn" type="button" data-sql-preview="' +
                   esc(s.sql) + '">查看 SQL</button></td><td class="num">' + (s.count || 0) +
                   '</td><td class="num">' + dur(s.avg_us) +
                   '</td><td class="num">' + dur(s.max_us) +
                   '</td><td class="num">' + dur(s.total_us) + '</td></tr>';
          }).join('')
        : '<tr><td colspan="6" class="muted">暂无 SQL 记录</td></tr>';
    }
  }

  function themeVar(name, fallback) {
    var v = getComputedStyle(document.documentElement).getPropertyValue(name);
    v = (v || '').trim();
    return v.length ? v : fallback;
  }

  function paintSeries(s) {
    var pts = s.points || [];
    var pal = {
      req: themeVar('--viz-1', '#18a058'),
      err: themeVar('--danger', '#d03050'),
      avg: themeVar('--viz-4', '#d03050'),
      p95: themeVar('--viz-3', '#f0a020'),
      q: themeVar('--viz-2', '#2080f0'),
      cpu: themeVar('--success', '#18a058'),
      mem: themeVar('--text-tertiary', '#767c82')
    };
    draw('chart-req', [
      { color: pal.req, cls: 'c-blue', label: '请求', values: pts.map(function (p) { return p.req; }) },
      { color: pal.err, cls: 'c-red', label: '错误', values: pts.map(function (p) { return p.err; }) }
    ], pts, '');
    draw('chart-lat', [
      { color: pal.avg, cls: 'c-violet', label: '平均', values: pts.map(function (p) { return p.req > 0 ? ms(p.avg_us) : null; }) },
      { color: pal.p95, cls: 'c-amber', label: 'Worker P95 最大值', values: pts.map(function (p) { return p.req > 0 ? ms(p.p95_us) : null; }) }
    ], pts, ' ms');
    draw('chart-db', [
      { color: pal.q, cls: 'c-teal', label: 'SQL 调用', values: pts.map(function (p) { return p.queries; }) }
    ], pts, ' 次/s');
    draw('chart-db-lat', [
      { color: pal.p95, cls: 'c-amber', label: '平均', values: pts.map(function (p) { return p.queries > 0 ? ms(p.query_avg_us) : null; }) }
    ], pts, ' ms');
    draw('chart-cpu', [
      { color: pal.cpu, cls: 'c-green', label: '进程 CPU', values: pts.map(function (p) { return p.cpu_percent >= 0 ? p.cpu_percent : null; }) }
    ], pts, '%');
    draw('chart-rss', [
      { color: pal.mem, cls: 'c-gray', label: '进程 RSS', values: pts.map(function (p) {
          return p.mem_rss_bytes > 0 ? Math.round(p.mem_rss_bytes / 10485.76) / 100 : null; }) }
    ], pts, ' MiB');
  }

  function monTip() {
    var t = document.getElementById('mon-tip');
    if (!t) {
      t = document.createElement('div');
      t.id = 'mon-tip';
      t.className = 'mon-tip';
      t.hidden = true;
      document.body.appendChild(t);
    }
    return t;
  }

  function niceMax(v) {
    if (v <= 0) { return 1; }
    var mag = Math.pow(10, Math.floor(Math.log(v) / Math.LN10));
    var n = v / mag;
    if (n <= 1) { return mag; }
    if (n <= 2) { return 2 * mag; }
    if (n <= 5) { return 5 * mag; }
    return 10 * mag;
  }

  function fmtAxis(v) {
    if (v >= 100) { return String(Math.round(v)); }
    return String(Math.round(v * 100) / 100);
  }

  function xTickLabel(p) {
    if (!p) { return ''; }
    if (p.label) { return p.label; }
    if (typeof p.t === 'number') { return p.t === 0 ? '本秒（汇总中）' : (-p.t) + 's前'; }
    return '';
  }

  function draw(id, series, pts, unit) {
    var root = panelEl();
    var c = root ? root.querySelector('#' + id) : null;
    if (!c) { return; }
    var dpr = window.devicePixelRatio || 1;
    var w = c.clientWidth, h = c.clientHeight;
    c.width = w * dpr; c.height = h * dpr;
    var g = c.getContext('2d');

    var peak = null;
    series.forEach(function (s) {
      s.values.forEach(function (v) {
        if (Number.isFinite(v) && (peak == null || v > peak)) { peak = v; }
      });
    });
    var max = niceMax(peak || 0);

    var padT = 6, padB = 18, padR = 10;
    var font = '10px system-ui, sans-serif';
    g.font = font;
    var padL = 0;
    for (var ti = 0; ti <= 4; ti++) {
      padL = Math.max(padL, g.measureText(fmtAxis(max - (max * ti) / 4)).width);
    }
    padL = Math.ceil(padL) + 12;
    var plotW = w - padL - padR, plotH = h - padT - padB;
    function px(i, n) { return padL + (n < 2 ? plotW / 2 : (plotW * i) / (n - 1)); }
    function py(v) { return padT + plotH - (plotH * v) / max; }

    function render(hover) {
      var grid = themeVar('--viz-grid', '#efeff5');
      var axis = themeVar('--border-strong', '#e0e0e6');
      var tick = themeVar('--text-tertiary', '#767c82');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, w, h);
      g.font = font;
      g.lineWidth = 1;
      g.fillStyle = tick;
      for (var i = 0; i <= 4; i++) {
        var gy = Math.round(padT + (plotH * i) / 4) + .5;
        g.strokeStyle = i === 4 ? axis : grid;
        g.beginPath(); g.moveTo(padL, gy); g.lineTo(w - padR, gy); g.stroke();
        g.textAlign = 'right';
        g.fillText(fmtAxis(max - (max * i) / 4), padL - 5, gy + 3.5);
      }
      g.strokeStyle = axis;
      g.beginPath();
      g.moveTo(padL + .5, padT);
      g.lineTo(padL + .5, padT + plotH);
      g.stroke();
      var n0 = pts ? pts.length : 0;
      if (n0 >= 2) {
        g.fillStyle = tick;
        var ends = [0, Math.round((n0 - 1) / 2), n0 - 1];
        for (var k = 0; k < ends.length; k++) {
          g.textAlign = k === 0 ? 'left' : (k === 2 ? 'right' : 'center');
          g.fillText(xTickLabel(pts[ends[k]]), px(ends[k], n0), h - 5);
        }
      }
      series.forEach(function (s) {
        var n = s.values.length;
        g.beginPath();
        var drawing = false;
        for (var i = 0; i < n; i++) {
          if (!Number.isFinite(s.values[i])) { drawing = false; continue; }
          var x = px(i, n);
          var y = py(s.values[i]);
          if (drawing) {
            g.lineTo(x, y);
          } else {
            g.moveTo(x, y);
            if (!Number.isFinite(s.values[i + 1])) { g.lineTo(x + 1, y); }
          }
          drawing = true;
        }
        g.strokeStyle = s.color;
        g.lineWidth = 1.5;
        g.stroke();
      });
      var nn = series[0] ? series[0].values.length : 0;
      if (hover >= 0 && nn >= 2) {
        var hx = px(hover, nn);
        g.strokeStyle = tick;
        g.lineWidth = 1;
        g.beginPath(); g.moveTo(hx + .5, padT); g.lineTo(hx + .5, padT + plotH); g.stroke();
        series.forEach(function (s) {
          if (!Number.isFinite(s.values[hover])) { return; }
          var y = py(s.values[hover]);
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
        var idx = Math.round(((ev.clientX - rect.left) - padL) / plotW * (n - 1));
        if (idx < 0) { idx = 0; }
        if (idx > n - 1) { idx = n - 1; }
        c._render(idx);
        var sp = c._pts, un = c._unit;
        var when = (sp && sp[idx] && sp[idx].label) ? sp[idx].label
          : ((sp && sp[idx] && typeof sp[idx].t === 'number')
              ? (sp[idx].t === 0 ? '本秒（汇总中）' : (-sp[idx].t) + ' 秒前') : '');
        var html = when ? '<b>' + when + '</b>' : '';
        sv.forEach(function (s) {
          html += '<div><i class="' + (s.cls || 'c-gray') + '"></i>'
            + esc(s.label || '') + ' ' + (Number.isFinite(s.values[idx]) ? s.values[idx] + un : '—') + '</div>';
        });
        var tip = monTip();
        tip.innerHTML = html;
        tip.hidden = false;
        var gap = 12;
        var x = ev.clientX + gap;
        var y = ev.clientY + gap;
        var tw = tip.offsetWidth;
        var th = tip.offsetHeight;
        if (x + tw > window.innerWidth - 8) { x = ev.clientX - tw - gap; }
        if (y + th > window.innerHeight - 8) { y = ev.clientY - th - gap; }
        tip.style.left = Math.max(8, x) + 'px';
        tip.style.top = Math.max(8, y) + 'px';
      });
      c.addEventListener('mouseleave', function () {
        c._render(-1);
        var tip = document.getElementById('mon-tip');
        if (tip) { tip.hidden = true; }
      });
    }

    var label = root.querySelector('[data-max="' + id + '"]');
    if (label) { label.textContent = peak == null ? '暂无采样' : '峰值 ' + peak + (unit || ''); }
  }

  function wireBell() {
    var bell = document.getElementById('ad-bell');
    if (!bell) { return; }
    var poll = function () {
      fetch('/admin/monitor/monitor/alerts', {
        headers: { 'X-Fragment': '1' }, credentials: 'same-origin'
      }).then(function (r) { return r.ok ? r.json() : null; })
        .then(function (j) {
          if (!j) { return; }
          var list = j.alerts || [];
          var n = list.length;
          var count = bell.querySelector('.ad-bell-n');
          if (count) {
            count.textContent = String(n);
            count.hidden = n === 0;
          }
          if (n > 0) {
            var msgs = [];
            list.forEach(function (a) { msgs.push(a.msg || ''); });
            bell.title = msgs.join('\n');
          } else {
            bell.title = '运行告警';
          }
          bell.classList.toggle('alerting', n > 0);
        }).catch(function () { /* offline or logged out: try again later */ });
    };
    poll();
    setInterval(poll, 30000);
  }

  if (bus) {
    if (bus.onInit) { bus.onInit(wireBell); }
    if (bus.onMount) {
      bus.onMount(function (_root, ctx) {
        if (!ctx || ctx.kind === 'initial' || (ctx.kind === 'panel' && ctx.active !== false)) {
          startStream();
        }
      });
    }
    if (bus.onActivate) {
      bus.onActivate(function () {
        if (!stream) { startStream(); }
      });
    }
    if (bus.onDeactivate) {
      bus.onDeactivate(function () { stopStream(); });
    }
    if (bus.onUnmount) {
      bus.onUnmount(function (root) {
        if (!root || root.id === 'ad-panel' || (root.classList && root.classList.contains('ad-pane'))) {
          stopStream();
        }
      });
    }
  }
})();
