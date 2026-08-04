/* charts.js -- the dashboard's time-series charts, drawn as SVG with no charting
   library. A dependency for four line charts would be ~1MB of vendor code and a
   CDN a private deployment cannot reach, while an SVG path is a string: one
   polling loop, one <path> per series, and the browser does the rest.

   Markup contract (see Admin/Dashboard.Index.html):

     <div id="charts" data-src="/admin/metrics/series" data-window="120">
       <figure class="chart" data-metric="req" data-title="QPS" data-unit="/s"></figure>
       <figure class="chart" data-metric="p95_ms,avg_ms" data-title="延迟"
               data-unit="ms" data-legend="p95,平均"></figure>
     </div>

   data-metric may name several point fields; the first is the filled series and
   the rest are drawn as lines over it. data-scale divides the raw value (RSS in
   bytes -> MB). A field that is -1 for the whole window (a probe the current OS
   cannot answer) renders as "不支持" rather than a flat line at zero. */

(function () {
  "use strict";

  var host = document.getElementById("charts");
  if (!host) { return; }

  // Drawn in CSS pixels (the viewBox matches the measured box, nothing is
  // stretched): a stretched viewBox would squash the axis labels and thin the
  // strokes by whatever the card's width happened to be.
  var H = 132, PAD_L = 40, PAD_R = 8, PAD_T = 12, PAD_B = 16;
  var charts = [].slice.call(host.querySelectorAll(".chart"));
  var timer = null;
  var last = null;   // last payload, so a resize can redraw without a request

  charts.forEach(function (fig) {
    fig.innerHTML =
      '<figcaption><span class="ct">' + (fig.dataset.title || "") + "</span>" +
      '<span class="cv"></span></figcaption>' +
      '<div class="cbody"></div>';
  });

  function fmt(v, unit) {
    if (v === null) { return "—"; }
    var s = (Math.abs(v) >= 100 || v === Math.round(v))
      ? String(Math.round(v)) : v.toFixed(1);
    return s + (unit ? " " + unit : "");
  }

  // A y-axis that lands on 1/2/5 x 10^n, so the gridline labels stay readable
  // instead of "0 / 3.7333 / 7.4666".
  function niceMax(v) {
    if (v <= 0) { return 1; }
    var mag = Math.pow(10, Math.floor(Math.log(v) / Math.LN10));
    var n = v / mag;
    if (n <= 1) { return mag; }
    if (n <= 2) { return 2 * mag; }
    if (n <= 5) { return 5 * mag; }
    return 10 * mag;
  }

  function path(vals, max, close, W) {
    var n = vals.length;
    var w = W - PAD_L - PAD_R, h = H - PAD_T - PAD_B;
    var d = "";
    for (var i = 0; i < n; i++) {
      var x = PAD_L + (n === 1 ? w : (i * w) / (n - 1));
      var y = PAD_T + h - (Math.max(0, vals[i]) / max) * h;
      d += (i === 0 ? "M" : "L") + x.toFixed(1) + " " + y.toFixed(1);
    }
    if (close) {
      d += "L" + (PAD_L + w).toFixed(1) + " " + (PAD_T + h) +
           "L" + PAD_L + " " + (PAD_T + h) + "Z";
    }
    return d;
  }

  function render(fig, points) {
    var fields = (fig.dataset.metric || "").split(",");
    var scale = parseFloat(fig.dataset.scale || "1");
    var unit = fig.dataset.unit || "";
    var legend = (fig.dataset.legend || "").split(",");
    var body = fig.querySelector(".cbody");

    var series = fields.map(function (f) {
      return points.map(function (p) {
        var v = p[f.trim()];
        return typeof v === "number" ? v / scale : 0;
      });
    });

    // -1 across the board means "this platform does not report it": say so,
    // rather than drawing a confident zero.
    var supported = points.some(function (p) {
      return points.length === 0 || p[fields[0].trim()] >= 0;
    });
    if (!supported) {
      fig.querySelector(".cv").textContent = "不支持";
      body.innerHTML = '<p class="cempty">当前平台无法采集该指标</p>';
      return;
    }

    var peak = 0;
    series.forEach(function (s) {
      s.forEach(function (v) { if (v > peak) { peak = v; } });
    });
    var max = niceMax(peak);
    var tail = series[0][series[0].length - 1];
    fig.querySelector(".cv").textContent = fmt(tail, unit);

    var W = Math.max(220, Math.round(body.clientWidth || 320));
    var h = H - PAD_T - PAD_B;
    var svg = '<svg viewBox="0 0 ' + W + " " + H + '"' +
      ' role="img" aria-label="' + (fig.dataset.title || "") + '">';
    for (var g = 0; g <= 2; g++) {
      var y = PAD_T + (h * g) / 2;
      svg += '<line class="grid" x1="' + PAD_L + '" y1="' + y + '" x2="' +
        (W - PAD_R) + '" y2="' + y + '"/>' +
        '<text class="tick" x="' + (PAD_L - 6) + '" y="' + (y + 3.5) +
        '" text-anchor="end">' + fmt(max - (max * g) / 2, "") + "</text>";
    }
    svg += '<path class="area" d="' + path(series[0], max, true, W) + '"/>';
    series.forEach(function (s, i) {
      svg += '<path class="line s' + i + '" d="' + path(s, max, false, W) + '"/>';
    });
    svg += "</svg>";

    var foot = '<div class="cfoot"><span>' + points.length + " 秒</span>";
    if (fields.length > 1) {
      foot += '<span class="clegend">';
      fields.forEach(function (f, i) {
        foot += '<i class="s' + i + '"></i>' + (legend[i] || f).trim();
      });
      foot += "</span>";
    }
    foot += "</div>";
    body.innerHTML = svg + foot;
  }

  function tick() {
    var url = host.dataset.src + "?sec=" + (host.dataset.window || "120");
    fetch(url, { credentials: "same-origin", headers: { Accept: "application/json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (d) {
        if (!d || !d.points) { return; }
        last = d;
        host.classList.remove("stale");
        charts.forEach(function (fig) { render(fig, d.points); });
      })
      .catch(function () { host.classList.add("stale"); });
  }

  // Polling stops while the tab is hidden: an unattended dashboard should not
  // keep waking the server up once a second.
  function schedule() {
    if (timer) { clearInterval(timer); timer = null; }
    if (document.hidden) { return; }
    tick();
    timer = setInterval(tick, 2000);
  }
  document.addEventListener("visibilitychange", schedule);

  var resizing = null;
  window.addEventListener("resize", function () {
    if (!last) { return; }
    clearTimeout(resizing);
    resizing = setTimeout(function () {
      charts.forEach(function (fig) { render(fig, last.points); });
    }, 120);
  });

  schedule();
})();
