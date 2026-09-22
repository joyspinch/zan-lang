<script setup>
import { ref, onMounted } from 'vue';
import TitleBar from './components/TitleBar.vue';

const count = ref(0);
const sysInfo = ref(null);
const loading = ref(false);
const logs = ref([]);
const updateInfo = ref(null);
const checkingUpdate = ref(false);
const updating = ref(false);

function addLog(type, text) {
  const time = new Date().toLocaleTimeString();
  logs.value.unshift({ id: Date.now() + Math.random(), time, type, text });
  if (logs.value.length > 50) logs.value.pop();
}

async function fetchSystemInfo() {
  loading.value = true;
  addLog('call', '调用原生 RPC: getSystemInfo...');
  try {
    if (window.zan && window.zan.invoke) {
      const info = await window.zan.invoke('getSystemInfo', {});
      sysInfo.value = info;
      addLog('success', `原生返回成功: OS=${info.os}, Ver=${info.version || '1.0.0'}`);
    } else {
      addLog('warn', '未检测到原生 Zan 桥接 (浏览器独立预览模式)');
      sysInfo.value = {
        os: 'Web Preview',
        version: '1.0.0',
        cpuCores: navigator.hardwareConcurrency || 4,
        engine: 'Standard Browser',
        security: 'Simulated Mode'
      };
    }
  } catch (err) {
    addLog('error', `调用失败: ${err.message}`);
  } finally {
    loading.value = false;
  }
}

async function checkAppUpdate() {
  checkingUpdate.value = true;
  addLog('call', '正在调用原生 RPC: checkUpdate...');
  try {
    if (window.zan && window.zan.invoke) {
      const res = await window.zan.invoke('checkUpdate', {});
      updateInfo.value = res;
      addLog('success', `检查更新返回: 发现新版本 v${res.latestVersion}`);
    } else {
      updateInfo.value = {
        hasUpdate: true,
        latestVersion: '1.1.0',
        releaseDate: '2026-09-21',
        changelog: '1. 跨平台二进制自更新平滑接力\n2. 内存解密性能大幅提升\n3. Vue 3 界面优化',
        downloadUrl: 'https://example.com/download/latest'
      };
      addLog('info', '模拟检查更新: 发现最新测试版本 v1.1.0');
    }
  } catch (err) {
    addLog('error', `检查更新失败: ${err.message}`);
  } finally {
    checkingUpdate.value = false;
  }
}

async function applyAppUpdate() {
  updating.value = true;
  addLog('call', '请求原生执行跨平台原子更新并重启...');
  try {
    if (window.zan && window.zan.invoke) {
      const res = await window.zan.invoke('applyUpdate', {});
      addLog('success', `更新响应: ${JSON.stringify(res)}`);
    } else {
      addLog('info', '模拟执行更新: Windows 重命名/POSIX 原子替换机制就绪');
      alert('已触发跨平台平滑自替换与重启！');
    }
  } catch (err) {
    addLog('error', `更新失败: ${err.message}`);
  } finally {
    updating.value = false;
  }
}

async function sendGreeting() {
  addLog('call', '调用原生 RPC: greet...');
  try {
    if (window.zan && window.zan.invoke) {
      const res = await window.zan.invoke('greet', { name: 'Vue 3 Developer' });
      addLog('success', `原生问候返回: ${res.message}`);
    } else {
      addLog('info', '问候: Hello from Mock Browser!');
    }
  } catch (err) {
    addLog('error', `调用失败: ${err.message}`);
  }
}

onMounted(() => {
  addLog('system', 'Vue 3 桌面应用已挂载，双向 IPC 桥就绪');
  fetchSystemInfo();

  if (window.zan && window.zan.on) {
    window.zan.on('system_event', (data) => {
      addLog('event', `收到 Zan 宿主推送事件: ${JSON.stringify(data)}`);
    });
  }
});
</script>

