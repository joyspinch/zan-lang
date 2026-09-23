// ==========================================================================
// Zan Language Official Site Interactive Engine · Enterprise Edition
// ==========================================================================

(function () {
  "use strict";

  // 1. Language Toggle (i18n)
  var body = document.body;
  var langBtn = document.getElementById("langBtn");
  var savedLang = (typeof localStorage !== "undefined" && localStorage.getItem("zan_lang")) || "zh";

  function setLanguage(lang) {
    if (lang === "en") {
      body.classList.remove("lang-zh");
      body.classList.add("lang-en");
      if (langBtn) langBtn.textContent = "中";
    } else {
      body.classList.remove("lang-en");
      body.classList.add("lang-zh");
      if (langBtn) langBtn.textContent = "EN";
    }
    if (typeof localStorage !== "undefined") {
      try {
        localStorage.setItem("zan_lang", lang);
      } catch (e) {}
    }
  }

  setLanguage(savedLang);

  if (langBtn) {
    langBtn.addEventListener("click", function () {
      var nextLang = body.classList.contains("lang-zh") ? "en" : "zh";
      setLanguage(nextLang);
    });
  }

  // 2. Navigation Scroll Shadow Effect
  var nav = document.getElementById("topNav");
  if (nav) {
    window.addEventListener("scroll", function () {
      if (window.scrollY > 24) {
        nav.classList.add("scrolled");
      } else {
        nav.classList.remove("scrolled");
      }
    }, { passive: true });
  }

  // 3. Enterprise Download Modal Controller
  var dlModal = document.getElementById("dlModal");
  var modalCloseBtn = document.getElementById("modalCloseBtn");
  var modalCopyBtn = document.getElementById("modalCopyBtn");
  var dlTriggers = document.querySelectorAll("[data-download]");

  function openDownloadModal(e) {
    if (e && e.preventDefault) e.preventDefault();
    if (!dlModal) return;
    dlModal.classList.add("open");
    document.body.style.overflow = "hidden";

    // Auto copy access code to clipboard seamlessly
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText("zanlang").then(function () {
        if (modalCopyBtn) {
          modalCopyBtn.innerHTML = '<span class="zh">已复制 ✓</span><span class="en">Copied ✓</span>';
          modalCopyBtn.classList.add("done");
          setTimeout(function () {
            modalCopyBtn.innerHTML = '<span class="zh">一键复制</span><span class="en">Copy</span>';
            modalCopyBtn.classList.remove("done");
          }, 2400);
        }
      }).catch(function () {});
    }
  }

  function closeDownloadModal() {
    if (!dlModal) return;
    dlModal.classList.remove("open");
    document.body.style.overflow = "";
  }

  dlTriggers.forEach(function (trigger) {
    trigger.addEventListener("click", openDownloadModal);
  });

  if (modalCloseBtn) {
    modalCloseBtn.addEventListener("click", closeDownloadModal);
  }

  if (dlModal) {
    dlModal.addEventListener("click", function (e) {
      if (e.target === dlModal) {
        closeDownloadModal();
      }
    });
  }

  window.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && dlModal && dlModal.classList.contains("open")) {
      closeDownloadModal();
    }
  });

  if (modalCopyBtn) {
    modalCopyBtn.addEventListener("click", function () {
      var code = this.getAttribute("data-copy") || "zanlang";
      var btn = this;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(code).then(function () {
          btn.innerHTML = '<span class="zh">已复制 ✓</span><span class="en">Copied ✓</span>';
          btn.classList.add("done");
          setTimeout(function () {
            btn.innerHTML = '<span class="zh">一键复制</span><span class="en">Copy</span>';
            btn.classList.remove("done");
          }, 2000);
        });
      }
    });
  }

  // 4. Hero Live Interactive Process Bar
  var demoActionBtn = document.getElementById("demoActionBtn");
  var demoCounterText = document.getElementById("demoCounterText");
  var clickCount = 0;

  if (demoActionBtn && demoCounterText) {
    demoActionBtn.addEventListener("click", function () {
      clickCount++;
      demoCounterText.innerHTML = '<span class="zh">已点击 ' + clickCount + ' 次</span><span class="en">' + clickCount + ' clicks</span>';

      // Haptic scale bounce
      demoActionBtn.style.transform = "scale(0.94)";
      setTimeout(function () {
        demoActionBtn.style.transform = "";
      }, 120);
    });
  }

  // 5. Quickstart Interactive Code Tabs & Copy
  var codeSnippets = {
    gui: '<span class="k">using</span> <span class="t">Gui</span>;\n<span class="k">using</span> <span class="t">Gui.Widget</span>;\n\n<span class="k">class</span> <span class="t">App</span> {\n    <span class="k">static</span> <span class="t">void</span> <span class="fn">Main</span>() {\n        <span class="k">var</span> form = <span class="t">Form</span>.<span class="fn">Create</span>(<span class="s">"Zan GUI"</span>, <span class="n">500</span>, <span class="n">360</span>);\n        <span class="k">var</span> label = <span class="k">new</span> <span class="t">Label</span>(<span class="s">"Hello Native World!"</span>);\n        form.<span class="fn">Add</span>(label);\n        form.<span class="fn">Run</span>(); <span class="c">// AOT Compiled Window</span>\n    }\n}',
    web: '<span class="k">using</span> <span class="t">Server</span>;\n<span class="k">using</span> <span class="t">Server.Mvc</span>;\n\n[<span class="t">Controller</span>(<span class="s">"/api/v1"</span>)]\n<span class="k">class</span> <span class="t">UserController</span> {\n    [<span class="t">HttpGet</span>(<span class="s">"/users/{id}"</span>)]\n    <span class="k">public</span> <span class="t">User</span> <span class="fn">GetUser</span>(<span class="k">int</span> id) {\n        <span class="k">return</span> <span class="k">new</span> <span class="t">User</span> { Id = id, Name = <span class="s">"Zan User"</span> };\n    }\n}',
    game: '<span class="k">using</span> <span class="t">Game</span>;\n\n<span class="k">class</span> <span class="t">PongGame</span> : <span class="t">GameEngine</span> {\n    <span class="k">override</span> <span class="t">void</span> <span class="fn">Update</span>(<span class="k">float</span> dt) {\n        ball.x += ball.vx * dt;\n        <span class="k">if</span> (ball.x &lt;= <span class="n">0</span> || ball.x &gt;= <span class="n">800</span>) ball.vx = -ball.vx;\n    }\n    <span class="k">override</span> <span class="t">void</span> <span class="fn">Render</span>(<span class="t">Graphics</span> g) {\n        g.<span class="fn">FillCircle</span>(ball.x, ball.y, <span class="n">8</span>, <span class="t">Color</span>.White);\n    }\n}',
    cli: '<span class="k">using</span> <span class="t">System</span>;\n<span class="k">using</span> <span class="t">System.IO</span>;\n\n<span class="k">class</span> <span class="t">Program</span> {\n    <span class="k">static</span> <span class="t">void</span> <span class="fn">Main</span>(<span class="k">string</span>[] args) {\n        <span class="k">var</span> files = <span class="t">Directory</span>.<span class="fn">GetFiles</span>(<span class="s">"."</span>, <span class="s">"*.zan"</span>);\n        <span class="t">Console</span>.<span class="fn">WriteLine</span>(<span class="s">$"Found {files.Length} source files."</span>);\n    }\n}'
  };

  var consoleTabBtns = document.querySelectorAll(".btn-console-tab");
  var consoleSnippetEl = document.getElementById("consoleSnippet");
  var consoleCopyBtn = document.getElementById("consoleCopyBtn");
  var currentTabKey = "gui";

  function setConsoleSnippet(key) {
    if (consoleSnippetEl && codeSnippets[key]) {
      currentTabKey = key;
      consoleSnippetEl.innerHTML = codeSnippets[key];
    }
    consoleTabBtns.forEach(function (btn) {
      btn.classList.toggle("active", btn.getAttribute("data-tab") === key);
    });
  }

  consoleTabBtns.forEach(function (btn) {
    btn.addEventListener("click", function () {
      var tab = this.getAttribute("data-tab");
      if (tab) setConsoleSnippet(tab);
    });
  });

  if (consoleCopyBtn) {
    consoleCopyBtn.addEventListener("click", function () {
      var rawSnippet = consoleSnippetEl ? consoleSnippetEl.textContent : "";
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(rawSnippet).then(function () {
          consoleCopyBtn.innerHTML = '<span class="zh">已复制 ✓</span><span class="en">Copied ✓</span>';
          consoleCopyBtn.classList.add("done");
          setTimeout(function () {
            consoleCopyBtn.innerHTML = '<span class="zh">复制代码</span><span class="en">Copy</span>';
            consoleCopyBtn.classList.remove("done");
          }, 2000);
        });
      }
    });
  }

  // Initialize first snippet
  setConsoleSnippet("gui");

  // 6. Interactive Cursor Spotlight & Bento Hover Tracker
  var spotlight = document.querySelector(".cursor-spotlight");
  var isTouch = "ontouchstart" in window || navigator.maxTouchPoints > 0;

  if (spotlight && !isTouch) {
    var sx = 0, sy = 0, rafSpot = 0;
    var updateSpotlight = function () {
      spotlight.style.transform = "translate3d(" + sx + "px, " + sy + "px, 0) translate(-50%, -50%)";
      rafSpot = 0;
    };

    window.addEventListener("pointermove", function (e) {
      if (e.pointerType && e.pointerType !== "mouse") return;
      sx = e.clientX;
      sy = e.clientY;
      body.classList.add("glow-active");
      if (!rafSpot) rafSpot = requestAnimationFrame(updateSpotlight);
    }, { passive: true });

    document.addEventListener("mouseleave", function () {
      body.classList.remove("glow-active");
    });

    // Bento Card Dynamic Mouse Gradient Tracking
    var bentoCards = document.querySelectorAll(".bento-feature-card");
    bentoCards.forEach(function (card) {
      card.addEventListener("pointermove", function (e) {
        var rect = card.getBoundingClientRect();
        card.style.setProperty("--mx", (e.clientX - rect.left) + "px");
        card.style.setProperty("--my", (e.clientY - rect.top) + "px");
      }, { passive: true });
    });

    // Micro Spark Burst on Click
    var sparkColors = ["#41a7f5", "#2b5cff", "#ffc838", "#38bdf8"];
    window.addEventListener("pointerdown", function (e) {
      if (e.pointerType && e.pointerType !== "mouse") return;
      var count = 8;
      for (var i = 0; i < count; i++) {
        var sp = document.createElement("span");
        sp.className = "spark";
        var angle = (Math.PI * 2 * i) / count + (Math.random() - 0.5) * 0.4;
        var distance = 24 + Math.random() * 30;
        sp.style.left = e.clientX + "px";
        sp.style.top = e.clientY + "px";
        sp.style.background = sparkColors[i % sparkColors.length];
        sp.style.setProperty("--dx", Math.cos(angle) * distance + "px");
        sp.style.setProperty("--dy", Math.sin(angle) * distance + "px");
        document.body.appendChild(sp);
        (function (elem) {
          setTimeout(function () { elem.remove(); }, 600);
        })(sp);
      }
    }, { passive: true });
  }

  // 7. Full-Screen Constellation Particle Canvas
  (function () {
    var cv = document.getElementById("heroCanvas");
    if (!cv || !cv.getContext) return;
    var ctx = cv.getContext("2d");
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var W = 0, H = 0, pts = [];
    var mousePos = { x: -9999, y: -9999 };
    var PALETTE = [[65, 167, 245], [43, 92, 255], [255, 200, 56]];

    function resizeCanvas() {
      W = cv.parentElement ? cv.parentElement.clientWidth : window.innerWidth;
      H = cv.parentElement ? cv.parentElement.clientHeight : window.innerHeight;
      cv.width = W * dpr;
      cv.height = H * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      var ptCount = Math.max(22, Math.min(50, Math.round((W * H) / 28000)));
      pts = [];
      for (var i = 0; i < ptCount; i++) {
        pts.push({
          x: Math.random() * W,
          y: Math.random() * H,
          vx: (Math.random() - 0.5) * 0.4,
          vy: (Math.random() - 0.5) * 0.4,
          r: Math.random() * 1.5 + 1.0,
          color: PALETTE[i % PALETTE.length]
        });
      }
    }

    resizeCanvas();
    window.addEventListener("resize", resizeCanvas);

    var heroSec = cv.closest(".hero-section") || document;
    heroSec.addEventListener("pointermove", function (e) {
      var rect = cv.getBoundingClientRect();
      mousePos.x = e.clientX - rect.left;
      mousePos.y = e.clientY - rect.top;
    }, { passive: true });

    heroSec.addEventListener("pointerleave", function () {
      mousePos.x = -9999;
      mousePos.y = -9999;
    });

    var MAX_DIST = 120;
    var MAX_DIST_SQ = MAX_DIST * MAX_DIST;

    function renderLoop() {
      ctx.clearRect(0, 0, W, H);

      for (var i = 0; i < pts.length; i++) {
        var p = pts[i];
        p.x += p.vx;
        p.y += p.vy;

        if (p.x < 0) { p.x = 0; p.vx *= -1; } else if (p.x > W) { p.x = W; p.vx *= -1; }
        if (p.y < 0) { p.y = 0; p.vy *= -1; } else if (p.y > H) { p.y = H; p.vy *= -1; }

        // Light repulsion near mouse
        var dx = p.x - mousePos.x;
        var dy = p.y - mousePos.y;
        var dsq = dx * dx + dy * dy;
        if (dsq < 14000) {
          var dist = Math.sqrt(dsq) || 1;
          var force = (118 - dist) / 118 * 0.6;
          p.x += (dx / dist) * force;
          p.y += (dy / dist) * force;
        }

        // Draw particle dot
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
        ctx.fillStyle = "rgba(" + p.color[0] + "," + p.color[1] + "," + p.color[2] + ", 0.75)";
        ctx.fill();

        // Connect nearby points
        for (var j = i + 1; j < pts.length; j++) {
          var p2 = pts[j];
          var ldx = p.x - p2.x;
          var ldy = p.y - p2.y;
          var ldsq = ldx * ldx + ldy * ldy;
          if (ldsq < MAX_DIST_SQ) {
            var alpha = (1 - Math.sqrt(ldsq) / MAX_DIST) * 0.22;
            ctx.beginPath();
            ctx.moveTo(p.x, p.y);
            ctx.lineTo(p2.x, p2.y);
            ctx.strokeStyle = "rgba(65, 167, 245, " + alpha + ")";
            ctx.lineWidth = 0.8;
            ctx.stroke();
          }
        }
      }

      requestAnimationFrame(renderLoop);
    }

    requestAnimationFrame(renderLoop);
  })();

})();
