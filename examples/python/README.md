# Python 互操作示例

`python_embed.zan` 演示 Zan 对 CPython 的自然调用语法：

- `Python.Eval("pow")(2.0, 10.0)`：直接调用 Python 对象；
- `math.GetAttr("floor")(22.7)`：调用模块属性；
- `obj[index]` 与 `obj[index] = value`：读写 Python 列表和字典；
- Zan 的 `string`、`long`、`double` 与 `PyObject` 自动封送。

在仓库根目录编译运行：

```powershell
build\zanc.exe examples\python\python_embed.zan --auto-stdlib -o build\python_embed.exe
build\python_embed.exe
```

如果尚未安装 `System.Scripting.Python`，ZanIDE 会提示安装免费库；命令行编译器会输出对应的缺库诊断和安装命令。

## 运行时来源（重要）

`System.Scripting.Python` **不随程序携带** CPython 运行时：它按
`python313.dll … python38.dll` 的新旧顺序探测系统安装的 Python
（PATH 中的官方安装、Store 版均可），找不到时
`Python.IsAvailable()` 返回 false——程序应据此降级或引导用户安装
（官方安装器支持 `/quiet InstallAllUsers=0 PrependPath=1` 静默装），
不要在标准库层做系统级安装。

因此发布一个使用 Python 的程序时，请把"目标机器装有 Python"作为
前置条件写进 README/安装引导；敏感逻辑不要放进 Python 脚本
（脚本以明文/可还原形态存在），核心逻辑用 Zan 编写（编译为机器码）。
需要加密内嵌脚本的场景请使用 `System.Scripting.Lua` +
`tools/pack_script.zan`（字节码 + AES-GCM，见 Lua.LoadBytes）。
