<script setup>
import { ref, onMounted, onUnmounted } from 'vue';

const isMaximized = ref(false);

function updateWindowState(state) {
  if (typeof state === 'boolean') {
    isMaximized.value = state;
  } else if (state && typeof state.isMaximized === 'boolean') {
    isMaximized.value = state.isMaximized;
  }
}

function handleStateEvent(e) {
  if (e && e.detail) {
    updateWindowState(e.detail.isMaximized);
  }
}

onMounted(() => {
  if (window.zan && window.zan.window && window.zan.window.isMaximized) {
    window.zan.window.isMaximized().then(updateWindowState);
  }
  if (window.zan && window.zan.on) {
    window.zan.on('window_state_changed', updateWindowState);
  }
  window.addEventListener('zan:window-state-changed', handleStateEvent);
});

onUnmounted(() => {
  if (window.zan && window.zan.off) {
    window.zan.off('window_state_changed', updateWindowState);
  }
  window.removeEventListener('zan:window-state-changed', handleStateEvent);
});

function minimize() {
  if (window.zan && window.zan.window) {
    window.zan.window.minimize();
  }
}

function toggleMaximize() {
  if (window.zan && window.zan.window) {
    if (window.zan.window.toggleMaximize) {
      window.zan.window.toggleMaximize();
    } else {
      window.zan.window.maximize();
    }
  }
}

function close() {
  if (window.zan && window.zan.window) {
    window.zan.window.close();
  }
}
</script>

<template>
  <header class="titlebar" data-zan-drag @dblclick="toggleMaximize">
    <div class="titlebar-left">
      <div class="app-icon">⚡</div>
      <div class="app-name">Zan + Vue 3 桌面应用</div>
      <span class="badge">原生内嵌</span>
    </div>
    
    <div class="titlebar-center">
      <span class="tip-text">按住标题栏可拖拽 · 双击可切换最大化/还原</span>
    </div>

    <div class="titlebar-controls" data-no-drag>
      <button class="win-btn min" @click="minimize" title="最小化">
        <svg width="10" height="1" viewBox="0 0 10 1"><rect width="10" height="1" fill="currentColor"/></svg>
      </button>
      <button class="win-btn max" @click="toggleMaximize" :title="isMaximized ? '还原' : '最大化'">
        <svg v-if="!isMaximized" width="10" height="10" viewBox="0 0 10 10" fill="none">
          <rect x="0.5" y="0.5" width="9" height="9" stroke="currentColor"/>
        </svg>
        <svg v-else width="10" height="10" viewBox="0 0 10 10" fill="none">
          <rect x="2.5" y="0.5" width="7" height="7" stroke="currentColor"/>
          <path d="M0.5 2.5V9.5H7.5V7.5" stroke="currentColor"/>
        </svg>
      </button>
      <button class="win-btn close" @click="close" title="关闭">
        <svg width="10" height="10" viewBox="0 0 10 10"><path d="M1 1L9 9M9 1L1 9" stroke="currentColor" stroke-width="1.2"/></svg>
      </button>
    </div>
  </header>
</template>

<style scoped>
.titlebar {
  height: 38px;
  background: rgba(26, 27, 30, 0.95);
  backdrop-filter: blur(12px);
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 14px 0 16px;
  user-select: none;
  cursor: default;
  position: relative;
  z-index: 100;
}

.titlebar-left {
  display: flex;
  align-items: center;
  gap: 10px;
}

.app-icon {
  font-size: 14px;
}

.app-name {
  font-size: 13px;
  font-weight: 600;
  color: #f1f3f5;
  letter-spacing: 0.3px;
}

.badge {
  font-size: 10px;
  padding: 2px 6px;
  background: rgba(66, 184, 131, 0.15);
  color: #42b883;
  border: 1px solid rgba(66, 184, 131, 0.3);
  border-radius: 4px;
  font-weight: 500;
}

.titlebar-center {
  font-size: 11px;
  color: rgba(255, 255, 255, 0.35);
}

.titlebar-controls {
  display: flex;
  align-items: center;
  gap: 4px;
}

.win-btn {
  width: 28px;
  height: 24px;
  background: transparent;
  border: none;
  border-radius: 4px;
  color: #adb5bd;
  display: flex;
  align-items: center;
  justify-content: center;
  cursor: pointer;
  transition: all 0.15s ease;
}

.win-btn:hover {
  background: rgba(255, 255, 255, 0.1);
  color: #fff;
}

.win-btn.close:hover {
  background: #fa5252;
  color: #fff;
}
</style>
