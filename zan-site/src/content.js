export const distributionRepo = 'https://github.com/joyspinch/zanlang';
export const downloadsUrl = distributionRepo;

export const examples = {
  hello: {
    filename: 'hello.zan',
    output: 'Hello, Zan!',
    code: `using System;\n\nclass Program {\n    static void Main() {\n        Console.WriteLine("Hello, Zan!");\n    }\n}`,
  },
  values: {
    filename: 'values.zan',
    output: '值类型片段 · LengthSquared 返回距离平方',
    code: `using System;\n\nstruct Point {\n    public int X;\n    public int Y;\n\n    public int LengthSquared() {\n        return X * X + Y * Y;\n    }\n}`,
  },
  interop: {
    filename: 'native.zan',
    output: 'Windows · 输出系统启动后的毫秒数',
    code: `using System;\n\nclass NativeClock {\n    [DllImport("kernel32.dll")]\n    static extern long GetTickCount64();\n\n    static void Main() {\n        Console.WriteLine(GetTickCount64());\n    }\n}`,
  },
};

export const aiSteps = {
  context: {
    label: 'zan_start_here',
    intro: '先把项目地图交给 AI。它知道从哪里开始，也知道哪些约定不能跳过。',
    body: '{\n  "project": "hello-zan",\n  "entry": "src/main.zan",\n  "stdlib": "source-linked",\n  "next": "inspect the entry point"\n}',
    next: 'lookup',
  },
  lookup: {
    label: 'zan_lookup_symbol',
    intro: '接口不是猜出来的。AI 可以从符号签名一路读到真实源码和行号。',
    body: '{\n  "symbol": "Console.WriteLine",\n  "signature": "void WriteLine(object value)",\n  "definedAt": "src/compiler/builtin_api.c:59",\n  "confidence": "source-backed"\n}',
    next: 'build',
  },
  build: {
    label: 'zan_compile',
    intro: '把修改交给编译器，获取结构化诊断。反馈越具体，下一次修改就越确定。',
    body: '{\n  "ok": true,\n  "target": "native",\n  "diagnostics": [],\n  "artifact": "hello.exe"\n}',
    next: 'refine',
  },
  refine: {
    label: 'zan_change_impact',
    intro: '继续追踪引用与影响面。AI 与你一起迭代，而不是把一段代码丢下就走。',
    body: '{\n  "changed": "Console.WriteLine",\n  "references": 3,\n  "affected": ["src/main.zan"],\n  "status": "ready for review"\n}',
    next: 'context',
  },
};

export { ecosystemPackages as packages } from "./ecosystem.js";
import { ecosystemPackages as packages } from "./ecosystem.js";

