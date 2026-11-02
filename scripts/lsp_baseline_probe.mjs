// scripts/lsp_baseline_probe.mjs -- zan-lsp baseline measurement.
// Speaks LSP over stdio to build/zan-lsp.exe and reports: initialize time,
// open->diagnostics latency, completion P50/P90 + hit-rate, goto-definition
// results, documentSymbol, didChange->diagnostics (quiet-coalesced) and
// completion-after-change latency.
// Baseline numbers + findings: TASKS.md 四期1/四期2 entries.
// Usage:  node scripts/lsp_baseline_probe.mjs            # mid-size root
//         LSP_PROBE_MODE=repo node scripts/...           # monorepo root
import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

// MODE=repo (whole zan-lang monorepo, worst case)
// MODE=gallery (examples/gui_gallery, mid-size root)
// default 'gallery': fast and representative. 'repo' exercises the
// full monorepo workspace (17s first-open converge, see TASKS.md).
//
// Roots are derived from this script's location, never from a checked-in
// absolute path: ZAN_REPO overrides the checkout, LSP_PROBE_ROOT points the
// probe at an external project (the ra2 mode).
const REPO_ROOT = process.env.ZAN_REPO
  || path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const MODE = process.env.LSP_PROBE_MODE || 'gallery';
const ROOT = process.env.LSP_PROBE_ROOT
  || (MODE === 'gallery' ? path.join(REPO_ROOT, 'examples', 'gui_gallery') : REPO_ROOT);
const LSP = process.env.ZAN_LSP || path.join(REPO_ROOT, 'build', 'zan-lsp.exe');
const perf = () => Number(process.hrtime.bigint() / 1000n) / 1000; // ms float

const child = spawn(LSP, [], { stdio: ['pipe', 'pipe', 'pipe'] });
let sbuf = Buffer.alloc(0);
const pending = new Map();
let nextId = 1;
const notes = [];

child.stdout.on('data', (chunk) => {
  sbuf = Buffer.concat([sbuf, chunk]);
  for (;;) {
    const head = sbuf.indexOf('\r\n\r\n');
    if (head < 0) break;
    const header = sbuf.slice(0, head).toString('utf8');
    const m = header.match(/Content-Length:\s*(\d+)/i);
    if (!m) { sbuf = sbuf.slice(head + 4); continue; }
    const len = parseInt(m[1], 10);
    if (sbuf.length < head + 4 + len) break;
    const body = sbuf.slice(head + 4, head + 4 + len).toString('utf8');
    sbuf = sbuf.slice(head + 4 + len);
    let msg;
    try { msg = JSON.parse(body); } catch { continue; }
    if (msg.id !== undefined && pending.has(msg.id)) {
      const p = pending.get(msg.id);
      pending.delete(msg.id);
      p.resolve({ result: msg.result, ms: perf() - p.t0 });
    } else if (msg.method) {
      notes.push(msg);
    }
  }
});
child.stderr.on('data', (d) => process.stderr.write('[lsp-err] ' + d));

function send(method, params) {
  const body = JSON.stringify({ jsonrpc: '2.0', method, params });
  const frame = Buffer.from(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`, 'utf8');
  child.stdin.write(frame);
}
function request(method, params) {
  const id = nextId++;
  const body = JSON.stringify({ jsonrpc: '2.0', id, method, params });
  const frame = Buffer.from(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`, 'utf8');
  return new Promise((resolve) => {
    pending.set(id, { resolve, t0: perf() });
    child.stdin.write(frame);
  });
}
function waitFor(method, uri, timeoutMs) {
  const t0 = perf();
  return new Promise((resolve) => {
    const tick = () => {
      const i = notes.findIndex(n => n.method === method &&
        (!uri || JSON.stringify(n.params || {}).includes(uri)));
      if (i >= 0) { notes.splice(i, 1); resolve(perf() - t0); return; }
      if (perf() - t0 > timeoutMs) { resolve(-1); return; }
      setTimeout(tick, 5);
    };
    tick();
  });
}

