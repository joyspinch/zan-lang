# docs 索引

本目录放**语法与开发规范**（七份）与少量**在册设计提案**。2026-11-16 清理掉了 64 份计划/台账/验收/
审计/归档文档（约 1.27 MB）：它们记录的多是某一轮的时点结论，落地后即失真，
读的人却无法分辨哪句还有效——上下文污染与"文档漂移"主要来自那里。规则见
[`WORKSPACE_CONVENTIONS.md`](WORKSPACE_CONVENTIONS.md) 的「docs 文档维护规则」。

**不新增第八份规范**：要写的规范先归到下表某一份；进度、缺陷归因、验收边界进
`TASKS.md` 的 B 编号与提交说明，不进这里。跨多个 B 编号的目标设计可以写成设计提案，
准入、收缩与退出规则见 `WORKSPACE_CONVENTIONS.md` 附录 §7——提案随落地收缩，
全部落地即删除。

| 文档 | 是什么 | 什么时候读 |
|---|---|---|
| [`SPEC.md`](SPEC.md) | Zan 语言规范（词法/类型/声明/语句/内存模型/FFI/标准库结构） | 写 Zan、判断"这写法合不合法" |
| [`SPEC.zh-CN.md`](SPEC.zh-CN.md) | 中文精编版：语言主干（词法/类型/声明/控制流/ARC/FFI/异步），不与英文版逐节一一对应 | 想快速抓语言要点 |
| [`WORKSPACE_CONVENTIONS.md`](WORKSPACE_CONVENTIONS.md) | 工作区规范全文（AGENTS.md 每条硬规则的可执行正文） | 动手前、遇到流程/验证/提交问题 |
| [`BUILD_TOOLCHAIN.md`](BUILD_TOOLCHAIN.md) | 每平台固定工具链配置（Windows 固定 clang + mozbuild LLVM） | 配置/重建 `build/`、链接失败 |
| [`ZANC_CLI.md`](ZANC_CLI.md) | `zanc` 命令全解：参数、交叉目标、环境变量、标准配方 | 编译/发布/测试命令搞不清 |
| [`STDLIB.md`](STDLIB.md) | 标准库与内建类型的语义契约 | 查 API 语义、改 stdlib |

## 设计提案（目标设计，落地前不为真）

| 提案 | 状态 | 是什么 |
|---|---|---|
| [`ZAN_STANDARD.md`](ZAN_STANDARD.md) | 立项（2026-11-24） | 生产级语言标准：AI 写、机器验（S1–S33，P1–P5 路线图） |
| [`GUI_REFACTOR.md`](GUI_REFACTOR.md) | 进行中（2026-11-22） | Zan.Gui 深度改造：类型化、唯一事实来源、拆包 |
| [`OBJECT_LAYOUT.md`](OBJECT_LAYOUT.md) | 进行中（2026-11-24） | 对象头 16+8 → 8 字节：rc+站点号同字、vptr 按继承树条件化 |
| [`BUILD_CACHE.md`](BUILD_CACHE.md) | 进行中（2026-11-24） | 构建缓存：阶段 1 LLVM 后端记忆化已落地；阶段 2 按来源分区 ThinLTO、阶段 3 前端缓存（B-ID227） |
| [`GUI_CONTROL_CATALOG.md`](GUI_CONTROL_CATALOG.md) | 已落地（2026-11-23） | 编译器生成的控件清单；按 §7 下次涉及时并入规范后删除 |

编码规范（含禁自绘等）不在本目录：在 `.agents/skills/`，清单与准入标准见
`.agents/skills/SKILLS_GOVERNANCE.md`。

通过脚本核验本目录没有死链（新增/改动后跑）：

```bash
python scripts/check_doc_refs.py --all
```