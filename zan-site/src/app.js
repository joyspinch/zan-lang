import { examples, aiSteps, packages, guides, searchItems, downloadsUrl } from './content.js';

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const reducedMotion = window.matchMedia?.('(prefers-reduced-motion: reduce)') || { matches: false };

function initPanelMotion() {
  const active = new Map();
  let paused = false;
  function clear(panel, animation) {
    if (active.get(panel) !== animation) return;
    active.delete(panel);
    panel.classList.remove('motion-enter');
  }
  function cancelAll() {
    active.forEach((animation, panel) => { animation.cancel(); clear(panel, animation); });
  }
  document.addEventListener('zan:panel-change', (event) => {
    const panel = event.target;
    const previous = active.get(panel);
    if (previous) { previous.cancel(); clear(panel, previous); }
    if (paused || reducedMotion.matches || panel.closest('[hidden], dialog:not([open])') || typeof panel.animate !== 'function') return;
    const animation = panel.animate([
      { opacity: .45, transform: 'translateY(6px)' },
      { opacity: 1, transform: 'translateY(0)' },
    ], { duration: 220, easing: 'cubic-bezier(.2, .7, .2, 1)' });
    active.set(panel, animation);
    panel.classList.add('motion-enter');
    animation.addEventListener('finish', () => clear(panel, animation), { once: true });
    animation.addEventListener('cancel', () => clear(panel, animation), { once: true });
  });
  reducedMotion.addEventListener?.('change', () => { if (reducedMotion.matches) cancelAll(); });
  document.addEventListener('zan:motion-change', (event) => {
    paused = Boolean(event.detail?.paused);
    if (paused) cancelAll();
  });
}

function initTabKeyboard(tabs) {
  tabs.forEach((tab) => tab.addEventListener('keydown', (event) => {
    if (!['ArrowRight', 'ArrowLeft', 'ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const index = tabs.indexOf(tab);
    const nextIndex = event.key === 'Home' ? 0 : event.key === 'End' ? tabs.length - 1 : (index + (['ArrowRight', 'ArrowDown'].includes(event.key) ? 1 : -1) + tabs.length) % tabs.length;
    tabs[nextIndex].focus();
    tabs[nextIndex].click();
  }));
}

const icon = (name) => `<svg class="icon" aria-hidden="true"><use href="#i-${name}"></use></svg>`;
const escapeHtml = (value) => value.replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#039;' }[char]));

function highlightCode(source) {
  const keywords = new Set(['using', 'class', 'struct', 'public', 'static', 'void', 'return', 'extern', 'new', 'weak', 'async', 'await']);
  const types = new Set(['int', 'string', 'bool', 'float', 'double', 'long']);
  const functions = new Set(['Console', 'WriteLine', 'Point', 'LengthSquared', 'DllImport', 'GetTickCount64', 'NativeClock', 'Program', 'Main']);
  return source.split(/(\/\/[^\n]*|"(?:\\.|[^"\\])*"|\b\d+(?:\.\d+)?\b|\b[A-Za-z_]\w*\b)/g).map((token) => {
    const kind = token.startsWith('//') ? 'comment' : token.startsWith('"') ? 'string' : /^\d/.test(token) ? 'number' : keywords.has(token) ? 'keyword' : types.has(token) ? 'type' : functions.has(token) ? 'function' : null;
    return kind ? `<span class="token-${kind}">${escapeHtml(token)}</span>` : escapeHtml(token);
  }).join('');
}

function showToast(message) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.add('show');
  window.clearTimeout(showToast.timer);
  showToast.timer = window.setTimeout(() => toast.classList.remove('show'), 2500);
}

function writeCode(example, animate = false) {
  const output = $('#example-code');
  const lines = highlightCode(example.code).split('\n');
  output.innerHTML = lines.map((line, index) => `<span class="code-line"><span class="line-number">${String(index + 1).padStart(2, '0')}</span>${line || ' '}</span>`).join('');
  $('#code-filename').textContent = example.filename;
  $('#example-output').textContent = example.output;
  const panel = $('#code-panel');
  panel.setAttribute('aria-labelledby', `tab-${Object.keys(examples).find((key) => examples[key] === example)}`);
  if (animate) panel.dispatchEvent(new CustomEvent('zan:panel-change', { bubbles: true }));
}

