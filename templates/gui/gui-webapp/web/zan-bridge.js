/**
 * zan-bridge.js
 * Zan WebApp 官方前后端双向通信桥与原生能力驱动库
 */
(function (global) {
    let rpcSeq = 1;
    const pendingRequests = new Map();
    const eventListeners = new Map();

    // 核心发送机制：经由 WebView2 原生消息通道向后端投递 JSON
    function sendNative(data) {
        const jsonStr = typeof data === 'string' ? data : JSON.stringify(data);
        if (window.chrome && window.chrome.webview && window.chrome.webview.postMessage) {
            window.chrome.webview.postMessage(jsonStr);
        } else if (window.zanNative && window.zanNative.postMessage) {
            window.zanNative.postMessage(jsonStr);
        } else {
            console.warn('[ZanBridge] Native messaging channel unavailable in current context:', data);
        }
    }

    // 后端 RPC 异步响应回调
    global.__zan_rpc_resolve = function (id, result) {
        if (pendingRequests.has(id)) {
            const [resolve, reject] = pendingRequests.get(id);
            pendingRequests.delete(id);
            if (result && result.error) {
                reject(new Error(result.error));
            } else {
                resolve(result);
            }
        }
    };

    // 后端主动推送事件触发
    global.__zan_event_dispatch = function (eventName, eventData) {
        if (eventListeners.has(eventName)) {
            const handlers = eventListeners.get(eventName);
            handlers.forEach(fn => {
                try { fn(eventData); } catch (e) { console.error('[ZanBridge] Event handler failed:', e); }
            });
        }
    };

    const zan = {
        // 双向 RPC 调用
        invoke: function (method, params) {
            return new Promise((resolve, reject) => {
                const id = rpcSeq++;
                pendingRequests.set(id, [resolve, reject]);
                sendNative({
                    id: id,
                    method: method,
                    params: params || {}
                });
            });
        },

        // 事件监听
        on: function (eventName, callback) {
            if (!eventListeners.has(eventName)) {
                eventListeners.set(eventName, []);
            }
            eventListeners.get(eventName).push(callback);
        },

        // 窗口系统级能力控制
        window: {
            minimize: function () {
                sendNative({ type: "window", action: "minimize" });
            },
            maximize: function () {
                sendNative({ type: "window", action: "maximize" });
            },
            restore: function () {
                sendNative({ type: "window", action: "restore" });
            },
            toggleMaximize: function () {
                sendNative({ type: "window", action: "toggleMaximize" });
            },
            close: function () {
                sendNative({ type: "window", action: "close" });
            },
            startDrag: function () {
                sendNative({ type: "window", action: "drag" });
            },
            isMaximized: function () {
                return zan.invoke("__get_window_state").then(function (res) {
                    return !!(res && res.isMaximized);
                }).catch(function () {
                    return false;
                });
            }
        },

        // 兼容原模板直属函数
        windowMinimize: function () { this.window.minimize(); },
        windowMaximize: function () { this.window.maximize(); },
        windowRestore: function () { this.window.restore(); },
        windowToggleMaximize: function () { this.window.toggleMaximize(); },
        windowClose: function () { this.window.close(); },
        startDrag: function () { this.window.startDrag(); }
    };

    // 监听带有 data-zan-drag 属性的区域并实现原生零延迟拖拽
    document.addEventListener("mousedown", function (e) {
        if (e.button !== 0) return; // 仅限鼠标左键
        const dragArea = e.target.closest("[data-zan-drag]");
        if (dragArea) {
            // 排除表单输入框、按钮等需要鼠标交互的元素
            const tag = e.target.tagName.toLowerCase();
            if (tag === "button" || tag === "input" || tag === "select" || tag === "textarea" || tag === "a" || e.target.closest("button, input, select, textarea, a, [data-no-drag]")) {
                return;
            }
            zan.window.startDrag();
        }
    });

    // 双击标题栏最大化 / 还原
    document.addEventListener("dblclick", function (e) {
        if (e.button !== 0) return;
        const dragArea = e.target.closest("[data-zan-drag]");
        if (dragArea) {
            if (e.target.closest("button, input, select, textarea, a, [data-no-drag]")) return;
            zan.window.toggleMaximize();
        }
    });

    global.zan = zan;
})(window);
