/**
 * Zan Admin Micro-Kernel: Layer
 * 轻量化浮层与通知管理器 (Toast, Confirm, Dialog, Drawer)
 */
(function (global) {
    'use strict';

    class LayerManager {
        constructor() {
            this._stack = [];
            this._ensureContainer();
        }

        _ensureContainer() {
            if (!document.getElementById('zan-layer-container')) {
                const el = document.createElement('div');
                el.id = 'zan-layer-container';
                el.style.cssText = 'position:fixed;top:20px;right:20px;z-index:99999;display:flex;flex-direction:column;gap:10px;pointer-events:none;';
                document.body.appendChild(el);
            }
        }

        toast(message, type = 'info', duration = 3000) {
            this._ensureContainer();
            const container = document.getElementById('zan-layer-container');
            const el = document.createElement('div');
            el.className = 'zan-toast zan-toast-' + type;
            el.style.cssText = 'pointer-events:auto;min-width:240px;padding:12px 18px;border-radius:8px;font-size:13px;font-weight:500;box-shadow:0 8px 24px rgba(0,0,0,0.15);transition:all .3s cubic-bezier(0.16,1,0.3,1);transform:translateX(50px);opacity:0;display:flex;align-items:center;gap:8px;';
            
            let bg = '#1e293b', fg = '#fff', border = '#334155';
            if (type === 'success') { bg = '#064e3b'; fg = '#34d399'; border = '#059669'; }
            else if (type === 'error') { bg = '#450a0a'; fg = '#f87171'; border = '#dc2626'; }
            else if (type === 'warning') { bg = '#451a03'; fg = '#fbbf24'; border = '#d97706'; }

            el.style.backgroundColor = bg;
            el.style.color = fg;
            el.style.border = '1px solid ' + border;
            el.textContent = message;

            container.appendChild(el);
            requestAnimationFrame(() => {
                el.style.transform = 'translateX(0)';
                el.style.opacity = '1';
            });

            setTimeout(() => {
                el.style.transform = 'translateX(50px)';
                el.style.opacity = '0';
                setTimeout(() => el.remove(), 300);
            }, duration);
        }

        confirm(message, onConfirm, title = '系统提示') {
            const mask = document.createElement('div');
            mask.className = 'zan-layer-mask';
            mask.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.6);backdrop-filter:blur(2px);z-index:99990;display:flex;align-items:center;justify-content:center;opacity:0;transition:opacity .2s ease;';

            const box = document.createElement('div');
            box.style.cssText = 'background:#1e293b;border:1px solid #334155;border-radius:12px;width:90%;max-width:400px;padding:24px;box-shadow:0 20px 40px rgba(0,0,0,0.5);transform:scale(0.95);transition:transform .2s ease;color:#e2e8f0;';

            box.innerHTML = `
                <h3 style="margin:0 0 12px;font-size:16px;color:#f8fafc;">${title}</h3>
                <p style="margin:0 0 20px;font-size:14px;color:#94a3b8;line-height:1.5;">${message}</p>
                <div style="display:flex;justify-content:flex-end;gap:10px;">
                    <button class="btn-cancel" style="background:#334155;color:#e2e8f0;border:none;border-radius:6px;padding:8px 16px;font-size:13px;cursor:pointer;">取消</button>
                    <button class="btn-ok" style="background:#3b82f6;color:#fff;border:none;border-radius:6px;padding:8px 16px;font-size:13px;font-weight:600;cursor:pointer;">确定</button>
                </div>
            `;

            mask.appendChild(box);
            document.body.appendChild(mask);

            requestAnimationFrame(() => {
                mask.style.opacity = '1';
                box.style.transform = 'scale(1)';
            });

            const close = () => {
                mask.style.opacity = '0';
                box.style.transform = 'scale(0.95)';
                setTimeout(() => mask.remove(), 200);
            };

            box.querySelector('.btn-cancel').onclick = close;
            box.querySelector('.btn-ok').onclick = () => {
                close();
                if (typeof onConfirm === 'function') onConfirm();
            };
        }

        open(options) {
            return this.dialog(options);
        }

        dialog(options) {
            const { title = '窗口', url, width = '720px', height = '500px', onComplete } = options;
            const mask = document.createElement('div');
            mask.className = 'zan-layer-mask';
            mask.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.6);backdrop-filter:blur(2px);z-index:99980;display:flex;align-items:center;justify-content:center;';

            const modal = document.createElement('div');
            modal.style.cssText = `background:#1e293b;border:1px solid #334155;border-radius:12px;width:${width};height:${height};display:flex;flex-direction:column;box-shadow:0 25px 50px rgba(0,0,0,0.5);overflow:hidden;`;

            modal.innerHTML = `
                <div style="display:flex;justify-content:space-between;align-items:center;padding:14px 20px;border-bottom:1px solid #334155;background:#0f172a;">
                    <span style="font-weight:600;font-size:15px;color:#f8fafc;">${title}</span>
                    <button class="btn-close" style="background:none;border:none;color:#94a3b8;font-size:20px;cursor:pointer;line-height:1;">&times;</button>
                </div>
                <div style="flex:1;position:relative;background:#0f172a;">
                    <iframe src="${url}" style="width:100%;height:100%;border:none;" frameborder="0"></iframe>
                </div>
            `;

            mask.appendChild(modal);
            document.body.appendChild(mask);

            const close = () => {
                mask.remove();
                if (typeof onComplete === 'function') onComplete();
            };

            modal.querySelector('.btn-close').onclick = close;
            return { close };
        }
    }

    global.ZanLayer = new LayerManager();
})(window);
