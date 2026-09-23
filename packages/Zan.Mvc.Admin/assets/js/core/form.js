/**
 * Zan Admin Micro-Kernel: Form
 * 声明式表单处理引擎 (AJAX 提交、防抖、自动校验、统一结果处理)
 */
(function (global) {
    'use strict';

    class FormEngine {
        bind(formSelector, options = {}) {
            const form = typeof formSelector === 'string' ? document.querySelector(formSelector) : formSelector;
            if (!form) return;

            form.addEventListener('submit', (e) => {
                e.preventDefault();
                this.submit(form, options);
            });
        }

        async submit(form, options = {}) {
            const submitBtn = form.querySelector('[type="submit"]') || form.querySelector('.btn-submit');
            if (submitBtn && submitBtn.disabled) return;

            if (submitBtn) {
                submitBtn.disabled = true;
                submitBtn.dataset.originText = submitBtn.textContent;
                submitBtn.textContent = '提交中...';
            }

            try {
                const formData = new FormData(form);
                const url = options.url || form.action || window.location.href;
                const method = (options.method || form.method || 'POST').toUpperCase();

                let res;
                if (method === 'GET') {
                    const params = new URLSearchParams(formData).toString();
                    res = await fetch(`${url}?${params}`, {
                        headers: { 'X-Requested-With': 'XMLHttpRequest' }
                    });
                } else {
                    res = await fetch(url, {
                        method: 'POST',
                        body: formData,
                        headers: { 'X-Requested-With': 'XMLHttpRequest' }
                    });
                }

                const json = await res.json();
                if (json.code === '0000') {
                    if (global.ZanLayer) {
                        global.ZanLayer.toast(json.msg || '操作成功', 'success');
                    }
                    if (options.onSuccess) {
                        options.onSuccess(json);
                    } else if (global.ZanBus) {
                        global.ZanBus.emit('form:success', { form, data: json });
                    }
                } else {
                    if (global.ZanLayer) {
                        global.ZanLayer.toast(json.msg || '操作失败', 'error');
                    }
                    if (options.onError) options.onError(json);
                }
            } catch (err) {
                console.error('[FormEngine] Submit error', err);
                if (global.ZanLayer) {
                    global.ZanLayer.toast('网络通信异常，请重试', 'error');
                }
            } finally {
                if (submitBtn) {
                    submitBtn.disabled = false;
                    submitBtn.textContent = submitBtn.dataset.originText || '提交';
                }
            }
        }
    }

    global.ZanForm = new FormEngine();
})(window);
