# Lua 互操作示例

`lua_embed.zan` 演示 Zan 对 Lua（5.3 / 5.4）的进程内嵌入：

- `Lua.Eval("2 ^ 10")`、`Lua.CallD("math.floor", 22.7)`：求值与标准库调用；
- `Lua.GetGlobal("greet")("Zan")`：直接调用脚本里定义的函数；
- `t[1]`、`t["k"]` 与对应的赋值：读写 Lua 表（数组与字典是同一种类型）；
- `Lua.RegisterFunction("zan_mul", ZanMul)`：把静态 Zan 方法暴露成 Lua 全局函数；
- Zan 的 `string`、`long`、`double` 与 `LuaValue` 自动封送，`LuaValue` 由 ARC
  持有注册表引用，回收时交回 Lua GC。

在仓库根目录编译运行：

```powershell
build\zanc.exe examples\lua\lua_embed.zan --auto-stdlib -o build\lua_embed.exe
build\lua_embed.exe
```

和 `System.Scripting.Python` 一样，Lua 是**可选运行期依赖**：所有入口点在运行时
用 `Interop.Load`/`Interop.Symbol` 解析，因此没装 Lua 的机器只会让
`Lua.IsAvailable()` 返回 false，而不是编译或加载失败。DLL 不在 PATH 时可用
`Lua.LoadPath("C:/lua/lua54.dll")` 指定路径。5.1 / LuaJIT 的注册表索引与
`pcall`/数值转换导出不同，不在支持范围内。
