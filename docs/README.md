# docs 索引

本目录只放**语法与开发规范**，共七份。2026-11-16 清理掉了 64 份计划/台账/验收/
审计/归档文档（约 1.27 MB）：它们记录的多是某一轮的时点结论，落地后即失真，
读的人却无法分辨哪句还有效——上下文污染与"文档漂移"主要来自那里。规则见
[`WORKSPACE_CONVENTIONS.md`](WORKSPACE_CONVENTIONS.md) 的「docs 文档维护规则」。

**不新增第八份**：要写的东西先归到下表某一份；进度、缺陷归因、验收边界进
`TASKS.md` 的 B 编号与提交说明，不进这里。

| 文档 | 是什么 | 什么时候读 |
|---|---|---|
| [`SPEC.md`](SPEC.md) | Zan 语言规范（词法/类型/声明/语句/内存模型/FFI/标准库结构） | 写 Zan、判断"这写法合不合法" |
| [`SPEC.zh-CN.md`](SPEC.zh-CN.md) | 中文精编版：语言主干（词法/类型/声明/控制流/ARC/FFI/异步），不与英文版逐节一一对应 | 想快速抓语言要点 |
| [`WORKSPACE_CONVENTIONS.md`](WORKSPACE_CONVENTIONS.md) | 工作区规范全文（AGENTS.md 每条硬规则的可执行正文） | 动手前、遇到流程/验证/提交问题 |
| [`BUILD_TOOLCHAIN.md`](BUILD_TOOLCHAIN.md) | 每平台固定工具链配置（Windows 固定 clang + mozbuild LLVM） | 配置/重建 `build/`、链接失败 |
| [`ZANC_CLI.md`](ZANC_CLI.md) | `zanc` 命令全解：参数、交叉目标、环境变量、标准配方 | 编译/发布/测试命令搞不清 |
| [`STDLIB.md`](STDLIB.md) | 标准库与内建类型的语义契约 | 查 API 语义、改 stdlib |

编码规范（含禁自绘等）不在本目录：在 `.agents/skills/`，清单与准入标准见
`.agents/skills/SKILLS_GOVERNANCE.md`。

通过脚本核验本目录没有死链（新增/改动后跑）：

```bash
python scripts/check_doc_refs.py --all
```