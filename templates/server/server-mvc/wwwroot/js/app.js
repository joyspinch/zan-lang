/* app.js -- the little glue htmx and Alpine do not cover.
   Loaded after both; nothing here is required for a page to render. */

(function () {
  "use strict";

  // Send the CSRF token with every htmx request that changes state, so a form
  // fragment never has to remember to include it.
  document.addEventListener("htmx:configRequest", function (e) {
    var meta = document.querySelector('meta[name="csrf-token"]');
    if (meta) { e.detail.headers["X-CSRF-Token"] = meta.content; }
  });

  // A server error would otherwise swap nothing and look like a dead button.
  document.addEventListener("htmx:responseError", function (e) {
    flash(e.detail.xhr.status + " " + (e.detail.xhr.statusText || "request failed"), "bad");
  });
  document.addEventListener("htmx:sendError", function () {
    flash("network error", "bad");
  });

  // Server-driven toast: respond with the HX-Flash header from any handler.
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

  // Live visitor count. One EventSource per page, opened once: the server
  // pushes when the number changes shape, and the browser reconnects by itself
  // when the stream is closed. No stream, no line -- the footer just stays put.
  (function live() {
    var host = document.querySelector("[data-live]");
    if (!host || typeof EventSource === "undefined") { return; }
    var visitors = host.querySelector("[data-live-visitors]");
    var pages = host.querySelector("[data-live-pages]");
    // How long the server took on this page, from the browser's own timing.
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
