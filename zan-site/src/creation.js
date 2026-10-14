const creationInstances = new WeakMap();
const creationInterval = 5000;
const creationScenarios = {
  todo: {
    label: '待办清单', lead: '做一个可勾选的', title: '待办清单',
    intentions: ['记录灵感', '勾选完成'], appTitle: '我的待办', symbol: '✓',
    steps: [['先查组件', '找到清单与勾选组件'], ['连接状态', '勾选后更新完成数量'], ['编译与检查', '验证勾选与取消操作']],
  },
  csv: {
    label: 'CSV 汇总', lead: '把 CSV 数据变成', title: '一眼懂的汇总',
    intentions: ['读取表格', '合计金额'], appTitle: 'CSV 汇总', symbol: 'Σ',
    steps: [['读取表格', '解析标题与金额列'], ['汇总数据', '统计行数并合计金额'], ['编译与检查', '用样例核对汇总结果']],
  },
  api: {
    label: 'HTTP 健康检查', lead: '做一个 HTTP 服务', title: '健康检查工具',
    intentions: ['查看状态', '识别维护'], appTitle: 'HTTP 健康检查', symbol: '↗',
    steps: [['查找接口', '确认请求与响应能力'], ['识别状态', '区分健康与维护响应'], ['编译与检查', '用模拟响应验证提示']],
  },
};
const creationSamples = {
  small: { label: '文具采购', cents: [1200, 1800, 2400] },
  large: { label: '活动采购', cents: [4500, 3250, 2800, 1950] },
};
const creationSpark = `<svg class="creation-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="m12 3 2.6 6.4L21 12l-6.4 2.6L12 21l-2.6-6.4L3 12l6.4-2.6L12 3Z" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round"/></svg>`;
const creationArrow = `<svg class="creation-icon" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M5 12h14m-5-5 5 5-5 5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>`;

