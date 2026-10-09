import { spawn } from 'child_process';
import path from 'path';

const LSP = path.resolve('build/zan-lsp.exe');
const child = spawn(LSP, [], { stdio: ['pipe', 'pipe', 'pipe'] });

let sbuf = Buffer.alloc(0);
const pending = new Map();
let nextId = 1;

child.stderr.on('data', (d) => process.stderr.write('[lsp-err] ' + d));

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
      p.resolve(msg.result);
    }
  }
});

function request(method, params) {
  const id = nextId++;
  const body = JSON.stringify({ jsonrpc: '2.0', id, method, params });
  const frame = Buffer.from(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`, 'utf8');
  return new Promise((resolve) => {
    pending.set(id, { resolve });
    child.stdin.write(frame);
  });
}

function notify(method, params) {
  const body = JSON.stringify({ jsonrpc: '2.0', method, params });
  const frame = Buffer.from(`Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`, 'utf8');
  child.stdin.write(frame);
}

async function main() {
  await request('initialize', {
    rootUri: 'file:///D:/project/zan-lang',
    capabilities: {}
  });
  notify('initialized', {});

  const testUri = 'file:///D:/project/zan-lang/scripts/test_chain_probe.zan';
  const code = `using System;
using System.Text;

class Bar {
    int Value() { return 42; }
}

class Foo {
    Bar GetBar() { return new Bar(); }
    void Run() {
        Foo f = new Foo();
        f.GetBar().
    }
}
`;

  notify('textDocument/didOpen', {
    textDocument: {
      uri: testUri,
      languageId: 'zan',
      version: 1,
      text: code
    }
  });

  // Find the exact line and character for "f.GetBar()."
  const lines1 = code.split('\n');
  const l1_idx = lines1.findIndex(l => l.includes('f.GetBar().'));
  const c1_idx = lines1[l1_idx].indexOf('f.GetBar().') + 'f.GetBar().'.length;

  const res1 = await request('textDocument/completion', {
    textDocument: { uri: testUri },
    position: { line: l1_idx, character: c1_idx }
  });

  const rawItems1 = Array.isArray(res1) ? res1 : (res1 && res1.items ? res1.items : []);
  const items1 = rawItems1.map(x => x.label);
  console.log('Case 1 (f.GetBar().): count =', items1.length, 'items =', items1);
  if (!items1.includes('Value')) {
    console.error('FAIL: Case 1 missing Value');
    process.exit(1);
  }

  const code2 = `using System;
using System.Text;

class App {
    void Main() {
        StringBuilder sb = new StringBuilder();
        sb.Append("abc").ToString().
    }
}
`;
  notify('textDocument/didChange', {
    textDocument: { uri: testUri, version: 2 },
    contentChanges: [{ text: code2 }]
  });

  const lines2 = code2.split('\n');
  const l2_idx = lines2.findIndex(l => l.includes('sb.Append("abc").ToString().'));
  const c2_idx = lines2[l2_idx].indexOf('sb.Append("abc").ToString().') + 'sb.Append("abc").ToString().'.length;

  const res2 = await request('textDocument/completion', {
    textDocument: { uri: testUri },
    position: { line: l2_idx, character: c2_idx }
  });

  const rawItems2 = Array.isArray(res2) ? res2 : (res2 && res2.items ? res2.items : []);
  const items2 = rawItems2.map(x => x.label);
  console.log('Case 2 (sb.Append("abc").ToString().): count =', items2.length, 'items =', items2);
  if (!items2.includes('Length') || !items2.includes('Substring')) {
    console.error('FAIL: Case 2 missing string members');
    process.exit(1);
  }

  console.log('ALL LSP CHAIN PROBE TESTS PASSED.');
  child.kill();
  process.exit(0);
}

main().catch(err => {
  console.error(err);
  child.kill();
  process.exit(1);
});
