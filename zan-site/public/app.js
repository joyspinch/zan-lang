/**
 * Zan Official Site JavaScript Engine
 * Language switching, interactive simulation, particle network, and quickstart console
 */

(function () {
  "use strict";

  // =========================================================================
  // 1. Language Toggle (i18n)
  // =========================================================================
  var KEY = "zan-lang-pref";
  var body = document.body;

  function applyLanguage(lang) {
    body.classList.remove("lang-zh", "lang-en");
    body.classList.add(lang === "en" ? "lang-en" : "lang-zh");
    document.documentElement.lang = lang === "en" ? "en" : "zh-CN";
    var btns = document.querySelectorAll(".lang-btn");
    for (var i = 0; i < btns.length; i++) {
      btns[i].textContent = lang === "en" ? "中文" : "EN";
    }
    try { localStorage.setItem(KEY, lang); } catch (e) {}
  }

  var savedLang = "zh";
  try { savedLang = localStorage.getItem(KEY) || "zh"; } catch (e) {}
  applyLanguage(savedLang);

  document.addEventListener("click", function (e) {
    var t = e.target;
    if (t && t.classList && t.classList.contains("lang-btn")) {
      applyLanguage(body.classList.contains("lang-en") ? "zh" : "en");
    }
  });

  // =========================================================================
  // 2. Navigation Scroll Shadow
  // =========================================================================
  var nav = document.querySelector(".nav");
  if (nav) {
    var onScroll = function () {
      nav.classList.toggle("scrolled", window.scrollY > 12);
    };
    window.addEventListener("scroll", onScroll, { passive: true });
    onScroll();
  }

  // =========================================================================
  // 3. Download Modal Dialog with Auto Copy
  // =========================================================================
  var modal = document.getElementById("dlModal");
  if (modal) {
    var copyBtn = document.getElementById("modalCopyBtn");
    var codeVal = "zanlang";

    function copyAccessCode(callback) {
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(codeVal).then(callback, callback);
      } else {
        var ta = document.createElement("textarea");
        ta.value = codeVal;
        ta.style.position = "fixed";
        ta.style.opacity = "0";
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand("copy"); } catch (e) {}
        document.body.removeChild(ta);
        if (callback) callback();
      }
    }

    function openModal(e) {
      if (e) e.preventDefault();
      modal.classList.add("open");
      // Auto-copy access code immediately on opening
      copyAccessCode(function () {
        if (copyBtn) {
          copyBtn.classList.add("done");
          copyBtn.querySelector(".zh").textContent = "已自动复制";
          copyBtn.querySelector(".en").textContent = "Copied!";
          setTimeout(function () {
            copyBtn.classList.remove("done");
            copyBtn.querySelector(".zh").textContent = "一键复制";
            copyBtn.querySelector(".en").textContent = "Copy";
          }, 2400);
        }
      });
    }

    function closeModal() {
      modal.classList.remove("open");
    }

    document.querySelectorAll("[data-download]").forEach(function (btn) {
      btn.addEventListener("click", openModal);
    });

    modal.addEventListener("click", function (e) {
      if (e.target === modal) closeModal();
    });

    var closeBtn = modal.querySelector(".modal-close-btn");
    if (closeBtn) closeBtn.addEventListener("click", closeModal);

    document.addEventListener("keydown", function (e) {
      if (e.key === "Escape") closeModal();
    });

    if (copyBtn) {
      copyBtn.addEventListener("click", function () {
        copyAccessCode(function () {
          copyBtn.classList.add("done");
          copyBtn.querySelector(".zh").textContent = "复制成功！";
          copyBtn.querySelector(".en").textContent = "Copied!";
          setTimeout(function () {
            copyBtn.classList.remove("done");
            copyBtn.querySelector(".zh").textContent = "一键复制";
            copyBtn.querySelector(".en").textContent = "Copy";
          }, 2000);
        });
      });
    }
  }

  // =========================================================================
  // 4. Interactive Hero Simulation (IDE F5 Run + Native Window Clicks)
  // =========================================================================
  var heroRunBtn = document.getElementById("heroRunBtn");
  var nativeSimWin = document.getElementById("nativeSimWin");
  var simClickBtn = document.getElementById("simClickBtn");
  var simCountLabel = document.getElementById("simCountLabel");
  var clickCounter = 0;

  if (simClickBtn && simCountLabel) {
    simClickBtn.addEventListener("click", function () {
      clickCounter++;
      var zhSpan = simCountLabel.querySelector(".zh");
      var enSpan = simCountLabel.querySelector(".en");
      if (zhSpan) zhSpan.textContent = "点击了 " + clickCounter + " 次";
      if (enSpan) enSpan.textContent = "Clicked " + clickCounter + " times";
      simClickBtn.style.transform = "scale(0.95)";
      setTimeout(function () { simClickBtn.style.transform = ""; }, 100);
    });
  }

  if (heroRunBtn && nativeSimWin) {
    heroRunBtn.addEventListener("click", function () {
      heroRunBtn.style.transform = "scale(0.92)";
      setTimeout(function () { heroRunBtn.style.transform = ""; }, 150);
      nativeSimWin.style.animation = "none";
      nativeSimWin.offsetHeight; // trigger reflow
      nativeSimWin.style.animation = "windowPop 0.4s cubic-bezier(0.16, 1, 0.3, 1)";
    });
  }

  // =========================================================================
  // 5. Quickstart Interactive Code Snippets Tabs
  // =========================================================================
  var codeSnippets = {
    gui:
`<span class="k">using</span> <span class="t">Gui</span>;
<span class="k">using</span> <span class="t">Gui.Widget</span>;

<span class="k">class</span> <span class="t">DesktopApp</span> {
    <span class="k">static</span> <span class="t">void</span> <span class="fn">Main</span>() {
        <span class="k">var</span> form = <span class="t">Form</span>.<span class="fn">Create</span>(<span class="s">"Zan Native GUI"</span>, <span class="n">480</span>, <span class="n">320</span>);
        <span class="k">var</span> panel = <span class="k">new</span> <span class="t">StackPanel</span>() { Orientation = <span class="t">Orientation</span>.Vertical };

        <span class="k">var</span> title = <span class="k">new</span> <span class="t">Label</span>(<span class="s">"Welcome to Zan Native UI"</span>) { FontSize = <span class="n">18</span> };
        <span class="k">var</span> btn = <span class="k">new</span> <span class="t">Button</span>(<span class="s">"Start Engine"</span>);
        btn.<span class="fn">OnClick</span>(() =&gt; Console.<span class="fn">WriteLine</span>(<span class="s">"Engine initialized in 2ms!"</span>));

        panel.<span class="fn">Add</span>(title);
        panel.<span class="fn">Add</span>(btn);
        form.<span class="fn">Add</span>(panel);
        form.<span class="fn">Run</span>(); <span class="c">// AOT Compiled · Zero GC</span>
    }
}`,

    web:
`<span class="k">using</span> <span class="t">Server.Mvc</span>;
<span class="k">using</span> <span class="t">Server.Http</span>;

[<span class="t">ApiController</span>]
<span class="k">class</span> <span class="t">UserController</span> {
    [<span class="t">Api</span>(get, route = <span class="s">"/api/v1/health"</span>)]
    <span class="k">static</span> <span class="t">JsonResult</span> <span class="fn">GetHealth</span>() {
        <span class="k">return</span> <span class="t">JsonResult</span>.<span class="fn">Ok</span>(<span class="k">new</span> { status = <span class="s">"healthy"</span>, qps = <span class="n">128000</span> });
    }

    [<span class="t">Api</span>(post, route = <span class="s">"/api/v1/login"</span>, auth = <span class="k">false</span>)]
    <span class="k">static</span> <span class="t">IResponse</span> <span class="fn">Login</span>(<span class="t">HttpContext</span> ctx) {
        <span class="k">var</span> body = ctx.<span class="fn">ReadJson</span>&lt;<span class="t">LoginDto</span>&gt;();
        <span class="k">return</span> <span class="t">AuthService</span>.<span class="fn">IssueJwt</span>(body.Username);
    }
}`,

    game:
`<span class="k">using</span> <span class="t">Zan.Game</span>;

<span class="k">class</span> <span class="t">PongGame</span> : <span class="t">Game</span> {
    <span class="t">Vec2</span> ball = <span class="k">new</span> <span class="t">Vec2</span>(<span class="n">400</span>, <span class="n">300</span>);
    <span class="t">Vec2</span> velocity = <span class="k">new</span> <span class="t">Vec2</span>(<span class="n">5</span>, <span class="n">4</span>);

    <span class="k">override</span> <span class="t">void</span> <span class="fn">Update</span>(<span class="t">float</span> dt) {
        ball += velocity;
        <span class="k">if</span> (ball.Y &lt; <span class="n">0</span> || ball.Y &gt; <span class="n">600</span>) velocity.Y = -velocity.Y;
        <span class="k">if</span> (ball.X &lt; <span class="n">0</span> || ball.X &gt; <span class="n">800</span>) velocity.X = -velocity.X;
    }

    <span class="k">override</span> <span class="t">void</span> <span class="fn">Draw</span>(<span class="t">Canvas</span> cv) {
        cv.<span class="fn">Clear</span>(<span class="t">Color</span>.Black);
        cv.<span class="fn">FillCircle</span>(ball, <span class="n">10</span>, <span class="t">Color</span>.LightCyan);
    }
}`,

    cli:
`<span class="k">using</span> <span class="t">System</span>;
<span class="k">using</span> <span class="t">System.IO</span>;

<span class="k">class</span> <span class="t">Tool</span> {
    <span class="k">static</span> <span class="k">int</span> <span class="fn">Main</span>(<span class="t">string</span>[] args) {
        <span class="k">if</span> (args.Length == <span class="n">0</span>) {
            Console.<span class="fn">WriteLine</span>(<span class="s">"Usage: zan-tool &lt;input.file&gt;"</span>);
            <span class="k">return</span> <span class="n">1</span>;
        }
        <span class="k">var</span> content = <span class="t">File</span>.<span class="fn">ReadAllText</span>(args[<span class="n">0</span>]);
        Console.<span class="fn">WriteLine</span>(<span class="s">$"Processed {content.Length} bytes natively."</span>);
        <span class="k">return</span> <span class="n">0</span>;
    }
}`
  };

  var consoleSnippetEl = document.getElementById("consoleSnippet");
  var consoleTabBtns = document.querySelectorAll(".console-tab-btn");
  var consoleCopyBtn = document.getElementById("consoleCopyBtn");

  function setSnippet(key) {
    if (consoleSnippetEl && codeSnippets[key]) {
      consoleSnippetEl.innerHTML = codeSnippets[key];
    }
    consoleTabBtns.forEach(function (b) {
      b.classList.toggle("active", b.getAttribute("data-tab") === key);
    });
  }

  if (consoleTabBtns.length) {
    consoleTabBtns.forEach(function (btn) {
      btn.addEventListener("click", function () {
        var key = btn.getAttribute("data-tab");
        setSnippet(key);
      });
    });
    setSnippet("gui");
  }

  if (consoleCopyBtn && consoleSnippetEl) {
    consoleCopyBtn.addEventListener("click", function () {
      var raw = consoleSnippetEl.textContent || consoleSnippetEl.innerText;
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(raw).then(function () {
          consoleCopyBtn.classList.add("done");
          var zh = consoleCopyBtn.querySelector(".zh");
          var en = consoleCopyBtn.querySelector(".en");
          if (zh) zh.textContent = "已复制！";
          if (en) en.textContent = "Copied!";
          setTimeout(function () {
            consoleCopyBtn.classList.remove("done");
            if (zh) zh.textContent = "复制代码";
            if (en) en.textContent = "Copy Code";
          }, 1800);
        });
      }
    });
  }

  // =========================================================================
  // 6. Bento Grid Spotlight Mouse-Move
  // =========================================================================
  document.querySelectorAll(".bento-card").forEach(function (card) {
    card.addEventListener("pointermove", function (e) {
      var rect = card.getBoundingClientRect();
      card.style.setProperty("--mx", (e.clientX - rect.left) + "px");
      card.style.setProperty("--my", (e.clientY - rect.top) + "px");
    });
  });

  // =========================================================================
  // 7. Visual Flourishes: Cursor Glow, Click Sparks, Tilt
  // =========================================================================
  var reduceMotion = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var isFinePointer = window.matchMedia && window.matchMedia("(pointer: fine)").matches;

  if (!reduceMotion && isFinePointer) {
    var cg = document.querySelector(".cursor-glow");
    if (cg) {
      var gx = 0, gy = 0, raf = 0;
      var moveGlow = function () {
        cg.style.transform = "translate3d(" + gx + "px," + gy + "px,0) translate(-50%,-50%)";
        raf = 0;
      };
      window.addEventListener("pointermove", function (e) {
        if (e.pointerType && e.pointerType !== "mouse") return;
        gx = e.clientX;
        gy = e.clientY;
        body.classList.add("glow-on");
        if (!raf) raf = requestAnimationFrame(moveGlow);
      }, { passive: true });
      document.addEventListener("mouseleave", function () {
        body.classList.remove("glow-on");
      });
    }

    // Click Sparks Burst
    var sparkColors = ["#41a7f5", "#2b5cff", "#ffc838", "#10b981", "#8b5cf6"];
    window.addEventListener("pointerdown", function (e) {
      if (e.pointerType && e.pointerType !== "mouse") return;
      var count = 8;
      for (var i = 0; i < count; i++) {
        var s = document.createElement("span");
        s.className = "spark";
        var ang = (Math.PI * 2 * i) / count + (Math.random() - 0.5) * 0.4;
        var dist = 28 + Math.random() * 32;
        s.style.left = e.clientX + "px";
        s.style.top = e.clientY + "px";
        s.style.background = sparkColors[i % sparkColors.length];
        s.style.setProperty("--dx", (Math.cos(ang) * dist).toFixed(1) + "px");
        s.style.setProperty("--dy", (Math.sin(ang) * dist).toFixed(1) + "px");
        body.appendChild(s);
        (function (el) {
          setTimeout(function () { el.remove(); }, 650);
        })(s);
      }
    }, { passive: true });
  }

  // =========================================================================
  // 8. Full-Screen Interactive Hero Particle Canvas (Constellation)
  // =========================================================================
  (function () {
    var canvas = document.querySelector(".hero-canvas");
    if (!canvas || !canvas.getContext) return;
    var ctx = canvas.getContext("2d");
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var width = 0, height = 0, particles = [];
    var mouse = { x: -9999, y: -9999 };

    var colors = [
      [65, 167, 245], // Electric cyan
      [43, 92, 255],  // Volt blue
      [139, 92, 246], // Purple
      [16, 185, 129]  // Emerald
    ];

    function resize() {
      width = canvas.clientWidth;
      height = canvas.clientHeight;
      canvas.width = width * dpr;
      canvas.height = height * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

      var count = Math.max(24, Math.min(54, Math.round((width * height) / 24000)));
      particles = [];
      for (var i = 0; i < count; i++) {
        particles.push({
          x: Math.random() * width,
          y: Math.random() * height,
          vx: (Math.random() - 0.5) * 0.38,
          vy: (Math.random() - 0.5) * 0.38,
          radius: Math.random() * 1.8 + 1.2,
          color: colors[i % colors.length]
        });
      }
    }
    resize();
    window.addEventListener("resize", resize);

    var heroSection = canvas.closest(".hero") || document;
    heroSection.addEventListener("pointermove", function (e) {
      var rect = canvas.getBoundingClientRect();
      mouse.x = e.clientX - rect.left;
      mouse.y = e.clientY - rect.top;
    }, { passive: true });
    heroSection.addEventListener("pointerleave", function () {
      mouse.x = -9999;
      mouse.y = -9999;
    });

    var maxDist = 140;
    var maxDistSq = maxDist * maxDist;

    function renderLoop() {
      ctx.clearRect(0, 0, width, height);

      for (var i = 0; i < particles.length; i++) {
        var p = particles[i];
        p.x += p.vx;
        p.y += p.vy;

        if (p.x < 0) { p.x = 0; p.vx *= -1; }
        else if (p.x > width) { p.x = width; p.vx *= -1; }
        if (p.y < 0) { p.y = 0; p.vy *= -1; }
        else if (p.y > height) { p.y = height; p.vy *= -1; }

        // Mouse attraction / repelling
        var dx = p.x - mouse.x;
        var dy = p.y - mouse.y;
        var distSq = dx * dx + dy * dy;
        if (distSq < 16000 && distSq > 1) {
          var dist = Math.sqrt(distSq);
          var force = ((126 - dist) / 126) * 0.75;
          p.x += (dx / dist) * force;
          p.y += (dy / dist) * force;
        }

        // Draw particle
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.radius, 0, Math.PI * 2);
        ctx.fillStyle = "rgba(" + p.color[0] + "," + p.color[1] + "," + p.color[2] + ",0.75)";
        ctx.fill();

        // Connect lines
        for (var j = i + 1; j < particles.length; j++) {
          var p2 = particles[j];
          var ldx = p.x - p2.x;
          var ldy = p.y - p2.y;
          var lineDistSq = ldx * ldx + ldy * ldy;
          if (lineDistSq < maxDistSq) {
            var alpha = (1 - lineDistSq / maxDistSq) * 0.22;
            ctx.beginPath();
            ctx.moveTo(p.x, p.y);
            ctx.lineTo(p2.x, p2.y);
            ctx.strokeStyle = "rgba(" + p.color[0] + "," + p.color[1] + "," + p.color[2] + "," + alpha + ")";
            ctx.lineWidth = 1;
            ctx.stroke();
          }
        }
      }

      requestAnimationFrame(renderLoop);
    }
    requestAnimationFrame(renderLoop);
  })();

  // =========================================================================
  // 9. Wiki Page Side Menu Switching (for /wiki etc.)
  // =========================================================================
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

})();
