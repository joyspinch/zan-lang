const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
let paused = document.getElementById('creation-demo')?.dataset.creationAutoplay === 'false';
const animate = () => !reducedMotion.matches && !paused;

function initNavigation() {
  const sections = [
    ['why-zan', '选择理由'], ['difference', '特色能力'], ['language', '语言体验'],
    ['ai-start', 'AI 入门'], ['platforms', '原生平台'], ['ecosystem', '生态探索'],
  ].map(([id, label]) => ({ element: document.getElementById(id), id, label })).filter(({ element }) => element);
  const rail = document.createElement('nav');
  rail.className = 'explore-rail';
  rail.setAttribute('aria-label', '页面探索路径');
  rail.innerHTML = sections.map(({ id, label }) => `<a href="#${id}" aria-label="${label}"><span>${label}</span><i aria-hidden="true"></i></a>`).join('');
  const progress = document.createElement('div');
  progress.className = 'reading-progress';
  progress.setAttribute('aria-hidden', 'true');
  const top = document.createElement('a');
  top.href = '#';
  top.className = 'return-top';
  top.setAttribute('aria-label', '返回顶部');
  top.innerHTML = '<span aria-hidden="true">↑</span>';
  top.hidden = true;
  document.body.append(progress, rail, top);
  const links = [...rail.querySelectorAll('a')];
  let frame = 0;
  function update() {
    frame = 0;
    const distance = document.documentElement.scrollHeight - window.innerHeight;
    progress.style.transform = `scaleX(${distance > 0 ? Math.max(0, Math.min(1, window.scrollY / distance)) : 0})`;
    let active = -1;
    sections.forEach(({ element }, index) => {
      if (element.getBoundingClientRect().top < window.innerHeight * 0.42) active = index;
    });
    links.forEach((link, index) => {
      if (index === active) link.setAttribute('aria-current', 'location');
      else link.removeAttribute('aria-current');
    });
    top.hidden = window.scrollY < window.innerHeight;
  }
  const schedule = () => { if (!frame) frame = requestAnimationFrame(update); };
  window.addEventListener('scroll', schedule, { passive: true });
  window.addEventListener('resize', schedule, { passive: true });
  new ResizeObserver(schedule).observe(document.body);
  document.addEventListener('click', (event) => {
    const link = event.target.closest('a[href^="#"]');
    if (!link || event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    const hash = link.getAttribute('href');
    const target = hash === '#' ? document.getElementById('main') : document.getElementById(hash.slice(1));
    if (!target) return;
    event.preventDefault();
    if (location.hash !== hash) history.pushState(null, '', hash);
    const focusTarget = target.querySelector('h1, h2, h3') || target;
    if (!focusTarget.hasAttribute('tabindex')) focusTarget.tabIndex = -1;
    focusTarget.focus({ preventScroll: true });
    if (hash === '#') window.scrollTo({ top: 0, behavior: animate() ? 'smooth' : 'instant' });
    else target.scrollIntoView({ behavior: animate() ? 'smooth' : 'instant', block: 'start' });
  });
  update();
}

function initCardLight() {
  const finePointer = window.matchMedia('(hover: hover) and (pointer: fine)');
  let frame = 0;
  let active;
  let x = 0;
  let y = 0;
  const clear = () => {
    if (active) active.removeAttribute('data-pointer');
    active = null;
    if (frame) cancelAnimationFrame(frame);
    frame = 0;
  };
  document.addEventListener('pointermove', (event) => {
    if (!finePointer.matches || !animate() || event.pointerType === 'touch') { clear(); return; }
    const card = event.target.closest('.difference-card, .ecosystem-domain');
    if (card !== active) { clear(); active = card; }
    if (!active) return;
    const rect = active.getBoundingClientRect();
    x = event.clientX - rect.left;
    y = event.clientY - rect.top;
    if (!frame) frame = requestAnimationFrame(() => {
      frame = 0;
      if (!active) return;
      active.style.setProperty('--pointer-x', `${x}px`);
      active.style.setProperty('--pointer-y', `${y}px`);
      active.dataset.pointer = 'true';
    });
  }, { passive: true });
  document.addEventListener('pointerout', (event) => { if (!event.relatedTarget) clear(); });
  document.addEventListener('visibilitychange', () => { if (document.hidden) clear(); });
  document.addEventListener('zan:motion-change', clear);
  reducedMotion.addEventListener('change', clear);
}

function syncMotion() {
  document.documentElement.classList.toggle('site-motion-paused', paused || reducedMotion.matches || document.hidden);
}
document.addEventListener('zan:motion-change', (event) => { paused = Boolean(event.detail?.paused); syncMotion(); });
reducedMotion.addEventListener('change', syncMotion);
document.addEventListener('visibilitychange', syncMotion);
syncMotion();
[initNavigation, initCardLight].forEach((init) => {
  try { init(); } catch (error) { console.warn('Interaction unavailable:', init.name, error); }
});