function initExamples() {
  writeCode(examples.hello);
  $$('.code-tabs [data-example]').forEach((tab) => {
    tab.addEventListener('click', () => {
      const changed = tab.getAttribute('aria-selected') !== 'true';
      $$('.code-tabs [data-example]').forEach((item) => {
        const selected = item === tab;
        item.setAttribute('aria-selected', String(selected));
        item.tabIndex = selected ? 0 : -1;
      });
      writeCode(examples[tab.dataset.example], changed);
    });
  });
  initTabKeyboard($$('.code-tabs [data-example]'));
  $('.copy-code').addEventListener('click', async () => {
    const active = $('.code-tabs [aria-selected=true]');
    try {
      await navigator.clipboard.writeText(examples[active.dataset.example].code);
      showToast('代码已复制到剪贴板');
    } catch {
      showToast('代码示例已准备好，复制功能需要浏览器权限');
    }
  });
}

function renderAiStep(stepName, animate = true) {
  const step = aiSteps[stepName];
  const response = $('#ai-response');
  response.innerHTML = `<p class="response-intro">${step.intro}</p><div class="tool-call"><div class="tool-call-title">${icon('spark')} ${step.label}<span>步骤示意</span></div><pre>${escapeHtml(step.body)}</pre></div>`;
  const current = $(`[data-ai="${stepName}"]`);
  const changed = current.getAttribute('aria-selected') !== 'true';
  $$('.ai-steps [data-ai]').forEach((button) => {
    const selected = button === current;
    button.setAttribute('aria-selected', String(selected));
    button.tabIndex = selected ? 0 : -1;
  });
  const panel = $('#ai-panel');
  panel.setAttribute('aria-labelledby', current.id);
  $('#ai-next').dataset.next = step.next;
  if (animate && changed) panel.dispatchEvent(new CustomEvent('zan:panel-change', { bubbles: true }));
}

function initAi() {
  renderAiStep('context', false);
  $$('.ai-steps [data-ai]').forEach((button) => {
    button.addEventListener('click', () => renderAiStep(button.dataset.ai));
  });
  initTabKeyboard($$('.ai-steps [data-ai]'));
  $('#ai-next').addEventListener('click', () => renderAiStep($('#ai-next').dataset.next));
}

const packageState = { filter: 'all', query: '', expanded: false };
const featuredPackageNames = ['Zan.Gui', 'Zan.Net', 'Zan.DataFrame', 'Zan.Game', 'Zan.Security', 'Zan.Sdk.Wechat'];

function renderPackages(filter = packageState.filter, query = packageState.query) {
  packageState.filter = filter;
  packageState.query = query;
  $('#package-query').value = query;
  let filterLabel = '';
  $$('.package-filters button').forEach((button) => {
    const selected = button.dataset.filter === filter;
    button.setAttribute('aria-pressed', String(selected));
    if (selected && filter !== 'all') filterLabel = button.dataset.filterLabel;
  });
  const normalized = query.trim().toLowerCase();
  const matches = packages.filter((item) => (filter === 'all' || item.category === filter) && (!normalized || `${item.name} ${item.title} ${item.description} ${item.tags.join(' ')} ${item.status || ''}`.toLowerCase().includes(normalized)));
  const browsing = filter === 'all' && !normalized;
  const featured = featuredPackageNames.map((name) => matches.find((item) => item.name === name)).filter(Boolean);
  const visible = browsing && !packageState.expanded ? [...featured, ...matches.filter((item) => !featured.includes(item))].slice(0, 6) : matches;
  const grid = $('#packages-grid');
  if (!matches.length) {
    grid.innerHTML = `<div class="packages-empty"><strong>还没有这个关键词的包</strong><p>试试另一个场景，或清除筛选重新探索。</p><button type="button" data-clear-packages aria-controls="packages-grid">清除筛选</button></div>`;
  } else {
    grid.innerHTML = visible.map((item) => `<a class="package-card" href="${downloadsUrl}" title="在发布页查找 ${escapeHtml(item.name)} 的可用版本" target="_blank" rel="noopener noreferrer">${icon(item.icon)}<h3>${escapeHtml(item.name)}${icon('diagonal')}</h3><p>${escapeHtml(item.description)}</p><div class="package-tags">${item.tags.map((tag) => `<span>${escapeHtml(tag)}</span>`).join('')}${item.status && item.status !== '源码可用' ? `<span class="package-status">${escapeHtml(item.status)}</span>` : ''}</div></a>`).join('');
  }
  const context = [filterLabel, normalized ? `搜索“${query.trim()}”` : ''].filter(Boolean).join(' · ');
  const count = visible.length < matches.length ? `精选 ${visible.length} / ${matches.length} 个扩展包` : `${matches.length} 个可探索的扩展包`;
  $('#package-count').textContent = context ? `${context}：${count}` : count;
  $('#packages-clear').hidden = filter === 'all' && !query.length;
  const more = $('#packages-more');
  more.hidden = !browsing || matches.length <= 6;
  more.setAttribute('aria-controls', 'packages-grid');
  more.setAttribute('aria-expanded', String(packageState.expanded));
  more.innerHTML = packageState.expanded ? '收起为精选包 <span aria-hidden="true">↑</span>' : '展开全部包 <span aria-hidden="true">↓</span>';
}

