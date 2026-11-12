import { readFile, writeFile, mkdir, readdir, lstat, unlink } from 'node:fs/promises';
import { join } from 'node:path';
import { createHash } from 'node:crypto';
import { documents, documentationDate } from '../src/documentation.js';
import { platforms } from '../src/capabilities.js';
import { renderMarkdown, escapeHtml, rebaseMarkdownLinks } from './markdown.mjs';
import { packageTitles, packageDescriptions } from '../src/package-guide.js';

const read = path => readFile(path, 'utf8');
const hash = text => createHash('sha256').update(text).digest('hex');

export function siteBase(value = '') {
  if (!value) return null;
  const url = new URL(value);
  if (!['http:', 'https:'].includes(url.protocol) || url.search || url.hash || url.username || url.password) throw new Error('SITE_URL must be an HTTP(S) deployment URL without credentials, query or fragment');
  if (!url.pathname.endsWith('/')) url.pathname += '/';
  return url;
}

function page(document, rendered, base) {
  const current = documents.findIndex(item => item.id === document.id);
  const groups = [...new Set(documents.map(item => item.group))];
  const navigation = groups.map(group => `<p>${escapeHtml(group)}</p>${documents.filter(item => item.group === group).map(item => `<a href="./${item.id}.html"${item.id === document.id ? ' aria-current="page"' : ''}>${escapeHtml(item.title)}</a>`).join('')}`).join('');
  const next = documents[current + 1];
  const previous = documents[current - 1];
  const canonical = base ? new URL(`docs/${document.id}.html`, base).href : '';
  const socialImage = base ? new URL('assets/social.png', base).href : '../assets/social.png';
  return `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="${escapeHtml(document.description)}">
<meta name="theme-color" content="#080d1b">
<title>${escapeHtml(document.title)} · Zan 文档</title>
<meta property="og:title" content="${escapeHtml(document.title)} · Zan 文档">
<meta property="og:description" content="${escapeHtml(document.description)}">
<meta property="og:type" content="article">
<meta property="og:site_name" content="Zan">
<meta property="og:locale" content="zh_CN">
<meta property="og:image" content="${escapeHtml(socialImage)}">
<meta property="og:image:width" content="1500">
<meta property="og:image:height" content="500">
<meta property="og:image:alt" content="Zan 官方品牌横幅">
${canonical ? `<link rel="canonical" href="${escapeHtml(canonical)}">\n<meta property="og:url" content="${escapeHtml(canonical)}">` : ''}
<link rel="icon" href="../assets/mark.svg" type="image/svg+xml">
<link rel="stylesheet" href="../src/theme.css">
<link rel="stylesheet" href="../src/docs.css">
<link rel="stylesheet" href="../src/controls.css">
<link rel="alternate" type="text/markdown" href="./${document.id}.md" title="Markdown 正文">
<link rel="alternate" type="text/plain" href="../llms-full.txt" title="AI 完整文档">
<script type="module" src="../src/docs.js"></script>
</head>
<body>
<a class="doc-skip" href="#content">跳到正文</a>
<header class="doc-header">
  <div class="doc-header-inner">
    <a class="doc-brand" href="../index.html" aria-label="Zan 首页"><img class="doc-brand-mark" src="../assets/mark.svg" width="34" height="34" alt=""><img class="doc-brand-wordmark" src="../assets/wordmark.svg" width="65" height="33" alt="Zan"><span class="doc-brand-label">文档</span></a>
    <nav aria-label="文档工具"><a href="./index.html"${document.id === 'index' ? ' aria-current="page"' : ''}>文档目录与搜索</a><a href="../llms.txt">AI 索引</a><a href="../llms-full.txt">完整文本</a></nav>
  </div>
</header>
<div class="doc-layout">
<details class="doc-sidebar" open><summary>阅读目录</summary><nav aria-label="文档章节">${navigation}</nav></details>
<main id="content" class="doc-article" tabindex="-1"><p class="doc-eyebrow">ZAN DOCUMENTATION / ${escapeHtml(document.group)}</p><div class="doc-meta"><span>源码核对：${documentationDate}</span><a href="./${document.id}.md">读取 Markdown</a></div>
${rendered.html}
<nav class="doc-pager" aria-label="相邻文档">${previous ? `<a href="./${previous.id}.html">← ${escapeHtml(previous.title)}</a>` : '<a href="./index.html">← 文档目录</a>'}${next ? `<a href="./${next.id}.html">${escapeHtml(next.title)} →</a>` : '<a href="./index.html">返回目录 →</a>'}</nav>
</main>
<aside class="doc-toc" aria-label="本页目录"><p>本页内容</p>${rendered.headings.filter(item => item.level === 2).map(item => `<a href="#${item.id}">${escapeHtml(item.title)}</a>`).join('')}</aside>
</div>
<footer class="doc-footer"><p>本文按当前源码整理；实际可用接口以安装的 SDK、包版本与目标平台为准。文内“核实来源”是仓库或 SDK 内的路径。</p><div class="doc-footer-links"><a href="../agent-guide.md">AI development guide</a><a href="https://github.com/joyspinch/zanlang">获取 SDK</a></div></footer>
</body></html>\n`;
}

