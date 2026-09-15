/* zan-table —— 管理后台表格增强组件（原生 JS，无框架依赖）。
 *
 * 服务端模板照常输出 <table class="table">（thead/tbody 静态行），组件
 * 按 data-table 属性渐进增强，不重渲染 DOM：操作列的 data-post/data-dialog
 * 等 admin.js 委托、行内 <a> 全部原样生效。增强四件事：
 *
 *   1. 列排序      th 加 data-sort="auto|num|text"（auto 按单元格文本智能
 *                  判断数字/日期/文本）。客户端排序当前页数据——数据已在
 *                  浏览器，交互零请求。
 *   2. 列即时筛选  data-table="filter" 时表头上方出现一行列内搜索框，
 *                  输入即过滤行（仅当前页）。
 *   3. 行选择      data-table="select" 出现首列复选框与全选；选中行数在
 *                  状态条显示，行加 .on 高亮。
 *   4. 密度切换    状态条上紧凑/舒适两档，记忆在 localStorage。
 *
 * 开关约定（服务端模板只写属性，不做 JS）：
 *   <table class="table" data-table="sort filter select">  空串 = 全开
 *   th[data-nosort]                                        该列不可排序
 *
 * 状态条（.zt-bar）由组件插入表格上方：左侧命中行数/选中数，右侧密度
 * 切换。排序与筛选状态在面板重载后自然复位（服务端分页语义下，跨页
 * 排序应回服务端做，这里明确只管当前页）。
 *
 * 供 admin.js 的 applyFragmentWidgets 在每个面板/弹窗载入后调用：
 *   ZanTable.wire(root)   —— root 内所有 [data-table] 尚未增强的表格
 *   ZanTable.wireAll()    —— 整个文档（首屏服务端渲染面板）
 */
