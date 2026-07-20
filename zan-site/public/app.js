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

  // ===== Download modal (surface the access code prominently) =====
  var modal = document.getElementById("dlModal");
  if (modal) {
    var open = function (e) { if (e) e.preventDefault(); modal.classList.add("open"); };
    var close = function () { modal.classList.remove("open"); };
    document.querySelectorAll("[data-download]").forEach(function (b) {
      b.addEventListener("click", open);
    });
    modal.addEventListener("click", function (e) { if (e.target === modal) close(); });
    var xBtn = modal.querySelector(".modal-x");
    if (xBtn) xBtn.addEventListener("click", close);
    document.addEventListener("keydown", function (e) { if (e.key === "Escape") close(); });
    var copyBtn = modal.querySelector(".copy-btn");
    if (copyBtn) {
      copyBtn.addEventListener("click", function () {
        var code = copyBtn.getAttribute("data-copy") || "zanlang";
        var done = function () {
          copyBtn.classList.add("done");
          copyBtn.setAttribute("data-copied", "1");
          setTimeout(function () { copyBtn.classList.remove("done"); }, 1600);
        };
        if (navigator.clipboard && navigator.clipboard.writeText) {
          navigator.clipboard.writeText(code).then(done, done);
        } else {
          var ta = document.createElement("textarea");
          ta.value = code; document.body.appendChild(ta); ta.select();
          try { document.execCommand("copy"); } catch (e) {}
          document.body.removeChild(ta); done();
        }
      });
    }
  }

  // ===== Animated counters =====
  var counters = document.querySelectorAll("[data-count]");
  if (counters.length && "IntersectionObserver" in window) {
    var cio = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (!en.isIntersecting) return;
        var el = en.target, target = parseInt(el.getAttribute("data-count"), 10) || 0, t0 = null;
        var step = function (ts) {
          if (!t0) t0 = ts;
          var p = Math.min((ts - t0) / 900, 1);
          el.textContent = Math.floor(p * p * (3 - 2 * p) * target).toString();
          if (p < 1) requestAnimationFrame(step); else el.textContent = target.toString();
        };
        requestAnimationFrame(step);
        cio.unobserve(el);
      });
    }, { threshold: 0.5 });
    counters.forEach(function (el) { cio.observe(el); });
  }

  // ===== visual flourishes (aurora bg, cursor glow, click spark, tilt) =====
  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var fine = window.matchMedia && window.matchMedia("(pointer: fine)").matches;

  // living aurora background (injected so all pages get it)
  if (!document.querySelector(".aurora-bg")) {
    var ab = document.createElement("div");
    ab.className = "aurora-bg";
    ab.setAttribute("aria-hidden", "true");
    body.insertBefore(ab, body.firstChild);
  }

  if (!reduce && fine) {
    // cursor spotlight glow
    var cg = document.createElement("div");
    cg.className = "cursor-glow";
    cg.setAttribute("aria-hidden", "true");
    body.appendChild(cg);
    var gx = 0, gy = 0, raf = 0;
    var moveGlow = function () { cg.style.transform = "translate3d(" + gx + "px," + gy + "px,0) translate(-50%,-50%)"; raf = 0; };
    window.addEventListener("pointermove", function (e) {
      if (e.pointerType && e.pointerType !== "mouse") return;
      gx = e.clientX; gy = e.clientY;
      body.classList.add("glow-on");
      if (!raf) raf = requestAnimationFrame(moveGlow);
    }, { passive: true });
    window.addEventListener("pointerdown", function () { body.classList.add("glow-on"); });
    document.addEventListener("mouseleave", function () { body.classList.remove("glow-on"); });

    // click spark burst
    var colors = ["#5b6cff", "#9b5cff", "#ff5c9d", "#0fb488"];
    window.addEventListener("pointerdown", function (e) {
      if (e.pointerType && e.pointerType !== "mouse") return;
      var n = 10;
      for (var i = 0; i < n; i++) {
        var s = document.createElement("span");
        s.className = "spark";
        var ang = (Math.PI * 2 * i) / n + Math.random() * 0.5;
        var dist = 26 + Math.random() * 34;
        s.style.left = e.clientX + "px";
        s.style.top = e.clientY + "px";
        s.style.background = colors[i % colors.length];
        s.style.setProperty("--dx", Math.cos(ang) * dist + "px");
        s.style.setProperty("--dy", Math.sin(ang) * dist + "px");
        body.appendChild(s);
        (function (el) { setTimeout(function () { el.remove(); }, 650); })(s);
      }
    }, { passive: true });

    // 3D tilt on cards
    document.querySelectorAll(".card").forEach(function (card) {
      card.classList.add("tilt");
      card.addEventListener("pointermove", function (e) {
        var r = card.getBoundingClientRect();
        var px = (e.clientX - r.left) / r.width - 0.5;
        var py = (e.clientY - r.top) / r.height - 0.5;
        card.style.transform = "perspective(760px) rotateX(" + (-py * 6).toFixed(2) + "deg) rotateY(" + (px * 6).toFixed(2) + "deg) translateY(-6px)";
      });
      card.addEventListener("pointerleave", function () { card.style.transform = ""; });
    });
  }

  // ===== full-screen hero particle constellation =====
  (function () {
    var cv = document.querySelector(".hero-canvas");
    if (!cv || !cv.getContext) return;
    var ctx = cv.getContext("2d");
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var W = 0, H = 0, pts = [], mouse = { x: -9999, y: -9999 };
    var COLORS = [[91, 108, 255], [155, 92, 255], [255, 92, 157], [15, 180, 136]];
    function resize() {
      W = cv.clientWidth; H = cv.clientHeight;
      cv.width = W * dpr; cv.height = H * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      var count = Math.max(20, Math.min(60, Math.round((W * H) / 26000)));
      pts = [];
      for (var i = 0; i < count; i++) {
        pts.push({
          x: Math.random() * W, y: Math.random() * H,
          vx: (Math.random() - 0.5) * 0.35, vy: (Math.random() - 0.5) * 0.35,
          r: Math.random() * 1.7 + 1.1, c: COLORS[i % COLORS.length]
        });
      }
    }
    resize();
    window.addEventListener("resize", resize);
    var host = cv.closest(".hero") || document;
    host.addEventListener("pointermove", function (e) {
      var r = cv.getBoundingClientRect(); mouse.x = e.clientX - r.left; mouse.y = e.clientY - r.top;
    }, { passive: true });
    host.addEventListener("pointerleave", function () { mouse.x = -9999; mouse.y = -9999; });

    var LINK = 132, LINK2 = LINK * LINK;
    function draw(animate) {
      ctx.clearRect(0, 0, W, H);
      var i, j, p;
      for (i = 0; i < pts.length; i++) {
        p = pts[i];
        if (animate) { p.x += p.vx; p.y += p.vy; }
        if (p.x < 0) { p.x = 0; p.vx *= -1; } else if (p.x > W) { p.x = W; p.vx *= -1; }
        if (p.y < 0) { p.y = 0; p.vy *= -1; } else if (p.y > H) { p.y = H; p.vy *= -1; }
        var mdx = p.x - mouse.x, mdy = p.y - mouse.y, md2 = mdx * mdx + mdy * mdy;
        if (md2 < 15000) { var md = Math.sqrt(md2) || 1, f = (122 - md) / 122 * 0.7; p.x += mdx / md * f; p.y += mdy / md * f; }
      }
      for (i = 0; i < pts.length; i++) {
        for (j = i + 1; j < pts.length; j++) {
          var a = pts[i], b = pts[j], dx = a.x - b.x, dy = a.y - b.y, d2 = dx * dx + dy * dy;
          if (d2 < LINK2) {
            var al = (1 - Math.sqrt(d2) / LINK) * 0.5;
            ctx.strokeStyle = "rgba(" + a.c[0] + "," + a.c[1] + "," + a.c[2] + "," + al.toFixed(3) + ")";
            ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke();
          }
        }
      }
      for (i = 0; i < pts.length; i++) {
        p = pts[i]; ctx.beginPath(); ctx.arc(p.x, p.y, p.r, 0, 6.2832);
        ctx.fillStyle = "rgba(" + p.c[0] + "," + p.c[1] + "," + p.c[2] + ",.9)"; ctx.fill();
      }
    }
    if (reduce) { draw(false); return; }
    (function loop() { if (!document.hidden) draw(true); requestAnimationFrame(loop); })();
  })();

  // ===== live IDE demo: type code → build → run native window =====
  (function () {
    var stage = document.getElementById("demo");
    if (!stage) return;
    var codeEl = document.getElementById("typeCode");
    var lines = [].slice.call(codeEl.querySelectorAll(".cline"));
    var status = stage.querySelector(".ide-status");
    var build = document.getElementById("buildState");
    var appwin = document.getElementById("appwin");
    var appLabel = document.getElementById("appLabel");
    var runBtn = stage.querySelector(".run-btn");
    var OK = '<span class="zh">就绪</span><span class="en">Ready</span>';
    var BUILDING = '<span class="zh">构建中…</span><span class="en">Building…</span>';
    var RUNNING = '<span class="zh">运行中 ▶</span><span class="en">Running ▶</span>';
    var timers = [], counter = 0, n = 0;
    function label(v) { appLabel.innerHTML = '<span class="zh">点击了 ' + v + ' 次</span><span class="en">clicked ' + v + '</span>'; }
    function clearT() { timers.forEach(clearTimeout); timers = []; clearInterval(counter); }
    function later(fn, ms) { var t = setTimeout(fn, ms); timers.push(t); return t; }

    function reset() {
      clearT();
      lines.forEach(function (l) { l.classList.remove("shown", "active"); });
      status.classList.remove("building", "running");
      build.innerHTML = OK;
      stage.classList.remove("running");
      appwin.classList.remove("run");
      n = 0; label(0);
    }
    function type() {
      reset();
      var i = 0;
      (function step() {
        if (i > 0) lines[i - 1].classList.remove("active");
        if (i < lines.length) {
          lines[i].classList.add("shown", "active");
          i++;
          later(step, 120 + Math.random() * 130);
        } else {
          lines[lines.length - 1].classList.remove("active");
          later(doBuild, 380);
        }
      })();
    }
    function doBuild() {
      status.classList.add("building"); build.innerHTML = BUILDING;
      later(doRun, 1050);
    }
    function doRun() {
      status.classList.remove("building"); status.classList.add("running");
      build.innerHTML = RUNNING; stage.classList.add("running"); appwin.classList.add("run");
      counter = setInterval(function () { n++; label(n); }, 2600);
      later(function () { type(); }, 8200);
    }

    if (reduce) {
      lines.forEach(function (l) { l.classList.add("shown"); });
      status.classList.add("running"); build.innerHTML = RUNNING;
      stage.classList.add("running"); appwin.classList.add("run"); label(3);
      return;
    }
    if (runBtn) runBtn.addEventListener("click", type);
    type();
  })();

  // ===== Docs sidebar: scrollspy + filter =====
  var docSide = document.querySelector(".doc-side");
  if (docSide) {
    var links = [].slice.call(docSide.querySelectorAll("a[href^='#']"));
    var secs = links.map(function (a) { return document.getElementById(a.getAttribute("href").slice(1)); });
    var spy = function () {
      var y = window.scrollY + 160, idx = 0;
      for (var i = 0; i < secs.length; i++) { if (secs[i] && secs[i].offsetTop <= y) idx = i; }
      links.forEach(function (a, i) { a.classList.toggle("active", i === idx); });
    };
    window.addEventListener("scroll", spy, { passive: true });
    spy();
    // filter box
    var box = document.getElementById("docSearch");
    if (box) {
      box.addEventListener("input", function () {
        var q = box.value.trim().toLowerCase();
        var groups = docSide.querySelectorAll("[data-grp]");
        links.forEach(function (a) {
          var hit = !q || a.textContent.toLowerCase().indexOf(q) >= 0;
          a.style.display = hit ? "" : "none";
        });
        groups.forEach(function (g) {
          var any = [].slice.call(g.querySelectorAll("a")).some(function (a) { return a.style.display !== "none"; });
          g.style.display = any ? "" : "none";
        });
      });
    }
  }
})();
