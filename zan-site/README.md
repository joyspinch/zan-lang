# Zan 官网

从零构建的中文展示站，采用原生 HTML、CSS、ES modules 和 Node 内置静态服务；没有第三方前端运行依赖。

## 本地预览

在仓库根目录运行：

```sh
npm --prefix zan-site run dev
```

默认地址为 `http://127.0.0.1:4173/`。使用 `PORT` 环境变量可更换端口。

## 检查与构建

```sh
npm --prefix zan-site run check
npm --prefix zan-site run build
```

发布文件输出到仓库的 `build/zan-site/`。将该目录内容上传到静态托管平台即可。网站不依赖 API 服务器，不需要 SPA 路由回退；资源与页面使用相对路径，可部署到子路径。

## 内容结构

- `index.html`：首页结构、基础语义与入口。
- `src/styles.css`、`src/experience.css`、`src/immersive.css`：基础视觉、渐进披露与蓝紫空间布局。
- `src/creation.js`、`src/creation.css`：待办、CSV、HTTP 三种创作场景及可操作概念预览，可将所选场景带入 AI 项目指令。
- `src/interactions.js`、`src/interactions.css`：阅读进度、章节导航、返回顶部、指针光效和统一暂停状态。
- `src/intelligence.js`、`src/intelligence.css`：粒子核心、轨道信号与指针响应；减少动态效果、离屏及页面隐藏时停止动画。
- `src/content.js`：代码片段、AI 流程示意、指南与搜索入口。
- `src/onboarding.js`：三类用户的选择理由、真实客户端配置与一句话 AI 指令；支持示例及自定义想法，指令附带发布仓库中的官方入门链接。
- `src/capabilities.js`：差异化能力与平台分层状态。
- `src/ecosystem.js`：核心标准库摘要、六类场景、38 个第一方包与清单状态。
- `src/app.js`、`src/explore.js`：交互演示、检索、可访问标签页与新增内容渲染。
- `agent-guide.md`、`llms.txt`：供 AI 读取的站点与开发指南。

## 内容维护

技术描述来自当前源码、包清单与仓库文档。更新功能时同步修订源码链接、限制与平台状态，避免把“存在编译路径”写成“全部设备实机验证”，或把“源码可用”写成“生产环境成熟”。38 是当前第一方包清单数量，不是生态总规模承诺。

AI 调用与 ARC 动画为流程示意；代码区域是静态示例，不是在线编译器。对外下载统一指向发布仓库 `https://github.com/joyspinch/zanlang`。该仓库当前提供打包的 Windows SDK，按 README 通过 Code → Download ZIP 下载，完整解压后运行 ZanIDE.exe；Releases 当前为空。生态卡片展示能力清单，相关包是否随附或单独提供以发布仓库说明为准，不承诺 38 个包均有独立下载。用户入口不链接编译器源码或要求自行构建工具链。JS 共用入口定义在 `src/content.js`，静态 HTML 与机器可读指南同步维护。页面视觉不依赖外链字体或图片。

默认入口为“描述想法 → 复制给 AI”，SDK 与 MCP 配置折叠在高级接入说明中；手动编译单独面向有经验的开发者。普通聊天窗口不能直接在用户电脑上创建和运行应用，环境准备由能操作本地文件的 AI 编程工具引导。

`--init-agent` 默认跳过已有文件，不会自动合并 MCP JSON，也不创建应用代码。网站所给 Windows 路径为示例，需要按真实环境替换。

性能部分解释 AOT、ARC、值类型与原生 I/O 的机制，不展示没有可重复基准支持的性能数字。商业授权包等独立许可按各包清单为准。