export async function loadPackages(root) {
  const directory = join(root, '../packages');
  const entries = (await readdir(directory, { withFileTypes: true })).filter(item => item.isDirectory() && item.name.startsWith('Zan.')).sort((a, b) => a.name.localeCompare(b.name, 'en'));
  const packages = [];
  for (const entry of entries) {
    const source = `packages/${entry.name}/zan.pkg`;
    let text;
    try { text = await read(join(root, '..', source)); }
    catch (error) { if (error.code === 'ENOENT') continue; throw error; }
    const clean = text.replace(/^\uFEFF/, '').trim();
    const metadata = ['name', 'version', 'description', 'author', 'license'];
    const raw = clean.startsWith('{') ? JSON.parse(clean) : Object.fromEntries(
      [...clean.split(/^\s*\[/m)[0].matchAll(/^\s*(\w+)\s*=\s*"([^"\r\n]*)"/gm)].map(match => [match[1], match[2]]),
    );
    const fields = Object.fromEntries(metadata.filter(key => typeof raw[key] === 'string').map(key => [key, raw[key]]));
    if (!fields.name || !fields.description) throw new Error(`Invalid package metadata: ${source}`);
    const status = /\[migration-in-progress\]/i.test(fields.description) ? '迁移中' : /\[experimental\]/i.test(fields.description) ? '实验性' : /\[frozen\]/i.test(fields.description) ? '冻结' : '源码可用';
    const namespaces = new Set();
    async function scanNamespaces(path) {
      let children;
      try { children = await readdir(path, { withFileTypes: true }); }
      catch (error) { if (error.code === 'ENOENT') return; throw error; }
      for (const child of children) {
        if (child.isDirectory()) await scanNamespaces(join(path, child.name));
        else if (child.isFile() && child.name.endsWith('.zan')) {
          const sourceText = await read(join(path, child.name));
          for (const match of sourceText.matchAll(/^\s*namespace\s+([A-Za-z_][\w.]*)\s*[;{]/gm)) namespaces.add(match[1]);
        }
      }
    }
    await scanNamespaces(join(directory, entry.name, 'src'));
    packages.push({ ...fields, namespaces: [...namespaces].sort(), source, status });
  }
  return packages;
}

function packagesMarkdown(packages) {
  const result = ['# 扩展包完整目录', '', `本目录由当前仓库的 ${packages.length} 份包清单生成。核对日期：${documentationDate}。包的存在、版本号和源码可用，不等于该版本 SDK 已随附，也不代表所有目标平台均通过验收。`, '', '包名用于安装与分发；代码中的 `using` 使用文件实际声明的命名空间。查询包时先用 `pkg_search` / `pkg_info`，核对版本、依赖与平台，再查具体 API。学习各层边界见[标准库与应用开发](libraries.md)。', '', '清单中的状态来自显式标记；未标记的项目记为“源码可用”，不据版本号推断成熟度。', '', '各包简介保留清单原文，描述可能沿用历史拆包名称。例如 Zan.DataFrame 清单中的 Zan.Csv 已归并到 Zan.Formats；当前安装名称以本目录标题为准，代码接口以实际命名空间与 SDK 查询为准。机器可读清单也保留原始描述，不能把描述中提到的旧名称当作现存包。', ''];
  for (const item of packages) {
    result.push(`## ${item.name}`, '', `**${packageTitles[item.name] || item.name}**`, '', ...(packageDescriptions[item.name] ? [`导览：${packageDescriptions[item.name]}`, ''] : []), `清单原文：${item.description}`, '', `- 状态：${item.status}`, `- 版本：${item.version || '未声明'}`, `- 命名空间：${item.namespaces.length ? item.namespaces.map(name => `\`${name}\``).join('、') : '以源码声明与 API 查询为准'}`, `- 许可：${item.license || '以包内许可文件为准'}`, `- 清单来源：\`${item.source}\``, '');
  }
  result.push('## 核实来源', '', '`packages/*/zan.pkg`（构建时读取）；详细使用方式和限制以同包 `README.md` 与源码为准。', '', '返回[文档目录](index.md) · [AI 使用说明](ai.md)', '');
  return result.join('\n');
}

