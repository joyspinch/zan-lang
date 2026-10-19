# 自举后端的托管字符串（selfhost managed strings）

适用：改 `src/selfhost/irgen*.zan`（自举 IR 后端）里任何与 `string` 有关的代码。

## 1. 布局：值 = payload 指针，头在 payload 前面

自举的 `string` 值是一个 `i8*`，指向托管块的 **payload**：

```
  [ i64 len ][ i64 magic ][ bytes... ][ NUL ]
  ^ payload-16  ^ payload-8  ^ payload(=值)
```

- `len` 是**精确字节数**（不是 `strlen`）：嵌入的 NUL 不再截断长度。
- `magic` = `1515082580`（0x5A4E5354）：`zstr_len` 读到它才认这是托管串。
- payload 仍然 **NUL 结尾** → 传给 C 运行时（`printf("%s")` 等）依然合法，
  与 C 宿主的运行时 ABI 不变。
- 运行时辅助（自举后端生成）：
  `zstr_new(len)` / `zstr_len(p)` / `zstr_dup(p)` / `zstr_of(cstr)` / `zstr_bad()`。

## 2. 每一个 string 的来源都要带头

新造一个 string 时只有三条路，**没有第四条**：

1. `zstr_new(len)` —— 自己知道长度；
2. `zstr_of(cstr)` —— 包装一个外部返回的裸 C 串（extern 的 string 返回值、
   `snprintf` 出来的数字字符串）；
3. **字面量** —— 常量区直接按 `[len][magic][bytes][NUL]` 排好，值取
   `getelementptr ..., 0, 16`（跳过 16 字节头）。

`zstr_len` 读到不认识的 magic 就 `abort()`——这是**故意的**：漏掉一个来源
会立刻炸，而不是静默返回一个错长度。调试时先想"哪个 string 不是托管的"。

自举后端里所有 string 的产生点（改后端时逐个核对）：

- `GenStr`（字面量）、`StrConcatN` / `StrConcat`（拼接）、`ToCStr`（数字→串，
  结果要 `zstr_of`）、`zsb_tostring`（StringBuilder）、`zstr_trim` / `zstr_case` /
  `zstr_replace` / `zstr_split` / `zstr_join`（字符串方法）、`Substring` /
  `ToString`（实例方法）、`NativeMemory.GetString` / `Base64Encode`、
  `File.ReadAllText` / `Environment.ExeDir` / `Directory.ListNames`、
  命令行参数 `[DllImport]` extern 的 string 返回值。

## 3. 数组 → string 的隐式转换要在**每个 string 位置**做

`byte[]` / `char[]` → `string`（C# 的 view）在自举里是**复制打包**：
自举的数组是「每元素一个 8 字节槽」，不是紧凑字节，所以不能直接当字符串
指针用，必须逐元素取低 8 位拷进新的托管串。

隐式转换发生在这些位置，**漏一个就会把裸数组指针当 string 用**：

- 变量声明（有初值）、局部赋值、字段赋值；
- **实参传给 string 形参**（`CallArg`）；
- `return`（同步路径与 async 路径都要）；
- 数组元素 / 字典值（`List<string>` / `Dictionary<K,string>` 的槽）。

统一入口：`StrSlotConv(targetTy, v)`——目标是 string、值不是 string 时打包。

## 5. 两种长度语义：精确长度 vs C 可见长度

C 宿主对**同一个**托管串给出两套长度，自举必须照抄，否则差分门红：

| 语义 | 辅助 | 用在 |
|------|------|------|
| 精确字节数（嵌入 NUL 也算） | `zstr_len(p)` | `Length`、索引 `p[i]`、`Substring`、拼接 `a + b`、`==` / `!=` |
| C 可见长度（截断于第一个 NUL） | `zstr_clen(p)` = `strlen(p)` | `Trim` / `ToUpper` / `ToLower` / `Replace` / `Split` / `ToString` / `EndsWith` / `LastIndexOf` / `StartsWith` |

`Contains` / `IndexOf` 用 `strstr`，本身就是 C 语义，不用改。

判据：**C 宿主把 receiver 当 C 字符串**的方法一律用 `zstr_clen`；
**C 宿主按串的真实长度**的方法用 `zstr_len`。实测（C 宿主，`h = "AB\0CD"`）：

```
len=5  sublen=2  sub1len=3  catlen=6  eq_same=1  eq_cut=0
trimlen=2  uplen=2  replen=2  parts=1  tostrlen=2
indexof_empty=0  lastindexof_empty=2
h.IndexOf("CD")=-1  h.LastIndexOf("C")=-1  h.EndsWith("CD")=0
```

- `LastIndexOf("")` 返回 **C 可见长度**（2，不是 5）：扫描到串尾空匹配。
- `ToString()` 也是 C 可见语义（长度 2），容易漏改。

**改后端的顺序**：先写探针用 C 宿主跑出期望值，再改自举，最后把探针固化成
`tests/conformance/string_nul_search.zan` + 差分门用例。**不要凭 C# 规范猜**
——差分门比的是「与 C 宿主一致」。

## 6. 踩过的坑（都真实发生过）

- **头不能写在 malloc 块之外**。`zstr_new` 一开始把 len/magic 写在
  `raw-16` / `raw-8`，而 `raw` 是 malloc 的块首 → 越界写 → 堆损坏
  （0xC0000374 / abort）。头必须放在**块内**（`raw+0` / `raw+8`，payload
  取 `raw+16`）。
- **payload 中间的指针没有头**。`zstr_split` 的末段、`zstr_replace` 的尾巴
  指向 payload 内部偏移，`zstr_len` / `zstr_clen` 在那里读到的是正文字节
  → magic 不匹配 → `abort()`（或读到垃圾长度）。这些位置只能用裸
  `strlen`。
- **`zstr_clen` 要保留 magic 校验**：base 指针先用 `zstr_len` 的 magic 判定
  是托管串，再取 `strlen`；指针不合法就 `abort()`，不要静默量垃圾。
- **LLVM 的标签名与 SSA 值名共享同一个命名空间**：`label %fin` 与
  `%fin = ...` 在同一函数里会报 "multiple definition of local value"。
  标签叫 `fin` 时，SSA 名要另取（比如 `%finbuf`）。
- **LLVM 的整数常量不支持十六进制字面量**：`store i64 0x5A4E5354` 会被
  当成浮点常量报错，只能写十进制。
- **`select` 的条件必须是 SSA 值**，不能内联 `icmp`：
  `select i64 (icmp ...)` 非法，要先 `icmp` 到一个具名值再 `select i1`。
- **LLVM 字符串常量的 `c"..."` 里每个字节都要转义**：`"\54\53\4E\5A\00"`
  是 8 字节，直接写 `"54534E5A"` 是 16 个字面字符。
- **clang 对 `.ll` 输入默认不跑 verifier**，非法 IR 静默进后端，表现为
  0xC0000005；IR 文本自己拼错类型片段时优先怀疑分支里手拼的类型。

## 7. 已知未收敛项

- `byte[]` / `char[]` → `string` 是复制而非共享 view（C 宿主是共享），
  见 `TASKS.md` A390。
- 自举的 string 不参与 ARC（`malloc` 之后不释放），与改造前一致。