// like waitFor, but resolves with the notification itself (or null)
function waitForMsg(method, uri, timeoutMs) {
  return new Promise((resolve) => {
    const tick = () => {
      const i = notes.findIndex(n => n.method === method &&
        (!uri || JSON.stringify(n.params || {}).includes(uri)));
      if (i >= 0) { resolve(notes.splice(i, 1)[0]); return; }
      if (perf() - t0 > timeoutMs) { resolve(null); return; }
      setTimeout(tick, 5);
    };
    const t0 = perf();
    tick();
  });
}
const fileUri = (p) => 'file:///' + p.replace(/\\/g, '/');

// ---------- workload ----------
const DOC_FILES = {
  repo: [
    'tests/gui/compref_designer_test.zan',
    'examples/gui_gallery/gui_gallery.zan',
    'src/ide_zan/src/shell/ZanIDE.zan',
  ],
  ra2: [
    'src/Assets/AssetDb.zan',
    'src/game/GameController.zan',
    'src/main.zan',
  ],
  gallery: [
    'gui_gallery.zan',
  ],
}[MODE];
const docs = new Map();
for (const rel of DOC_FILES) {
  const p = path.join(ROOT, rel);
  docs.set(fileUri(p), { rel, text: fs.readFileSync(p, 'utf8') });
}