function resetPackages() {
  packageState.expanded = false;
  renderPackages('all', '');
  $('#package-query').focus();
}

function explorePackageCategory(category) {
  if (!$$('.package-filters button').some((button) => button.dataset.filter === category)) return;
  packageState.expanded = false;
  renderPackages(category, '');
  const heading = $('.catalog-heading h3') || $('#package-count');
  heading.tabIndex = -1;
  heading.focus({ preventScroll: true });
  heading.scrollIntoView({ behavior: reducedMotion.matches ? 'instant' : 'smooth', block: 'start' });
}

function initPackages() {
  const query = $('#package-query');
  const count = $('#package-count');
  count.tabIndex = -1;
  count.setAttribute('role', 'status');
  count.setAttribute('aria-live', 'polite');
  count.setAttribute('aria-atomic', 'true');
  const clear = document.createElement('button');
  clear.id = 'packages-clear';
  clear.type = 'button';
  clear.className = 'text-link packages-clear';
  clear.dataset.clearPackages = '';
  clear.textContent = '清除筛选';
  clear.setAttribute('aria-controls', 'packages-grid');
  clear.hidden = true;
  ($('.ecosystem-bottom') || query.parentElement).append(clear);
  $$('.package-filters button').forEach((button) => {
    const label = button.textContent.trim();
    const total = button.dataset.filter === 'all' ? packages.length : packages.filter((item) => item.category === button.dataset.filter).length;
    button.dataset.filterLabel = label;
    button.innerHTML = `${escapeHtml(label)} <span class="package-filter-count" aria-hidden="true">${total}</span>`;
    button.setAttribute('aria-label', `${label}，${total} 个扩展包`);
    button.setAttribute('aria-controls', 'packages-grid');
    button.addEventListener('click', () => renderPackages(button.dataset.filter));
  });
  renderPackages(packageState.filter, query.value);
  query.addEventListener('input', () => renderPackages(packageState.filter, query.value));
  document.addEventListener('click', (event) => {
    if (event.target.closest('[data-clear-packages]')) { resetPackages(); return; }
    const explore = event.target.closest('[data-explore-category]');
    if (explore) explorePackageCategory(explore.dataset.exploreCategory);
  });
  $('#packages-more').addEventListener('click', () => {
    packageState.expanded = !packageState.expanded;
    renderPackages();
  });
}

