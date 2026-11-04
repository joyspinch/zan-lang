/* app.js：htmx/Alpine 未覆盖的粘合层，页面渲染不依赖。 */

(function () {
  "use strict";

  // 每次 htmx 请求携带 CSRF 令牌。
  document.addEventListener("htmx:configRequest", function (e) {
    var meta = document.querySelector('meta[name="csrf-token"]');
    if (meta) { e.detail.headers["X-CSRF-Token"] = meta.content; }
  });

  // 服务端错误显示 toast，避免按钮看起来卡死。
  document.addEventListener("htmx:responseError", function (e) {
    flash(e.detail.xhr.status + " " + (e.detail.xhr.statusText || "request failed"), "bad");
  });
  document.addEventListener("htmx:sendError", function () {
    flash("network error", "bad");
  });

  // HX-Flash 头驱动的 toast。
  document.addEventListener("htmx:afterRequest", function (e) {
    var msg = e.detail.xhr.getResponseHeader("HX-Flash");
    if (msg) { flash(msg, e.detail.xhr.getResponseHeader("HX-Flash-Kind") || "ok"); }
  });

  function flash(text, kind) {
    var host = document.getElementById("flash");
    if (!host) { return; }
    var el = document.createElement("div");
    el.className = "flash " + (kind === "bad" ? "bad" : "ok");
    el.textContent = text;
    host.appendChild(el);
    setTimeout(function () { el.remove(); }, 4000);
  }

  // 在线人数：每页一个 EventSource，流断开自动重连。
  (function live() {
    var host = document.querySelector("[data-live]");
    if (!host || typeof EventSource === "undefined") { return; }
    var visitors = host.querySelector("[data-live-visitors]");
    var pages = host.querySelector("[data-live-pages]");
    var ttfb = host.querySelector("[data-live-ttfb]");
    if (ttfb) {
      var nav = performance.getEntriesByType("navigation")[0];
      if (nav) { ttfb.textContent = Math.round(nav.responseStart - nav.requestStart) + " ms"; }
    }
    var stream = new EventSource("/live/stream");
    stream.addEventListener("presence", function (ev) {
      var d = {};
      try { d = JSON.parse(ev.data); } catch (err) { return; }
      if (visitors) { visitors.textContent = d.visitors; }
      if (pages) { pages.textContent = d.pages; }
    });
    window.addEventListener("pagehide", function () { stream.close(); });
  })();

  window.appFlash = flash;
})();
