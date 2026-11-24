/* auth.js：企业认证页轻量交互动效（零内联样式，严格遵循 CSP）。 */

(function () {
  "use strict";

  // 1. 密码显隐切换。
  document.addEventListener("click", function (ev) {
    var btn = ev.target.closest("[data-pass-toggle]");
    if (!btn) { return; }
    var wrap = btn.closest(".input-wrap");
    if (!wrap) { return; }
    var input = wrap.querySelector("input");
    if (!input) { return; }
    var shown = input.type === "text";
    input.type = shown ? "password" : "text";
    btn.classList.toggle("is-shown", !shown);
    btn.setAttribute("aria-pressed", shown ? "false" : "true");
    input.focus();
  });

  // 2. 大写锁定提示（仅在密码框聚焦且 CapsLock 开启时显示）。
  (function capsLockHint() {
    var inputs = document.querySelectorAll("[data-pass-input]");
    if (!inputs.length) { return; }
    for (var i = 0; i < inputs.length; i++) {
      (function (input) {
        var row = input.closest(".form-row");
        var hint = row ? row.querySelector("[data-caps-hint]") : null;
        if (!hint) { return; }
        function sync(ev) {
          if (typeof ev.getModifierState !== "function") { return; }
          if (ev.getModifierState("CapsLock")) { hint.removeAttribute("hidden"); }
          else { hint.setAttribute("hidden", ""); }
        }
        input.addEventListener("keydown", sync);
        input.addEventListener("keyup", sync);
        input.addEventListener("blur", function () { hint.setAttribute("hidden", ""); });
      })(inputs[i]);
    }
  })();

  // 3. 新密码强度指示器（注册 / 找回密码）。
  (function passwordStrength() {
    var input = document.querySelector("[data-pass-strength-target]");
    if (!input) { return; }
    var row = input.closest(".form-row");
    var meter = row ? row.querySelector("[data-pass-strength]") : null;
    var text = meter ? meter.querySelector("[data-strength-text]") : null;
    if (!meter || !text) { return; }

    var labels = ["", "密码强度：弱（建议 8 位以上混合字符）", "密码强度：中", "密码强度：强", "密码强度：极强"];
    input.addEventListener("input", function () {
      var v = input.value || "";
      if (!v.length) {
        meter.setAttribute("hidden", "");
        meter.className = "auth-strength";
        return;
      }
      meter.removeAttribute("hidden");
      var score = 0;
      if (v.length >= 8) { score += 1; }
      if (/[a-z]/.test(v) && /[A-Z]/.test(v)) { score += 1; }
      if (/[0-9]/.test(v)) { score += 1; }
      if (/[^A-Za-z0-9]/.test(v) || v.length >= 12) { score += 1; }
      if (score < 1) { score = 1; }
      if (score > 4) { score = 4; }
      meter.className = "auth-strength lvl-" + score;
      text.textContent = labels[score];
    });
  })();

  // 4. 表单提交态防重提交 + 加载动画。
  (function submitState() {
    var forms = document.querySelectorAll("[data-auth-form]");
    for (var i = 0; i < forms.length; i++) {
      forms[i].addEventListener("submit", function () {
        var btn = this.querySelector("[data-auth-submit]");
        if (!btn || btn.classList.contains("is-loading")) { return; }
        btn.classList.add("is-loading");
        btn.disabled = true;
        var label = btn.querySelector(".btn-label");
        var loadingText = btn.getAttribute("data-loading-text");
        if (label && loadingText) { label.textContent = loadingText; }
      });
    }
  })();

  // 5. 左侧商业安全特性展示卡片轮换（支持点击切换，悬停/减少动效自动暂停）。
  (function showcaseSwitcher() {
    var root = document.querySelector("[data-auth-showcase]");
    if (!root) { return; }
    var tabs = root.querySelectorAll("[data-auth-slide]");
    var panels = root.querySelectorAll("[data-auth-panel]");
    if (!tabs.length || !panels.length) { return; }

    var current = 0;
    var paused = false;

    function activate(idx) {
      current = ((idx % tabs.length) + tabs.length) % tabs.length;
      for (var i = 0; i < tabs.length; i++) {
        var on = i === current;
        tabs[i].classList.toggle("active", on);
        tabs[i].setAttribute("aria-selected", on ? "true" : "false");
      }
      for (var j = 0; j < panels.length; j++) {
        var match = j === current;
        panels[j].classList.toggle("active", match);
        if (match) { panels[j].removeAttribute("hidden"); }
        else { panels[j].setAttribute("hidden", ""); }
      }
    }

    for (var i = 0; i < tabs.length; i++) {
      (function (index) {
        tabs[index].addEventListener("click", function () { activate(index); });
      })(i);
    }

    root.addEventListener("mouseenter", function () { paused = true; });
    root.addEventListener("mouseleave", function () { paused = false; });
    root.addEventListener("focusin", function () { paused = true; });
    root.addEventListener("focusout", function () { paused = false; });

    var reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (!reduceMotion) {
      setInterval(function () {
        if (!paused) { activate(current + 1); }
      }, 5500);
    }
  })();
})();
