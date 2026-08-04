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

  window.appFlash = flash;
})();
