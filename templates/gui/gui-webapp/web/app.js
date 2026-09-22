// 页面业务逻辑
document.addEventListener("DOMContentLoaded", function () {
    const logOutput = document.getElementById("logOutput");

    function appendLog(msg, tag = "INFO") {
        const item = document.createElement("div");
        item.className = "log-item";
        const now = new Date();
        const timeStr = now.toTimeString().split(" ")[0] + "." + String(now.getMilliseconds()).padStart(3, "0");
        item.innerHTML = `<span class="time">[${timeStr}] [${tag}]</span> ${escapeHtml(msg)}`;
        logOutput.appendChild(item);
        logOutput.scrollTop = logOutput.scrollHeight;
    }

    function escapeHtml(str) {
        return String(str)
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;");
    }

    // 1. 绑定窗口控制按钮
    const btnMax = document.getElementById("btnMax");
    const iconMax = document.getElementById("iconMax");
    const iconRestore = document.getElementById("iconRestore");

    function setMaximizedState(isMax) {
        if (iconMax && iconRestore) {
            iconMax.style.display = isMax ? "none" : "block";
            iconRestore.style.display = isMax ? "block" : "none";
        }
        if (btnMax) {
            btnMax.title = isMax ? "还原" : "最大化";
        }
    }

    document.getElementById("btnMin").addEventListener("click", () => {
        appendLog("点击最小化窗口", "WIN");
        zan.window.minimize();
    });

    if (btnMax) {
        btnMax.addEventListener("click", () => {
            appendLog("点击最大化/还原切换", "WIN");
            zan.window.toggleMaximize();
        });
    }

    document.getElementById("btnClose").addEventListener("click", () => {
        appendLog("点击关闭窗口", "WIN");
        zan.window.close();
    });

    // 初始化窗口状态并监听状态变动
    if (window.zan && window.zan.window && window.zan.window.isMaximized) {
        window.zan.window.isMaximized().then(setMaximizedState);
    }
    if (window.zan && window.zan.on) {
        window.zan.on("window_state_changed", function (state) {
            const isMax = (typeof state === "boolean") ? state : !!(state && state.isMaximized);
            setMaximizedState(isMax);
            appendLog(`窗口状态更新: ${isMax ? "最大化" : "常规窗口"}`, "WIN");
        });
    }
    window.addEventListener("zan:window-state-changed", function (e) {
        if (e && e.detail) {
            setMaximizedState(!!e.detail.isMaximized);
        }
    });

    document.getElementById("btnClearLog").addEventListener("click", () => {
        logOutput.innerHTML = "";
    });

    // 2. 原生 RPC 调用演示
    document.getElementById("btnGetSysInfo").addEventListener("click", async () => {
        appendLog("正在请求原生系统信息: zan.invoke('getSystemInfo')...", "RPC");
        try {
            const res = await zan.invoke("getSystemInfo", {});
            appendLog("系统信息返回: " + JSON.stringify(res), "SUCCESS");
        } catch (err) {
            appendLog("RPC 失败: " + err.message, "ERROR");
        }
    });

    document.getElementById("btnCalcSecret").addEventListener("click", async () => {
        const payload = { input: "ZanNativeDesktopSecretData", rounds: 1000 };
        appendLog("请求原生安全算法计算: zan.invoke('calculateHash', ...)", "RPC");
        try {
            const res = await zan.invoke("calculateHash", payload);
            appendLog("原生哈希计算结果: " + JSON.stringify(res), "SUCCESS");
        } catch (err) {
            appendLog("RPC 失败: " + err.message, "ERROR");
        }
    });

    document.getElementById("btnTriggerEvent").addEventListener("click", async () => {
        appendLog("触发宿主后台任务并等待事件通知...", "EVENT");
        try {
            await zan.invoke("startBackgroundTask", { durationMs: 2000 });
            appendLog("后台任务已在 Zan 宿主启动，等待完成事件推送...", "TASK");
        } catch (err) {
            appendLog("触发任务失败: " + err.message, "ERROR");
        }
    });

    // 3. 监听原生推送事件
    zan.on("task_completed", (data) => {
        appendLog("收到宿主主动事件推送 [task_completed]: " + JSON.stringify(data), "EVENT");
    });
});
