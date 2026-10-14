const intelligenceInstances = new WeakMap();
const TAU = Math.PI * 2;
const phases = [
  { color: [142, 201, 255], speed: 0.13 },
  { color: [180, 154, 255], speed: 0.21 },
  { color: [143, 247, 209], speed: 0.16 },
];
const rgba = (color, alpha) => `rgba(${color[0]},${color[1]},${color[2]},${alpha})`;

/** Local visual illustration; returns an idempotent cleanup function. */
export function mountIntelligenceScene(root = document.getElementById('intelligence-scene')) {
  if (!root) return undefined;
  if (intelligenceInstances.has(root)) return intelligenceInstances.get(root);
  const canvas = root.querySelector('#intelligence-canvas');
  const ctx = canvas?.getContext('2d', { alpha: true });
  if (!ctx) return undefined; // The CSS core remains visible without Canvas2D.

  const events = new AbortController();
  const motion = window.matchMedia('(prefers-reduced-motion: reduce)');
  const originalStage = root.getAttribute('data-intelligence-stage');
  const originalRunning = root.getAttribute('data-intelligence-running');
  const particles = [];
  const projected = [];
  const edges = [];
  const orbits = [
    { radius: 1.24, tilt: 0.30, turn: -0.43, offset: 0.3 },
    { radius: 1.16, tilt: 1.05, turn: 0.60, offset: 2.6 },
    { radius: 1.35, tilt: 0.48, turn: 1.84, offset: 4.2 },
  ];
  let width = 0;
  let height = 0;
  let dpr = 1;
  let centerX = 0;
  let centerY = 0;
  let radius = 1;
  let angle = 0.43;
  let clock = 0;
  let phase = 0;
  let frame = 0;
  let lastTime = 0;
  let destroyed = false;
  let inView = false;
  let pageActive = true;
  let userPaused = document.getElementById('creation-demo')?.dataset.creationAutoplay === 'false';
  let resizeObserver;
  let intersectionObserver;
  let stageObserver;
  let appearanceObserver;
  let pixelQuery;
  let pixelListener;
  let demo;
  let speed = phases[0].speed;
  const tint = [...phases[0].color];
  const pointer = { x: 0, y: 0, aimX: 0, aimY: 0, localX: 0, localY: 0, active: false };

  // A deterministic Fibonacci shell looks composed even on its first, static frame.
  // Neighbours use a fixed set of Fibonacci offsets, never an all-pairs scan.
  const count = 540;
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  for (let i = 0; i < count; i += 1) {
    const y = 1 - 2 * (i + 0.5) / count;
    const ring = Math.sqrt(1 - y * y);
    const theta = i * goldenAngle;
    const shell = 0.95 + 0.045 * Math.sin(i * 2.17);
    particles.push({
      x: Math.cos(theta) * ring * shell,
      y: y * shell,
      z: Math.sin(theta) * ring * shell,
      signal: i % 61 === 0,
      size: i % 17 === 0 ? 1.65 : 0.65 + (i % 7) * 0.1,
    });
    projected.push({ x: 0, y: 0, z: 0, scale: 1 });
  }
  particles.forEach((point, i) => {
    for (const offset of [13, 21, 34]) {
      const j = i + offset;
      if (j >= count || (i + j) % 3 === 0) continue;
      const other = particles[j];
      const distance = Math.hypot(point.x - other.x, point.y - other.y, point.z - other.z);
      if (distance < 0.24) edges.push([i, j]);
    }
  });

  function project(x, y, z) {
    const yaw = angle + pointer.x * 0.13;
    const pitch = -0.12 + pointer.y * 0.10;
    const xx = x * Math.cos(yaw) + z * Math.sin(yaw);
    const zz = z * Math.cos(yaw) - x * Math.sin(yaw);
    const yy = y * Math.cos(pitch) - zz * Math.sin(pitch);
    const depth = y * Math.sin(pitch) + zz * Math.cos(pitch);
    const perspective = 3.8 / (3.8 - depth);
    return { x: centerX + xx * radius * perspective, y: centerY + yy * radius * perspective, z: depth, scale: perspective };
  }

  function orbitPoint(orbit, t) {
    const x = Math.cos(t) * orbit.radius;
    const y = Math.sin(t) * orbit.radius * Math.sin(orbit.tilt);
    const z = Math.sin(t) * orbit.radius * Math.cos(orbit.tilt);
    return project(x * Math.cos(orbit.turn) - y * Math.sin(orbit.turn), x * Math.sin(orbit.turn) + y * Math.cos(orbit.turn), z);
  }

  function glow(x, y, size, color, opacity) {
    const gradient = ctx.createRadialGradient(x, y, 0, x, y, size);
    gradient.addColorStop(0, rgba(color, opacity));
    gradient.addColorStop(0.32, rgba(color, opacity * 0.34));
    gradient.addColorStop(1, rgba(color, 0));
    ctx.fillStyle = gradient;
    ctx.fillRect(x - size, y - size, size * 2, size * 2);
  }

  function drawOrbit(orbit, front) {
    ctx.lineWidth = 0.65;
    ctx.strokeStyle = front ? rgba(tint, 0.24) : 'rgba(136,135,237,0.11)';
    ctx.beginPath();
    let previous = orbitPoint(orbit, 0);
    for (let i = 1; i <= 112; i += 1) {
      const next = orbitPoint(orbit, i / 112 * TAU);
      if ((next.z >= 0) === front) {
        ctx.moveTo(previous.x, previous.y);
        ctx.lineTo(next.x, next.y);
      }
      previous = next;
    }
    ctx.stroke();

    const travel = clock * speed * 1.7 + orbit.offset;
    const head = orbitPoint(orbit, travel);
    if ((head.z >= 0) !== front) return;
    for (let i = 0; i < 15; i += 1) {
      const a = orbitPoint(orbit, travel - i * 0.023);
      const b = orbitPoint(orbit, travel - (i + 1) * 0.023);
      ctx.strokeStyle = rgba(tint, (1 - i / 15) * (front ? 0.72 : 0.27));
      ctx.lineWidth = 1.45;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }
    glow(head.x, head.y, 12, tint, front ? 0.4 : 0.18);
    ctx.fillStyle = front ? '#e7fff8' : rgba(tint, 0.55);
    ctx.beginPath();
    ctx.arc(head.x, head.y, front ? 2 : 1.3, 0, TAU);
    ctx.fill();
  }

  function render() {
    if (destroyed || !width || !height) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.globalCompositeOperation = 'lighter';
    glow(centerX - radius * 0.34, centerY + radius * 0.06, radius * 1.65, [83, 69, 232], 0.19);
    glow(centerX + radius * 0.18, centerY - radius * 0.22, radius * 1.18, [61, 137, 250], 0.16);
    glow(centerX, centerY, radius * 0.62, [177, 218, 255], 0.12);
    orbits.forEach((orbit) => drawOrbit(orbit, false));

    particles.forEach((point, index) => {
      const p = project(point.x, point.y, point.z);
      if (pointer.active && !motion.matches) {
        const dx = pointer.localX - p.x;
        const dy = pointer.localY - p.y;
        const distance = Math.hypot(dx, dy);
        const attraction = Math.max(0, 1 - distance / 115) * 0.065;
        p.x += dx * attraction;
        p.y += dy * attraction;
      }
      projected[index] = p;
    });

    for (const [i, j] of edges) {
      const a = projected[i];
      const b = projected[j];
      const depth = (a.z + b.z) * 0.5;
      const pulse = Math.max(0, Math.cos(i * 0.117 - clock * 0.65) - 0.83) / 0.17;
      const nearPointer = pointer.active && !motion.matches
        ? Math.max(0, 1 - Math.hypot(a.x - pointer.localX, a.y - pointer.localY) / 90)
        : 0;
      const highlight = Math.max(pulse * 0.45, nearPointer * 0.8);
      ctx.strokeStyle = rgba(highlight > 0.25 ? tint : [128, 164, 230], 0.035 + (depth + 1) * 0.035 + highlight * 0.4);
      ctx.lineWidth = highlight > 0.25 ? 0.85 : 0.5;
      ctx.beginPath();
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
      ctx.stroke();
    }

    // Back and front passes preserve depth without sorting the cloud every frame.
    for (let pass = 0; pass < 2; pass += 1) {
      particles.forEach((point, i) => {
        const p = projected[i];
        if ((p.z >= 0 ? 1 : 0) !== pass) return;
        const depth = (p.z + 1) * 0.5;
        const luminance = 0.17 + depth * 0.66;
        const color = point.signal ? [151, 247, 212] : p.z > 0.3 ? [192, 224, 255] : [123, 140, 218];
        if (point.signal && p.z > 0) glow(p.x, p.y, 7, color, 0.32);
        ctx.fillStyle = rgba(color, luminance);
        ctx.beginPath();
        ctx.arc(p.x, p.y, point.size * p.scale * (width <= 600 ? 0.8 : 1), 0, TAU);
        ctx.fill();
      });
    }
    orbits.forEach((orbit) => drawOrbit(orbit, true));
    ctx.globalCompositeOperation = 'source-over';
  }

  function canAnimate() {
    return !destroyed && root.isConnected && inView && pageActive && !document.hidden
      && !motion.matches && !userPaused && width > 0 && height > 0 && window.getComputedStyle(root).visibility !== 'hidden';
  }

  function stop() {
    if (frame) window.cancelAnimationFrame(frame);
    frame = 0;
    lastTime = 0;
    root.dataset.intelligenceRunning = 'false';
  }

  function tick(time) {
    frame = 0;
    if (!root.isConnected) { destroy(); return; }
    if (!canAnimate()) { stop(); return; }
    const dt = lastTime ? Math.min((time - lastTime) / 1000, 0.05) : 0;
    lastTime = time;
    clock += dt;
    const ease = 1 - Math.exp(-dt * 3.5);
    speed += (phases[phase].speed - speed) * ease;
    angle += dt * speed;
    pointer.x += (pointer.aimX - pointer.x) * ease;
    pointer.y += (pointer.aimY - pointer.y) * ease;
    tint.forEach((value, index) => { tint[index] = value + (phases[phase].color[index] - value) * ease; });
    render();
    frame = window.requestAnimationFrame(tick);
  }

  function sync() {
    if (destroyed) return;
    if (canAnimate()) {
      root.dataset.intelligenceRunning = 'true';
      if (!frame) frame = window.requestAnimationFrame(tick);
    } else {
      stop();
    }
  }

  function resize() {
    if (destroyed) return;
    width = root.clientWidth;
    height = root.clientHeight;
    dpr = Math.min(window.devicePixelRatio || 1, 2.5);
    canvas.width = Math.max(1, Math.round(width * dpr));
    canvas.height = Math.max(1, Math.round(height * dpr));
    const compact = width <= 600;
    centerX = width * (compact ? 0.46 : 0.40);
    centerY = height * 0.47;
    radius = Math.min(width * (compact ? 0.34 : 0.285), height * 0.34);
    render();
    sync();
  }

  function watchPixels() {
    if (pixelQuery && pixelListener) pixelQuery.removeEventListener('change', pixelListener);
    pixelQuery = window.matchMedia(`(resolution: ${window.devicePixelRatio || 1}dppx)`);
    pixelListener = () => { resize(); watchPixels(); };
    pixelQuery.addEventListener('change', pixelListener, { once: true });
  }

  function readStage() {
    const selected = demo?.querySelector('.creation-tab[aria-selected="true"]');
    const index = selected ? [...demo.querySelectorAll('.creation-tab')].indexOf(selected) : 0;
    phase = Math.max(0, Math.min(2, index));
    root.dataset.intelligenceStage = String(phase);
    if (!canAnimate()) {
      tint.splice(0, 3, ...phases[phase].color);
      speed = phases[phase].speed;
      render();
    }
  }

  function connectCreation() {
    const next = document.getElementById('creation-demo');
    if (!next || next === demo) return;
    demo = next;
    stageObserver?.disconnect();
    stageObserver = new MutationObserver(readStage);
    stageObserver.observe(demo, { subtree: true, childList: true, attributes: true, attributeFilter: ['aria-selected'] });
    readStage();
  }

  root.addEventListener('pointermove', (event) => {
    if (motion.matches || userPaused || event.pointerType === 'touch') return;
    const rect = root.getBoundingClientRect();
    pointer.localX = event.clientX - rect.left;
    pointer.localY = event.clientY - rect.top;
    pointer.aimX = Math.max(-1, Math.min(1, (pointer.localX - centerX) / radius));
    pointer.aimY = Math.max(-1, Math.min(1, (pointer.localY - centerY) / radius));
    pointer.active = true;
  }, { passive: true, signal: events.signal });
  const releasePointer = () => { pointer.active = false; pointer.aimX = 0; pointer.aimY = 0; };
  root.addEventListener('pointerleave', releasePointer, { signal: events.signal });
  root.addEventListener('pointercancel', releasePointer, { signal: events.signal });
  document.addEventListener('zan:motion-change', (event) => {
    userPaused = Boolean(event.detail?.paused);
    if (userPaused) releasePointer();
    sync();
  }, { signal: events.signal });
  document.addEventListener('visibilitychange', sync, { signal: events.signal });
  window.addEventListener('pagehide', () => { pageActive = false; sync(); }, { signal: events.signal });
  window.addEventListener('pageshow', () => { pageActive = true; resize(); sync(); }, { signal: events.signal });
  motion.addEventListener('change', () => {
    releasePointer();
    pointer.x = 0;
    pointer.y = 0;
    render();
    sync();
  }, { signal: events.signal });
  window.addEventListener('resize', resize, { passive: true, signal: events.signal });
  if ('ResizeObserver' in window) {
    resizeObserver = new ResizeObserver(resize);
    resizeObserver.observe(root);
  }
  if ('IntersectionObserver' in window) {
    intersectionObserver = new IntersectionObserver(([entry]) => {
      inView = entry.isIntersecting && entry.intersectionRatio > 0;
      sync();
    }, { threshold: [0, 0.01] });
    intersectionObserver.observe(root);
  } else {
    const checkViewport = () => {
      const rect = root.getBoundingClientRect();
      inView = rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.top < window.innerHeight
        && rect.right > 0 && rect.left < window.innerWidth;
      sync();
    };
    window.addEventListener('scroll', checkViewport, { passive: true, capture: true, signal: events.signal });
    window.addEventListener('resize', checkViewport, { passive: true, signal: events.signal });
    checkViewport();
  }
  appearanceObserver = new MutationObserver(() => { resize(); connectCreation(); });
  for (let ancestor = root; ancestor; ancestor = ancestor.parentElement) {
    appearanceObserver.observe(ancestor, { attributes: true, attributeFilter: ['hidden', 'style', 'class'] });
  }
  // The demo may mount after this module during the same DOMContentLoaded dispatch.
  document.addEventListener('DOMContentLoaded', connectCreation, { once: true, signal: events.signal });
  window.addEventListener('load', connectCreation, { once: true, signal: events.signal });

  function destroy() {
    if (destroyed) return;
    stop();
    destroyed = true;
    events.abort();
    resizeObserver?.disconnect();
    intersectionObserver?.disconnect();
    stageObserver?.disconnect();
    appearanceObserver?.disconnect();
    pixelQuery?.removeEventListener('change', pixelListener);
    for (const [name, value] of [['data-intelligence-stage', originalStage], ['data-intelligence-running', originalRunning]]) {
      if (value === null) root.removeAttribute(name);
      else root.setAttribute(name, value);
    }
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    canvas.width = 1;
    canvas.height = 1;
    intelligenceInstances.delete(root);
  }

  intelligenceInstances.set(root, destroy);
  readStage();
  connectCreation();
  resize();
  watchPixels();
  sync();
  return destroy;
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => mountIntelligenceScene(), { once: true });
} else {
  mountIntelligenceScene();
}
