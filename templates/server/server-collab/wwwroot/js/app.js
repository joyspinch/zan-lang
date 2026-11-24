/* app.js：htmx/Alpine 未覆盖的粘合层，以及商业科技官网的轻量交互（零内联脚本）。 */

(function () {
  "use strict";

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

  // 工作台交互预览 Tab 切换（[data-preview-tab] -> [data-preview-panel]）。
  (function previewTabs() {
    var tabs = document.querySelectorAll("[data-preview-tab]");
    var panels = document.querySelectorAll("[data-preview-panel]");
    if (!tabs.length || !panels.length) { return; }
    for (var i = 0; i < tabs.length; i++) {
      tabs[i].addEventListener("click", function () {
        var target = this.getAttribute("data-preview-tab");
        for (var j = 0; j < tabs.length; j++) {
          var on = tabs[j] === this;
          tabs[j].classList.toggle("active", on);
          tabs[j].setAttribute("aria-selected", on ? "true" : "false");
        }
        for (var k = 0; k < panels.length; k++) {
          var match = panels[k].getAttribute("data-preview-panel") === target;
          panels[k].classList.toggle("active", match);
          if (match) { panels[k].removeAttribute("hidden"); }
          else { panels[k].setAttribute("hidden", ""); }
        }
      });
    }
  })();

  // 移动端导航折叠按钮。
  (function mobileNav() {
    var btn = document.querySelector("[data-nav-toggle]");
    var nav = document.getElementById("site-nav");
    if (!btn || !nav) { return; }
    btn.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });
    var links = nav.querySelectorAll("a");
    for (var i = 0; i < links.length; i++) {
      links[i].addEventListener("click", function () {
        nav.classList.remove("open");
        btn.setAttribute("aria-expanded", "false");
      });
    }
  })();

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
