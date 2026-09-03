/* search.js — 顶栏全局搜索（T8，bell.js 同款 IIFE 骨架）：300ms 防抖 +
   ≥2 字符才请求；fetch /admin/oa/search/query 带 X-Fragment 头取片段回填
   下拉列表；空 kw/无结果隐藏面板；Esc/点击外部关闭；结果 <a> 走 admin.js
   既有 data-tab 委托开标签，无需额外接线。 */
(function () {
  'use strict';

  var box = document.querySelector('[data-gsearch]');
  if (!box) { return; }
  var input = box.querySelector('[data-gsearch-input]');
  var menu = box.querySelector('[data-gsearch-menu]');
  var list = box.querySelector('[data-gsearch-list]');

  var QUERY = '/admin/oa/search/query';
  var DEBOUNCE_MS = 300;
  var MIN_CHARS = 2;
  var timer = null;
  var lastKw = '';

  function close() { if (menu) { menu.hidden = true; } }

  function request(kw) {
    fetch(QUERY + '?kw=' + encodeURIComponent(kw), {
      credentials: 'same-origin',
      headers: { 'X-Fragment': '1' }
    }).then(function (r) { return r.text(); })
      .then(function (html) {
        list.innerHTML = html;
        /* 无命中（空态/全部源被屏闸滤空）不展开面板。 */
        var has = list.querySelectorAll('.ad-gsearch-item').length > 0;
        menu.hidden = !has;
      }).catch(function () { /* 网络抖动静默，下次输入重试 */ });
  }

  if (input) {
    input.addEventListener('input', function () {
      var kw = input.value.trim();
      if (timer) { clearTimeout(timer); timer = null; }
      if (kw.length < MIN_CHARS) { lastKw = ''; close(); return; }
      if (kw === lastKw) { return; }
      timer = setTimeout(function () {
        timer = null;
        lastKw = kw;
        request(kw);
      }, DEBOUNCE_MS);
    });
  }

  /* 点击结果即开标签，同时收起面板。 */
  if (list) {
    list.addEventListener('click', function () { close(); });
  }

  document.addEventListener('click', function (ev) {
    if (!box.contains(ev.target)) { close(); }
  });
  document.addEventListener('keydown', function (ev) {
    if (ev.key === 'Escape') { close(); }
  });
})();
