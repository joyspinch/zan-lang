# Zan.Linq

LINQ 风格的序列算子（纯 Zan，无原生依赖）。`using System.Linq;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Linq 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Enumerable.zan` | Where/Select/OrderBy/GroupBy/Sum/… 查询扩展与委托 |
| `Expression.zan` | 表达式树（Expr/ExprNode）与分析 |
| `Stream.zan` | 流式管道（Stream） |

## 消费者

- `Zan.Data`（Orm 的 ExprSql/OrmSelect 用查询算子）
- conformance：`linq_*.zan`、`generics_linq.zan`、`generics_uniform_repr.zan`、
  `linq_and_zandb_opt.zan`（从仓库根编译即可，包发现自动拉入）

## 边界

生成器子编译（`--no-packages`）的闭包不含 Linq——`GenDbEmit.zan` 里的
`using System.Linq;` 只是生成代码的字符串字面量，不是真实依赖（已核验）。
