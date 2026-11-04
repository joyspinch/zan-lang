(function () {
  var form = document.getElementById('ai-design-form');
  if (!form) { return; }
  var tableId = form.getAttribute('data-table');
  var review = document.getElementById('ai-review');
  var rowsEl = document.getElementById('ai-rows');
  var note = document.getElementById('ai-note');
  var draft = [];

  function esc(s) {
    return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  form.addEventListener('submit', function (ev) {
    ev.preventDefault();
    var desc = form.querySelector('[name=desc]').value.trim();
    if (!desc) { return; }
    var go = document.getElementById('ai-go');
    go.disabled = true;
    go.textContent = '生成中…';
    fetch('/admin/dev/coder/aidesign', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ tableId: tableId, desc: desc }).toString()
    }).then(function (r) {
      if (!r.ok) { throw new Error('生成请求失败（HTTP ' + r.status + '）'); }
      return r.json();
    }).then(function (j) {
      if (!j || j.code !== '0000') { throw new Error((j && j.msg) || '生成失败'); }
      var data = typeof j.data === 'string' ? JSON.parse(j.data) : j.data;
      draft = data && Array.isArray(data.columns) ? data.columns : [];
      if (!draft.length) { throw new Error('AI 没有给出有效字段，换个描述再试'); }
      paint();
    }).catch(function (e) {
      window.Layer && window.Layer.msg(e.message || '生成失败', 'bad');
    }).then(function () {
      go.disabled = false;
      go.textContent = '生成设计草稿';
    });
  });

  function paint() {
    var mark = function (o, k) { return o[k] ? '✓' : ''; };
    rowsEl.innerHTML = draft.map(function (o, i) {
      var rel = o.dictType || (o.relTable ? o.relTable + '.' + o.relLabel : '');
      return '<tr><td><input type="checkbox" class="ai-pick" data-i="' + i + '" checked></td>'
        + '<td class="mono">' + esc(o.name) + '</td>'
        + '<td>' + esc(o.label) + '</td>'
        + '<td class="mono">' + esc(o.kind) + '</td>'
        + '<td class="mono">' + esc(o.widget) + '</td>'
        + '<td class="mono muted">' + esc(rel) + '</td>'
        + '<td class="mid">' + mark(o, 'inList') + '</td>'
        + '<td class="mid">' + mark(o, 'inFilter') + '</td>'
        + '<td class="mid">' + mark(o, 'inCreate') + '</td>'
        + '<td class="mid">' + mark(o, 'inEdit') + '</td>'
        + '<td class="mid">' + mark(o, 'required') + '</td></tr>';
    }).join('');
    note.textContent = 'AI 给出 ' + draft.length + ' 个字段，去掉不要的再应用；'
      + '类型/控件/关联之后仍可在字段表单里逐个改。';
    form.hidden = true;
    review.hidden = false;
  }

  document.getElementById('ai-all').addEventListener('change', function () {
    rowsEl.querySelectorAll('.ai-pick').forEach(function (b) { b.checked = this.checked; }, this);
  });
  document.getElementById('ai-back').addEventListener('click', function () {
    review.hidden = true;
    form.hidden = false;
  });
  document.getElementById('ai-apply').addEventListener('click', function () {
    var pick = [];
    rowsEl.querySelectorAll('.ai-pick').forEach(function (b) {
      if (b.checked) { pick.push(draft[Number(b.getAttribute('data-i'))]); }
    });
    if (!pick.length) { window.Layer && window.Layer.msg('至少勾选一个字段', 'bad'); return; }
    var btn = this;
    btn.disabled = true;
    btn.textContent = '应用中…';
    fetch('/admin/dev/coder/aiapply', {
      method: 'POST', credentials: 'same-origin',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({
        tableId: tableId, columns: JSON.stringify(pick)
      }).toString()
    }).then(function (r) {
      if (!r.ok) { throw new Error('应用请求失败（HTTP ' + r.status + '）'); }
      return r.json();
    }).then(function (j) {
      if (!j || j.code !== '0000') { throw new Error((j && j.msg) || '应用失败'); }
      window.Layer && window.Layer.closeTop();
      var rf = document.querySelector('.ad-top [data-refresh]');
      if (rf) { rf.click(); }
    }).catch(function (e) {
      btn.disabled = false;
      btn.textContent = '应用所选';
      window.Layer && window.Layer.msg(e.message || '应用失败', 'bad');
    });
  });
})();
