(function () {
  "use strict";

  // 1. Language toggle
  var body = document.body;
  var langBtn = document.getElementById("langBtn");
  var savedLang = (typeof localStorage !== "undefined" && localStorage.getItem("zan_lang")) || "zh";

  function setLanguage(lang) {
    body.classList.toggle("lang-zh", lang === "zh");
    body.classList.toggle("lang-en", lang === "en");
    if (langBtn) langBtn.textContent = lang === "zh" ? "EN" : "中";
    try { localStorage.setItem("zan_lang", lang); } catch (e) {}
  }

  setLanguage(savedLang);
  if (langBtn) {
    langBtn.addEventListener("click", function () {
      setLanguage(body.classList.contains("lang-zh") ? "en" : "zh");
    });
  }

  // 2. Nav scroll shadow — find by class since docs and index both use .nav
  var nav = document.querySelector(".nav");
  if (nav) {
    window.addEventListener("scroll", function () {
      nav.classList.toggle("scrolled", window.scrollY > 24);
    }, { passive: true });
  }

  // 3. Download modal
  var dlModal = document.getElementById("dlModal");

  function openModal(e) {
    if (e) e.preventDefault();
    if (!dlModal) return;
    dlModal.classList.add("open");
    body.style.overflow = "hidden";
  }

  function closeModal() {
    if (!dlModal) return;
    dlModal.classList.remove("open");
    body.style.overflow = "";
  }

  document.querySelectorAll("[data-download]").forEach(function (t) {
    t.addEventListener("click", openModal);
  });

  var modalCloseBtn = document.getElementById("modalCloseBtn");
  if (modalCloseBtn) modalCloseBtn.addEventListener("click", closeModal);
  if (dlModal) {
    dlModal.addEventListener("click", function (e) {
      if (e.target === dlModal) closeModal();
    });
  }
  window.addEventListener("keydown", function (e) {
    if (e.key === "Escape") closeModal();
  });

  // 4. Generic copy — any [data-copy] button anywhere on the page
  function attachCopyBtn(btn) {
    btn.addEventListener("click", function () {
      var code = btn.getAttribute("data-copy") || "";
      if (!navigator.clipboard) return;
      var origHTML = btn.innerHTML;
      navigator.clipboard.writeText(code).then(function () {
        btn.innerHTML = '<span class="zh">已复制 ✓</span><span class="en">Copied ✓</span>';
        btn.classList.add("done");
        setTimeout(function () { btn.innerHTML = origHTML; btn.classList.remove("done"); }, 2000);
      }).catch(function () {});
    });
  }
  document.querySelectorAll("[data-copy]").forEach(attachCopyBtn);

  // 5. Hero demo counter
  var demoBtn  = document.getElementById("demoActionBtn");
  var demoText = document.getElementById("demoCounterText");
  var clickCount = 0;
  if (demoBtn && demoText) {
    demoBtn.addEventListener("click", function () {
      clickCount++;
      demoText.innerHTML = '<span class="zh">已点击 ' + clickCount + ' 次</span>'
                         + '<span class="en">' + clickCount + ' clicks</span>';
      demoBtn.style.transform = "scale(0.94)";
      setTimeout(function () { demoBtn.style.transform = ""; }, 120);
    });
  }

  // 6. Quickstart code tabs
  var codeSnippets = {
    gui: '<span class="k">using</span> <span class="t">Gui</span>;\n'
       + '<span class="k">using</span> <span class="t">Gui.Widget</span>;\n\n'
       + '<span class="k">class</span> <span class="t">App</span> {\n'
       + '    <span class="k">static</span> <span class="t">void</span> <span class="fn">Main</span>() {\n'
       + '        <span class="k">var</span> form = <span class="t">Form</span>.<span class="fn">Create</span>(<span class="s">"Zan GUI"</span>, <span class="n">500</span>, <span class="n">360</span>);\n'
       + '        <span class="k">var</span> btn  = <span class="k">new</span> <span class="t">Button</span>(<span class="s">"点击我"</span>);\n'
       + '        form.<span class="fn">Add</span>(btn); form.<span class="fn">Run</span>();\n'
       + '    }\n}',
    web: '<span class="k">using</span> <span class="t">Server</span>;\n'
       + '<span class="k">using</span> <span class="t">Server.Mvc</span>;\n\n'
       + '[<span class="t">Controller</span>(<span class="s">"/api/v1"</span>)]\n'
       + '<span class="k">class</span> <span class="t">UserController</span> {\n'
       + '    [<span class="t">HttpGet</span>(<span class="s">"/users/{id}"</span>)]\n'
       + '    <span class="k">public</span> <span class="t">User</span> <span class="fn">GetUser</span>(<span class="k">int</span> id)\n'
       + '        => <span class="t">Db</span>.<span class="fn">Find</span>&lt;<span class="t">User</span>&gt;(id);\n}',
    game: '<span class="k">using</span> <span class="t">Game</span>;\n\n'
        + '<span class="k">class</span> <span class="t">MyGame</span> : <span class="t">GameEngine</span> {\n'
        + '    <span class="k">override</span> <span class="t">void</span> <span class="fn">Update</span>(<span class="k">float</span> dt) {\n'
        + '        ball.x += ball.vx * dt;\n'
        + '        <span class="k">if</span> (ball.x &lt;= <span class="n">0</span> || ball.x &gt;= <span class="n">800</span>) ball.vx = -ball.vx;\n'
        + '    }\n'
        + '    <span class="k">override</span> <span class="t">void</span> <span class="fn">Render</span>(<span class="t">Graphics</span> g) {\n'
        + '        g.<span class="fn">FillCircle</span>(ball.x, ball.y, <span class="n">8</span>, <span class="t">Color</span>.White);\n'
        + '    }\n}',
    cli: '<span class="k">using</span> <span class="t">System</span>;\n'
       + '<span class="k">using</span> <span class="t">System.IO</span>;\n\n'
       + '<span class="k">class</span> <span class="t">Program</span> {\n'
       + '    <span class="k">static</span> <span class="t">void</span> <span class="fn">Main</span>(<span class="k">string</span>[] args) {\n'
       + '        <span class="k">var</span> files = <span class="t">Directory</span>.<span class="fn">GetFiles</span>(<span class="s">"."</span>, <span class="s">"*.zan"</span>);\n'
       + '        <span class="t">Console</span>.<span class="fn">WriteLine</span>(<span class="s">$"Found {files.Length} source files."</span>);\n'
       + '    }\n}'
  };

  var tabBtns   = document.querySelectorAll(".btn-console-tab");
  var snippetEl = document.getElementById("consoleSnippet");
  var consoleCopyBtn = document.getElementById("consoleCopyBtn");

  function setTab(key) {
    if (snippetEl && codeSnippets[key]) snippetEl.innerHTML = codeSnippets[key];
    tabBtns.forEach(function (b) { b.classList.toggle("active", b.dataset.tab === key); });
  }

  tabBtns.forEach(function (b) {
    b.addEventListener("click", function () { setTab(b.dataset.tab); });
  });

  if (consoleCopyBtn) {
    consoleCopyBtn.addEventListener("click", function () {
      var text = snippetEl ? snippetEl.textContent : "";
      if (!navigator.clipboard) return;
      var origHTML = consoleCopyBtn.innerHTML;
      navigator.clipboard.writeText(text).then(function () {
        consoleCopyBtn.innerHTML = '<span class="zh">已复制 ✓</span><span class="en">Copied ✓</span>';
        consoleCopyBtn.classList.add("done");
        setTimeout(function () { consoleCopyBtn.innerHTML = origHTML; consoleCopyBtn.classList.remove("done"); }, 2000);
      }).catch(function () {});
    });
  }

  setTab("gui");

  // 7. Doc sidebar keyword filter
  var searchInput = document.getElementById("docSearch");
  if (searchInput) {
    var sideLinks = document.querySelectorAll(".doc-side a");
    searchInput.addEventListener("input", function () {
      var q = this.value.trim().toLowerCase();
      sideLinks.forEach(function (a) {
        a.style.display = (!q || a.textContent.toLowerCase().indexOf(q) >= 0) ? "" : "none";
      });
      // Hide entire nsgrp details when all its links are filtered out
      document.querySelectorAll("details.nsgrp").forEach(function (det) {
        var anyVisible = false;
        det.querySelectorAll("a").forEach(function (a) {
          if (a.style.display !== "none") anyVisible = true;
        });
        det.style.display = anyVisible ? "" : "none";
      });
    });
  }

})();
