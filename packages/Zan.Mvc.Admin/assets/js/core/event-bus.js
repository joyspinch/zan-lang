/**
 * Zan Admin Micro-Kernel: EventBus
 * 极简发布-订阅事件中枢，解耦标签页、弹窗、数据表格间的状态联动
 */
(function (global) {
    'use strict';

    class EventBus {
        constructor() {
            this._listeners = new Map();
        }

        on(event, callback) {
            if (!this._listeners.has(event)) {
                this._listeners.set(event, []);
            }
            this._listeners.get(event).push(callback);
            return () => this.off(event, callback);
        }

        once(event, callback) {
            const wrap = (...args) => {
                this.off(event, wrap);
                callback.apply(this, args);
            };
            return this.on(event, wrap);
        }

        off(event, callback) {
            if (!this._listeners.has(event)) return;
            if (!callback) {
                this._listeners.delete(event);
                return;
            }
            const list = this._listeners.get(event).filter(fn => fn !== callback);
            if (list.length === 0) {
                this._listeners.delete(event);
            } else {
                this._listeners.set(event, list);
            }
        }

        emit(event, ...args) {
            if (!this._listeners.has(event)) return;
            const handlers = [...this._listeners.get(event)];
            for (const fn of handlers) {
                try {
                    fn.apply(this, args);
                } catch (err) {
                    console.error('[EventBus] Error in listener for ' + event, err);
                }
            }
        }
    }

    global.ZanBus = new EventBus();
})(window);