/** Mounts a local, illustrative demo. No compiler, network, or persistence is used. */
export function mountCreationDemo(root = document.getElementById('creation-demo')) {
  if (!root) return undefined;
  if (creationInstances.has(root)) return creationInstances.get(root);

  const events = new AbortController();
  const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const state = {
    scenario: 'todo', step: 0, autoplay: !reducedMotion.matches,
    inView: false, pageActive: true,
    tasks: [false, false], sample: 'small', healthy: true,
  };
  let timer = null;

  root.classList.add('creation-demo');
  root.innerHTML = `
    <section class="creation-shell" aria-label="从想法到应用" aria-describedby="creation-disclaimer">
      <header class="creation-header">
        <span class="creation-brand">${creationSpark}<span>让想法发生</span></span>
        <button class="creation-playback" type="button">
          <span class="creation-playback-icon" aria-hidden="true"></span>
          <span class="creation-playback-label"></span>
        </button>
      </header>

      <label class="creation-scenario-picker"><span>试个想法</span>
        <select class="creation-select creation-scenario" aria-label="选择创作场景">
          ${Object.entries(creationScenarios).map(([key, scenario]) => `<option value="${key}">${scenario.label}</option>`).join('')}
        </select>
      </label>

      <div class="creation-tabs" role="tablist" aria-label="创作演示步骤">
        ${['想法', 'AI 创作', '应用'].map((label, index) => `
          <button class="creation-tab" type="button" role="tab"
            id="creation-tab-${index}" aria-controls="creation-panel-${index}"
            aria-selected="${index === 0}" tabindex="${index === 0 ? 0 : -1}">
            <span class="creation-tab-number" aria-hidden="true">${index + 1}</span>
            <span>${label}</span>
            <span class="creation-tab-progress" aria-hidden="true"></span>
          </button>`).join('')}
      </div>

      <div class="creation-stage">
        <div class="creation-aura" aria-hidden="true"></div>
        <div class="creation-panel" id="creation-panel-0" role="tabpanel"
          aria-labelledby="creation-tab-0" tabindex="0">
          <div class="creation-idea creation-surface">
            <div class="creation-card-kicker"><span class="creation-signal" aria-hidden="true"></span>从一句话开始</div>
            <p class="creation-prompt"><span class="creation-prompt-lead"></span><br><strong><span class="creation-prompt-title"></span><span class="creation-caret" aria-hidden="true"></span></strong></p>
            <div class="creation-intentions"></div>
            <button class="creation-next" type="button" data-creation-next="1">看看 AI 怎么做 ${creationArrow}</button>
          </div>
        </div>

        <div class="creation-panel" id="creation-panel-1" role="tabpanel"
          aria-labelledby="creation-tab-1" tabindex="0" hidden>
          <div class="creation-process creation-surface">
            <div class="creation-process-heading"><h3>AI 会怎样实现</h3><span class="creation-badge">流程示意</span></div>
            <ol class="creation-evidence-list"></ol>
            <button class="creation-text-next" type="button" data-creation-next="2">试试应用预览 ${creationArrow}</button>
          </div>
        </div>

        <div class="creation-panel" id="creation-panel-2" role="tabpanel"
          aria-labelledby="creation-tab-2" tabindex="0" hidden>
          <div class="creation-app creation-surface">
            <div class="creation-app-chrome">
              <span class="creation-app-title"><span class="creation-app-symbol" aria-hidden="true"></span><span class="creation-app-name"></span></span>
              <span class="creation-app-tag">交互预览</span>
            </div>
            <div class="creation-app-body" data-creation-preview="todo">
              <h3>今天，轻松一点。</h3>
              <div class="creation-completion"><span class="creation-count" role="status" aria-atomic="true"></span><span class="creation-completion-line" aria-hidden="true"><span></span></span></div>
              <div class="creation-tasks" role="group" aria-label="试着勾选两件小事">
                <label class="creation-task"><input class="creation-checkbox" type="checkbox"><span class="creation-check" aria-hidden="true"></span><span class="creation-task-text">记下第一个灵感</span></label>
                <label class="creation-task"><input class="creation-checkbox" type="checkbox"><span class="creation-check" aria-hidden="true"></span><span class="creation-task-text">试试勾选完成</span></label>
              </div>
              <p class="creation-app-hint"></p>
            </div>
            <div class="creation-app-body" data-creation-preview="csv" hidden>
              <h3>换组数据，看看汇总。</h3>
              <label class="creation-sample-picker"><span>样例</span>
                <select class="creation-select creation-sample" aria-label="选择 CSV 样例数据">
                  ${Object.entries(creationSamples).map(([key, sample]) => `<option value="${key}">${sample.label}</option>`).join('')}
                </select>
              </label>
              <div class="creation-summary" role="status" aria-atomic="true">
                <span>总额 <strong class="creation-total"></strong></span>
                <span>数据行 <strong class="creation-rows"></strong></span>
              </div>
              <p class="creation-app-hint">内置样例 · 金额单位：元</p>
            </div>
            <div class="creation-app-body" data-creation-preview="api" hidden>
              <h3>服务状态，一眼可见。</h3>
              <div class="creation-response" role="status" aria-atomic="true"><strong class="creation-http-status"></strong><span class="creation-http-detail"></span></div>
              <button class="creation-request" type="button"></button>
              <p class="creation-app-hint">模拟响应 · 不发送网络请求</p>
            </div>
          </div>
        </div>
      </div>

      <footer class="creation-footer">
        <a class="creation-start" href="#ai-start">用这个项目开始 ${creationArrow}</a>
        <p class="creation-disclaimer" id="creation-disclaimer">交互示意 · 非在线编译 · 不保存</p>
      </footer>
    </section>`;

  const tabs = [...root.querySelectorAll('.creation-tab')];
  const panels = [...root.querySelectorAll('.creation-panel')];
  const playback = root.querySelector('.creation-playback');
  const playbackLabel = root.querySelector('.creation-playback-label');
  const scenarioSelect = root.querySelector('.creation-scenario');
  const app = root.querySelector('.creation-app');
  const previews = [...root.querySelectorAll('[data-creation-preview]')];
  const tasks = [...root.querySelectorAll('.creation-checkbox')];
  const count = root.querySelector('.creation-count');
  const completionLine = root.querySelector('.creation-completion-line > span');
  const hint = root.querySelector('[data-creation-preview="todo"] .creation-app-hint');
  const sampleSelect = root.querySelector('.creation-sample');
  const total = root.querySelector('.creation-total');
  const rows = root.querySelector('.creation-rows');
  const response = root.querySelector('.creation-response');
  const httpStatus = root.querySelector('.creation-http-status');
  const httpDetail = root.querySelector('.creation-http-detail');
  const request = root.querySelector('.creation-request');

  function renderPreview() {
    const completed = state.tasks.filter(Boolean).length;
    tasks.forEach((task, index) => { task.checked = state.tasks[index]; });
    count.textContent = `已完成 ${completed} / ${tasks.length}`;
    completionLine.style.width = `${completed / tasks.length * 100}%`;
    app.dataset.creationComplete = String(state.scenario === 'todo' && completed === tasks.length);
    hint.textContent = completed === tasks.length ? '两件小事，都完成了' : '点击勾选，体验一下';

    const sample = creationSamples[state.sample];
    sampleSelect.value = state.sample;
    total.textContent = `¥${(sample.cents.reduce((sum, value) => sum + value, 0) / 100).toFixed(2)}`;
    rows.textContent = String(sample.cents.length);

    response.dataset.healthy = String(state.healthy);
    httpStatus.textContent = state.healthy ? '200 · 健康（模拟）' : '503 · 维护（模拟）';
    httpDetail.textContent = state.healthy ? '示例服务可用' : '示例服务暂时不可用';
    request.textContent = state.healthy ? '模拟请求 → 维护' : '模拟请求 → 健康';
  }

  function renderScenario() {
    const scenario = creationScenarios[state.scenario];
    scenarioSelect.value = state.scenario;
    root.dataset.creationScenario = state.scenario;
    root.querySelector('.creation-prompt-lead').textContent = scenario.lead;
    root.querySelector('.creation-prompt-title').textContent = scenario.title;
    root.querySelector('.creation-intentions').innerHTML = scenario.intentions.map((text) => `<span>${text}</span>`).join('');
    root.querySelector('.creation-evidence-list').innerHTML = scenario.steps.map(([title, detail], index) => `
      <li class="creation-evidence"><span class="creation-evidence-mark" aria-hidden="true">${index + 1}</span><div><strong>${title}</strong><p>${detail}</p></div></li>`).join('');
    root.querySelector('.creation-app-symbol').textContent = scenario.symbol;
    root.querySelector('.creation-app-name').textContent = scenario.appTitle;
    previews.forEach((preview) => { preview.hidden = preview.dataset.creationPreview !== state.scenario; });
    renderPreview();
  }

  function clearClock() {
    window.clearTimeout(timer);
    timer = null;
    root.dataset.creationRunning = 'false';
  }

  function syncClock() {
    clearClock();
    const autoplay = String(state.autoplay);
    const changed = root.dataset.creationAutoplay !== autoplay;
    root.dataset.creationAutoplay = autoplay;
    playbackLabel.textContent = state.autoplay ? '暂停' : '播放';
    playback.setAttribute('aria-label', state.autoplay ? '暂停自动演示' : '播放自动演示，每 5 秒切换一步');
    playback.title = state.autoplay ? '暂停自动演示' : '播放自动演示 · 每步 5 秒';
    if (changed) document.dispatchEvent(new CustomEvent('zan:motion-change', { detail: { paused: !state.autoplay } }));
    if (!state.autoplay || !state.inView || document.hidden || !state.pageActive) return;
    root.dataset.creationRunning = 'true';
    timer = window.setTimeout(() => selectStage((state.step + 1) % tabs.length), creationInterval);
  }

  function stopAutoplay() {
    state.autoplay = false;
    syncClock();
  }

  function selectStage(index, manual = false, moveFocus = false) {
    if (manual) state.autoplay = false;
    state.step = index;
    tabs.forEach((tab, tabIndex) => {
      const active = tabIndex === state.step;
      tab.setAttribute('aria-selected', String(active));
      tab.tabIndex = active ? 0 : -1;
      panels[tabIndex].hidden = !active;
    });
    if (moveFocus) tabs[state.step].focus({ preventScroll: true });
    syncClock();
  }

  scenarioSelect.addEventListener('change', () => {
    if (!Object.hasOwn(creationScenarios, scenarioSelect.value)) return;
    state.scenario = scenarioSelect.value;
    renderScenario();
    selectStage(0, true);
  }, { signal: events.signal });

  tabs.forEach((tab, index) => {
    tab.addEventListener('click', () => selectStage(index, true), { signal: events.signal });
    tab.addEventListener('keydown', (event) => {
      let next;
      switch (event.key) {
        case 'ArrowRight': next = (index + 1) % tabs.length; break;
        case 'ArrowLeft': next = (index + tabs.length - 1) % tabs.length; break;
        case 'Home': next = 0; break;
        case 'End': next = tabs.length - 1; break;
        default: return;
      }
      event.preventDefault();
      selectStage(next, true, true);
    }, { signal: events.signal });
  });

  root.querySelectorAll('[data-creation-next]').forEach((button) => {
    button.addEventListener('click', () => selectStage(Number(button.dataset.creationNext), true, true), { signal: events.signal });
  });
  playback.addEventListener('click', () => {
    state.autoplay = !state.autoplay;
    syncClock();
  }, { signal: events.signal });

  // Keep a focused control from disappearing during keyboard exploration.
  root.addEventListener('focusin', (event) => {
    if (!playback.contains(event.target)) stopAutoplay();
  }, { signal: events.signal });
  app.addEventListener('pointerdown', stopAutoplay, { signal: events.signal });
  app.addEventListener('click', stopAutoplay, { signal: events.signal });
  tasks.forEach((task, index) => {
    task.addEventListener('change', () => {
      stopAutoplay();
      state.tasks[index] = task.checked;
      renderPreview();
    }, { signal: events.signal });
  });
  sampleSelect.addEventListener('change', () => {
    if (!Object.hasOwn(creationSamples, sampleSelect.value)) return;
    stopAutoplay();
    state.sample = sampleSelect.value;
    renderPreview();
  }, { signal: events.signal });
  request.addEventListener('click', () => {
    stopAutoplay();
    state.healthy = !state.healthy;
    renderPreview();
  }, { signal: events.signal });
  root.querySelector('.creation-start').addEventListener('click', () => {
    stopAutoplay();
    const projectIdea = document.getElementById('project-idea');
    if (projectIdea) {
      projectIdea.value = state.scenario;
      projectIdea.dispatchEvent(new Event('change', { bubbles: true }));
    }
  }, { signal: events.signal });

  // Ambient suspension preserves the user's play/pause choice; reentry gets a full step.
  document.addEventListener('visibilitychange', syncClock, { signal: events.signal });
  window.addEventListener('pagehide', () => {
    state.pageActive = false;
    syncClock();
  }, { signal: events.signal });
  window.addEventListener('pageshow', () => {
    state.pageActive = true;
    syncClock();
  }, { signal: events.signal });
  reducedMotion.addEventListener('change', (event) => {
    if (event.matches) stopAutoplay();
  }, { signal: events.signal });

  let observer;
  if ('IntersectionObserver' in window) {
    observer = new IntersectionObserver(([entry]) => {
      const visible = entry.isIntersecting && entry.intersectionRatio > 0;
      if (state.inView !== visible) {
        state.inView = visible;
        syncClock();
      }
    }, { threshold: [0, 0.01] });
    observer.observe(root);
  } else {
    const checkViewport = () => {
      const rect = root.getBoundingClientRect();
      const visible = rect.bottom > 0 && rect.top < window.innerHeight
        && rect.right > 0 && rect.left < window.innerWidth;
      if (state.inView !== visible) {
        state.inView = visible;
        syncClock();
      }
    };
    window.addEventListener('scroll', checkViewport, { passive: true, capture: true, signal: events.signal });
    window.addEventListener('resize', checkViewport, { passive: true, signal: events.signal });
    checkViewport();
  }

  const destroy = () => {
    clearClock();
    events.abort();
    observer?.disconnect();
    creationInstances.delete(root);
    root.replaceChildren();
    root.classList.remove('creation-demo');
    delete root.dataset.creationRunning;
    delete root.dataset.creationAutoplay;
    delete root.dataset.creationScenario;
  };
  creationInstances.set(root, destroy);
  renderScenario();
  syncClock();
  return destroy;
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => mountCreationDemo(), { once: true });
} else {
  mountCreationDemo();
}
