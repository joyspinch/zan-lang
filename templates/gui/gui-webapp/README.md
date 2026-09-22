# Zan 安全现代网页桌面应用模板 (gui-webapp)

基于 Zan 原生编译二进制与轻量 WebView 的跨平台桌面应用开发模板。

## 核心特性
- **极致轻量**：整包体积仅约 **4.8 MB**，内存开销低，启动毫秒级；
- **全内存解密防逆向**：静态网页（HTML/CSS/JS/图片）加密内嵌至 EXE，运行时全内存流式解密，**磁盘 0 临时文件**，无法被文件监控工具提取；
- **防外部调试注入**：自动清空 `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS` 环境变量，严格清洗 `--remote-debugging-port` 等命令行参数，发布版物理锁定 DevTools 与右键菜单；
- **动态全随机端口 + 128 位 Token 隔离**：启动时动态绑定空闲高位端口，每次启动生成强随机路径 Token，杜绝端口冲突与外部探测；
- **无边框原生手感**：HTML 自定义现代标题栏，支持 `data-zan-drag` 零延迟系统原生拖拽（Win32 WM_NCLBUTTONDOWN 原生接管），支持最小化/最大化/关闭；
- **双向 IPC 消息桥**：前端 `window.zan.invoke(method, params)` 与 Zan 后台声明式 RPC 交互，跨平台统一；
- **零外部环境依赖**：**不需要 Python，不需要 Node.js，不需要 Rust**，自包含纯 Zan 工具链。

## 目录结构
```text
gui-webapp/
├── zan.proj              # 工程配置文件
├── template.manifest     # IDE 模板清单（在新建项目面板中可见）
├── src/
│   ├── App.html          # 无边框窗口设计器声明
│   ├── App.zan           # 主业务逻辑与 Native RPC 处理器注册
│   ├── WebSecurity.zan   # 启动参数清洗与环境变量清空
│   ├── WebServer.zan     # 动态随机端口与内存解密微服务
│   ├── WebBridge.zan     # 双向 IPC 桥与原生窗口控制
│   ├── WebUpdater.zan    # 跨平台版本自更新与热更新管理
│   └── WebAssets.zan     # 加密内嵌的静态资产字节池
├── web/                  # 前端工程（支持 Vue 3 / React / Vite / 纯 HTML）
│   ├── package.json      # Vue 3 + Vite 工程配置
│   ├── vite.config.js    # Vite 配置文件（已配置 base: './' 相对路径）
│   ├── src/              # Vue 3 单文件组件源码（App.vue, TitleBar.vue）
│   ├── dist/             # 预编译好的生产产物（免 Node 环境开箱即用）
│   ├── index.html        # 界面入口
│   ├── style.css         # 界面样式
│   └── zan-bridge.js     # 前端 window.zan 通信桥
├── wwwroot/              # 可选的独立 Web 根目录（打包器优先检测 wwwroot，无产物则打包 web/dist）
└── tools/
    └── PackWeb.zan       # 纯 Zan 编写的前端资源自举打包器
```

## 开发与发布

### 1. 前端开发（Vue 3 / Vite）
模板中内置了完整的 Vue 3 组件化工程：
- **有 Node.js 环境**：在 `web/` 目录下运行 `npm install` 与 `npm run build`，产物将输出至 `web/dist/`；
- **免 Node 环境**：模板自带了预构建好的 `web/dist/`，无需安装任何 Node.js 依赖即可直接开箱运行与发布。

### 2. 开发调试阶段
在 `App.zan` 中将 `isDevMode` 设为 `true`：
- 启动时自动开启 DevTools（按 F12 调试控制台与 DOM）；
- 可在 Zan IDE 中直接按 F5 运行和调试。

**前端热开发（免打包免编译）**：开发模式下内嵌 WebServer 优先从磁盘
直读前端资产，并由 `WebDevWatch` 递归监视变化自动刷新页面——改
HTML/CSS/JS 保存后约半秒 WebView 自动重载，完全跳过 `PackWeb` 打包与
主程序重编译：

- 磁盘 Web 根目录解析优先级与打包器一致：`wwwroot/` > `web/dist/` > `web/`；
  可用环境变量 `ZAN_WEBROOT` 显式指定，找不到磁盘目录时回退加密内嵌资产；
- 纯 HTML 开发：模板自带预构建的 `web/dist/`，默认直读的就是它；若要直接
  编辑 `web/` 下的源文件，删除 `web/dist/` 或设置 `ZAN_WEBROOT=web`；
- Vue 开发：改 `web/src/` 后执行 `npm run build`（或 `npm run build -- --watch`
  持续构建），产物落盘 `web/dist/` 时页面自动刷新；
- Zan 侧 RPC 逻辑改动仍需重编译主程序（AOT 固有），前端无需任何操作；
- 控制台可见 `[zan-webapp] dev` 前缀的日志（webRoot 路径、服务地址、刷新事件）。

### 3. 生产发布阶段（加密打包）
执行纯 Zan 自举打包器：
```bash
zanc tools/PackWeb.zan --run
```
（或先编译 `zanc tools/PackWeb.zan -o packweb.exe` 后执行）
打包器会自动检测：
1. 若存在 `web/dist/`，优先打包前端构建后的生产包；
2. 否则自动回退打包 `web/` 下的静态资源并排除开发工程文件；
3. 将静态资源按安全掩码加密并输出至 `src/WebAssets.zan`。

接着编译主程序：
```bash
zanc src/App.html --auto-stdlib -o myapp.exe
```
即可生成单一、加密内嵌、防逆向的 4.6MB 独立可执行文件！

## 跨平台版本自更新机制

本模板内置了统一的跨平台程序自更新（Self-Update）与 Web 资源热更新能力（`src/WebUpdater.zan`）：

1. **Windows 平台运行锁突破**：
   - Windows 操作系统对运行中的可执行文件持有文件锁（禁止覆盖与删除）。
   - Zan 通过**原子重命名接力**：将正在运行的自身重命名为 `app.exe.old`，将下载的新版本移动为正式文件名 `app.exe`，启动新进程并退出当前旧进程。
   - 新程序启动时在 `App.zan` 首行自动调用 `WebUpdater.CleanupOldFiles()` 静默清理 `.old` 备份文件，磁盘零残留。
2. **macOS / Linux 平台原子替换**：
   - 基于 POSIX inode 文件系统语义，对正在运行的进程文件通过 `File.Move` 执行原子覆盖，并通过 `File.SetExecutable` 赋予可执行权限，平滑启动新进程。
3. **Web 资源包增量热更新**：
   - 纯前端改动时可仅分发加密的 `webpatch.bin` 补丁文件，`WebServer` 优先读取补丁包，无需重新下载主程序二进制。
4. **前端开箱即用的更新交互**：
   - 前端通过 `window.zan.invoke('checkUpdate')` 与 `applyUpdate` 驱动原生自更新，Vue 3 界面提供检查更新按钮、日志展示与更新进度提示。
