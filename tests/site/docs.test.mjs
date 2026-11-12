import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, readdir, stat } from 'node:fs/promises';
import { resolve, dirname, relative, isAbsolute } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { spawn } from 'node:child_process';
import { documents } from '../../zan-site/src/documentation.js';
import { renderMarkdown, rebaseMarkdownLinks } from '../../zan-site/scripts/markdown.mjs';
import { loadPackages, siteBase } from '../../zan-site/scripts/generate-docs.mjs';

const root = fileURLToPath(new URL('../../', import.meta.url));
const output = resolve(root, 'build/zan-site');
const read = path => readFile(path, 'utf8');
const manifest = JSON.parse(await read(resolve(output, 'docs/manifest.json')));

async function filesAt(path) {
  const files = [];
  for (const entry of await readdir(path, { withFileTypes: true })) {
    if (entry.isDirectory()) files.push(...await filesAt(resolve(path, entry.name)));
    else if (entry.isFile()) files.push(resolve(path, entry.name));
  }
  return files;
}

function localPath(from, href) {
  if (!href || href === '#' || /^(?:https?:|mailto:|data:|tel:)/.test(href)) return null;
  const url = new URL(href.replace(/&amp;/g, '&'), `https://site.test/${relative(output, from).replaceAll('\\', '/')}`);
  const path = resolve(output, `.${decodeURIComponent(url.pathname)}`);
  const rel = relative(output, path);
  assert.ok(!rel.startsWith('..') && !isAbsolute(rel), `Link escapes site: ${href}`);
  return { path, fragment: decodeURIComponent(url.hash.slice(1)) };
}

async function assertTarget(from, href) {
  const target = localPath(from, href);
  if (!target) return;
  const info = await stat(target.path).catch(() => null);
  assert.ok(info, `${relative(output, from)} links to missing ${href}`);
  const path = info.isDirectory() ? resolve(target.path, 'index.html') : target.path;
  if (target.fragment && path.endsWith('.html')) {
    const content = await read(path);
    assert.ok(content.includes(`id="${target.fragment}"`) || content.includes(`id='${target.fragment}'`), `Missing anchor ${href} from ${relative(output, from)}`);
  }
}

test('every published chapter is complete without JavaScript and matches its AI text', async () => {
  const full = await read(resolve(output, 'llms-full.txt'));
  const search = JSON.parse(await read(resolve(output, 'docs/search.json')));
  assert.equal(manifest.documents.length, documents.length + 1);
  assert.equal(search.length, documents.length);
  for (const item of manifest.documents) {
    const markdown = await read(resolve(output, item.markdown));
    const html = await read(resolve(output, item.html));
    assert.equal(item.sha256, createHash('sha256').update(markdown).digest('hex'));
    assert.ok(markdown.length > 800, `Insufficient chapter body: ${item.id}`);
    assert.ok(html.includes(renderMarkdown(markdown).html), `HTML differs from Markdown: ${item.id}`);
    assert.ok(full.includes(`Source: ${item.markdown}\n\n${rebaseMarkdownLinks(markdown, item.markdown)}`), `Full text omits ${item.id}`);
    if (item.id !== 'index') assert.equal(search.find(record => record.id === item.id)?.text, markdown);
  }
});

test('all generated page, asset and Markdown links resolve under the deployment root', async () => {
  const files = (await filesAt(output)).filter(path => /\.(html|md|txt)$/.test(path));
  for (const path of files) {
    if (path.endsWith('robots.txt')) continue;
    const source = await read(path);
    const links = path.endsWith('.html') ? [...source.matchAll(/(?:href|src)=["']([^"']+)["']/g)].map(match => match[1]) : [...source.replace(/```[^\n]*\n[\s\S]*?```/g, '').matchAll(/\[[^\]]+\]\(([^\s)]+)\)/g)].map(match => match[1]);
    for (const href of links) await assertTarget(path, href);
  }
});

test('catalog includes every current manifest and actual namespaces without removed packages', async () => {
  const packages = await loadPackages(resolve(root, 'zan-site'));
  const published = JSON.parse(await read(resolve(output, 'docs/packages.json'))).packages;
  assert.deepEqual(published, packages);
  assert.equal(new Set(packages.map(item => item.name)).size, packages.length);
  assert.ok(packages.find(item => item.name === 'Zan.Formats'));
  assert.ok(packages.find(item => item.name === 'Zan.Ai').namespaces.includes('Ai'));
  assert.ok(!packages.find(item => item.name === 'Zan.Ai').namespaces.includes('Ai.Models'));
  assert.equal(packages.find(item => item.name === 'Zan.Commercial').license, 'Proprietary');
  const { ecosystemPackages, ecosystemGroups } = await import('../../build/zan-site/src/ecosystem.js');
  assert.deepEqual(ecosystemPackages.map(item => item.name), packages.map(item => item.name));
  assert.equal(ecosystemGroups.flatMap(item => item.packages).length, packages.length);
  for (const item of ecosystemPackages) {
    assert.ok(item.href.startsWith('./docs/packages.html#'), `Package lacks a direct documentation link: ${item.name}`);
    await assertTarget(resolve(output, 'index.html'), item.href);
  }
});

