/* AttachBlob 附件区（T4）：详情页放一个容器即挂附件区——
 *   <div data-attach data-module="flow_task" data-related="{{id}}"></div>
 * 脚本拉 /admin/oa/attachments/blob 回填列表，上传以原始字节直传
 * （application/octet-stream，文件名/类型走 query——流式上传路由不解析
 * multipart 字段），删除/列表走表单编码 POST 与片段 GET。admin.js 载入
 * 片段后统一调 window.applyFragmentWidgets，本文件包一层使每个片段
 * （含弹窗、面板重载）自动完成挂载。 */
(function () {
  function esc(s) {
    return String(s == null ? '' : s);
  }

  function mount(root) {
    var hosts = (root || document).querySelectorAll('[data-attach]');
    for (var i = 0; i < hosts.length; i++) { wire(hosts[i]); }
  }

  function wire(host) {
    if (host.getAttribute('data-attach-on')) { return; }
    host.setAttribute('data-attach-on', '1');
    var module = host.getAttribute('data-module') || '';
    var related = host.getAttribute('data-related') || '0';

    function blobUrl() {
      return '/admin/oa/attachments/blob?module=' + encodeURIComponent(module)
        + '&relatedId=' + encodeURIComponent(related);
    }

    function status(text) {
      var n = host.querySelector('[data-attach-status]');
      if (n) { n.textContent = text || ''; }
    }

    function say(msg, bad) {
      if (window.Layer) { Layer.msg(msg, bad ? 'bad' : 'ok'); }
      else { status(msg); }
    }

    function load() {
      fetch(blobUrl(), { credentials: 'same-origin',
                         headers: { 'X-Fragment': '1' } })
        .then(function (r) { return r.ok ? r.text() : ''; })
        .then(function (html) { host.innerHTML = html; wireInner(); })
        .catch(function () { status('附件区加载失败'); });
    }

    function upload(file) {
      if (!file) { return; }
      status('上传中…');
      fetch('/admin/oa/attachments/upload?module=' + encodeURIComponent(module)
        + '&relatedId=' + encodeURIComponent(related)
        + '&name=' + encodeURIComponent(file.name)
        + '&mime=' + encodeURIComponent(file.type || ''), {
        method: 'POST',
        credentials: 'same-origin',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: file
      }).then(function (r) { return r.json(); }).then(function (j) {
        if (j && j.code === '0000') { say('已上传'); load(); }
        else { status(''); say((j && j.msg) || '上传失败', true); }
      }).catch(function () { status(''); say('上传失败', true); });
    }

    function del(id) {
      fetch('/admin/oa/attachments/delete', {
        method: 'POST',
        credentials: 'same-origin',
        headers: {
          'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8'
        },
        body: 'id=' + encodeURIComponent(id)
      }).then(function (r) { return r.json(); }).then(function (j) {
        if (j && j.code === '0000') { load(); }
        else { say((j && j.msg) || '删除失败', true); }
      }).catch(function () { say('删除失败', true); });
    }

    function wireInner() {
      var input = host.querySelector('[data-attach-file]');
      if (input) {
        input.addEventListener('change', function () {
          var f = input.files && input.files[0];
          input.value = '';
          upload(f);
        });
      }
      var dels = host.querySelectorAll('[data-attach-del]');
      for (var i = 0; i < dels.length; i++) {
        dels[i].addEventListener('click', function (ev) {
          ev.preventDefault();
          var id = this.getAttribute('data-attach-del');
          if (window.Layer) {
            Layer.confirm('删除该附件？', function () { del(id); });
          } else { del(id); }
        });
      }
    }

    load();
  }

  // 片段注入点：admin.js 在每次面板/弹窗内容就位后调用此钩子。
  var prev = window.applyFragmentWidgets;
  window.applyFragmentWidgets = function (root) {
    if (prev) { prev(root); }
    try { mount(root); } catch (e) { /* 附件区失败不拖累屏幕本身 */ }
  };

  document.addEventListener('DOMContentLoaded', function () { mount(document); });

  window.AttachBlob = { mount: mount };
})();
