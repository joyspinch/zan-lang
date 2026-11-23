/* collab.js — 协同域管理台通用前端增强：
   1) CSSOM 回填 [data-w]/[data-h]（ECBI 报表进度条与迷你柱图）；
   2) 挂接 CSV 导入弹窗按钮（#odimpGo/#ctimpGo/#cimpGo/#skimpGo），
      解决 Keep-Alive/弹窗片段内 <script src=...> 不重新执行的问题。 */
(function () {
  'use strict';

  function onAdminMount(fn) {
    if (window.ZanAdmin && typeof ZanAdmin.onMount === 'function') {
      ZanAdmin.onMount(fn);
    } else {
      window.ZanAdmin = window.ZanAdmin || {};
      window.ZanAdmin._q = window.ZanAdmin._q || [];
      window.ZanAdmin._q.push(['onMount', fn]);
    }
  }

  function applyCssomDims(root) {
    var els = (root || document).querySelectorAll('[data-w],[data-h]');
    for (var i = 0; i < els.length; i++) {
      var el = els[i];
      var w = el.getAttribute('data-w');
      if (w !== null && w !== '') { el.style.width = /^\d+(\.\d+)?$/.test(w) ? w + '%' : w; }
      var h = el.getAttribute('data-h');
      if (h !== null && h !== '') { el.style.height = /^\d+(\.\d+)?$/.test(h) ? h + '%' : h; }
    }
  }

  function esc(s) {
    return String(s == null ? '' : s).replace(/&/g, '&amp;')
      .replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  var IMPORTS = [
    { go: 'odimpGo', file: 'odimpFile', result: 'odimpResult', url: '/admin/ecbi/import' },
    { go: 'ctimpGo', file: 'ctimpFile', result: 'ctimpResult', url: '/admin/crm/contacts/import' },
    { go: 'cimpGo',  file: 'cimpFile',  result: 'cimpResult',  url: '/admin/crm/customers/import' },
    { go: 'skimpGo', file: 'skimpFile', result: 'skimpResult', url: '/admin/wms/skus/import' }
  ];

  function wireImport(root, spec) {
    var scope = root && root.querySelector ? root : document;
    var go = scope.querySelector('#' + spec.go) || document.getElementById(spec.go);
    if (!go || go.getAttribute('data-collab-import') === '1') { return; }
    go.setAttribute('data-collab-import', '1');
    go.onclick = function () {
      var inp = document.getElementById(spec.file);
      var box = document.getElementById(spec.result);
      var f = inp && inp.files && inp.files[0];
      if (!f) {
        if (box) { box.innerHTML = '<div class="empty">请先选择 CSV 文件</div>'; }
        return;
      }
      go.disabled = true;
      if (box) { box.innerHTML = '<div class="muted">导入中…</div>'; }
      fetch(spec.url, {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/octet-stream' }, body: f
      }).then(function (r) { return r.json(); }).then(function (j) {
        go.disabled = false;
        var d = (j && j.data) || {};
        if (!j || j.code !== '0000') {
          if (box) { box.innerHTML = '<div class="empty">' + esc((j && j.msg) || '导入失败') + '</div>'; }
          return;
        }
        var h = '<div class="card mtop10"><b>导入完成</b>：成功 '
          + esc(d.ok) + ' 条，失败 ' + esc(d.fail) + ' 条';
        var errs = d.errors || [];
        if (errs.length) {
          h += '<table class="table"><thead><tr><th width="70">行号</th>'
            + '<th width="100">字段</th><th>问题</th></tr></thead><tbody>';
          for (var i = 0; i < errs.length; i++) {
            h += '<tr><td class="num">' + esc(errs[i].row) + '</td><td>'
              + esc(errs[i].field || '—') + '</td><td>' + esc(errs[i].msg) + '</td></tr>';
          }
          h += '</tbody></table>';
        }
        h += '</div>';
        if (box) { box.innerHTML = h; }
        if (Number(d.ok) > 0) {
          setTimeout(function () {
            if (window.ZanAdmin && ZanAdmin.Layer && ZanAdmin.reload) {
              ZanAdmin.Layer.closeTop();
              ZanAdmin.reload();
            } else {
              location.reload();
            }
          }, 1200);
        }
      }).catch(function () {
        go.disabled = false;
        if (box) { box.innerHTML = '<div class="empty">请求失败</div>'; }
      });
    };
  }

  function mount(root) {
    applyCssomDims(root);
    for (var i = 0; i < IMPORTS.length; i++) { wireImport(root, IMPORTS[i]); }
  }

  onAdminMount(mount);
  document.addEventListener('DOMContentLoaded', function () { mount(document); });
  document.addEventListener('htmx:afterSwap', function (ev) {
    mount((ev && ev.detail && ev.detail.target) || document);
  });
})();
