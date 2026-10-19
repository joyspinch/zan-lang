// 出片脚本（本片自带）：与 promo-video/scripts/encode.mjs 同流程，差别有三：
//   ① ratio 显式传 "16:9" / "9:16"（页面据此切版式，横竖同一份源码）；
//   ② 超时放宽到 20 分钟（skill 版硬编码 180s，66s×60fps 跑不完）；
//   ③ 禁用窗口遮挡节流（--disable-backgrounding-occluded-windows 等），
//      否则不可见窗口会停发 rAF / 定时器，逐帧循环挂死。
// 用法: node video/basics/encode.mjs <项目目录> <fps> <宽> <高> [时长秒数(调试短样片)]
// 例:   node video/basics/encode.mjs video/basics 30 1080 1920      # 竖版主交付
//       node video/basics/encode.mjs video/basics 8  1080 1920      # 8 秒竖版样片
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';

const projectDir = path.resolve(process.argv[2] || '.');
const FPS = Number(process.argv[3]) || 60;
const W = Number(process.argv[4]) || 1920;
const H = Number(process.argv[5]) || 1080;
const RATIO = W > H ? '16:9' : '9:16';
const DUR = Number(process.argv[6]) || 0;
const OUT = path.join(projectDir, 'export');
const CHROME = process.env.CHROME_PATH || 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
fs.mkdirSync(OUT, { recursive: true });

const TYPES = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.png': 'image/png', '.css': 'text/css' };
const server = http.createServer((req, res) => {
  const u = decodeURIComponent((req.url || '/').split('?')[0]);
  const p = path.join(projectDir, u === '/' ? 'index.html' : u);
  if (!p.startsWith(projectDir)) { res.writeHead(403).end(); return; }
  fs.readFile(p, (e, d) => {
    if (e) { res.writeHead(404).end('404'); return; }
    res.writeHead(200, { 'Content-Type': TYPES[path.extname(p)] || 'application/octet-stream' });
    res.end(d);
  });
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

const url = `http://127.0.0.1:${port}/index.html?encode=1&fps=${FPS}&w=${W}&h=${H}&ratio=${encodeURIComponent(RATIO)}` + (DUR ? `&dur=${DUR}` : '');
console.log('encode', url);

const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'zanenc-'));
const dbg = 9800 + Math.floor(Math.random() * 150);
const chrome = spawn(CHROME, [
  `--app=${url}`, `--user-data-dir=${profile}`, `--remote-debugging-port=${dbg}`,
  `--window-size=${Math.min(W, 1920)},${Math.min(H, 1080)}`, '--window-position=0,0',
  '--no-first-run', '--no-default-browser-check', '--disable-extensions',
  '--autoplay-policy=no-user-gesture-required', '--force-color-profile=srgb',
  // 编码期间窗口可能失焦/被遮挡：禁掉后台节流，别让 rAF / 定时器停摆
  '--disable-backgrounding-occluded-windows', '--disable-renderer-backgrounding',
  '--disable-background-timer-throttling', '--disable-features=CalculateNativeWinOcclusion',
], { stdio: 'ignore' });

const sleep = ms => new Promise(r => setTimeout(r, ms));
let target;
for (let i = 0; i < 300 && !target; i++) {
  try {
    const list = await (await fetch(`http://127.0.0.1:${dbg}/json/list`)).json();
    target = list.find(t => t.type === 'page' && t.webSocketDebuggerUrl);
  } catch {}
  if (!target) await sleep(150);
}
if (!target) { console.error('Chrome 未就绪'); server.close(); process.exit(1); }

const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((res, rej) => { ws.onopen = res; ws.onerror = () => rej(new Error('ws')); });
let seq = 0; const pending = new Map();
ws.onmessage = ev => {
  const m = JSON.parse(ev.data);
  if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); m.error ? p.rej(new Error(JSON.stringify(m.error))) : p.res(m.result); }
};
const send = (method, params = {}) => new Promise((res, rej) => { const id = ++seq; pending.set(id, { res, rej }); ws.send(JSON.stringify({ id, method, params })); });

try { await send('Browser.setDownloadBehavior', { behavior: 'allow', downloadPath: OUT, eventsEnabled: true }); } catch {}
await send('Page.enable'); await send('Runtime.enable');

const deadline = Date.now() + 1200000;
let info = null, lastMsg = '';
let n = 0;
while (Date.now() < deadline) {
  const r = await send('Runtime.evaluate', { expression: 'JSON.stringify(window.__recording || null)', returnByValue: true });
  const v = r.result?.value;
  if (v && v !== 'null') { info = JSON.parse(v); break; }
  const e = await send('Runtime.evaluate', { expression: 'window.__encodeError || ""', returnByValue: true });
  if (e.result?.value) { console.log('\nENCODE-ERROR:', e.result.value); break; }
  const s = await send('Runtime.evaluate', { expression: "document.getElementById('rec')?.textContent || ''", returnByValue: true });
  const msg = s.result?.value || '';
  if (msg !== lastMsg) { lastMsg = msg; n++; if (n % 5 === 0) console.log('  ' + msg); }
  await sleep(1000);
}
console.log('\n', info || '超时');
await sleep(4000);

const files = fs.readdirSync(OUT).filter(f => /\.(mp4|webm)$/i.test(f));
for (const f of files) console.log('FILE', path.join(OUT, f), (fs.statSync(path.join(OUT, f)).size / 1048576).toFixed(1) + 'MB');
if (!files.length) console.log('没有拿到文件');

ws.close(); chrome.kill(); server.close();
try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
process.exit(0);
