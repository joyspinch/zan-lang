/* 自定义部门 only means anything for the 自定义 scope, so its checkboxes follow
     the select instead of sitting there dead. */
  (function () {
    var picks = document.querySelectorAll('[data-scope-pick]');
    var pick = picks[picks.length - 1];
    if (!pick) { return; }
    var box = pick.form.querySelector('[data-scope-depts]');
    var lab = pick.form.querySelector('[data-scope-label]');
    function sync() {
      var on = pick.value === '5';
      box.hidden = !on;
      lab.hidden = !on;
    }
    pick.addEventListener('change', sync);
    sync();
  })();