(function () {
  'use strict';

  var DENSITY_KEY = 'zanweb.zantable.density';
  var ARROW_DESC = ' \u2193';
  var ARROW_ASC = ' \u2191';

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) { n.className = cls; }
    if (text !== undefined) { n.textContent = text; }
    return n;
  }

  /* 单元格值的排序类型：纯数字（含 %、千分位、单位后缀）按数值，
     日期样（2026-09-15、2026-09-15 10:20:30）按字符串（同格式字典序
     即时间序），其余按 localeCompare。 */
  function kindOf(text) {
    var t = text.trim();
    if (t === '') { return 'empty'; }
    if (/^-?[\d,]+(\.\d+)?\s*(%|ms|us|µs|s|KB|MB|GB)?$/.test(t)) { return 'num'; }
    if (/^\d{4}-\d{2}-\d{2}([ T]\d{2}:\d{2}(:\d{2})?)?$/.test(t)) { return 'date'; }
    return 'text';
  }

  function sortValue(cell, kind) {
    var t = cell.textContent.trim();
    if (kind === 'num') { return parseFloat(t.replace(/[,\s]/g, '')) || 0; }
    return t;
  }

  function enhance(table) {
    if (table.getAttribute('data-zt')) { return; }
    table.setAttribute('data-zt', '1');

    var modes = (table.getAttribute('data-table') || 'sort filter select').split(/\s+/);
    var canSort = modes.indexOf('sort') >= 0;
    var canFilter = modes.indexOf('filter') >= 0;
    var canSelect = modes.indexOf('select') >= 0;

    var thead = table.tHead;
    var tbody = table.tBodies[0];
    if (!thead || !tbody) { return; }
    var headRow = thead.rows[0];
    if (!headRow) { return; }

    var state = { sortCol: -1, sortDir: 1, query: '', selected: 0, hiddenByFilter: 0 };

    /* ---- 收集列元数据 -------------------------------------------- */
    var cols = [];
    for (var c = 0; c < headRow.cells.length; c++) {
      var th = headRow.cells[c];
      var sortable = canSort && !th.hasAttribute('data-nosort')
        && !th.classList.contains('ops');
      cols.push({
        th: th,
        label: th.textContent,
        sortable: sortable,
        kind: null              // 首次排序时按数据判定
      });
      if (sortable) { th.classList.add('zt-sortable'); }
    }

    /* ---- 状态条 --------------------------------------------------- */
    var bar = el('div', 'zt-bar');
    var info = el('span', 'zt-info');
    bar.appendChild(info);
    var sp = el('span', 'sp');
    bar.appendChild(sp);

    if (canFilter) {
      var find = el('input', 'input zt-find');
      find.type = 'search';
      find.placeholder = '在当前页内筛选…';
      find.setAttribute('aria-label', '表格内筛选');
      find.addEventListener('input', function () {
        state.query = find.value.trim().toLowerCase();
        apply();
      });
      bar.appendChild(find);
    }

    if (canSelect) {
      var selInfo = el('span', 'zt-selinfo');
      var clear = el('button', 'btn sm zt-clear', '取消选择');
      clear.type = 'button';
      clear.hidden = true;
      clear.addEventListener('click', function () {
        rows().forEach(function (tr) { setRow(tr, false); });
        apply();
      });
      bar.appendChild(selInfo);
      bar.appendChild(clear);
    }

    var density = el('button', 'btn sm zt-density');
    density.type = 'button';
    density.addEventListener('click', function () {
      var next = table.classList.contains('zt-compact') ? 'cozy' : 'compact';
      setDensity(next);
      try { localStorage.setItem(DENSITY_KEY, next); } catch (e) { /* private mode */ }
    });
    bar.appendChild(density);

    /* 状态条插到表格元素之前（表格常直接躺在 .card 里，同级即视觉上位） */
    table.parentNode.insertBefore(bar, table);

    function setDensity(mode) {
      var compact = mode === 'compact';
      table.classList.toggle('zt-compact', compact);
      density.textContent = compact ? '舒适' : '紧凑';
    }
    var saved = 'cozy';
    try { saved = localStorage.getItem(DENSITY_KEY) || 'cozy'; } catch (e) { /* */ }
    setDensity(saved === 'compact' ? 'compact' : 'cozy');

    /* ---- 行选择 --------------------------------------------------- */
    function rows() {
      return Array.prototype.slice.call(tbody.rows);
    }

    function setRow(tr, on) {
      var box = tr.querySelector('td.zt-cell input[type="checkbox"]');
      if (box) { box.checked = on; }
      tr.classList.toggle('on', on);
    }

    if (canSelect) {
      var headCell = el('th', 'zt-cell');
      headCell.style.width = '36px';
      var all = el('input');
      all.type = 'checkbox';
      all.setAttribute('aria-label', '全选');
      all.addEventListener('change', function () {
        rows().forEach(function (tr) {
          if (tr.style.display !== 'none') { setRow(tr, all.checked); }
        });
        apply();
      });
      headCell.appendChild(all);
      headRow.insertBefore(headCell, headRow.cells[0]);

      rows().forEach(function (tr) {
        var td = el('td', 'zt-cell');
        var box = el('input');
        box.type = 'checkbox';
        box.setAttribute('aria-label', '选择本行');
        box.addEventListener('change', function () {
          tr.classList.toggle('on', box.checked);
          apply();
        });
        td.appendChild(box);
        tr.insertBefore(td, tr.cells[0]);
      });
    }

    /* ---- 排序 ----------------------------------------------------- */
    if (canSort) {
      cols.forEach(function (col, i) {
        if (!col.sortable) { return; }
        col.th.addEventListener('click', function () {
          if (!col.kind) {
            /* 用第一非空行判定该列类型，整列同一种才有意义 */
            var kind = 'text';
            for (var r = 0; r < tbody.rows.length; r++) {
              var cell = tbody.rows[r].cells[i + (canSelect ? 1 : 0)];
              if (!cell) { continue; }
              var k = kindOf(cell.textContent);
              if (k !== 'empty') { kind = k; break; }
            }
            col.kind = kind;
          }
          if (state.sortCol === i) { state.sortDir = -state.sortDir; }
          else { state.sortCol = i; state.sortDir = col.kind === 'text' ? 1 : -1; }
          apply();
        });
      });
    }

    function paintHead() {
      cols.forEach(function (col, i) {
        if (!col.sortable) { return; }
        var on = state.sortCol === i;
        col.th.textContent = col.label + (on ? (state.sortDir < 0 ? ARROW_DESC : ARROW_ASC) : '');
        col.th.classList.toggle('zt-sorted', on);
      });
    }

    /* ---- 应用：筛选 → 排序 → 计数 --------------------------------- */
    function apply() {
      var list = rows();
      var visible = 0;

      /* 筛选：整行文本包含即命中（大小写不敏感） */
      list.forEach(function (tr) {
        var hit = state.query === '' ||
          tr.textContent.toLowerCase().indexOf(state.query) >= 0;
        tr.style.display = hit ? '' : 'none';
        if (hit) { visible++; }
      });

      /* 排序：对可见行重排（display:none 的行也在 tbody 里，一并排），
         直接移动 DOM 节点——委托事件不依赖行位置，安全。 */
      if (state.sortCol >= 0) {
        var col = cols[state.sortCol];
        var off = canSelect ? 1 : 0;
        var kind = col.kind || 'text';
        var dir = state.sortDir;
        var ordered = list.slice().sort(function (a, b) {
          var ca = a.cells[state.sortCol + off];
          var cb = b.cells[state.sortCol + off];
          if (!ca || !cb) { return 0; }
          var va = sortValue(ca, kind);
          var vb = sortValue(cb, kind);
          if (va < vb) { return -dir; }
          if (va > vb) { return dir; }
          return 0;
        });
        ordered.forEach(function (tr) { tbody.appendChild(tr); });
      }

      /* 计数与状态条 */
      state.selected = rows().filter(function (tr) {
        var box = tr.querySelector('td.zt-cell input[type="checkbox"]');
        return box && box.checked;
      }).length;

      var parts = [];
      if (state.query !== '') {
        parts.push('命中 ' + visible + ' / ' + list.length + ' 行');
      }
      info.textContent = parts.join('　');
      info.hidden = parts.length === 0;

      if (canSelect) {
        selInfo.textContent = state.selected > 0 ? '已选 ' + state.selected + ' 行' : '';
        selInfo.hidden = state.selected === 0;
        clear.hidden = state.selected === 0;
        var boxes = rows().filter(function (tr) { return tr.style.display !== 'none'; });
        var checked = boxes.filter(function (tr) {
          var b = tr.querySelector('td.zt-cell input[type="checkbox"]');
          return b && b.checked;
        }).length;
        var allBox = headRow.querySelector('th.zt-cell input[type="checkbox"]');
        if (allBox) {
          allBox.checked = boxes.length > 0 && checked === boxes.length;
          allBox.indeterminate = checked > 0 && checked < boxes.length;
        }
      }

      paintHead();
    }

    table.ztApply = apply;
  }

  function wire(root) {
    var scope = root && root.querySelectorAll ? root : document;
    var tables = scope.querySelectorAll('table[data-table]');
    for (var i = 0; i < tables.length; i++) { enhance(tables[i]); }
    /* root 自身就是一张表时（弹窗主体直接是表格的场景） */
    if (scope !== document && scope.matches && scope.matches('table[data-table]')) {
      enhance(scope);
    }
  }

  window.ZanTable = { wire: wire, enhance: enhance };
})();
