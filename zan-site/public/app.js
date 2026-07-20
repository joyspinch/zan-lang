(function () {
  var KEY = "zan-lang-pref";
  var body = document.body;

  function apply(lang) {
    body.classList.remove("lang-zh", "lang-en");
    body.classList.add(lang === "en" ? "lang-en" : "lang-zh");
    document.documentElement.lang = lang === "en" ? "en" : "zh-CN";
    var btns = document.querySelectorAll(".lang-btn");
    for (var i = 0; i < btns.length; i++) {
      btns[i].textContent = lang === "en" ? "中文" : "EN";
    }
    try { localStorage.setItem(KEY, lang); } catch (e) {}
  }

  var saved = "zh";
  try { saved = localStorage.getItem(KEY) || "zh"; } catch (e) {}
  apply(saved);

  document.addEventListener("click", function (e) {
    var t = e.target;
    if (t && t.classList && t.classList.contains("lang-btn")) {
      apply(body.classList.contains("lang-en") ? "zh" : "en");
    }
  });

  // Wiki section switcher
  var sideLinks = document.querySelectorAll(".wiki-side a[data-sec]");
  if (sideLinks.length) {
    function showSec(id) {
      var secs = document.querySelectorAll(".wiki-sec");
      var found = false;
      for (var i = 0; i < secs.length; i++) {
        var on = secs[i].id === id;
        secs[i].classList.toggle("active", on);
        if (on) found = true;
      }
      for (var j = 0; j < sideLinks.length; j++) {
        sideLinks[j].classList.toggle("active", sideLinks[j].getAttribute("data-sec") === id);
      }
      if (found) window.scrollTo({ top: 0, behavior: "smooth" });
    }
    for (var k = 0; k < sideLinks.length; k++) {
      sideLinks[k].addEventListener("click", function (e) {
        e.preventDefault();
        var id = this.getAttribute("data-sec");
        showSec(id);
        history.replaceState(null, "", "#" + id);
      });
    }
    var initial = location.hash.replace("#", "");
    if (initial && document.getElementById(initial)) { showSec(initial); }
  }

  // Nav shadow on scroll
  var nav = document.querySelector(".nav");
  if (nav) {
    var onScroll = function () { nav.classList.toggle("scrolled", window.scrollY > 8); };
    window.addEventListener("scroll", onScroll, { passive: true });
    onScroll();
  }

  // Card spotlight (mouse-follow)
  document.querySelectorAll(".card").forEach(function (card) {
    card.addEventListener("pointermove", function (e) {
      var r = card.getBoundingClientRect();
      card.style.setProperty("--mx", (e.clientX - r.left) + "px");
      card.style.setProperty("--my", (e.clientY - r.top) + "px");
    });
  });

  // Scroll reveal
  if ("IntersectionObserver" in window) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (en.isIntersecting) { en.target.classList.add("in"); io.unobserve(en.target); }
      });
    }, { threshold: 0.12 });
    document.querySelectorAll(".reveal").forEach(function (el) { io.observe(el); });
  } else {
    document.querySelectorAll(".reveal").forEach(function (el) { el.classList.add("in"); });
  }
})();
