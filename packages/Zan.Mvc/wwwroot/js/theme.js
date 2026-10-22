/* 主题引导：在任何样式绘制之前把 data-theme 落到 <html> 上，避免闪白/闪黑。
 *
 * 独立文件而不是内联脚本：管理后台可以开 CSP（default-src 'self'），
 * 内联脚本会被直接拦掉，主题就永远停在默认值。
 *
 * 取值优先级：用户显式选择（localStorage） > 系统偏好。
 * 点任意 [data-theme-toggle] 元素切换；系统偏好变化只在用户没选过时跟随。
 */
(function () {
  var KEY = 'zan-theme';
  var root = document.documentElement;

  function stored() {
    try { return localStorage.getItem(KEY); } catch (e) { return null; }
  }
  function prefersDark() {
    try { return window.matchMedia('(prefers-color-scheme: dark)').matches; } catch (e) { return false; }
  }
  function apply(mode, remember) {
    root.setAttribute('data-theme', mode);
    if (remember) {
      try { localStorage.setItem(KEY, mode); } catch (e) { /* 隐私模式：不记忆即可 */ }
    }
  }

  var picked = stored();
  apply(picked || (prefersDark() ? 'dark' : 'light'), false);

  document.addEventListener('click', function (ev) {
    var btn = ev.target.closest ? ev.target.closest('[data-theme-toggle]') : null;
    if (!btn) { return; }
    var next = root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
    apply(next, true);
    btn.setAttribute('aria-pressed', next === 'dark' ? 'true' : 'false');
  });

  try {
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', function (e) {
      if (stored()) { return; }
      apply(e.matches ? 'dark' : 'light', false);
    });
  } catch (e) { /* 老浏览器没有 addEventListener：只认首次偏好 */ }
})();
