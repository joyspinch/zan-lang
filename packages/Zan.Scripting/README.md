# Zan.Scripting

Zan 程序的进程内 Lua 嵌入（Lua 5.3 / 5.4）。`using System.Scripting;`
按需拉入，命名空间保留零破坏。

## 内容（stdlib/System/Scripting 整目录迁入）

| 文件 | 内容 |
|---|---|
| `Lua.zan` | Lua/LuaValue：状态机、栈操作、脚本求值与宿主互调 |
| `lua54.def` | Windows 链接 lua54.dll 的导出表 |
| `drivers/` | 六平台原生驱动束（driver.manifest：`lua`），`--publish` 自动捆包 |

驱动束随包机制：`drivers/driver.manifest` 在包 src 树内，
zanc 发布时发现并复制 `lua.bundle`（与 Zan.Data 的 Postgres 驱动同构）。

## 消费者

- `src/ide_zan`（ZanIDE.Workspace 的脚本执行）
- `examples/lua/lua_embed.zan`、conformance `lua_embed_smoke.zan`
  （从仓库根编译即可，包发现自动拉入）
