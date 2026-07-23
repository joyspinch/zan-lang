# ZanIDE 生产可用执行计划（2026-07）

> 本计划来自对当前代码（非文档）的逐项核实。目标：把 ZanIDE 发布成生产可用、
> 可开发商业程序的版本。已确认**不做安装器**（保持免安装可搬运目录发布）。
>
> 已排除的过时结论（核实时发现文档滞后，勿再据此立项）：
> - Windows 自包含工具链已落地（bundled ld + mingw + gdb，无需 clang）；
> - System.IO / Process 已经通过 `DllImport("crt")` 跨平台；
> - 调试是 gdb 驱动的真调试（含随包 gdb）；
> - LSP 已有 references / signatureHelp / codeAction；DAP 已有 evaluate / 异常断点 / logpoints / setVariable；
> - IDE 已有 Git 面板和中英双语（`ZanIDE.T`）。

---

## 阶段 1：修已知硬伤（P0，先于一切发布动作）

| # | 事项 | 现状 | 验收标准 |
|---|------|------|----------|
| 1.1 | 修复 O0 codegen bug：`docs/bugs/O0-list-index-write-stack-leak.md` | 已知未修，含复现用例 | `tests/conformance/list_index_write_o0.zan` 在 O0 下通过且无泄漏报告 |
| 1.2 | stdlib 错误模型统一：File/Net/Json 等失败静默返回空串/0 | 语言已有 try/catch/throw，但库不用 | 制定并落实约定（IO/Net/Json 失败 throw 标准异常类型）；conformance 增加异常路径用例 |
| 1.3 | IDE 长时运行泄漏验证 | 无 | 用 `--check-leaks` + 脚本化 IDE 操作跑 2h+，零净增长 |

## 阶段 2：编辑体验补齐（P1）

| # | 事项 | 现状 | 验收标准 |
|---|------|------|----------|
| 2.1 | LSP rename（跨文件重命名） | 缺 | `textDocument/rename` + prepareRename，含跨文件测试 |
| 2.2 | LSP workspace/symbol | 缺 | 工作区符号搜索可用 |
| 2.3 | IDE 接入 zanfmt（format-on-save / 手动格式化） | zanfmt 已构建但 IDE 0 处引用 | IDE 菜单/保存钩子调用 zanfmt，往返稳定（format 两次幂等） |
| 2.4 | LSP 增量文档同步 + 大文件性能 | 全量同步；10MB 级未验证 | 打开/编辑 10MB 文件不卡顿（编辑延迟 < 100ms） |
| 2.5 | 崩溃恢复 / 未保存文件自动恢复 | 缺 | 强杀 IDE 后重启可恢复未保存内容 |

## 阶段 3：产品化基础设施（P1–P2）

| # | 事项 | 决策/现状 | 验收标准 |
|---|------|-----------|----------|
| 3.1 | 代码签名 | 要做（SmartScreen 会拦未签名 exe） | publish 脚本签名 ZanIDE.exe/zanc.exe/工具链 exe；干净虚拟机上无 SmartScreen 拦截 |
| 3.2 | 自动更新 | 要做 | IDE 内检查更新 + 下载替换 dist 目录，失败可回滚 |
| 3.3 | 崩溃上报 | 要做 | IDE 崩溃捕获（minidump/栈）+ 用户同意后上传；编译器 zan_crash 同通道 |
| 3.4 | 标准库/包商店 | 要做（zanpkg 为底座） | IDE 内可浏览/安装/发布版本化包 |
| 3.5 | 安装器 | **不做** | — |

## 阶段 4：质量保障体系（P1，与阶段 2/3 并行）

| # | 事项 | 现状 | 验收标准 |
|---|------|------|----------|
| 4.1 | IDE 自动化 UI 测试 | tests/gui 仅 3 例；IDE 本体无 | 覆盖golden path：新建项目→编辑→构建→运行→调试→git 提交 |
| 4.2 | 发布包冒烟测试 | 无 | CI 对 dist 产物在干净环境跑：编译 hello、GUI 程序、调试会话 |
| 4.3 | Regex 等文本能力补齐 | Encoding 已扩充，Regex 缺 | System.Text.Regex 基本集（match/replace/groups） |

## 阶段 5：跨平台（P2，二期）

| # | 事项 | 现状 | 验收标准 |
|---|------|------|----------|
| 5.1 | Linux 自包含链接 | 设计已定（SELF_CONTAINED_TOOLCHAIN.md §4），未落地 | 无 gcc/clang 的 Linux 机器上 `zanc hello.zan` 可编译运行 |
| 5.2 | macOS 自包含链接 | 同上，需处理 tbd 存根 + ad-hoc 签名 | 无 Xcode 的 macOS 上可编译运行 |
| 5.3 | Linux/macOS IDE 发布脚本 | 仅 publish_ide.ps1 | 各平台产出可搬运目录，GUI（x11/sdl/mac 后端）验证通过 |

## 语言层已确认不阻塞（可选增强）

yield 目前急切降级为 List（非惰性迭代器）、无 LINQ 查询语法、无
checked/using 声明/init/with、无反射与 attributes 体系。均不阻塞商业开发，
按需求排期。

---

### 建议执行顺序

1. 阶段 1（硬伤）→ 2. 阶段 2.1–2.3 + 阶段 4.1–4.2 → 3. 阶段 3（签名→自动更新→崩溃上报→商店）→ 4. 阶段 2.4–2.5、4.3 → 5. 阶段 5。