// completion query sites: [fileRel, regex to find the query line, cursor
// placed right after the regex match, expected substring in results]
const QUERIES = {
  repo: [
    ['tests/gui/compref_designer_test.zan', /d\.SetUserComponents\(comps\);/, 2, 'SetUserComponents'],
    ['tests/gui/compref_designer_test.zan', /string saved = d\.SaveJson\(\);/, 17, 'SaveJson'],
    ['tests/gui/compref_designer_test.zan', /d\.LoadJson\(saved\);/, 2, 'LoadJson'],
    ['tests/gui/compref_designer_test.zan', /comps\.Add\(new UserComponent\(/, 6, 'Add'],
    ['examples/gui_gallery/gui_gallery.zan', /app\.RequestRedraw\(\);/, 4, 'RequestRedraw'],
    ['examples/gui_gallery/gui_gallery.zan', /app\.Post\(/, 4, 'Post'],
    ['src/ide_zan/src/shell/ZanIDE.zan', /File\.WriteAllText\(/, 5, 'WriteAllText'],
    ['src/ide_zan/src/shell/ZanIDE.zan', /Directory\.CreateDirectoryRecursive\(/, 10, 'CreateDirectoryRecursive'],
    ['src/ide_zan/src/shell/ZanIDE.zan', /log\.Add\(/, 4, 'Add'],
  ],
  ra2: [
    ['src/Assets/AssetDb.zan', /l\.Add\("expandmd03\.mix"\);/, 2, 'Add'],
    ['src/Assets/AssetDb.zan', /l\.Add\("ra2\.mix"\);/, 2, 'Add'],
    ['src/main.zan', /using System\.Collections\.Generic;/, 0, 'System'],
  ],
  gallery: [
    ['gui_gallery.zan', /app\.RequestRedraw\(\);/, 4, 'RequestRedraw'],
    ['gui_gallery.zan', /app\.Post\(/, 4, 'Post'],
  ],
}[MODE];
const DEFS = {
  repo: [
    ['tests/gui/compref_designer_test.zan', /d\.SetUserComponents\(comps\);/, 17, 'SetUserComponents'],
    ['src/ide_zan/src/shell/ZanIDE.zan', /File\.WriteAllText\(/, 10, 'WriteAllText'],
  ],
  ra2: [
    ['src/Assets/AssetDb.zan', /l\.Add\("expandmd03\.mix"\);/, 4, 'Add'],
  ],
  gallery: [
    ['gui_gallery.zan', /app\.RequestRedraw\(\);/, 8, 'RequestRedraw'],
  ],
}[MODE];

function findPos(rel, re, off) {
  const uri = fileUri(path.join(ROOT, rel));
  const text = docs.get(uri).text.replace(/\r\n/g, '\n');
  const lines = text.split('\n');
  for (let i = 0; i < lines.length; i++) {
    const m = lines[i].match(re);
    if (m) {
      const ch = m.index + Math.min(off, m[0].length);
      return { uri, line: i, character: ch };
    }
  }
  return null;
}

const pct = (arr, p) => {
  if (!arr.length) return -1;
  const s = [...arr].sort((a, b) => a - b);
  return s[Math.min(s.length - 1, Math.floor(p * s.length))];
};

// ---------- run ----------
const t_init0 = perf();
await request('initialize', {
  processId: process.pid,
  rootUri: fileUri(ROOT),
  capabilities: {},
});
const initMs = perf() - t_init0;
send('initialized', {});

const openMs = [];
for (const [uri, d] of docs) {
  const t0 = perf();
  send('textDocument/didOpen', {
    textDocument: { uri, languageId: 'zan', version: 1, text: d.text },
  });
  const diagMs = await waitFor('textDocument/publishDiagnostics', uri, 120000);
  openMs.push({ rel: d.rel, bytes: Buffer.byteLength(d.text), diagMs, total: perf() - t0 });
}

const compMs = [], hits = [], misses = [];
for (const [rel, re, off, expected] of QUERIES) {
  const pos = findPos(rel, re, off);
  if (!pos) { misses.push(rel + ' site-not-found ' + String(re)); continue; }
  const r = await request('textDocument/completion', {
    textDocument: { uri: pos.uri },
    position: { line: pos.line, character: pos.character },
  });
  compMs.push(r.ms);
  const items = Array.isArray(r.result) ? r.result
    : (r.result && Array.isArray(r.result.items) ? r.result.items : []);
  const lbl = (it) => (it.label || '') + '|' + (it.insertText || '');
  const hit = items.some(it => lbl(it).includes(expected));
  (hit ? hits : misses).push(`${rel.split('/').pop()} ~${String(re).slice(0, 24)} -> ${expected}${hit ? '' : ` (got ${items.length} items)`}`);
}

const defMs = [], defOk = [];
for (const [rel, re, off, expect] of DEFS) {
  const pos = findPos(rel, re, off);
  if (!pos) { defOk.push('site-not-found'); continue; }
  const r = await request('textDocument/definition', {
    textDocument: { uri: pos.uri },
    position: { line: pos.line, character: pos.character },
  });
  defMs.push(r.ms);
  const res = Array.isArray(r.result) ? r.result : (r.result ? [r.result] : []);
  defOk.push(res.length > 0 ? 'ok' : 'empty');
}

const symRel = { ra2: 'src/game/GameController.zan', gallery: 'gui_gallery.zan' }[MODE] || 'src/ide_zan/src/shell/ZanIDE.zan';
const symUri = fileUri(path.join(ROOT, symRel));
const sym = await request('textDocument/documentSymbol', { textDocument: { uri: symUri } });
const symCount = Array.isArray(sym.result) ? sym.result.length : -1;
console.log(`documentSymbol (${symRel}): ${sym.ms.toFixed(0)} ms, ${symCount} top-level symbols`);

// didChange -> diagnostics (server coalesces edits with a ~200ms quiet
// window, so the metric reads quiet+run; the win is that requests no
// longer wait behind the front-end pass — see completion-after-change).
// Rounds 0-2 send full-text edits (legacy client shape), 3-5 send range
// edits (incremental sync). The garbage rounds assert the front-end
// actually saw the spliced text; the clean rounds assert it was removed.
const smallRel = { ra2: 'src/main.zan', gallery: 'gui_gallery.zan' }[MODE] || 'tests/gui/compref_test.zan';
const smallUri = fileUri(path.join(ROOT, smallRel));
const smallText = fs.readFileSync(path.join(ROOT, smallRel), 'utf8');
if (!docs.has(smallUri)) {
  send('textDocument/didOpen', { textDocument: { uri: smallUri, languageId: 'zan', version: 1, text: smallText } });
  await waitFor('textDocument/publishDiagnostics', smallUri, 60000);
}
const cleanErrCount = (() => {
  const m = notes.find(n => n.method === 'textDocument/publishDiagnostics' && JSON.stringify(n.params || {}).includes(smallUri));
  return m ? (m.params.diagnostics || []).length : -1;
})();
const lineCount = smallText.split('\n').length; // append position for range edits
const chMs = [], compAfterChMs = [], chFails = [];
let version = 2;
for (let i = 0; i < 6; i++) {
  const incremental = i >= 3;
  const garbage = (i === 0 || i === 3); // syntax error the front-end must see
  const line = garbage ? '???garbage' + i : '// probe edit ' + i;
  const edits = incremental
    ? [{ range: { start: { line: lineCount, character: 0 }, end: { line: lineCount, character: 0 } }, text: line + '\n' }]
    : [{ text: smallText + '\n' + line + '\n' }];
  const t0 = perf();
  send('textDocument/didChange', {
    textDocument: { uri: smallUri, version: version++ },
    contentChanges: edits,
  });
  // typing responsiveness: a completion fired mid-edit must not queue
  // behind the diagnostics run
  const tc = perf();
  await request('textDocument/completion', {
    textDocument: { uri: smallUri },
    position: { line: lineCount + 1, character: 0 },
  });
  compAfterChMs.push(perf() - tc);
  const msg = await waitForMsg('textDocument/publishDiagnostics', smallUri, 60000);
  if (!msg) { chFails.push(i); continue; }
  chMs.push(perf() - t0);
  const errCount = (msg.params.diagnostics || []).filter(d => d.severity === 1).length;
  if (garbage && errCount === 0) chFails.push(i + ':garbage-not-seen');
  if (!garbage && cleanErrCount >= 0 && errCount !== cleanErrCount)
    chFails.push(i + ':edit-leaked(err=' + errCount + ',want=' + cleanErrCount + ')');
}

await request('shutdown', null);
send('exit', null);
child.kill();

// ---------- report ----------
console.log('=== zan-lsp baseline (四期1) ===');
console.log(`initialize: ${initMs.toFixed(0)} ms`);
for (const o of openMs) console.log(`open+diagnose ${o.rel} (${o.bytes} B): diag ${o.diagMs.toFixed(0)} ms`);
console.log(`completion: n=${compMs.length} P50=${pct(compMs, .5).toFixed(1)} P90=${pct(compMs, .9).toFixed(1)} max=${pct(compMs, 1).toFixed(1)} ms`);
console.log(`hit-rate: ${hits.length}/${hits.length + misses.filter(m => !m.includes('site-not-found')).length}`);
for (const m of misses) console.log('  miss/skip: ' + m);
for (const h of hits) console.log('  hit: ' + h);
console.log(`definition: n=${defMs.length} P50=${pct(defMs, .5).toFixed(1)} ms; results: ${defOk.join(',')}`);
console.log(`didChange->diag (quiet-coalesced; full+range edits): n=${chMs.length} P50=${pct(chMs, .5).toFixed(1)} P90=${pct(chMs, .9).toFixed(1)} ms${chFails.length ? ' FAIL:[' + chFails.join(',') + ']' : ' ok'}`);
console.log(`completion-after-change: n=${compAfterChMs.length} P50=${pct(compAfterChMs, .5).toFixed(1)} P90=${pct(compAfterChMs, .9).toFixed(1)} max=${pct(compAfterChMs, 1).toFixed(1)} ms`);
