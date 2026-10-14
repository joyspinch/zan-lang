import { distributionRepo } from './content.js';

export const audiences = {
  developer: {
    eyebrow: '从已有经验，走向原生开发',
    title: '保留熟悉的表达。把控制权带到原生层。',
    description: '如果你希望用类、泛型和 async/await 开发桌面工具或服务，同时需要原生交付、可追踪的资源生命周期与系统接口，Zan 值得用一个小项目来检验。',
    points: [
      ['学习成本有起点', 'C# 风格让语法更熟悉；需要新掌握的是 ARC、值与引用的边界，以及目标平台能力。'],
      ['应用能力可以连起来', '源码标准库与包、GUI、原生互操作和发布工具，让你少搭几段独立的工具链。'],
      ['让 AI 在项目里协作', '把签名、源码上下文和编译诊断交给 AI；你把精力留给架构、边界与验收。'],
    ],
    action: '看看具体的技术选择', href: '#difference',
    boundary: 'Zan 是独立语言，不能直接运行 .NET / NuGet 程序。已有 C# 经验可以迁移，库与工具仍需按 Zan 的实现核对。',
  },
  beginner: {
    eyebrow: '从一个能说清楚的需求开始',
    title: '不必先懂整个技术栈。先做出一个小作品。',
    description: '从待办清单、表格汇总或一个简单窗口开始。用自然语言告诉 AI 你想做什么，再让它查询接口、生成代码、编译并解释结果。每完成一个小功能，就多理解一点程序。',
    points: [
      ['第一步，只说你的想法', '例如“帮我做一个能保存的待办清单”。把这句话交给 AI，让它先检查准备条件，再一步步带你做。'],
      ['准备工作也让 AI 带着走', '复制下面的一句话到 ZCode、Cursor 或 Claude Code。需要安装工具或确认操作时，让 AI 一次只解释一步。'],
      ['把“能用”当第一目标', '让 AI 演示：程序在哪里、怎么启动、出错怎么看。一次只加一个功能，保留可以运行的版本。'],
    ],
    action: '用 AI 做我的第一个作品', href: '#ai-start',
    boundary: '实际生成和运行应用需要能操作本地文件的 AI 编程工具及 Zan SDK。可以先让 AI 帮你检查；环境还没准备好时，它应说明缺少什么，再引导你完成。',
  },
  agent: {
    eyebrow: '对智能体，接口本身就是上下文',
    title: '少靠猜测补接口。多用工具获取证据。',
    description: 'Zan 提供一条显式的工具路径：先读项目入口与约定，查询真实符号，读取必要源码，再用编译结果决定下一步。新语言的知识可以从本地 SDK 获取。',
    points: [
      ['可发现的项目与接口', 'zan_start_here、zan_api_search、zan_lookup_symbol 与 zan_read_context 提供项目入口、签名和源码位置。'],
      ['可消费的验证结果', 'zan_compile / zan_build_project 返回 ok、exitCode、diagnostics 等信息，便于把错误反馈接入迭代。'],
      ['可携带的开发规则', '--init-agent 把项目规则、技能与客户端配置带入工程；没有 MCP 时，也可用 zanc --api 查询索引。'],
    ],
    action: '读取机器可读的开发指南', href: './agent-guide.md',
    boundary: '工具依赖本地 SDK、索引与客户端权限。引用候选不等同完整语义分析；应核对实际入口、运行目标程序并报告真实结果。',
  },
};

export const setupCommand = String.raw`New-Item -ItemType Directory -Force "D:\work\hello-zan" | Out-Null
& "D:\Zan\tools\zan-mcp.exe" --init-agent "D:\work\hello-zan"`;

export const editorConfigs = {
  claude: { title: 'Claude Code', file: '.mcp.json', config: { mcpServers: { zan: { type: 'stdio', command: 'D:/Zan/tools/zan-mcp.exe', args: ['--stdio', '.'], env: {} } } } },
  cursor: { title: 'Cursor', file: '.cursor/mcp.json', config: { mcpServers: { zan: { command: 'D:/Zan/tools/zan-mcp.exe', args: ['--stdio', '.'] } } } },
  vscode: { title: 'VS Code / Copilot', file: '.vscode/mcp.json', config: { servers: { zan: { type: 'stdio', command: 'D:/Zan/tools/zan-mcp.exe', args: ['--stdio', '${workspaceFolder}'] } } } },
};

const projects = {
  todo: '一个能添加、勾选并保存事项的桌面待办清单',
  csv: '一个能读入表格、按类别汇总金额的小工具',
  api: '一个能在浏览器里查看运行状态的小服务',
};

export function projectPrompt(key = 'todo', custom = '') {
  const idea = custom.trim().replace(/\s+/g, ' ') || projects[key] || projects.todo;
  return `我不懂编程，请用 Zan 帮我做：${idea}。请先读官方入门说明 ${distributionRepo}/blob/main/AI_README.md ，从该发布仓库获取适合我电脑的 IDE 和相关包，用白话引导我准备环境，做出可以运行的版本，实际验证后告诉我怎么打开。`;
}
