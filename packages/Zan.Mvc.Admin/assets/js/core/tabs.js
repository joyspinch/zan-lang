/**
 * Zan Admin Micro-Kernel: Tabs
 * 现代化多标签工作区管理器 (动态开闭、上下文菜单、记忆与通信)
 */
(function (global) {
    'use strict';

    class TabManager {
        constructor() {
            this._tabs = [];
            this._activeId = null;
            this._headerEl = null;
            this._contentEl = null;
        }

        init(headerSelector = '#admin-tabs-header', contentSelector = '#admin-tabs-content') {
            this._headerEl = document.querySelector(headerSelector);
            this._contentEl = document.querySelector(contentSelector);
            if (!this._headerEl || !this._contentEl) return;

            // 打开首页默认标签
            this.open({ id: 'dashboard', title: '工作台', url: '/admin/dashboard', closable: false });
        }

        open({ id, title, url, closable = true }) {
            const existing = this._tabs.find(t => t.id === id);
            if (existing) {
                this.switch(id);
                return;
            }

            const tab = { id, title, url, closable };
            this._tabs.push(tab);

            // 渲染 Tab Header
            const tabHeader = document.createElement('div');
            tabHeader.className = 'zan-tab-item';
            tabHeader.dataset.id = id;
            tabHeader.style.cssText = 'display:flex;align-items:center;gap:8px;padding:8px 16px;background:#1e293b;border:1px solid #334155;border-bottom:none;border-radius:6px 6px 0 0;font-size:13px;cursor:pointer;user-select:none;color:#94a3b8;transition:all .15s ease;';
            tabHeader.innerHTML = `
                <span class="tab-title">${title}</span>
                ${closable ? '<span class="tab-close" style="font-size:14px;color:#64748b;line-height:1;margin-left:4px;">&times;</span>' : ''}
            `;

            tabHeader.onclick = (e) => {
                if (e.target.classList.contains('tab-close')) {
                    this.close(id);
                } else {
                    this.switch(id);
                }
            };
            this._headerEl.appendChild(tabHeader);

            // 渲染 Tab IFrame 内容
            const iframe = document.createElement('iframe');
            iframe.id = 'tab-frame-' + id;
            iframe.src = url;
            iframe.style.cssText = 'width:100%;height:100%;border:none;display:none;';
            this._contentEl.appendChild(iframe);

            this.switch(id);
        }

        switch(id) {
            this._activeId = id;
            // 更新 Tab 头部状态
            const headers = this._headerEl.querySelectorAll('.zan-tab-item');
            headers.forEach(h => {
                const isActive = h.dataset.id === id;
                h.style.background = isActive ? '#0f172a' : '#1e293b';
                h.style.color = isActive ? '#38bdf8' : '#94a3b8';
                h.style.borderTop = isActive ? '2px solid #38bdf8' : '1px solid #334155';
            });

            // 更新 Iframe 内容显隐
            const frames = this._contentEl.querySelectorAll('iframe');
            frames.forEach(f => {
                f.style.display = f.id === 'tab-frame-' + id ? 'block' : 'none';
            });

            if (global.ZanBus) {
                global.ZanBus.emit('tab:switched', id);
            }
        }

        close(id) {
            const index = this._tabs.findIndex(t => t.id === id);
            if (index === -1) return;

            const [removed] = this._tabs.splice(index, 1);
            const header = this._headerEl.querySelector(`.zan-tab-item[data-id="${id}"]`);
            if (header) header.remove();

            const frame = document.getElementById('tab-frame-' + id);
            if (frame) frame.remove();

            // 若关闭的是当前选中的 Tab，则激活前一个或后一个
            if (this._activeId === id) {
                const nextTab = this._tabs[index] || this._tabs[index - 1];
                if (nextTab) {
                    this.switch(nextTab.id);
                }
            }
        }
    }

    global.ZanTabs = new TabManager();
})(window);
