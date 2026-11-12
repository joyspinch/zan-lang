import { packageCatalog } from './package-catalog.js';

// 实际包名、状态与许可来自构建时的清单；这里仅维护中文导览与场景分类。
export const standardLibrary = [
  { name: 'System', title: '语言与运行基础', description: '日期时间、数值转换、异常、环境信息和原生互操作。', icon: 'chip' },
  { name: 'System.Collections', title: '集合与缓冲', description: '队列、栈、链表、集合、并发容器与数组池。', icon: 'data' },
  { name: 'System.IO', title: '基础文件读写', description: '文件、目录、文件流和字节缓冲；压缩与监听见扩展包。', icon: 'box' },
  { name: 'System.Text', title: '文本编码', description: '文本编码与字节处理；正则、模板和检索见文本扩展包。', icon: 'code' },
  { name: 'System.Threading', title: '线程与异步协调', description: '线程、定时器、取消令牌、通道和任务协调。', icon: 'chip' },
  { name: 'System.DependencyInjection', title: '依赖注入', description: '服务注册、依赖组织与生命周期管理。', icon: 'box' },
  { name: 'System.Diagnostics', title: '活动追踪', description: '基础活动追踪；进程、日志与指标见诊断扩展包。', icon: 'network' },
  { name: 'System.Interop', title: '原生互操作工具', description: '原生内存、句柄、结构布局与平台接口辅助能力。', icon: 'code' },
  { name: 'System.Runtime.Intrinsics', title: '处理器能力', description: '处理器检测、向量与指令封装，受目标架构约束。', icon: 'chip' },
];

const groups = [
  { id: 'desktop', title: '原生界面与桌面', description: '窗口、控件、图表、设计器、内嵌浏览器和文档处理。', icon: 'screen', example: '数据工作台：编辑表格、查看图表并生成文档。', names: ['Zan.Gui', 'Zan.Gui.Browser', 'Zan.Gui.Charts', 'Zan.Gui.CodeEditor', 'Zan.Gui.DataTable', 'Zan.Gui.Designer', 'Zan.Gui.Workplace', 'Zan.Browser.Firefox', 'Zan.Drawing', 'Zan.Industrial', 'Zan.Barcode', 'Zan.Pdf'] },
  { id: 'server', title: '服务与网络', description: '网络协议、Web 服务、宿主、后台任务、缓存与消息设施。', icon: 'network', example: '设备状态服务：接收请求、访问数据并推送状态变化。', names: ['Zan.Net', 'Zan.Ssh', 'Zan.Kafka', 'Zan.RabbitMQ', 'Zan.Web', 'Zan.Mvc', 'Zan.Hosting', 'Zan.Jobs', 'Zan.Caching', 'Zan.Messaging', 'Zan.Validation', 'Zan.Mapping', 'Zan.Resilience'] },
  { id: 'data', title: '数据与内容', description: '数据库、数据帧、数值计算、文本、配置与交换格式。', icon: 'data', example: '报表工具：导入 CSV、按维度汇总并输出结构化结果。', names: ['Zan.Configuration', 'Zan.Json', 'Zan.Xml', 'Zan.Formats', 'Zan.Text', 'Zan.Linq', 'Zan.Data', 'Zan.DataFrame', 'Zan.Tensor'] },
  { id: 'game', title: '游戏与玩法', description: '游戏基础与按玩法拆分的可复用包。', icon: 'game', example: '玩法原型：选择对应游戏包，组合场景、规则与资源。', names: ['Zan.Game', 'Zan.Game.Arpg', 'Zan.Game.Board', 'Zan.Game.Cards', 'Zan.Game.Factory', 'Zan.Game.Idle', 'Zan.Game.Tactics', 'Zan.Resources'] },
  { id: 'system', title: '系统与应用工具', description: '桌面、硬件、文件、诊断、安全、本地化与应用更新。', icon: 'chip', example: '备份工具：监测目录、压缩文件并显示任务状态。', names: ['Zan.Desktop', 'Zan.Hardware', 'Zan.IO', 'Zan.Diagnostics', 'Zan.Security', 'Zan.Globalization', 'Zan.AppUpdate'] },
  { id: 'integration', title: 'AI 与平台集成', description: 'AI 应用、业务平台、脚本、命令行、测试与基准工具。', icon: 'spark', example: '知识助手：连接模型与检索器，在应用中组织工具调用。', names: ['Zan.Ai', 'Zan.Oss', 'Zan.Web3', 'Zan.Sdk.Wechat', 'Zan.Sdk.Jd', 'Zan.Sdk.Steam', 'Zan.Scripting', 'Zan.Cli', 'Zan.Testing', 'Zan.Bench', 'Zan.Commercial'] },
];

import { packageTitles as titles, packageDescriptions } from './package-guide.js';

export const ecosystemPackages = packageCatalog.map(item => {
  const group = groups.find(group => group.names.includes(item.name)) || groups.find(group => group.id === 'integration');
  const description = packageDescriptions[item.name] || item.description;
  return { ...item, description, href: `./docs/packages.html#${item.name.toLowerCase().replace(/[^a-z0-9]+/g, '-')}`, category: group.id, title: titles[item.name] || item.name, icon: group.icon, tags: item.namespaces.slice(0, 2) };
});

export const ecosystemGroups = groups.map(({ names, ...group }) => ({ ...group, packages: ecosystemPackages.filter(item => item.category === group.id).map(item => item.name) }));
