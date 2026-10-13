(function () {
  "use strict";

  // i18n
  var body = document.body;
  var langBtn = document.getElementById("langBtn");
  var lang = (typeof localStorage !== "undefined" && localStorage.getItem("zl")) || "zh";

  function applyLang(l) {
    body.classList.toggle("zh", l === "zh");
    body.classList.toggle("en", l === "en");
    if (langBtn) langBtn.textContent = l === "zh" ? "EN" : "中";
    try { localStorage.setItem("zl", l); } catch (_) {}
  }
  applyLang(lang);
  if (langBtn) langBtn.addEventListener("click", function () {
    applyLang(body.classList.contains("zh") ? "en" : "zh");
  });

  // Nav shadow on scroll
  var nav = document.querySelector(".site-nav");
  if (nav) window.addEventListener("scroll", function () {
    nav.classList.toggle("raised", window.scrollY > 16);
  }, { passive: true });

  // Download modal
  var modal = document.getElementById("dlModal");
  function openModal(e) {
    if (e) e.preventDefault();
    if (modal) { modal.classList.add("open"); body.style.overflow = "hidden"; }
  }
  function closeModal() {
    if (modal) { modal.classList.remove("open"); body.style.overflow = ""; }
  }
  document.querySelectorAll("[data-dl]").forEach(function (el) {
    el.addEventListener("click", openModal);
  });
  var closeBtn = document.getElementById("dlClose");
  if (closeBtn) closeBtn.addEventListener("click", closeModal);
  if (modal) modal.addEventListener("click", function (e) {
    if (e.target === modal) closeModal();
  });
  window.addEventListener("keydown", function (e) {
    if (e.key === "Escape") closeModal();
  });

  // Generic [data-copy] buttons
  document.querySelectorAll("[data-copy]").forEach(function (btn) {
    btn.addEventListener("click", function () {
      var val = btn.dataset.copy;
      var orig = btn.textContent;
      navigator.clipboard && navigator.clipboard.writeText(val).then(function () {
        btn.textContent = "✓ Copied";
        btn.classList.add("done");
        setTimeout(function () { btn.textContent = orig; btn.classList.remove("done"); }, 1800);
      });
    });
  });

  // Hero demo counter
  var demoBtn  = document.getElementById("demoBtn");
  var demoOut  = document.getElementById("demoOut");
  var demoOut2 = document.getElementById("demoOut2");
  var n = 0;
  if (demoBtn && demoOut) demoBtn.addEventListener("click", function () {
    n++;
    demoOut.textContent = n;
    if (demoOut2) demoOut2.textContent = n;
    demoBtn.style.transform = "scale(.93)";
    setTimeout(function () { demoBtn.style.transform = ""; }, 100);
  });

  // Bento card mouse-tracking glow
  document.querySelectorAll(".bcard").forEach(function (card) {
    card.addEventListener("mousemove", function (e) {
      var r = card.getBoundingClientRect();
      var x = ((e.clientX - r.left) / r.width  * 100).toFixed(1) + "%";
      var y = ((e.clientY - r.top)  / r.height * 100).toFixed(1) + "%";
      card.style.setProperty("--mx", x);
      card.style.setProperty("--my", y);
    });
  });

  // Code example tabs
  var tabBtns = document.querySelectorAll("[data-tab]");
  var panels  = document.querySelectorAll("[data-panel]");
  tabBtns.forEach(function (btn) {
    btn.addEventListener("click", function () {
      var key = btn.dataset.tab;
      tabBtns.forEach(function (b) { b.classList.toggle("active", b.dataset.tab === key); });
      panels.forEach(function (p) { p.classList.toggle("active", p.dataset.panel === key); });
    });
  });
  if (tabBtns.length) tabBtns[0].click();

})();