function platformsMarkdown() {
  const result = ['# 平台支持与边界', '', `核对日期：${documentationDate}。下列状态分别描述编译、运行、GUI 和交付。构建成功不等同目标设备已经验收，存在目标后端也不等同有对应平台的完整 SDK 下载。`, '', '在本机执行 `zanc --list-targets` 获取当前工具链可用目标；它会受链接进编译器的 LLVM 后端影响。发布前还需核对系统版本、CPU 指令集、原生依赖、设备签名和包所支持的 API。', ''];
  for (const item of platforms) result.push(`## ${item.title}`, '', `**${item.status}** · ${item.arch}`, '', item.description, '', `- 编译：${item.compile}`, `- 运行：${item.run}`, `- GUI：${item.gui}`, `- 交付：${item.ship}`, `- 边界：${item.limit}`, '');
  result.push('## RISC-V 与嵌入式目标', '', '`linux-riscv64`（别名 `riscv64`）有 Linux 静态 musl 链接路径；`riscv32`（别名 `esp32c3`）面向裸机，目前只生成 ELF32 对象文件，启动代码和最终链接由外部工具链承担。RV32 QEMU 示例有运行记录，不能据此推断 ESP32 实物或 RISC-V GUI 已验收。', '', '当前源码配置的目标名称为 `win-x64`、`win-arm64`、`linux-x64`、`linux-musl`、`linux-arm64`、`macos-x64`、`macos-arm64`、`wasm32`、`linux-riscv64`、`riscv64`、`riscv32`、`esp32c3`、`android-x64`、`android-arm64`、`ohos-x64`、`ohos-arm64`、`ios-arm64`。这是含别名的配置表，实际可用名称以当前 `--list-targets` 为准。', '', '## 如何判断一个目标可用于自己的项目', '', '1. 用当前 SDK 查询目标名称，并编译最小程序。', '2. 核对应用实际使用的包、系统调用和原生驱动。', '3. 在目标系统运行，验证 I/O、字体、窗口、线程和退出清理。', '4. 检查发布目录、资源与签名，再进行真实安装验收。', '', '命令和发布选项见[工具链与发布](tooling.md)，跨平台原生接口见[原生互操作](interop.md)。', '', '## 核实来源', '', '`src/compiler/crosscomp.c`、`docs/ZANC_CLI.md`、`docs/platform-targets.md`、各平台 `src/drivers/` 实现与测试；站点平台状态统一维护于 `zan-site/src/capabilities.js`。历史文档与后续实现不一致时以实现及其实际验收记录为准。', '');
  return result.join('\n');
}

function indexMarkdown() {
  return ['# Zan 文档中心', '', '从语言语义到应用开发，为开发者和 AI 提供可以直接读取、引用和检索的完整入口。每篇页面和 Markdown 正文来自同一份内容，无需 JavaScript 也能阅读全文。', '', '## 从这里开始', '', '第一次接触 Zan：认识语言 → 安装与第一个程序 → 语法与类型 → 内存与异步。已有 C# 经验：优先核对 UTF-8 字符串、默认可见性、集合别名、ARC、异步结果与原生依赖，不能按 .NET 的语义推断。', '', ...documents.flatMap(item => [`- [${item.title}](${item.id}.md)：${item.description}`]), '', '## 交给 AI 阅读', '', '- [llms.txt](../llms.txt)：文档索引与推荐读取顺序。', '- [llms-full.txt](../llms-full.txt)：所有主题正文与完整包目录，可作为上下文或检索资料。', '- [English agent guide](../agent-guide.md)：英文接入说明与关键语义摘要。', '- [结构化文档清单](./manifest.json)：页面地址、摘要、来源与内容 SHA-256。', '- [机器可读包清单](./packages.json)：当前构建读取的全部包清单。', '', '这些文件让模型可以检索与引用资料；是否读取、能读多长以及如何使用上下文，取决于所用 AI 工具。实际编程仍需查询当前 SDK 的 API 并编译运行。', '', '## 查找具体问题', '', '可以在本页搜索正文，也可以直接下载完整文本，用编辑器查找 `weak`、`Task.Spawn`、`DllImport` 或包名。', ''].join('\n');
}

