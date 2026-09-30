# Zan.Resources

资源包打包与读取（纯 Zan，无原生依赖）。`using System.Resources;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Resources 整目录迁入）

| 文件 | 内容 |
|---|---|
| `ResourcePack.zan` | ResourcePack/ResourcePackWriter/ResourceEntry/PackIndexEntry：打包、索引、读取 |

## 消费者

- `src/ide_zan`（AssetManager 的资源包管理）、`Zan.Game`（Packed 资产）
- conformance `respack_roundtrip.zan`（从仓库根编译即可，包发现自动拉入）