<template>
  <div class="app-shell">
    <!-- 自定义无边框原生标题栏组件 -->
    <TitleBar />

    <!-- 主窗口内容区 -->
    <main class="main-content">
      <div class="hero-section">
        <div class="vue-logo-badge">
          <svg class="vue-logo" viewBox="0 0 128 128" width="38" height="38">
            <path fill="#42b883" d="M78.8,10L64,35.4L49.2,10H0l64,110l64-110H78.8z"/>
            <path fill="#35495e" d="M78.8,10L64,35.4L49.2,10H25.6L64,76l38.4-66H78.8z"/>
          </svg>
          <div class="hero-text">
            <h2>Vue 3 现代化跨平台轻量桌面应用</h2>
            <p>Zan AOT 机器码宿主 · 内存加密流式微服务 · 跨平台平滑自更新 · 零临时文件</p>
          </div>
        </div>
      </div>

      <div class="grid-container">
        <!-- 响应式状态与 RPC 卡片 -->
        <div class="card interactive-card">
          <div class="card-header">
            <span class="card-tag">Vue 响应式与原生交互</span>
            <h3>Native RPC 与业务</h3>
          </div>

          <div class="counter-box">
            <span class="counter-label">Vue 响应式计数器:</span>
            <span class="counter-value">{{ count }}</span>
            <div class="btn-group">
              <button class="btn primary" @click="count++">增加计数</button>
              <button class="btn secondary" @click="count = 0">重置</button>
            </div>
          </div>

          <div class="action-buttons">
            <button class="btn accent" :disabled="loading" @click="fetchSystemInfo">
              {{ loading ? '查询中...' : '刷新底层指标' }}
            </button>
            <button class="btn outline" @click="sendGreeting">发送原生问候</button>
          </div>
        </div>

        <!-- 原生系统监控与安全指标卡片 -->
        <div class="card system-card">
          <div class="card-header">
            <span class="card-tag">Zan 原生机器码宿主</span>
            <h3>运行环境与安全指标</h3>
          </div>

          <div v-if="sysInfo" class="info-list">
            <div class="info-row">
              <span class="info-label">运行平台</span>
              <span class="info-val highlight">{{ sysInfo.os }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">程序版本</span>
              <span class="info-val">v{{ sysInfo.version || '1.0.0' }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">核心引擎</span>
              <span class="info-val highlight">{{ sysInfo.engine }}</span>
            </div>
            <div class="info-row">
              <span class="info-label">防逆向状态</span>
              <span class="info-val badge-safe">{{ sysInfo.security }}</span>
            </div>
          </div>
          <div v-else class="loading-box">读取底层指标中...</div>
        </div>
      </div>

      <!-- 版本更新管理卡片 -->
      <div class="card update-card">
        <div class="update-header">
          <div>
            <span class="card-tag">跨平台版本更新机制</span>
            <h3 style="margin-top:2px; font-size:14px;">自更新与热更新管理</h3>
          </div>
          <button class="btn accent" :disabled="checkingUpdate" @click="checkAppUpdate">
            {{ checkingUpdate ? '正在检查...' : '检查版本更新' }}
          </button>
        </div>

        <div v-if="updateInfo" class="update-details">
          <div class="update-badge">发现新版本: v{{ updateInfo.latestVersion }} ({{ updateInfo.releaseDate }})</div>
          <pre class="update-log">{{ updateInfo.changelog }}</pre>
          <div class="update-actions">
            <button class="btn primary" :disabled="updating" @click="applyAppUpdate">
              {{ updating ? '正在替换重启...' : '立即应用更新并重启' }}
            </button>
          </div>
        </div>
        <div v-else class="update-hint">
          <span>跨平台兼容：Windows 下通过独占锁绕过重命名接力更新；Linux / macOS 下通过 POSIX inode 原子覆盖更新。</span>
        </div>
      </div>

      <!-- 实时 IPC 日志控制台 -->
      <div class="card logs-card">
        <div class="logs-header">
          <span>IPC 双向通信与事件监控</span>
          <button class="clear-btn" @click="logs = []">清空日志</button>
        </div>
        <div class="logs-body">
          <div v-for="log in logs" :key="log.id" :class="['log-line', log.type]">
            <span class="log-time">[{{ log.time }}]</span>
            <span class="log-text">{{ log.text }}</span>
          </div>
        </div>
      </div>
    </main>
  </div>
</template>

<style>
/* 全局基础重置与深色主题 */
* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
  background: #141517;
  color: #e9ecef;
  overflow: hidden;
  user-select: none;
}

.app-shell {
  display: flex;
  flex-direction: column;
  height: 100vh;
  width: 100vw;
  background: #141517;
}

.main-content {
  flex: 1;
  overflow-y: auto;
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.hero-section {
  padding: 4px 0;
}

.vue-logo-badge {
  display: flex;
  align-items: center;
  gap: 12px;
}

.hero-text h2 {
  font-size: 16px;
  font-weight: 600;
  color: #f8f9fa;
}

.hero-text p {
  font-size: 11px;
  color: #909296;
  margin-top: 2px;
}

.grid-container {
  display: grid;
  grid-template-columns: 1.1fr 0.9fr;
  gap: 12px;
}

.card {
  background: #1a1b1e;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  padding: 14px;
}

.card-header {
  margin-bottom: 10px;
}

.card-tag {
  font-size: 10px;
  text-transform: uppercase;
  color: #42b883;
  letter-spacing: 0.5px;
  font-weight: 600;
}

.card-header h3 {
  font-size: 14px;
  font-weight: 600;
  color: #f1f3f5;
  margin-top: 2px;
}

.counter-box {
  display: flex;
  align-items: center;
  gap: 12px;
  background: rgba(255, 255, 255, 0.03);
  padding: 8px 12px;
  border-radius: 6px;
  margin-bottom: 10px;
}

.counter-label {
  font-size: 12px;
  color: #ced4da;
}

.counter-value {
  font-size: 16px;
  font-weight: 700;
  color: #42b883;
  min-width: 24px;
}

.btn-group {
  margin-left: auto;
  display: flex;
  gap: 6px;
}

.action-buttons {
  display: flex;
  gap: 8px;
}

.btn {
  padding: 6px 12px;
  border-radius: 5px;
  font-size: 11px;
  font-weight: 500;
  cursor: pointer;
  border: none;
  transition: all 0.15s;
}

.btn.primary {
  background: #42b883;
  color: #141517;
}

.btn.secondary {
  background: rgba(255, 255, 255, 0.08);
  color: #e9ecef;
}

.btn.accent {
  background: #3b82f6;
  color: #fff;
}

.btn.outline {
  background: transparent;
  border: 1px solid rgba(255, 255, 255, 0.15);
  color: #ced4da;
}

.btn:hover {
  filter: brightness(1.1);
}

.info-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.info-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 12px;
  padding-bottom: 4px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.04);
}

