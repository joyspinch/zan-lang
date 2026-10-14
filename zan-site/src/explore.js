import { audiences, setupCommand, editorConfigs, projectPrompt } from './onboarding.js';
import { ecosystemGroups, standardLibrary } from './ecosystem.js';
import { guides, searchItems, downloadsUrl } from './content.js';
import { differences, platforms } from './capabilities.js';

const $ = (selector) => document.querySelector(selector);
const escape = (text) => String(text).replace(/[&<>"']/g, (char) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
const icon = (name) => `<svg class="icon" aria-hidden="true"><use href="#i-${name}"></use></svg>`;
const downloadLink = (label) => `<a class="text-link" href="${downloadsUrl}" target="_blank" rel="noopener noreferrer">${label} ↗</a>`;

function keyboardTabs(container, select) {
  const buttons = [...container.querySelectorAll('[role="tab"]')];
  buttons.forEach((button, index) => {
    button.addEventListener('click', () => select(button));
    button.addEventListener('keydown', (event) => {
      if (!['ArrowRight', 'ArrowLeft', 'ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
      event.preventDefault();
      const nextIndex = event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : (index + (['ArrowRight', 'ArrowDown'].includes(event.key) ? 1 : -1) + buttons.length) % buttons.length;
      buttons[nextIndex].focus();
      select(buttons[nextIndex]);
    });
  });
}

function selectTab(button, panel) {
  const changed = button.getAttribute('aria-selected') !== 'true';
  [...button.parentElement.querySelectorAll('[role="tab"]')].forEach((item) => {
    item.setAttribute('aria-selected', String(item === button));
    item.tabIndex = item === button ? 0 : -1;
  });
  panel.setAttribute('aria-labelledby', button.id);
  return changed;
}

function initAudiences() {
  const introductions = {
    developer: '用熟悉的类、泛型和 async/await，试做一个原生工具或服务。',
    beginner: '先说出你想做什么，复制一句话给 AI，让它带你完成第一个作品。',
    agent: '从本地 SDK 查询真实接口，用编译与运行结果推进开发。',
  };
  const panel = $('#audience-panel');
  const render = (button) => {
    const item = audiences[button.dataset.audience];
    const changed = selectTab(button, panel);
    panel.innerHTML = `<div class="audience-lead"><p class="eyebrow">${item.eyebrow}</p><h3>${item.title}</h3><p>${introductions[button.dataset.audience] || item.description}</p><a class="button button-primary" href="${item.href}"${item.href.startsWith('./') ? ' target="_blank"' : ''}>${item.action} ${icon('arrow')}</a></div><div class="audience-points">${item.points.map(([title, description]) => `<details class="detail-disclosure"><summary>${title}</summary><p>${description}</p></details>`).join('')}</div><div class="audience-boundary"><details class="detail-disclosure"><summary>这条路径适合我吗？了解准备与边界</summary><p>${item.description}</p><p>${item.boundary}</p></details></div>`;
    if (changed) panel.dispatchEvent(new CustomEvent('zan:panel-change', { bubbles: true }));
  };
  keyboardTabs($('.audience-tabs'), render);
  render($('#audience-developer'));
}

function initDifferences() {
  const previews = {
    'MARKUP → NATIVE CONTROLS': ['熟悉的标记，原生的窗口', '用 HTML / CSS 子集描述界面，生成原生控件树。'],
    'BINDING IN THE COMPILER': ['让数据与界面连起来', '用简洁赋值表达双向绑定，由编译器检查支持的形式。'],
    'SOURCE-AWARE COMPILATION': ['源码可查，依赖按需加载', '开发者和 AI 都能查实现，编译器按引用加载源码。'],
    'PUBLISH WHAT YOU USE': ['用到的驱动，一起交付', '发布时收集应用需要的原生驱动与依赖。'],
    'THE IDE IS A ZAN APP': ['用 Zan，造 Zan 的工具', 'ZanIDE 使用自己的 GUI 组件，设计器预览真实控件树。'],
    'CONTEXT THAT CAN BE QUERIED': ['让 AI 有据可查', '通过工具查询真实签名、源码索引与编译诊断。'],
  };
  $('#difference-cards').innerHTML = differences.map((item, index) => {
    const [title, description] = previews[item.label] || [item.title, item.description];
    return `<article class="difference-card"><p class="eyebrow">0${index + 1} / ${item.label}</p><h3>${escape(title)}</h3><p>${escape(description)}</p><details class="detail-disclosure"><summary>了解能力与使用边界</summary><p>${escape(item.description)}</p><div class="difference-detail">${escape(item.detail)}</div>${downloadLink('获取 IDE 与相关包')}</details></article>`;
  }).join('');
}

function initSetup() {
  $('#ai-setup').innerHTML = `<article class="setup-card"><span class="setup-number">01</span><h3>下载并打开 ZanIDE</h3><p>当前提供 Windows SDK。在发布仓库选择 Code → Download ZIP，完整解压后打开 ZanIDE.exe。</p>${downloadLink('获取 ZanIDE 与 SDK')}<small>这是已打包的开发工具，日常使用无需自行编译工具链。</small><details class="detail-disclosure"><summary>检查目录与获取方式</summary><p>以 <code>D:\\Zan</code> 为例，确认 SDK 包含 <code>tools/zan-mcp.exe</code> 与 <code>toolchain/zanc.exe</code>。ZanIDE 也可从“新建项目”选择模板。</p><p>保留完整目录与随附的 DLL、toolchain、stdlib；项目放在 SDK 目录之外。其他系统与额外包的可用情况以发布仓库说明为准。</p></details></article><article class="setup-card"><span class="setup-number">02</span><h3>初始化 AI 接入</h3><p>替换示例路径，在 PowerShell 中运行。</p><div class="setup-command"><pre id="init-command">${escape(setupCommand)}</pre><button class="copy-action" data-copy-target="init-command">复制 PowerShell 命令 ↗</button></div><details class="detail-disclosure"><summary>会安装什么？已有配置如何处理？</summary><p>安装项目规则、技能，以及 Claude Code、Cursor、VS Code 的配置。已有文件默认跳过，不会自动合并配置；它还不会创建应用代码。</p></details></article><article class="setup-card"><span class="setup-number">03</span><h3>连接并确认</h3><p>用 AI 编辑器打开项目，启用 MCP，让 AI 调用 <code>zan_start_here</code>。</p><button class="text-link" data-guide="ai">查看三个编辑器的具体配置 ↗</button><details class="detail-disclosure"><summary>确认项目与排查连接</summary><p>打开 <code>D:\\work\\hello-zan</code>（替换为实际项目路径），按客户端提示启用项目 MCP，确认工具看到的是这个项目。</p><p>若工具没有出现：检查配置中的 SDK 路径、项目目录及客户端启用状态。已有 MCP 配置时，请手动合并 zan 节点。</p></details></article>`;
  const idea = $('#project-idea');
  const custom = $('#project-custom');
  const updatePrompt = () => { $('#project-prompt').textContent = projectPrompt(idea.value, custom.value); };
  idea.addEventListener('change', () => {
    custom.value = '';
    updatePrompt();
  });
  custom.addEventListener('input', updatePrompt);
  updatePrompt();
  const oldPromptButton = $('#copy-ai-prompt');
  const replacement = oldPromptButton.cloneNode(true);
  oldPromptButton.replaceWith(replacement);
  replacement.dataset.copyTarget = 'project-prompt';
  const copyFeedback = new WeakMap();
  document.addEventListener('click', async (event) => {
    const button = event.target.closest('[data-copy-target]');
    if (!button) return;
    const target = document.getElementById(button.dataset.copyTarget);
    if (!target) return;
    const feedback = copyFeedback.get(button) || { nodes: [...button.childNodes].map(node => node.cloneNode(true)), timer: 0 };
    window.clearTimeout(feedback.timer);
    copyFeedback.set(button, feedback);
    try {
      await navigator.clipboard.writeText(target.textContent);
      button.textContent = '已复制 ✓';
    } catch {
      for (let ancestor = target.parentElement; ancestor; ancestor = ancestor.parentElement) {
        if (ancestor.tagName === 'DETAILS') ancestor.open = true;
      }
      const range = document.createRange();
      range.selectNodeContents(target);
      const selection = window.getSelection();
      selection.removeAllRanges();
      selection.addRange(range);
      target.scrollIntoView({ block: 'center', behavior: 'instant' });
      button.textContent = '已选中文本，请按 Ctrl/Cmd+C';
    }
    window.clearTimeout(feedback.timer);
    feedback.timer = window.setTimeout(() => {
      button.replaceChildren(...feedback.nodes);
      copyFeedback.delete(button);
    }, 2500);
  });
}

function initPlatforms() {
  const boundaries = {
    windows: 'ARM64 待实机验收；Win7 待全 SDK 验收。',
    linux: 'ARM64 有 QEMU 探针记录，尚非 ARM 硬件 GUI 验收。',
    macos: '完整 GUI 待真实 Mac 验收，签名、公证与 SDK 交付链待补齐。',
    android: '已验证 x64 模拟器；ARM 手机待实测，正式发布需正式签名。',
    ios: '无 iPhone / iPad 验收记录，GUI 支持未完整，IPA 封装不等于可安装发布。',
    openharmony: 'ARM 硬件待验收；HAP 需外部宿主工程。',
    wasi: '当前单线程，网络能力不完整；正式发布需自行整合宿主。',
  };
  const tabs = $('#platform-tabs');
  const panel = $('#platform-panel');
  tabs.innerHTML = platforms.map((item, index) => `<button role="tab" id="platform-${item.id}" aria-controls="platform-panel" aria-selected="${index === 0}" tabindex="${index === 0 ? 0 : -1}" data-platform="${item.id}">${item.title}</button>`).join('');
  function render(button) {
    const item = platforms.find((platform) => platform.id === button.dataset.platform);
    const changed = selectTab(button, panel);
    panel.innerHTML = `<div class="platform-lead"><span class="platform-badge">${item.status}</span><h3>${item.title}</h3><p>${item.description}</p><code>${item.arch}</code><p class="boundary">${boundaries[item.id] || item.limit}</p></div><details class="detail-disclosure"><summary>${item.title} 的编译、运行与交付证据</summary><dl class="platform-facts"><div><dt>编译</dt><dd>${item.compile}</dd></div><div><dt>运行</dt><dd>${item.run}</dd></div><div><dt>界面</dt><dd>${item.gui}</dd></div><div><dt>交付</dt><dd>${item.ship}</dd></div><div><dt>边界</dt><dd class="boundary">${item.limit}</dd></div></dl></details>`;
    if (changed) panel.dispatchEvent(new CustomEvent('zan:panel-change', { bubbles: true }));
  }
  keyboardTabs(tabs, render);
  render($('#platform-' + platforms[0].id));
}

function initEcosystem() {
  const uses = {
    desktop: '搭建窗口、表格、图表和内嵌网页。',
    server: '搭建网络服务；管理后台框架仍在迁移。',
    data: '连接数据库，整理表格、文本与图像。',
    game: '组织二维场景、玩法与素材。',
    system: '处理文件、桌面集成、诊断与更新。',
    integration: '对接业务平台，嵌入脚本与开发工具。',
  };
  $('#ecosystem-map').innerHTML = ecosystemGroups.map((item) => `<article class="ecosystem-domain">${icon(item.icon)}<h3>${item.title}</h3><p>${uses[item.id] || item.description}</p><span class="domain-package-count">${item.packages.length} 个扩展包</span><button type="button" class="text-link domain-explore" data-explore-category="${item.id}" aria-controls="packages-grid" aria-label="探索${escape(item.title)}的 ${item.packages.length} 个包">探索 ${item.packages.length} 个包 ${icon('arrow')}</button><details class="detail-disclosure"><summary>查看${item.title}包与应用思路</summary><p>${item.description}</p><div class="domain-example"><strong>应用思路：</strong>${item.example}<small>项目方向示意，不代表已有成品或生产验证。</small></div><div class="domain-packages">${item.packages.map((name) => `<span>${name}</span>`).join('')}</div></details></article>`).join('');
  $('#stdlib-foundation').innerHTML = `<h3>System：所有应用的公共基础</h3><p>核心标准库随 SDK 提供，扩展能力按包引入。</p><details class="detail-disclosure"><summary>查看 ${standardLibrary.length} 项标准库基础能力</summary><p>包名与命名空间不同，部分扩展包也使用 System.* 命名空间。</p><div class="stdlib-list">${standardLibrary.map((item) => `<div><h4>${item.title}</h4><p>${item.description}</p></div>`).join('')}</div></details>`;
}

function enhanceGuides() {
  guides.ai.body = `<p>以 Windows SDK 根目录 <code>D:\\Zan</code>、项目目录 <code>D:\\work\\hello-zan</code> 为例。请替换成实际路径。</p><h3>初始化 AI 接入</h3><pre><code>${escape(setupCommand)}</code></pre><p>初始化器安装项目约定、技能和客户端配置，默认跳过已有文件。已有 MCP 配置时，手动合并下面的 zan 节点。此步骤不会生成应用源码。</p>${Object.values(editorConfigs).map((item) => `<h3>${item.title} · ${item.file}</h3><pre><code>${escape(JSON.stringify(item.config, null, 2))}</code></pre>`).join('')}<h3>确认已连接</h3><p>用编辑器打开项目，启用该 MCP 服务，让 AI 先调用 <code>zan_start_here</code>。正常情况下由客户端启动 stdio 子进程，无需另开一个常驻终端。</p><h3>第一个请求</h3><p>让 AI 先读规则与示例、查询 API，再实现一个小功能，最后编译整个项目并运行。若项目入口不在默认路径，显式传入入口。编译通过之后仍需验证实际行为。</p><p>${downloadLink('获取 IDE / SDK')} · <a href="./agent-guide.md" target="_blank">机器可读指南 ↗</a></p>`;
  guides.platforms.body = `<p>各目标的编译、实机证据与 GUI 能力分开列出。原生目标不代表所有包、驱动和系统 API 都已支持。</p>${platforms.map((item) => `<h3>${item.title} · ${item.status}</h3><p><strong>${item.arch}</strong></p><ul><li>编译：${item.compile}</li><li>运行：${item.run}</li><li>GUI：${item.gui}</li><li>交付：${item.ship}</li><li>限制：${item.limit}</li></ul>`).join('')}<p>${downloadLink('查看各版本的平台与下载说明')}</p>`;
  guides.quickstart.body = `<p>从 ${downloadLink('官方发布仓库')} 获取 IDE / SDK 与相关包。初次使用可以让 AI 引导准备，或从 ZanIDE 的“新建项目”选择模板。</p><h3>下载、解压、打开</h3><ol><li>在仓库页面点击 <strong>Code → Download ZIP</strong>，下载当前 Windows SDK。</li><li>完整解压，保留 <code>toolchain</code>、<code>stdlib</code> 和随附 DLL。</li><li>双击 <code>ZanIDE.exe</code>，从“新建项目”开始。</li></ol><p>日常使用无需单独安装 LLVM / clang，也无需构建编译器。其他系统和额外包以仓库发布说明为准。</p><h3>手动使用编译器</h3><p>Windows SDK 的编译器位于 <code>toolchain/zanc.exe</code>。以下命令供希望手动编译的开发者使用。</p><h3>创建 hello.zan</h3><pre><code>using System;\nclass Program {\n    static void Main() {\n        Console.WriteLine("Hello, Zan!");\n    }\n}</code></pre><h3>Windows PowerShell</h3><p>在保存了 hello.zan 的目录运行，SDK 路径按实际位置替换：</p><pre><code>&amp; "D:\\Zan\\toolchain\\zanc.exe" .\\hello.zan -o .\\hello.exe\nif ($LASTEXITCODE -eq 0) { .\\hello.exe }</code></pre><h3>Linux CLI（工具链已就绪）</h3><pre><code>zanc hello.zan -o hello\n./hello</code></pre><h3>发布构建</h3><p>加上 <code>--publish</code> 可使用发布优化与符号裁剪。目标平台的原生依赖仍需要匹配。</p><p>更多参数、模板与 API 说明请查阅下载包随附文档或 ZanIDE。${downloadLink('查看版本说明与相关包')}</p>`;
  searchItems.push(...[
    ['为什么选择 Zan', '开发者、新手、AI 智能体的三条入门路径', 'why-zan', 'spark'],
    ['用 AI 开始', '复制一句话，让 AI 引导下载 IDE、准备环境并做出作品', 'ai-start', 'spark'],
    ['下载 IDE 与相关包', '官方发布、版本说明与可用的 IDE / SDK', 'start', 'box'],
    ['原生平台支持', 'Windows Linux macOS Android iOS OpenHarmony WASI', 'platforms', 'chip'],
    ['Zan 的差异化', '按需编译、源码驱动、界面与应用交付', 'difference', 'code'],
  ].map(([title, description, section, iconName]) => ({ title, description, section, kind: 'SECTION', icon: iconName })));
}

[initAudiences, initDifferences, initSetup, initPlatforms, initEcosystem, enhanceGuides].forEach((init) => {
  try { init(); } catch (error) { console.error('Zan site section failed:', init.name, error); }
});
