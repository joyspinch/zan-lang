# agent-kb — 智能体开发 Zan 项目的知识库

这个目录是给**在 Zan 仓库里干活的智能体（以及新来的人）**准备的。它回答的不是
"Zan 语言怎么用"（那是 `docs/SPEC.md`、`docs/STDLIB.md`），而是：

> 拿到一个需求，怎么在这个 30 万行、编译器 + 运行时 + 标准库 + 自举 IDE 的仓库里，
> 又快又准地找到该改的地方，改完怎么证明它对，出错怎么定位到根因而不是绕过去。

## 读法

| 文件 | 什么时候读 |
| --- | --- |
| [project-map.md](project-map.md) | 每次开工第一份：目录 → 职责 → 入口文件 → 改这类需求该动哪儿 |
| [debugging-playbook.md](debugging-playbook.md) | 编译报错 / 运行崩 / 结果不对：分层定位 + 最小探针 + 根因修复的完整方法论 |
| [gui-development.md](gui-development.md) | 改控件、主题、设计器、HMI：立即模式渲染约定、Theme token、控件清单 |
| [templates-and-wizard.md](templates-and-wizard.md) | 改模板 / 新建项目向导 / 项目脚手架：manifest 字段与数据流 |
| [testing.md](testing.md) | 提交前：选哪个测试层级、哪些测试互斥、怎么加回归 |
| [agent-workflow.md](agent-workflow.md) | 完整闭环：需求 → 拆解 → 计划 → 实施 → 自检 → 回归 → 交付 → 沉淀 |
| [knowledge-graph.md](knowledge-graph.md) | IDE 内置知识库/知识图谱的数据模型、面板设计、清理规则 |
| [diagnostics-reporting.md](diagnostics-reporting.md) | 帮助与反馈面板：错误采集、脱敏、上报协议（服务端后做） |
| [gaps.md](gaps.md) | 小助手要能独立做出项目，还缺什么（现状 → 缺口 → 落地形式） |

## 三条硬规矩

1. **不绕过编译器缺陷。** 遇到"这么写编译不过，换个写法绕开"就停下：先判断是不是
   编译器的锅（见 [debugging-playbook.md](debugging-playbook.md)），是就修编译器 + 补
   `tests/diag` 或 `tests/conformance` 回归。绕过去的写法会在标准库里留下一堆无法解释
   的怪代码，也让下一个人重复踩坑。
2. **一切临时产物进 `_scratch/`，构建只进 `build/`，测试只进 `tests/`。** 仓库根目录保持干净。
3. **验证到位再说完成。** "能编过"不等于"能跑"，"能跑"不等于"界面对"。分层验证见
   [testing.md](testing.md)。

## 这份知识库自己怎么维护

- 手写部分（本目录）只写**方法、约定、判断依据**——这些不会因为一次重构就失效。
- 会随代码漂移的事实（有哪些控件/模板/主题 token/测试/符号、谁引用谁）**不手写**，
  由 IDE 内置的知识库索引从代码里抽取，见 [knowledge-graph.md](knowledge-graph.md)。
- 本目录里每处引用具体文件/行号的地方，都要写清"怎么重新找到它"（grep 关键字、
  函数名），这样即使行号漂了也能自愈。