export async function generateDocs(root, output, { base = siteBase(process.env.SITE_URL) } = {}) {
  const directory = join(output, 'docs');
  await mkdir(directory, { recursive: true });
  const packages = await loadPackages(root);
  await writeFile(join(output, 'src/package-catalog.js'), `// Generated from packages/*/zan.pkg and source namespace declarations.\nexport const packageCatalog = ${JSON.stringify(packages, null, 2)};\n`);
  const generated = { packages: packagesMarkdown(packages), platforms: platformsMarkdown(), index: indexMarkdown() };
  const records = [];
  const pages = [{ id: 'index', title: '文档目录与搜索', description: 'Zan 语言、标准库、工具链、平台与 AI 开发文档。', group: '开始' }, ...documents];
  for (const document of pages) {
    const markdown = generated[document.id] || (await read(join(root, 'docs', `${document.id}.md`))).replace(/\r\n/g, '\n');
    const rendered = renderMarkdown(markdown);
    if (document.id === 'index') rendered.html += '<label class="doc-search-label" for="docs-query">搜索语言、API、关键字或包名</label><input class="doc-search" id="docs-query" type="search" placeholder="例如 weak、UTF-8、Task.Spawn" autocomplete="off" spellcheck="false" aria-describedby="docs-search-count"><p id="docs-search-count" class="doc-search-count" role="status">全文搜索需要 JavaScript；上方目录可直接访问。</p><ul id="docs-results" class="doc-search-results"></ul>';
    await writeFile(join(directory, `${document.id}.md`), markdown);
    await writeFile(join(directory, `${document.id}.html`), page(document, rendered, base));
    records.push({ ...document, html: `docs/${document.id}.html`, markdown: `docs/${document.id}.md`, source: document.generated || document.id === 'index' ? 'generated from site catalog and repository manifests' : `zan-site/docs/${document.id}.md`, sha256: hash(markdown), text: markdown });
  }
  const manifest = { schemaVersion: 1, reviewedAt: documentationDate, documents: records.map(({ text, ...record }) => record) };
  await writeFile(join(directory, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  await writeFile(join(directory, 'search.json'), JSON.stringify(records.filter(item => item.id !== 'index')) + '\n');
  await writeFile(join(directory, 'packages.json'), JSON.stringify({ reviewedAt: documentationDate, packages }, null, 2) + '\n');
  const guide = await read(join(root, 'agent-guide.md'));
  const full = [`# Zan — complete documentation / 完整文档`, '', `Reviewed against repository sources: ${documentationDate}. Match APIs to the installed SDK; do not assume C#/.NET semantics.`, '', 'Each section below preserves its published Markdown content, with relative prose links rebased to this file location. Code examples are unchanged. Local repository source paths are provenance, not public download URLs.', '', guide, ...records.map(item => `\n---\n\nSource: ${item.markdown}\n\n${rebaseMarkdownLinks(item.text, item.markdown)}`)].join('\n');
  await writeFile(join(output, 'llms-full.txt'), full);
  await writeFile(join(output, 'robots.txt'), `User-agent: *\nAllow: /\n${base ? `\nSitemap: ${new URL('sitemap.xml', base).href}\n` : ''}`);
  if (base) {
    const urls = ['index.html', ...records.map(item => item.html)];
    await writeFile(join(output, 'sitemap.xml'), `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n${urls.map(path => `<url><loc>${escapeHtml(new URL(path, base).href)}</loc></url>`).join('\n')}\n</urlset>\n`);
    const homePath = join(output, 'index.html');
    const home = await read(homePath);
    await writeFile(homePath, home.replace('</head>', `<link rel="canonical" href="${escapeHtml(new URL('index.html', base).href)}">\n<meta property="og:url" content="${escapeHtml(new URL('index.html', base).href)}">\n</head>`));
  }
  if (!base) {
    const sitemap = join(output, 'sitemap.xml');
    const info = await lstat(sitemap).catch(error => { if (error.code === 'ENOENT') return null; throw error; });
    if (info) {
      if (!info.isFile() || info.isSymbolicLink()) throw new Error('Refusing to remove non-file sitemap output');
      const content = await read(sitemap);
      if (!content.startsWith('<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">')) throw new Error('Unexpected sitemap output; inspect before replacing');
      await unlink(sitemap);
    }
  }
  return { documents: records.length, packages: packages.length };
}