function initArc() {
  const demo = $('.arc-demo');
  const count = $('#arc-count');
  const status = $('#arc-object-status');
  const caption = $('#arc-caption');
  const release = $('#arc-release');
  const retain = $('#arc-retain');
  const reset = $('#arc-reset');
  let refs = 2;
  function update() {
    count.textContent = String(refs);
    $$('.arc-tracks i').forEach((track, index) => track.classList.toggle('empty', index >= refs));
    const released = refs === 0;
    demo.classList.toggle('released', released);
    status.textContent = released ? '对象已释放' : '对象存活';
    caption.textContent = released ? '引用归零，ARC 释放对象' : '点一下，观察对象的生命周期';
    release.disabled = released;
    retain.disabled = released || refs >= 4;
    reset.hidden = !released;
  }
  release.addEventListener('click', () => { refs = Math.max(0, refs - 1); update(); });
  retain.addEventListener('click', () => { if (refs > 0) refs = Math.min(4, refs + 1); update(); });
  reset.addEventListener('click', () => { refs = 2; update(); });
  update();
}

function initShellTabs() {
  const command = $('#start-command');
  $$('.shell-tabs [data-shell]').forEach((tab) => tab.addEventListener('click', () => {
    $$('.shell-tabs [data-shell]').forEach((item) => {
      const selected = item === tab;
      item.setAttribute('aria-selected', String(selected));
      item.tabIndex = selected ? 0 : -1;
    });
    command.textContent = tab.dataset.shell === 'linux' ? 'zanc hello.zan -o hello\n./hello' : 'zanc hello.zan -o hello.exe\n.\\hello.exe';
    $('#quickstart-panel').setAttribute('aria-labelledby', tab.id);
  }));
  initTabKeyboard($$('.shell-tabs [data-shell]'));
  $('#copy-command').addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(command.textContent);
      showToast('编译命令已复制');
    } catch { showToast('命令已显示在终端中'); }
  });
  $('#copy-ai-prompt').addEventListener('click', async () => {
    const prompt = '请先读取 Zan 项目入口与开发约定，再查询真实 API，实现修改，最后用 zan_compile 验证并报告诊断。';
    try {
      await navigator.clipboard.writeText(prompt);
      showToast('AI 开发指令已复制');
    } catch { showToast(prompt); }
  });
}

const dialogOpeners = new WeakMap();

function syncModalState() {
  document.body.classList.toggle('modal-open', Boolean($('dialog[open]')));
  $('#site-query')?.setAttribute('aria-expanded', String(Boolean($('#search-dialog')?.open)));
}

function finishDialogClose(dialog, restoreFocus = true) {
  const opener = dialogOpeners.get(dialog);
  dialogOpeners.delete(dialog);
  syncModalState();
  if (dialog.id === 'search-dialog') $('#site-query')?.removeAttribute('aria-activedescendant');
  if (!restoreFocus || !opener || $('dialog[open]')) return;
  const target = opener.isConnected && opener.getClientRects().length && !opener.closest('[hidden], dialog:not([open])') ? opener : $('.menu-toggle')?.getClientRects().length ? $('.menu-toggle') : $('.search-trigger');
  target?.focus({ preventScroll: true });
}

function closeDialog(dialog, restoreFocus = true) {
  if (!dialog?.open) return;
  dialog.close();
  finishDialogClose(dialog, restoreFocus);
}

function openDialog(dialog, focusTarget) {
  if (!dialog.open) {
    let opener = document.activeElement;
    $$('dialog[open]').forEach((other) => {
      opener = dialogOpeners.get(other) || opener;
      closeDialog(other, false);
    });
    dialogOpeners.set(dialog, opener);
    try {
      dialog.showModal();
    } catch (error) {
      finishDialogClose(dialog);
      throw error;
    }
  }
  syncModalState();
  focusTarget?.focus({ preventScroll: true });
}

function initDialog(dialog) {
  dialog.addEventListener('cancel', (event) => { event.preventDefault(); closeDialog(dialog); });
  dialog.addEventListener('close', () => { if (!dialog.open) finishDialogClose(dialog); });
  dialog.addEventListener('click', (event) => { if (event.target === dialog) closeDialog(dialog); });
  $('.close-dialog', dialog).addEventListener('click', () => closeDialog(dialog));
}