export const guides = {
  overview: {
    kicker: 'ZAN / FIELD GUIDE · 00',
    title: '从熟悉开始，向原生深入。',
    nav: '认识 Zan',
    body: `<p>Zan 是一门使用 C# 风格语法、通过 LLVM 生成原生机器码的系统编程语言。它把自动引用计数、异步 I/O、原生互操作和一套可查询的开发工具放在同一个工作流里。</p><div class="guide-callout"><strong>适合先试试：</strong> 写过 C#、Java、TypeScript 或其他面向对象语言的人，可以从一个 <code>class Program</code> 和 <code>Main</code> 入口开始。</div><h3>四个关键词</h3><ul><li><strong>易上手：</strong>熟悉的类、结构体、泛型和 async/await 表达。</li><li><strong>原生：</strong>LLVM AOT 编译，应用面向目标平台机器码运行。</li><li><strong>可控：</strong>ARC 管理引用，结构体和值语义帮助你组织数据。</li><li><strong>可协作：</strong>MCP、LSP、DAP 与源码标准库让人和 AI 都有真实上下文。</li></ul><h3>一个小程序</h3><pre><code>using System;\n\nclass Program {\n    static void Main() {\n        Console.WriteLine("Hello, Zan!");\n    }\n}</code></pre><p>把它保存为 <code>hello.zan</code>，再交给 <code>zanc</code> 编译。你会得到一个面向目标平台的原生程序。</p>`,
  },
  language: {
    kicker: 'ZAN / LANGUAGE · 01',
    title: '熟悉的表达，更多控制。',
    nav: '语言特性',
    body: `<p>Zan 的语法选择服务于阅读与创造：类和接口组织行为，结构体表达紧凑的值，泛型复用算法，属性与模式让边界更清晰。</p><h3>值与引用</h3><p>结构体适合坐标、配置和小型数据记录；类适合拥有身份和生命周期的对象。标准库集合使用写时复制等策略，在表达力和实际开销之间取得平衡。</p><h3>ARC 内存管理</h3><p>编译器在引用变化处插入 retain/release。强引用归零时对象析构；使用 <code>weak</code> 可以打断可能形成的强引用环。它给出确定的资源释放路径，同时保留高层语言的易用性。</p><h3>直接接入原生世界</h3><p>用 <code>[DllImport]</code> 声明系统 API 或原生库函数，配合结构体布局和函数指针完成 C ABI 互操作。原生依赖和平台能力仍需按目标环境准备。</p>`,
  },
  ai: {
    kicker: 'ZAN / AI COLLABORATION · 02',
    title: '让 AI 有上下文，也有反馈。',
    nav: 'AI 协作',
    body: `<p>AI 辅助编程的关键不是让模型写得更多，而是让它能在真实项目里查到依据、理解影响，并把修改交给编译器验证。</p><h3>把 Zan 接入你的工作流</h3><ol><li>使用 Zan SDK 中的 MCP 服务，把项目入口、API 查询、源码上下文和编译工具提供给 AI。</li><li>在支持 LSP 的编辑器里获得补全、诊断、定义、引用、重命名和语义信息。</li><li>用 DAP 启动或附加调试，设置断点、步进、查看调用栈和变量。</li></ol><h3>一个明确的循环</h3><p><code>zan_start_here</code> → <code>zan_lookup_symbol</code> → 修改 → <code>zan_compile</code> → 读取诊断 → 再次验证。</p><div class="guide-callout"><strong>诚实的上下文：</strong> MCP 工具依赖项目索引与 SDK 配置；API 引用检索仍应结合编译器和人工审阅。好的工具链让验证变容易，不承诺模型永不出错。</div><h3>常见入口</h3><p><code>tools/zan-mcp.exe</code> 提供本地工具服务；<code>zanc --api Symbol</code> 可以查询 API。具体接入步骤会随 SDK 与编辑器配置更新。</p>`,
  },
  platforms: {
    kicker: 'ZAN / TARGETS · 03',
    title: '选择目标，逐步交付。',
    nav: '平台支持',
    body: `<p>Zan 面向原生目标设计。编译能力、运行时支持、GUI 驱动和第三方依赖会随平台不同而变化，官网用分层状态标记它们。</p><table class="platform-table"><thead><tr><th>目标</th><th>当前可探索</th></tr></thead><tbody><tr><td>Windows x64</td><td>CLI 工具链与 ZanIDE 作为首要体验路径。</td></tr><tr><td>Linux x64 / arm64</td><td>CLI SDK 打包路径；GUI 能力按驱动与环境配置。</td></tr><tr><td>macOS</td><td>编译目标持续演进，GUI 与实机验证依赖具体环境。</td></tr><tr><td>Android / WASI</td><td>存在对应目标路径；网络、线程和系统能力受平台限制。</td></tr></tbody></table><p>某个 API 或包是否适合目标平台，请查看发布说明与随附文档，也可以让 AI 通过本地工具查询并验证。</p>`,
  },
  quickstart: {
    kicker: 'ZAN / QUICKSTART · 04',
    title: '下载 IDE，开始第一个作品。',
    nav: '快速开始',
    body: `<p>从官方发布仓库获取已经打包的 IDE 与 SDK。新手可以先把想法交给 AI。</p><h3>1. 下载并打开 IDE</h3><p>前往 <a href="${downloadsUrl}" target="_blank" rel="noopener noreferrer">下载 IDE 与相关包</a>，点击 <strong>Code → Download ZIP</strong> 下载当前 Windows SDK，完整解压后打开 <code>ZanIDE.exe</code>。保留随附目录和 DLL，无需自行编译工具链。以下是已有开发经验时的手动编译方式。</p><h3>2. 写下 hello.zan</h3><pre><code>using System;\n\nclass Program {\n    static void Main() {\n        Console.WriteLine("Hello, Zan!");\n    }\n}</code></pre><h3>3. 编译与发布</h3><pre><code>zanc hello.zan -o hello.exe\nhello.exe\n\n# 发布构建\nzanc hello.zan --publish -o hello-release.exe</code></pre><div class="guide-callout"><strong>提示：</strong>命令中的目标扩展名、标准库路径和发布选项会随平台与 SDK 安装方式变化。遇到问题，直接阅读编译诊断与 <code>docs/ZANC_CLI.md</code>。</div>`,
  },
};

export const searchItems = [
  { title: '认识 Zan', description: '从语法、ARC、原生编译到工具链的总览', kind: 'GUIDE', guide: 'overview', icon: 'spark' },
  { title: '语言特性', description: '类、结构体、泛型、async/await、DllImport', kind: 'GUIDE', guide: 'language', icon: 'code' },
  { title: 'AI 协作', description: 'MCP、LSP、DAP 与编译验证循环', kind: 'GUIDE', guide: 'ai', icon: 'spark' },
  { title: '快速开始', description: '下载 IDE / SDK、完整解压、打开 ZanIDE，或手动编译', kind: 'GUIDE', guide: 'quickstart', icon: 'arrow' },
  { title: '平台支持', description: '目标平台、工具链与运行时边界', kind: 'GUIDE', guide: 'platforms', icon: 'chip' },
  ...packages.map((item) => ({ title: item.name, description: item.description, kind: 'PACKAGE', package: item.name, icon: item.icon })),
];
