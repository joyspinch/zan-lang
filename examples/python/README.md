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

如果尚未安装 `System.Scripting.Python`，ZanIDE 会提示安装免费库；命令行编译器会输出对应的缺库诊断和安装命令。商城发行包携带目标平台的 Python 嵌入式运行时及其许可证，因此不要求用户另行安装系统 Python。
