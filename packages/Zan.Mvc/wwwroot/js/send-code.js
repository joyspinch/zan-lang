// “发送验证码”按钮：提交邮件并倒计时，无需框架。
document.addEventListener('click', function (ev) {
  var btn = ev.target.closest('[data-send-code]');
  if (!btn) { return; }
  var form = btn.closest('form');
  var mail = form.querySelector('[name=email]');
  if (!mail || !mail.value) { mail.focus(); return; }
  btn.disabled = true;
  var body = new URLSearchParams({ email: mail.value });
  fetch(btn.getAttribute('data-send-code'), {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: body.toString()
  }).then(function (r) { return r.json(); }).then(function (j) {
    var ok = j && j.code === '0000';
    var note = form.querySelector('.flash') || document.createElement('p');
    note.className = 'flash ' + (ok ? 'ok' : 'bad');
    note.textContent = (j && j.msg) || (ok ? '验证码已发送' : '发送失败');
    form.prepend(note);
    if (!ok) { btn.disabled = false; return; }
    var left = 60;
    btn.textContent = left + ' 秒后重发';
    var t = setInterval(function () {
      left = left - 1;
      btn.textContent = left + ' 秒后重发';
      if (left <= 0) { clearInterval(t); btn.disabled = false; btn.textContent = '获取验证码'; }
    }, 1000);
  }).catch(function () { btn.disabled = false; });
});
