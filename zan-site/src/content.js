import { documents, documentHref } from './documentation.js';
import { ecosystemPackages as packages } from './ecosystem.js';

export const distributionRepo = 'https://github.com/joyspinch/zanlang';
export const downloadsUrl = distributionRepo;
export { ecosystemPackages as packages } from './ecosystem.js';

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
    intro: '先读取项目入口、SDK 能力与约定。以下内容只是流程示意。',
    body: '{\n  "step": "inspect project and SDK",\n  "next": "read documentation and look up APIs"\n}',
    next: 'lookup',
  },
  lookup: {
    label: 'zan_lookup_symbol',
    intro: '查询实际签名与源码位置；工具返回与可用索引以本地 SDK 为准。',
    body: '{\n  "query": "Console.WriteLine",\n  "next": "inspect the returned overloads"\n}',
    next: 'build',
  },
  build: {
    label: 'zan_compile',
    intro: '编译最小示例，读取诊断。完整工程需使用包含全部源码的实际构建命令。',
    body: '{\n  "step": "compile and inspect diagnostics",\n  "next": "run the actual program"\n}',
    next: 'refine',
  },
  refine: {
    label: 'zan_change_impact',
    intro: '核对引用候选与影响面，然后验证实际行为。工具查询不能替代运行。',
    body: '{\n  "step": "review references and verify behavior",\n  "next": "report observed results"\n}',
    next: 'context',
  },
};

export const guides = Object.fromEntries(documents.map(item => [item.id, { ...item, nav: item.title, href: documentHref(item.id) }]));
export const searchItems = [
  { title: '真实项目示例与模板', description: '控件画廊、图表、数据库示例与 ZanIDE 项目模板。', keywords: '示例 模板 gallery example', kind: 'SECTION', section: 'examples', icon: 'code' },
  { title: '采用前常见问题', description: 'C# 与 .NET 兼容边界、商用许可、原生依赖、平台支持和问题反馈。', keywords: 'FAQ 商用 授权 分发 依赖 反馈', kind: 'SECTION', section: 'faq', icon: 'book' },
  ...documents.map(item => ({ title: item.title, description: item.description, keywords: item.keywords, kind: 'GUIDE', guide: item.id, icon: item.icon })),
  ...packages.map(item => ({ title: item.name, description: item.description, keywords: `${item.title} ${item.namespaces.join(' ')}`, kind: 'PACKAGE', package: item.name, icon: item.icon })),
];