function scrollToSection(id) {
  const section = document.getElementById(id.replace(/^#/, ''));
  if (!section) return;
  $$('dialog[open]').forEach((dialog) => closeDialog(dialog, false));
  const focusTarget = $('h2, h1', section) || section;
  if (!focusTarget.hasAttribute('tabindex')) focusTarget.tabIndex = -1;
  focusTarget.focus({ preventScroll: true });
  section.scrollIntoView({ behavior: reducedMotion.matches ? 'instant' : 'smooth', block: 'start' });
}

function openGuide(id = 'overview') {
  if (!guides[id]) id = 'overview';
  const guide = guides[id];
  const dialog = $('#guide-dialog');
  const switchingChapter = dialog.open;
  $('#guide-kicker').textContent = guide.kicker;
  $('#guide-title').textContent = guide.title;
  $('#guide-body').innerHTML = guide.body;
  $('#guide-nav').innerHTML = Object.entries(guides).map(([key, item]) => `<button type="button" data-guide-nav="${escapeHtml(key)}" aria-current="${key === id ? 'page' : 'false'}">${escapeHtml(item.nav)}</button>`).join('');
  $$('#guide-nav [data-guide-nav]').forEach((button) => button.addEventListener('click', () => openGuide(button.dataset.guideNav)));
  openDialog(dialog, switchingChapter ? $('#guide-nav [aria-current="page"]') : $('.close-dialog', dialog));
  dialog.scrollTop = 0;
  $('.guide-content', dialog).scrollTop = 0;
}

function initGuides() {
  initDialog($('#guide-dialog'));
  document.addEventListener('click', (event) => {
    const trigger = event.target.closest('[data-guide]');
    if (trigger) {
      event.preventDefault();
      openGuide(trigger.dataset.guide);
      return;
    }
    const link = event.target.closest('dialog a[href^="#"]');
    if (link && document.getElementById(link.hash.slice(1))) {
      event.preventDefault();
      scrollToSection(link.hash);
    }
  });
}

let searchIndex = 0;
function selectSearchResult(index, scroll = true) {
  const results = $$('.search-result', $('#search-results'));
  searchIndex = results.length ? (index + results.length) % results.length : -1;
  results.forEach((item, itemIndex) => {
    const selected = itemIndex === searchIndex;
    item.classList.toggle('selected', selected);
    item.setAttribute('aria-selected', String(selected));
  });
  const active = results[searchIndex];
  if (active) {
    $('#site-query').setAttribute('aria-activedescendant', active.id);
    if (scroll) active.scrollIntoView({ block: 'nearest', behavior: 'instant' });
  } else $('#site-query').removeAttribute('aria-activedescendant');
}

function renderSearchResults(query = '') {
  const normalized = query.trim().toLowerCase();
  const results = searchItems.filter((item) => !normalized || `${item.title} ${item.description} ${item.kind}`.toLowerCase().includes(normalized)).slice(0, 8);
  const target = $('#search-results');
  target.innerHTML = results.length ? results.map((item, index) => `<div role="option" id="search-option-${index}" class="search-result" data-search-index="${index}" data-result-type="${escapeHtml(item.kind.toLowerCase())}" data-result-id="${escapeHtml(item.guide || item.package || item.section || '')}" aria-selected="false">${icon(item.icon)}<span><strong>${escapeHtml(item.title)}</strong><small>${escapeHtml(item.description)}</small></span><b>${escapeHtml(item.kind)}</b></div>`).join('') : '<div class="search-empty" role="status">没有匹配结果。试试 “ARC”、 “GUI” 或 “Zan.Net”。</div>';
  $$('.search-result', target).forEach((result) => {
    result.addEventListener('mousedown', (event) => event.preventDefault());
    result.addEventListener('click', () => activateSearchResult(result));
    result.addEventListener('pointermove', () => selectSearchResult(Number(result.dataset.searchIndex), false));
  });
  target.scrollTop = 0;
  selectSearchResult(0, false);
}

function activateSearchResult(result) {
  if (result.dataset.resultType === 'guide') openGuide(result.dataset.resultId);
  else if (result.dataset.resultType === 'section') scrollToSection(result.dataset.resultId);
  else if (result.dataset.resultType === 'package') {
    packageState.expanded = false;
    renderPackages('all', result.dataset.resultId);
    scrollToSection('ecosystem');
    $('#package-query').focus({ preventScroll: true });
  }
}

function initSearch() {
  const dialog = $('#search-dialog');
  const input = $('#site-query');
  initDialog(dialog);
  input.setAttribute('role', 'combobox');
  input.setAttribute('aria-autocomplete', 'list');
  input.setAttribute('aria-haspopup', 'listbox');
  input.setAttribute('aria-controls', 'search-results');
  input.setAttribute('aria-expanded', 'false');
  $('#search-results').setAttribute('role', 'listbox');
  $('#search-results').setAttribute('aria-label', '站点搜索结果');
  $('.search-trigger').addEventListener('click', () => {
    openDialog(dialog, input);
    input.value = '';
    renderSearchResults();
  });
  input.addEventListener('input', () => renderSearchResults(input.value));
  input.addEventListener('keydown', (event) => {
    const results = $$('.search-result', dialog);
    if (['ArrowDown', 'ArrowUp'].includes(event.key)) {
      event.preventDefault();
      selectSearchResult(searchIndex + (event.key === 'ArrowDown' ? 1 : -1));
    } else if (event.key === 'Enter' && results[searchIndex]) {
      event.preventDefault();
      activateSearchResult(results[searchIndex]);
    }
  });
  window.addEventListener('keydown', (event) => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') {
      event.preventDefault();
      if (dialog.open) input.focus();
      else $('.search-trigger').click();
    }
  });
}