.info-label {
  color: #868e96;
}

.info-val {
  color: #dee2e6;
  font-family: monospace;
}

.info-val.highlight {
  color: #60a5fa;
}

.badge-safe {
  background: rgba(66, 184, 131, 0.15);
  color: #42b883;
  padding: 2px 6px;
  border-radius: 4px;
  font-size: 10px;
}

.update-card {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.update-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.update-details {
  background: rgba(59, 130, 246, 0.08);
  border: 1px solid rgba(59, 130, 246, 0.2);
  border-radius: 6px;
  padding: 10px 12px;
  margin-top: 6px;
}

.update-badge {
  font-size: 12px;
  font-weight: 600;
  color: #60a5fa;
}

.update-log {
  font-family: inherit;
  font-size: 11px;
  color: #cbd5e1;
  margin: 6px 0;
  white-space: pre-wrap;
}

.update-hint {
  font-size: 11px;
  color: #94a3b8;
  line-height: 1.5;
}

.logs-card {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 110px;
}

.logs-header {
  display: flex;
  justify-content: space-between;
  font-size: 11px;
  color: #adb5bd;
  margin-bottom: 6px;
}

.clear-btn {
  background: transparent;
  border: none;
  color: #6c757d;
  cursor: pointer;
  font-size: 10px;
}

.clear-btn:hover {
  color: #f87171;
}

.logs-body {
  flex: 1;
  background: #101113;
  border-radius: 4px;
  padding: 6px 10px;
  overflow-y: auto;
  font-family: "Cascadia Code", Consolas, monospace;
  font-size: 11px;
}

.log-line {
  line-height: 1.5;
  color: #ced4da;
}

.log-time {
  color: #495057;
  margin-right: 8px;
}

.log-line.call { color: #93c5fd; }
.log-line.success { color: #86efac; }
.log-line.event { color: #fde047; }
.log-line.error { color: #fca5a5; }
</style>
