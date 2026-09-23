/**
 * Zan Admin Micro-Kernel: Boot Assembly (< 60 行)
 * 全局声明式动作委托与模块装配
 */
(function (global) {
    'use strict';

    document.addEventListener('DOMContentLoaded', () => {
        // 初始化标签页系统
        if (global.ZanTabs) {
            global.ZanTabs.init();
        }

        // 全局事件代理：声明式 data-action 路由
        document.body.addEventListener('click', (e) => {
            const target = e.target.closest('[data-action]');
            if (!target) return;

            const action = target.dataset.action;
            const url = target.dataset.url || target.getAttribute('href');
            const title = target.dataset.title || target.textContent.trim();

            if (action === 'tab') {
                e.preventDefault();
                const id = target.dataset.id || url.replace(/[^a-zA-Z0-9]/g, '_');
                global.ZanTabs.open({ id, title, url });
            } else if (action === 'dialog') {
                e.preventDefault();
                global.ZanLayer.dialog({
                    title,
                    url,
                    width: target.dataset.width || '720px',
                    height: target.dataset.height || '500px'
                });
            } else if (action === 'post') {
                e.preventDefault();
                const confirmMsg = target.dataset.confirm;
                const executePost = () => {
                    fetch(url, { method: 'POST', headers: { 'X-Requested-With': 'XMLHttpRequest' } })
                        .then(r => r.json())
                        .then(j => {
                            if (j.code === '0000') {
                                global.ZanLayer.toast(j.msg || '操作成功', 'success');
                                if (global.ZanBus) global.ZanBus.emit('grid:reload');
                            } else {
                                global.ZanLayer.toast(j.msg || '操作失败', 'error');
                            }
                        });
                };

                if (confirmMsg) {
                    global.ZanLayer.confirm(confirmMsg, executePost);
                } else {
                    executePost();
                }
            }
        });
    });
})(window);