function initMobileNav() {
  const toggle = $('.menu-toggle');
  const nav = $('#mobile-nav');
  toggle.addEventListener('click', () => {
    const open = toggle.getAttribute('aria-expanded') === 'true';
    toggle.setAttribute('aria-expanded', String(!open));
    nav.hidden = open;
  });
  $$('#mobile-nav a, #mobile-nav button').forEach((link) => link.addEventListener('click', () => { toggle.setAttribute('aria-expanded', 'false'); nav.hidden = true; }));
}

function initScrollEffects() {
  if (typeof IntersectionObserver !== 'function') return;
  try {
    if (!reducedMotion.matches) {
      const observer = new IntersectionObserver((entries) => entries.forEach((entry) => {
        if (!entry.isIntersecting) return;
        entry.target.classList.add('visible');
        observer.unobserve(entry.target);
      }), { threshold: .11 });
      $$('.reveal').forEach((item) => observer.observe(item));
      document.documentElement.classList.add('js-motion');
      reducedMotion.addEventListener?.('change', (event) => {
        if (!event.matches) return;
        document.documentElement.classList.remove('js-motion');
        observer.disconnect();
      });
    }
    const links = $$('.desktop-nav a[href^="#"]');
    const sections = links.map((link) => document.getElementById(link.hash.slice(1))).filter(Boolean);
    const navObserver = new IntersectionObserver((entries) => entries.forEach((entry) => { if (entry.isIntersecting) links.forEach((link) => link.classList.toggle('active', link.hash === `#${entry.target.id}`)); }), { rootMargin: '-35% 0px -55%' });
    sections.forEach((section) => navObserver.observe(section));
  } catch (error) {
    document.documentElement.classList.remove('js-motion');
    console.warn('Scroll effects unavailable', error);
  }
}

