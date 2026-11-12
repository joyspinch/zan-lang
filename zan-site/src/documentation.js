export const documentationDate = '2026-11-11';

// Markdown 是正文唯一来源；页面、搜索和 AI 导出由构建器一起生成。
export const documents = [
  { id: 'overview', title: '认识 Zan', description: '语言定位、原生编译、应用场景与学习路线。', keywords: 'AOT LLVM ARC C# .NET 语言概览', group: '开始', icon: 'spark' },
  { id: 'quickstart', title: '安装与第一个程序', description: '准备 SDK，编译运行，组织多文件项目并交付应用。', keywords: '安装 Hello World Main auto-stdlib publish', group: '开始', icon: 'arrow' },
  { id: 'language', title: '语法与程序结构', description: '声明、表达式、控制流、命名空间、异常与资源清理。', keywords: 'using namespace if foreach switch try catch finally var', group: '语言', icon: 'code' },
  { id: 'types', title: '类型与抽象', description: '基础类型、类、结构体、接口、泛型、委托、属性与模式。', keywords: 'class struct interface enum delegate lambda generic tuple pattern char', group: '语言', icon: 'code' },
  { id: 'memory', title: '内存与资源生命周期', description: 'ARC、weak、空值、集合别名、UTF-8 字符串和显式释放。', keywords: 'retain release weak null Dispose Close using UTF-8 字节 引用环 copy-on-write', group: '语言', icon: 'shield' },
  { id: 'async', title: '异步与并发', description: 'async/await、Task、调度器、取消、线程和同步原语。', keywords: 'Task.Spawn Task.Run Task<T> Channel CancellationToken ZAN_CO_WORKERS', group: '语言', icon: 'network' },
  { id: 'interop', title: '原生互操作', description: 'DllImport、C ABI、结构布局、指针和平台条件编译。', keywords: 'DllImport extern unsafe Marshal StructLayout nint sizeof fixed', group: '语言', icon: 'chip' },
  { id: 'libraries', title: '标准库与应用开发', description: '区分内建、标准库与扩展包，按 GUI、网络、数据、AI 和游戏查能力。', keywords: 'System Gui HTML CSS Binding JsonValue HTTP 数据库 MCP 游戏', group: '应用与工具', icon: 'box' },
  { id: 'packages', title: '扩展包完整目录', description: '第一方包的用途、命名空间、状态与来源，一页检索。', keywords: 'package zan.pkg namespace 包管理 生态', group: '应用与工具', icon: 'box', generated: true },
  { id: 'tooling', title: '工具链与发布', description: 'zanc、包发现、诊断、ZanIDE、LSP、DAP、测试和发布选项。', keywords: 'CLI IDE LSP DAP check-leaks strict-runtime api package-project embed', group: '应用与工具', icon: 'chip' },
  { id: 'platforms', title: '平台支持与边界', description: '分别了解编译目标、运行证据、GUI 后端和打包条件。', keywords: 'Windows Linux macOS Android iOS OpenHarmony WASI RISC-V', group: '应用与工具', icon: 'chip', generated: true },
  { id: 'ai', title: '让 AI 正确使用 Zan', description: '文档读取顺序、MCP 接入、真实 API 查询和编译运行验证。', keywords: 'LLM llms.txt llms-full.txt agent MCP Cursor Claude Code Copilot', group: 'AI', icon: 'spark' },
];

export function documentHref(id) {
  return `./docs/${id}.html`;
}
