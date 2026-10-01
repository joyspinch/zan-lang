# Zan.Knowledge

> **成熟度：实验性** —— 由仓库内 CLI/MCP 工具链消费，但尚无 conformance 测试锁；
> 接口可能随工具链调整。

设计器知识库：gallery 索引与 Zform schema 生成（CLI 与 MCP server 共用的
source-driven 产物与告警）。`using System.Knowledge;` 按需拉入，
命名空间保留零破坏。

## 内容（stdlib/System/Knowledge 整目录迁入）

| 文件 | 内容 |
|---|---|
| `GalleryIndex.zan` | gallery 索引生成与产物（gallery JSON、条目/发现/模板计数、告警） |
| `ZformSchema.zan` | Zform schema 生成与产物（schema JSON、控件清单、解析告警）、设计器属性静态视图 |

## 消费者

- `Zan.Gui`（Designer 的 schema 视图）
- zan-site / build_gallery 流程（从仓库根编译即可，包发现自动拉入）