function initOrbit() {
  const canvas = $('#orbit-canvas');
  if (!canvas) return;
  const context = canvas.getContext('2d');
  if (!context) return;
  let width = 1;
  let height = 1;
  let frame = null;
  let elapsed = 0;
  let previousTime = null;
  let inView = false;
  let pageHidden = false;
  let pointer = { x: 0, y: 0 };
  const particles = Array.from({ length: 42 }, (_, index) => ({ angle: (Math.PI * 2 * index) / 42 + Math.random() * .2, radius: .2 + Math.random() * .34, speed: (Math.random() * .00025 + .00008) * (index % 2 ? 1 : -1), size: Math.random() * 1.2 + .3, alpha: Math.random() * .55 + .18 }));
  function updateVisibility() {
    const rect = canvas.getBoundingClientRect();
    inView = rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.right > 0 && rect.top < window.innerHeight && rect.left < window.innerWidth;
  }
  function resize() {
    const scale = Math.min(window.devicePixelRatio || 1, 2);
    const rect = canvas.getBoundingClientRect();
    width = rect.width;
    height = rect.height;
    canvas.width = Math.round(width * scale);
    canvas.height = Math.round(height * scale);
    context.setTransform(scale, 0, 0, scale, 0, 0);
    updateVisibility();
    syncAnimation();
  }
  function draw(time) {
    context.clearRect(0, 0, width, height);
    const cx = width * (.53 + pointer.x * .01); const cy = height * (.51 + pointer.y * .01); const base = Math.min(width, height);
    context.save(); context.translate(cx, cy); context.rotate(-.13);
    const rings = [{ rx: base * .4, ry: base * .11, rotation: -.28, alpha: .35 }, { rx: base * .47, ry: base * .17, rotation: .22, alpha: .25 }, { rx: base * .32, ry: base * .24, rotation: 1.25, alpha: .24 }, { rx: base * .55, ry: base * .29, rotation: -.55, alpha: .12 }];
    rings.forEach((ring) => { context.save(); context.rotate(ring.rotation); context.beginPath(); context.ellipse(0, 0, ring.rx, ring.ry, 0, 0, Math.PI * 2); context.strokeStyle = `rgba(108, 238, 205, ${ring.alpha})`; context.lineWidth = .65; context.stroke(); context.restore(); });
    particles.forEach((particle) => { const angle = particle.angle + time * particle.speed; const radius = base * particle.radius; const x = Math.cos(angle) * radius; const y = Math.sin(angle) * radius * .39; context.beginPath(); context.fillStyle = `rgba(157, 255, 224, ${particle.alpha})`; context.arc(x, y, particle.size, 0, Math.PI * 2); context.fill(); });
    context.restore();
  }
  function canDraw() { return inView && !document.hidden && !pageHidden && width > 0 && height > 0; }
  function tick(time) {
    frame = null;
    if (!canDraw() || reducedMotion.matches) { previousTime = null; return; }
    if (previousTime !== null) elapsed += Math.min(time - previousTime, 64);
    previousTime = time;
    draw(elapsed);
    frame = requestAnimationFrame(tick);
  }
  function syncAnimation() {
    if (frame !== null) cancelAnimationFrame(frame);
    frame = null;
    previousTime = null;
    if (!canDraw()) return;
    if (reducedMotion.matches) {
      pointer = { x: 0, y: 0 };
      draw(0);
    } else frame = requestAnimationFrame(tick);
  }
  const visual = $('.hero-visual');
  visual?.addEventListener('pointermove', (event) => {
    if (reducedMotion.matches || !canDraw()) return;
    const rect = visual.getBoundingClientRect();
    if (rect.width && rect.height) pointer = { x: (event.clientX - rect.left) / rect.width * 2 - 1, y: (event.clientY - rect.top) / rect.height * 2 - 1 };
  });
  visual?.addEventListener('pointerleave', () => { pointer = { x: 0, y: 0 }; });
  window.addEventListener('resize', resize);
  document.addEventListener('visibilitychange', () => { updateVisibility(); syncAnimation(); });
  reducedMotion.addEventListener?.('change', syncAnimation);
  window.addEventListener('pagehide', () => { pageHidden = true; syncAnimation(); });
  window.addEventListener('pageshow', () => { pageHidden = false; updateVisibility(); syncAnimation(); });
  if (typeof IntersectionObserver === 'function') {
    const observer = new IntersectionObserver((entries) => { inView = entries[0].isIntersecting; syncAnimation(); });
    observer.observe(canvas);
  } else window.addEventListener('scroll', () => { updateVisibility(); syncAnimation(); }, { passive: true });
  resize();
}

function boot() {
  if ($('#year')) $('#year').textContent = new Date().getFullYear();
  [initPanelMotion, initExamples, initAi, initPackages, initArc, initShellTabs, initGuides, initSearch, initMobileNav, initScrollEffects, initOrbit].forEach((initialize) => {
    try { initialize(); }
    catch (error) {
      document.documentElement.classList.remove('js-motion');
      console.warn(`${initialize.name} unavailable`, error);
    }
  });
}

boot();