test('renderer escapes code and HTML and emits stable headings and useful links', () => {
  const rendered = renderMarkdown('# Test\n\n## 重复\n\n## 重复\n\n`<x>` & <script>x</script>\n\n[Types](types.md)\n\n[bad](javascript:alert)\n\n```zan\nif (a < b) { }\n```\n');
  assert.deepEqual(rendered.headings.map(item => item.id), ['test', '重复', '重复-2']);
  assert.ok(rendered.html.includes('href="types.html"'));
  assert.ok(rendered.html.includes('href="#"'));
  assert.ok(!rendered.html.includes('<script>'));
  assert.ok(rendered.html.includes('a &lt; b'));
  assert.throws(() => renderMarkdown('```zan\nmissing close'));
});

test('Chinese package labels are searchable on the homepage and in full documentation', async () => {
  const { searchItems } = await import('../../build/zan-site/src/content.js');
  const corpus = JSON.parse(await read(resolve(output, 'docs/search.json')));
  for (const [term, name] of [['条码', 'Zan.Barcode'], ['基准工具', 'Zan.Bench']]) {
    assert.ok(searchItems.some(item => item.package === name && item.keywords.includes(term)));
    assert.ok(corpus.find(item => item.id === 'packages').text.includes(term));
  }
  for (const item of searchItems.filter(item => item.kind === 'SECTION')) {
    await assertTarget(resolve(output, 'index.html'), `#${item.section}`);
  }
});

test('full text rebases relative links without changing examples', () => {
  const sample = '[Next](types.md#values) [Index](../llms.txt) [Web](https://example.test/)\n`[literal](types.md)`\n```zan\n// [literal](types.md)\n```';
  assert.equal(rebaseMarkdownLinks(sample, 'docs/language.md'), '[Next](docs/types.md#values) [Index](llms.txt) [Web](https://example.test/)\n`[literal](types.md)`\n```zan\n// [literal](types.md)\n```');
});

test('deployment base preserves subpaths and rejects invalid public URL configuration', () => {
  assert.equal(siteBase('https://example.test/zan').href, 'https://example.test/zan/');
  assert.equal(siteBase(), null);
  for (const url of ['file:///tmp/site', 'https://user:pass@example.test', 'https://example.test/?q=x', 'https://example.test/#x']) assert.throws(() => siteBase(url));
});

test('HTTP serves docs and AI exports directly with correct types, HEAD and real 404s', { timeout: 20000 }, async () => {
  const server = spawn(process.execPath, [resolve(root, 'zan-site/scripts/serve.mjs'), '--preview'], { cwd: root, env: { ...process.env, PORT: '0', HOST: '127.0.0.1' }, stdio: ['ignore', 'pipe', 'pipe'] });
  let stderr = '';
  server.stderr.on('data', chunk => { stderr += chunk; });
  try {
    const address = await new Promise((accept, reject) => {
      let text = '';
      server.stdout.on('data', chunk => { text += chunk; const match = text.match(/http:\/\/127\.0\.0\.1:\d+/); if (match) accept(match[0]); });
      server.once('error', reject);
      server.once('exit', code => reject(new Error(`Server exited ${code}: ${stderr}`)));
    });
    for (const [path, type, marker] of [['/docs/types.html', 'text/html', '<h1'], ['/docs/memory.md', 'text/markdown', '# '], ['/llms.txt', 'text/plain', 'docs/memory.md'], ['/llms-full.txt', 'text/plain', 'Task.Spawn'], ['/docs/packages.json', 'application/json', 'Zan.Formats']]) {
      const response = await fetch(address + path);
      assert.equal(response.status, 200, path);
      assert.ok(response.headers.get('content-type').startsWith(type));
      assert.ok((await response.text()).includes(marker));
    }
    const head = await fetch(`${address}/llms-full.txt`, { method: 'HEAD' });
    assert.equal(head.status, 200);
    assert.ok(Number(head.headers.get('content-length')) > 10000);
    assert.equal(await head.text(), '');
    assert.equal((await fetch(`${address}/docs/does-not-exist.html`)).status, 404);
    assert.equal((await fetch(`${address}/llms.txt`, { method: 'POST' })).status, 405);
  } finally {
    const exited = once(server, 'exit');
    server.kill();
    if (server.exitCode === null) await exited;
  }
});
