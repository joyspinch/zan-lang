# Zan.Cli

终端输出工具箱（纯 Zan，无原生依赖）。`using System.Cli;` 按需拉入，
命名空间保留零破坏。

C# 参考 Spectre.Console / Terminal.Gui：ANSI 颜色、表格、进度条、交互提示，
外加**宽字符对齐**（中文表格在等宽终端里必须对齐）。

## 文件

| 文件 | 内容 |
|---|---|
| `Ansi.zan` | ANSI/VT 转义序列原语：SGR、16 色、256 色、真彩色、颜色开关策略 |
| `TextWidth.zan` | 显示宽度：宽字符 / 零宽字符判定、补齐、截断 |
| `Style.zan` | 作用域样式：`Style.Red().AddBold().Apply(text)` |
| `Table.zan` | 表格：`Table` + `TableColumn`（Unicode / ASCII / 无边框） |
| `Progress.zan` | 进度条：`ProgressBar`（纯渲染）+ `Progress`（写同一行） |
| `Prompt.zan` | 交互提示：文本 / 确认 / 单选，读取器与输出 sink 均可注入 |
| `Cli.zan` | 门面：`Cli.Success/Error/Warning/Info/Header/Rule/Table` |

## 为什么是"纯渲染"

所有渲染都返回**字符串**，不直接写终端。这样同一段代码在控制台、文件、
测试断言里产出同样的字节——conformance 能逐字节比对，进度条也不用依赖
计时器。真正写终端由调用方决定（`Console.Write`）。

## 样式

```zan
string line = Style.Red().Apply("失败");
// 组合：一行里只有一段带样式
string mixed = Style.Bold().Apply("标题") + "：" + Style.Gray().Apply("详情");
```

与 `Console.ForegroundColor` 的区别：后者是**粘性**的（设了之后后面所有
输出都带颜色），`Style` 是**局部**的——套完立刻复位。

## 颜色开关

| 依据 | 行为 |
|---|---|
| `ZAN_CLI_COLOR=never` / `off` / `0` | 关闭 |
| `ZAN_CLI_COLOR=always` / `on` / `1` | 开启 |
| `NO_COLOR` 非空（https://no-color.org/） | 关闭 |
| `TERM=dumb` | 关闭 |
| 其余 | 开启 |

也可以在代码里显式控制：`Cli.EnableColor(false)`。

⚠ 运行时目前**没有 isatty 探测能力**（见 `TASKS.md`），所以上表判不出
"输出被重定向到文件"这种情况——需要严格 TTY 语义的应用请自己显式关色。

## 表格

```zan
Table t = new Table();
t.AddColumn("名字", 0).AddColumn("年龄", 2);   // 0 左 1 中 2 右
t.AddRow("爱丽丝", "30");
t.AddRow("bob", "7");
Console.Write(t.Render());
```

```
┌────────┬──────┐
│ 名字   │ 年龄 │
├────────┼──────┤
│ 爱丽丝 │   30 │
│ bob    │    7 │
└────────┴──────┘
```

列宽按**显示宽度**算（`TextWidth`）：中文占两列，组合记号占零列。列宽默认
自适应（取表头与所有单元格的最大显示宽度），也可以用
`table.columns[0].width = 12` 钉死。边框三选一：`UnicodeBorder()`（默认）、
`AsciiBorder()`、`NoBorder()`。

## 进度条

```zan
ProgressBar bar = new ProgressBar(20);
Console.Write(bar.Render(0.42));      // [████████░░░░░░░░░░░░] 42%
Console.Write(bar.RenderCount(3, 7));

Progress p = new Progress(20, "编译");
p.Update(0.5);
p.Done();
```

## 交互提示

```zan
string name = Prompt.Text("名字", "alice");     // 回车用默认值
bool go = Prompt.Confirm("继续", true);
int pick = Prompt.Select("选一个", options);     // 1 起编号
```

读取器与输出 sink 都能注入，所以交互逻辑可以逐字节断言：

```zan
Prompt.TextTo(s => log.Add(s), "名字", "alice", () => "bob");
```

## 已知边界

- 制表字符（`─ │ ┌`）、进度条字符（`█ ░`）在 East Asian Width 里是**歧义
  宽度**：极少数按 CJK 宽度渲染的终端会把它们当两列。需要绝对确定的场合
  用 `AsciiBorder()` / `AsciiBar()`。
- 宽度表覆盖 CJK、假名、谚文、全角标点、常用 emoji 区段与 CJK 扩展 B
  以后；零宽覆盖组合记号区、变体选择符、零宽连接符。
- 提示语是 ASCII + 中文，不带图标——图标字符的显示宽度不确定，会破坏对齐。
