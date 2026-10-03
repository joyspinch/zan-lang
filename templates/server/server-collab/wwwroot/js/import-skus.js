(function () {
    var go = document.getElementById('skimpGo');
    if (!go) { return; }
    function esc(s) {
      return String(s == null ? '' : s).replace(/&/g, '&amp;')
        .replace(/</g, '&lt;').replace(/>/g, '&gt;');
    }
    go.onclick = function () {
      var inp = document.getElementById('skimpFile');
      var box = document.getElementById('skimpResult');
      var f = inp && inp.files && inp.files[0];
      if (!f) { box.innerHTML = '<div class="empty">请先选择 CSV 文件</div>'; return; }
      go.disabled = true;
      box.innerHTML = '<div class="muted">导入中…</div>';
      fetch('/admin/wms/skus/import', {
        method: 'POST', credentials: 'same-origin',
        headers: { 'Content-Type': 'application/octet-stream' }, body: f
      }).then(function (r) { return r.json(); }).then(function (j) {
        go.disabled = false;
        var d = (j && j.data) || {};
        if (!j || j.code !== '0000') {
          box.innerHTML = '<div class="empty">' + esc((j && j.msg) || '导入失败') + '</div>';
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
        box.innerHTML = h;
        if (Number(d.ok) > 0) { setTimeout(function () { location.reload(); }, 1200); }
      }).catch(function () {
        go.disabled = false;
        box.innerHTML = '<div class="empty">请求失败</div>';
      });
    };
  })();
