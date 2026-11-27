# 自举闭环专题（三）：泛型特化、委托/Lambda、字面量与插值（§76–§114）

返回总索引：[selfhost-closure.md](selfhost-closure.md)

## 76. 无类型的 lambda 参数在推断 body 类型时要换成委托的形参类型

- 坑（2026-10-21 实测）：`us.MinBy(u => u.name)`（`MinBy<T, R>(this List<T>, Sel<T, R>)`）
  在自举里把方法类型参数 **R 绑成了 `var`**：克隆体的符号名是
  `Ext_MinBy__SP_double_var_List.T_Sel.T.R`——`var` 是 parser 给**无类型**
  lambda 参数写的占位类型（`parser.zan` 的 `ParseLambda`：`p.type =
  MkTypeRef("var")`）。
  后果是克隆体里 `R mk` 成了**指针槽**，而 `k < mk` 按整数比较 →
  `icmp slt i64 %t51, %t52`（`%t51` 是 `i8*`）→ **坏 IR**（用例
  `generics_uniform_repr`）。
- 根因：`LambdaBodyType` 只在形参类型为 **null** 时才用委托的形参类型；
  parser 给的是 `"var"`（不是 null）→ 形参类型保持 `var` → body 的类型
  （`u.name`）推不出来 → 类型参数绑成 `var`。
- 做法：把 `"var"` 和 `null` 一样看待——无类型的 lambda 参数取委托的形参
  类型（`dps[pi]`）。绑上之后 `u => u.name` 的 body 类型是 `string`、
  `u => u.score` 是 `double`、`x => x`（`List<double>`）是 `double`，
  R 绑到真正的类型，克隆体里的 `R mk` 才是 double/string 而不是指针。
- 通用教训：**"没写类型"和"类型未知"在 AST 里是两种表示**（null vs
  `"var"`），凡是"没有就用上下文补"的地方，两种都要认。
## 77. 硬件内建要复刻宿主的降级表；自举 IR 不能用目标特性 intrinsic

- 坑（2026-10-21 实测）：自举把 `NativeMemory.AesCbcEncrypt` 这类硬件内建
  当成普通 extern 方法，符号按驼峰拼成 `@zan_hw_AesCbcEncrypt` → 链接期
  LNK2019（运行时导出的是小写下划线 `zan_hw_aes_cbc_encrypt`）。
- 根因：**宿主不是在 extern 声明层改符号名**，而是在调用点把整张硬件内建表
  **内联降级**成对 `zan_hw_*` 的直接调用（`src/compiler/irgen_expr.c` 的
  `emit_native_memory_call` / `emit_pixel_ops_call`）：每个方法的**参数顺序按
  运行时签名重排**（`AesCbcEncrypt(dst,src,size,key,keybits,iv)` 在运行时的
  顺序是 `src,size,key,keybits,iv,dst`），`keybits` 还要收窄成 i32。
  所以自举不能"声明 extern 再按声明序调用"，必须照抄这张表。
- 做法：自举侧建三个助手——`NmCall`/`NmCallVoid` 发调用，`NmI32` 把整数实参
  收窄到 i32；参数值是裸的，**类型由运行时签名串给出**，按签名逐个拼成
  typed value。覆盖清单：AES（CBC/ECB/CTR/GCM）、GHASH、RSA（ModPow/CRT）、
  X25519、SM4、JSON 扫描、Base64、摘要（SHA1/256/512、SM3）、Load64/AsI64/
  AsF64、ScanNotByte，以及 PixelOps 的 BlendOver/SwapRB/FillRect/
  ResampleBilinearRow。
- 坑（LLVM 文本 IR 语法，实测）：call 的实参**必须**是 typed value
  （`i8* %p`、`i64 16`、`i32 %t5`），裸 SSA 名或裸常量都报
  "expected value token"；指令操作数**不能**是括号表达式
  （`and i64 (lshr i64 %d64, 8), 255` 非法），要拆成单独的 SSA 名；
  同类型的 `bitcast` 也非法（用 `add x, 0` 代替）。
- 坑（可移植性，实测）：自举产出的 .ll 由 sweep 用 **generic CPU** 的 clang
  编译；宿主自己出码带 `-mcpu/-mattr` 所以不受限。于是自举 IR **不能**用
  需要目标特性的 intrinsic——`llvm.x86.sse42.crc32.*` 在 generic CPU 下
  "Cannot select"。`Sse42.Crc32` 改成查表 CRC32C 的软件实现（逐字节、
  小端），语义与硬件指令一致。
- 通用教训：**宿主是规格**——自举要复刻的是宿主的**降级表**（符号名 + 参数
  顺序 + 类型宽度 + 可移植实现），不是"能编译过"就行。

## 78. byte[] 在自举里必须真的是字节数组（一字节一个元素）

- 坑（2026-10-21 实测）：自举的数组一律按 **8 字节槽**存元素，但宿主里
  `byte[]` 就是字节数组——标准库把它当**原始缓冲**交给运行时（Base64、
  摘要内核、协议编解码）。自举里 `byte[]` 每元素占 8 字节，于是所有
  "按字节读"的代码读到垃圾：Base64 输出 `SAAAAAAAAABhAAAAA...`，
  `Aes.DecryptCbc` 解不出明文，hw_crypto_pixel 在 `dec.ToStr(...)` 上
  **空指针崩溃**（解密返回 null）。
- 判据：**标准库有没有把这种数组当字节缓冲用**。有，就必须按字节存；
  没有（只是元素列表），8 字节槽行为等价，可以不动。
- 做法（自举侧）：`ArrElemSize` 决定元素宽度——`byte`/`sbyte` 是 1 字节，
  其余仍是 8 字节槽（`char[]` 保持 8 字节：char 是码点不是字节，截到
  1 字节会丢值）。要跟着改的点：分配（`new T[n]` 的字节数与初始化器）、
  字符串↔byte[] 桥接（ToBytes / ToStr）、索引读写、`List<T>.ToArray()`、
  `foreach`。
- 陷阱：数组指针的静态类型还是 `i64*`（宽度未知时才统一），所以 1 字节
  元素要 `bitcast i64* → i8*` 再 `getelementptr i8`；`store`/`load` 的
  宽度也要跟着变（`trunc i64 → i8` / `zext i8 → i64`）。
- 通用教训：**"行为一致"不等于"随便换个布局"**——布局只要被标准库看见
  （被当缓冲、被当指针传出去），就必须和宿主一样。
## 79. 位运算内建要按实参的**声明宽度**落码（32 位 vs 64 位）

- 坑（2026-10-21 实测）：自举所有整数都住在 64 位槽上，
  `BitOperations.RotateLeft/RotateRight` 一律用 `llvm.fshl.i64`/
  `fshr.i64`；宿主按实参的**声明类型**选宽度（`is_64bit_int_arg`：
  `long`/`ulong` 走 64 位，其余收窄到 32 位）。于是
  `BitOperations.RotateLeft(int, int)` 在自举里是 64 位旋转——
  `RotL32` 全部算错，`Md5.Hash("")` 给出的是 `md5("hello world")`；
  `LeadingZeroCount`/`TrailingZeroCount` 对负数也错（64 位的符号扩展
  把前导零算没了）。
- 做法：判宽度用**实参的声明类型**（自举的 `ArgTypeNode`），不是值的
  运行时类型——自举的值永远是 64 位槽。32 位路径：`trunc i64 → i32`，
  用 `fshl.i32`/`fshr.i32`/`ctlz.i32`/`cttz.i32`，再 `zext` 回槽。
- 通用教训：**自举的"统一槽宽"会把宿主的定宽语义抹平**。凡是宿主按
  参数类型分 32/64 位的地方（位运算、`ToString` 的格式宽度、窄化转换），
  自举都得显式收窄再算。

## 80. 给未声明标识符赋值：宿主**静默丢弃**，只有"读"才是错

- 坑（2026-10-21 实测）：自举的 checker 对未声明标识符一律报
  `undefined identifier 'x'`（读和写都报）。宿主只在**读**的时候报错
  （措辞 `use of undeclared identifier 'x'`，由 irgen 兜底发出）；**写**
  （`x = v`）在宿主里既不报错也不存——左值解析到"局部变量 / 实例字段 /
  静态字段"三处都没命中时，这次赋值被直接丢掉。于是标准库里一段
  "赋了但没人读"的代码（Zan.Gui 的 `Control.zan` 给从未声明的
  `paneMin`/`paneMax` 赋值）把整包编译拖垮：自举报
  `undefined identifier 'paneMin'`，宿主照收不误。
- 做法：checker 对**普通 `=`** 的左值放行——左值是标识符且
  `KnownValue` 为假时只检查右值；复合赋值（`+=`）和 `??=` 仍要报错
  （它们要先读左值）。自举的 irgen 本来就"静默丢弃"（`StoreTo` 找不到
  落点就 `return`），所以只有 checker 这一处要放宽。
- 通用教训：**"宿主是规格"包含宿主的宽容**。自举比宿主更严，就会把
  宿主能编译的程序判死；诊断的"宽严"也要对齐，不能只对齐措辞。
## 81. `phi` 放进函数入口块：初值永远不落地，循环变量是垃圾

- 坑（2026-10-21 实测）：自举给"结构体数组填默认实例"生成循环时写成
  ```
  define void @T_initarr(i64* %arr, i64 %n) {
  entry:
    %i = phi i64 [ 0, %entry ], [ %i2, %body ]
  ```
  `phi` 待在**函数入口块**里：前驱只有回边 `body`，入口自环那条入边
  没有独立的块来承载"把 0 写进 phi 槽"的赋值——LLVM 把它降成一次
  **未初始化的栈槽读取**。于是循环计数器是垃圾值：要么一次都不跑
  （数组元素全是 null），要么写到野地址（当场崩）。表现是
  `new Point[3]` 之后 `pts[0].x = 5` 空指针崩溃，而
  `new Point[]{p, p}`（初始化器路径）正常。
- 做法：循环用**规范形状**——入口块只 `br label %loop`，`phi` 放在单独
  的 `%loop` 块里：
  ```
  entry: br label %loop
  loop:  %i = phi i64 [ 0, %entry ], [ %i2, %body ]
  ```
  自举里另两处同样的循环（`__zan_nm_crc32`、`__zan_async_*`）本来就是
  规范形状，只有新加的这处踩了坑。
- 通用教训：**生成的 IR 要按语义写，不要按"看着像"写**。LLVM 文本 IR
  里 `phi` 的入边必须与前驱一一对应；入口块自带的那条"函数入口"入边
  没有块来承载它的赋值。
## 82. 自举把结构体**装箱**：数组元素与静态字段各要一个默认实例

- 坑（2026-10-21 实测）：自举没有聚合值——一个结构体实例是堆对象
  （`%Name*`），结构体字段存的是**指向嵌套实例的指针**。宿主把结构体
  **按值内联**在数组槽/静态字段里，所以 `new Point[3]` 的三个元素天生
  就是三个"默认值"。自举的数组槽是 i64，calloc 出来是 0 → 槽里是
  **空指针**，`pts[0].x = 5` 直接空指针崩溃；静态结构体字段
  （`static Outer global;`）同理。
- 做法：给每个结构体类型生成两个模块级函数——`<T>_newobj()`（分配一个
  零值实例，并给它的结构体字段递归分配嵌套实例）与
  `<T>_initarr(arr, n)`（按槽填默认实例）；`new T[n]` 在没有初始化器
  时调用后者。静态结构体字段在**静态初始化**里调 `<T>_newobj()` 存进
  全局。类实例的结构体字段本来就有（`InitNestedFields`）。
- 通用教训：**换一种表示法（装箱 vs 内联）就要把"默认构造"的语义一起
  搬过来**——零值不再自动等于"一个可用的实例"。
## 83. 对象初始化器写 `Binding<T>` 字段：值要包成**新的 Binding 对象**

- 坑（2026-10-21 实测）：`new Card { title = "x" }`（`title` 是
  `Binding<string>`）在自举里把**裸字符串**直接塞进 `%Binding*` 槽，还
  按对象指针去 `zretain`——把字符串头当成 Binding 头改引用计数，
  随后 `a.title.Get()` 在字符串头上读长度，当场崩。普通赋值路径
  （`a.title = "y"`）是对的：它走 `GenBindingValue`，会包一个新 Binding
  对象。
- 做法：对象初始化器的字段写入要和普通赋值**同一条路**——先按字段的
  声明类型判断是不是 `Binding<T>`，是就调 `GenBindingValue` 包一个新
  对象再存（`GenBindingValue` 自己也只在"普通存储"时才返回 null）。
  坑出处：`tests/conformance/objinit_ctor_field_overwrite.zan`。
- 通用教训：**同一个语义只留一条实现路径**。初始化器与普通赋值是同一
  件事的两种写法，分开实现就会漏掉其中一种的转换规则。
## 84. 委托重载打平，多半是**参与打分的类型没推出来**

- 坑（2026-10-21 实测）：`items.OrderBy(x => x.Length)`（x 是 string）
  在自举里选中了 `StrKeySelector` 重载而不是 `KeySelector`（int 返回），
  委托按 string 返回而 lambda 返回 int，运行时崩（exit -1073741819）。
  宿主靠"lambda 体类型 == 委托返回类型"给候选加分
  （`src/compiler/irgen_expr_core.c`：`types_concrete_equal(bt,
  pt->delegate_ret_type)` → +2）。自举的 `ArgTypeNode` 对成员访问
  **只查类字段**，`x.Length` 的接收者是 string 这个内建类型 → 查不到
  → 返回类型推不出来 → 三个同名重载全部打平 → 按声明顺序选中第一个
  （`Enumerable.zan` 里 `OrderBy` 的第一个重载就是 `StrKeySelector`）。
- 做法：
  - `ArgTypeNode` 的成员访问按宿主 `src/compiler/builtin_api.c` 的属性表
    补内建成员：string.Length、List.Count、Dict.Count/Keys/Values、
    StringBuilder.Length，以及数组的 Length/Count。
  - 块体 lambda 的返回类型按宿主 `stmt_lambda_return_type` 收集：所有
    `return` 的类型必须一致（第一个说了算）；裸 `return;`、循环体、
    推不出的类型 → 未知。
  - 评分与宿主一致：**类型相同加分，类型不同不扣分**（未知与不匹配同分，
    按声明顺序决胜）。自举原来给"不匹配"扣分，于是"候选 A 未知 /
    候选 B 不匹配"时会选错。
- 通用教训：**"重载打平"往往不是重载规则的问题，是参与打分的那个类型
  没推出来**——先确认评分输入的类型是真的，再去调权重。宿主是规格：
  加分制不是扣分制。
## 85. 字符串转义解码只有一份实现（宿主在**词法期**解）

- 坑（2026-10-21 实测）：`"\u0001"` 在自举里解成了 "u0001" —— 自举的
  `DecodeStr` 只认 `\n \r \t \0`，其余转义原样落成字母。宿主在**词法期**
  就把转义解掉（`src/compiler/lexer.c` 的 `lexer_escape_seq`）：`\xNN`
  （1..4 位十六进制）与 `\uNNNN`（4 位）解成码点，再按 UTF-8 编码
  （`zan_utf8_encode`）；代理码点（D800..DFFF）不接受。
  表现：`Log.EscapeJson("x\u0001y").Contains("\u0001")` 在自举里恒为真
  → 断言反转 → `log_structured` 第 6 行输出 false（宿主 true）。
- 做法：转义解码只留一份实现（`ast.zan` 的 `StrEsc`），运行期字符串
  （`irgen`）、属性字面量（`parser`）、编译期元数据（`attrgen`）三处都
  走它。原生后端那套早就自己解了 `\x` `\u`（`ngen.zan` 的
  `DecodeLiteralNg`），这次是把 LLVM IR 后端补齐。
- 通用教训：**同一个语义只留一条实现路径**。转义解码曾经有三份
  （irgen / parser / attrgen），各写各的，改一处就漂。宿主在词法期解、
  自举在生成期解——位置可以不同，规则必须只有一份。
## 86. 泛型类的构造器要**按实例特化**（宿主 find_generic_ctor）

- 坑（2026-10-21 实测）：`class Bag<T> { List<T> src; Bag(...) { src = new List<T>(); } }`
  在自举里构造器是**所有实例共用的那一份**（`Bag_ctor__TextOf.T`），于是
  `new List<T>()` 的元素类型是擦除的 T → 列表建出来是
  `zlist_new(i64 0)`（**不拥有元素**）→ 元素没被 retain → lambda 读完
  字段就 release 掉 → 之后再读就是 use-after-free → 堆损坏
  （exit 0xC0000374，离崩溃点很远）。
  宿主按实例特化构造器（`src/compiler/irgen_expr.c` 的
  `find_generic_ctor`）：`Bag<Item>` 的构造器里 `new List<Item>()` 的
  列表带元素描述符、**拥有**元素。
- 做法：泛型类的 `new` 调用点按类实例特化构造器（符号带类型实参），
  构造器体用**代入后的** AST（`CloneSubst`）生成，接收者的静态类型带上
  具体类型实参（`curThisTy`/`curInstArgs`），字段读回来才是
  `List<Item>` 而不是 `List<T>`；调用点的实参也用具体形参类型。
- 通用教训：**"所有实例共用一份泛型代码"和"每个实例一份"是两种语义**，
  差别通常落在集合的**拥有关系**上（谁 retain 谁）。宿主选了每个实例
  一份，自举就得跟上，否则表现为远处的堆损坏。
## 87. 泛型类里的委托字段：签名要按接收者特化（宿主 subst_delegate_sig）

- 坑（2026-10-21 实测）：`class Bag<T> { TextOf<T> text; string TextAt(int i) { return text(src[i]); } }`
  在自举里 `text` 的类型是擦除的 `TextOf<T>` → 委托调用按
  `i8* (i8*, i64)*` 发出，把元素**当整数**传进去；而闭包
  （`__lambda_0`）的形参是 `%Item*` → 闭包把整数当指针解 → 崩。
  宿主在 `src/compiler/irgen_expr.c` 的 `subst_delegate_sig` 里按
  **接收者的类型实参**特化委托签名（`T` → `Item`）。
- 做法：委托调用发射前，按当前实例（`curInstCls`/`curInstArgs`）把委托
  类型的类型实参代入（`InstDelegateType`）。
- 通用教训：**擦除的槽位形状和真实的值形状要分开**——存储用擦除形，
  值的类型按实例还原；漏了还原就是"把整数当指针"。
## 88. 构造器重载：lambda 实参**只**匹配委托形参（宿主 find_ctor）

- 坑（2026-10-21 实测）：`new Bag<Item>(i => i.name)`（`Bag<T>` 有
  `Bag(List<string>)`、`Bag(TextOf<T>)`、`Bag(RowOf<T>)` 三个构造器），
  自举按"实参类型未知"把三个都打平 → 按声明顺序选中
  `Bag(List<string>)` → lambda 的形参没有类型 → 体里的 `i.name` 报
  "member 'name' of type 'int'"。
  宿主的 `find_ctor`（`src/compiler/irgen_expr_core.c:917`）把不匹配的
  候选**排除**（compatible=false）：lambda 实参只匹配委托形参，且委托的
  形参个数必须与 lambda 的相同。
- 做法：构造器候选筛选阶段加这条规则（`CtorTakesLambda`）。
- 通用教训：**"实参类型未知"不等于"实参适配任何形参"**——宿主在实参
  类型推不出来时按**形状**（是不是 lambda、委托的元数）先筛一遍。
## 89. 重载评分：形参提到类型参数时按"模类型参数"匹配

- 坑（2026-10-21 实测）：`new Bag<Item>(new Col<Item>("Name", Cells.NameOf))`
  （`Bag<T>` 有 `Bag(List<Col<T>>)`、`Bag(Col<T>)`、`Bag(RowOf<T>)` 三个
  构造器）在自举里三个候选都拿到同样的 +2（"形参提到了类型参数"就加分）
  → 打平 → 按声明顺序选中 `Bag(List<Col<T>>)` → 列数不对。
  宿主只在实参**模掉类型参数**后与形参同形时才加分
  （`src/compiler/irgen_expr_core.c` 的 `types_match_modulo_tp` → +3）：
  `Col<T>` 匹配 `Col<Item>`（+3），`List<Col<T>>` 与 `RowOf<T>` 都不匹配
  （+0）。
- 做法：`ScoreCand` 把"形参提到类型参数"这一支从"无条件 +2"改成
  "模类型参数匹配得上 +3，否则 0"（`TypesMatchModuloTP`）。
- 通用教训：**"提到了类型参数"不等于"匹配"**——泛型形参要比的是**去掉
  类型参数后的形状**（类名 + 层数 + 其余类型实参），不是"有没有提到"。
## 90. 方法组实参只匹配委托形参（宿主 arg_method_group）

- 坑（2026-10-21 实测）：`new Bag<Item>(Rows.Of)`（`Rows.Of` 是静态方法，
  当委托值用）在自举里选中了 `Bag(List<Col<T>>)` → 委托字段是空的 → 崩。
  宿主把方法组实参**排除**在非委托形参之外（`find_ctor` 的
  compatible=false 规则 + `method_group_score`：必须是委托形参、参数个数
  相同、返回类型同族）。
- 做法：构造器候选筛选加方法组规则（`CtorArgFits`），评分按
  `MethodGroupScore`（返回类型同族：同型 +2，不同族直接排除）。
- 通用教训：**"方法名不带括号"是一种实参形状**，要按形状（而不是按
  "类型未知"）去筛候选。
## 91. 命名空间限定的静态接收者也要走命名空间解析

- 坑（2026-10-21 实测）：`Widget.Tooltip.Flush(this)`（`namespace Gui;`
  里的 `Gui.Widget.Tooltip`）在自举里报 "unresolved identifier 'Widget'"。
  自举的 nsresolve 只对**单段**接收者（`Cls.Method`）做命名空间解析，
  限定接收者（`A.B.C`）只做**全名精确匹配**——"Widget.Tooltip" 不是
  任何声明的全名（"Gui.Widget.Tooltip" 才是），于是没被折叠成那个声明。
  宿主的 nsresolve 对限定名同样按"当前命名空间 → using → 全局"解析。
- 做法：限定接收者的折叠改用与静态接收者同一套解析
  （`ResolveStaticReceiver`），并加一条保护：链头的标识符若是局部变量
  （被遮蔽）就不折叠——那是字段访问，不是类型。
- 通用教训：**同一套名字解析规则要覆盖单段名和限定名**——只给一半用
  命名空间解析，缺口就会出现在"带点号的名字"上。
## 92. `base.M()` 是**直调**，绕过虚表

- 坑（2026-10-21 实测）：`base.Name()` 在自举里报 "call to 'Name' is not
  supported by the self-hosted compiler yet"——自举根本没处理 base 接收者。
  宿主把 `base.M()` 降成**直调**（`src/compiler/irgen_call.c` 的 base_recv →
  `emit_dispatch_call` 传 NULL 类，绕过虚表）：基类自己的函数体执行，不是
  虚函数分发。
- 做法：`EmitInstanceCall` 加 `direct` 开关（跳过虚表），`EmitBaseCall`
  把 `this` 按基类指针转换后直调基类方法。
- 通用教训：**"基类调用"不是"虚调用的另一种写法"**——它绕过虚表，语义
  与 `this.M()` 相反，实现时不能复用虚分发那条路。
## 93. async 泛型方法按实例特化（与同步泛型方法同一套）

- 坑（2026-10-21 实测）：`await s.Echo<int>(42)`（async 泛型方法）在自举里
  报 "not supported by the self-hosted compiler yet"——`CanSpecialize` 里
  一句 `if (IsAsync(m)) return false;` 把所有 async 方法挡在特化之外，
  调用点只能打到**擦除**的共享体（`async U Echo<U>(U x)` 的 `U` 是 i64 槽）。
  宿主对 async 方法一样按实例特化（与普通泛型方法同一条路）。
- 做法：放开 `CanSpecialize` 的 async 限制；克隆体的符号是克隆自己的名字
  （`Owner_M__SPint`），`GenAsyncMethod` 用 `MethodSym(cls.sval, clone)` 算
  出来正好等于 `SpecQueue` 交给调用点的那个符号——两边天然对齐；调用点的
  **awaited 结果类型**要用代入后的返回类型（`tt.a = SubstTP(m.type, ...)`）。
- 通用教训：**"特化"是按调用点的类型实参做的 monomorphization，与方法
  是不是 async 无关**；async 只是把"结果类型"藏在了协程帧里，别把它当成
  不能特化的理由。
## 94. 调用点的返回类型要代入类型实参（接收者实例化 + 显式类型实参）

- 坑（2026-10-21 实测）：`await ps.Get<int>(1)`（`Pool<string>` 的方法，
  返回 `T`）在自举里把结果打成了一串指针数字（`140698019280840`）而不是
  `hello`：await 临时量的类型来自 `AwaitCallTy` → 克隆方法**声明的**返回
  类型 `T`（擦除），没有代入。宿主在检查器里就代入了
  （`src/compiler/checker.c:2880`：`binder_subst_named(ret, tps, targs)`）。
- 做法：加 `SubstCallType`（接收者的具体实例化绑定**类**的类型参数，
  显式类型实参绑定**方法**的类型参数）+ `CallResultType`；`ArgTypeNode` 的
  call/await 分支与 `AwaitCallTy` 都走它。宿主还有一条：接收者的实例化
  决定成员类型（`Pool<string>.item` 是 `string` 不是 `T`）。
- 通用教训：**"声明的返回类型"和"这次调用的返回类型"是两回事**——凡是
  返回类型里出现类型参数的地方，都要问"这次调用把它代入成什么了"。
## 95. 方法签名含**类型参数个数**（泛型重载与普通重载并存）

- 坑（2026-10-21 实测）：`b.GetAsync<User>("k")` 与 `b.GetAsync("k")`
  指向同一对重载（`GetAsync(string)` 与 `GetAsync<T>(string)`），自举的
  候选去重按**参数类型标签**（`ParamTags`）判"同签名"，于是泛型那个被当
  成重复候选丢掉 → 显式类型实参选不到它。C# 的方法签名含类型参数的
  **元数**，`M<T>(string)` 与 `M(string)` 是两个签名。
- 做法：去重改用 `SigTags`（类型参数个数 + 参数类型标签）；显式类型实参
  另外按宿主的规则筛候选（`checker.c:1681`：候选的类型参数个数与调用点
  的类型实参个数不同的**不是候选**），`AwaitTarget` 也要同一条。
- 通用教训：**"重载"要按语言定义的签名判，不是按参数列表判**；参数
  列表相同但类型参数元数不同的两个方法，在 C# 里是两个重载。
## 96. `Task.Cancel` = 帧上的取消标志 + resume 时中止

- 坑（2026-10-21 实测）：自举对 `Task.Cancel(handle)` 报 "not supported"，
  根因是自举把任务句柄降级成**自己的扁平帧**（句柄就是帧指针），而帧头
  里没有取消标志这一格。宿主在协程帧头里有 `ASYNC_FRAME_CANCEL`，取消
  时置位（`src/compiler/irgen_async.c` 的 `get_co_cancel_fn`），帧在
  **下一次 resume** 时检查它并走完成路径（`emit_async_cancel_check`）。
- 做法：自举的帧头扩一格（槽 10 = cancel，帧体整体后移一格）；帧的 resume
  入口检查该标志，置位则**不跑函数体**，直接释放帧自己拥有的引用、标记
  完成、唤醒 awaiter（帧本身由 awaiter 回收，所以取消路径不能自己
  `__zan_co_frame_free`）。
- 通用教训：**"取消"是协作式的**——置标志 + 在恢复点检查，而不是立刻
  杀线程；标志放在**帧**上（不是全局表）才能随帧一起被复用与回收。
## 97. async 跨帧异常：longjmp 的目标帧可能已经死了（已知缺口）

- 坑（2026-10-21 实测）：`await s.Thrown()`（被 await 的 async 方法里
  `throw`）在自举里**堆损坏**（0xC0000374），宿主正常。原因：自举的
  异常传播是 `longjmp` 到**外层** try 的 setjmp 缓冲，而那个 setjmp 是在
  awaiter **上一次** resume 调用里下的——那次调用在挂起时已经返回，栈帧
  死了，longjmp 恢复到一段已复用的栈上。
- 宿主的两条机制（`src/compiler/irgen_async.c`）：异常**经帧传递**（子任务
  的异常存在帧里，awaiter 恢复后在自己的帧里重新抛出），且**每次 resume
  都重新挂载**处理器（帧的处理器栈 + resume 入口的 re-arm 段）。
- 通用教训：**协程里的 setjmp 目标必须是"还活着"的栈帧**——要么把异常
  变成帧上的数据（由恢复方在自己的栈里重抛），要么在每次恢复时重新
  下 setjmp；只把 jmp_buf 放进全局数组**不够**（缓冲活着，里面存的
  rsp/rip 仍然指向死栈）。
## 98. 取消要沿"等待关系"下传（自举与宿主的已知差异）

- 坑（2026-10-21 实测）：宿主的 `__zan_co_cancel` 不止置目标帧的
  `ASYNC_FRAME_CANCEL`，还沿帧头的 `ASYNC_FRAME_CHILD` 链把**后代帧**
  一起置位；自举的 `Task.Cancel` 只置目标帧的槽 10——父帧没有"当前正在
  等的那个子帧"这一格可循。
- 现状：自举的帧是"每个 await 点一个子帧槽"，父帧不保存"当前子帧"，
  因此取消无法沿链下传：被取消的任务若正卡在一次 `await` 上，要等它被
  恢复时才会看到标志。
- 通用教训：**"取消"在协程里是沿"等待关系"传播的**，只置目标帧等于只取消
  了"自己那一层"；要补齐得先有"当前子帧"指针（父帧记住正在等的子帧）。
## 99. reject 用例的"钉子"会随实现推进失效——要改钉，不是删用例

- 坑（2026-10-21 实测）：`tests/selfhost/reject_cases.txt` 里的
  `unsupported_builtin_call` 拿 `Task.Cancel` 当"自举还没实现的 C 宿主
  内建"的钉子；自举把 `Task.Cancel` 实现之后，这条用例反而**编译通过了**
  → 差分门判 "the self-hosted compiler accepted a program it must reject"。
- 做法：实现一个内建前先看它是不是别处的钉子；是的话把钉子**改钉**到
  另一个仍未实现的内建（同一个"仍不支持"诊断措辞），而不是删用例或放弃
  实现。钉子的作用是守住 fail-closed 边界，边界本身不能消失。
  顺带把"未支持的 `Task.X` 静态成员"做成一条干净的诊断分支（不要再往下
  走接收者求值——`Task` 是类型不是值，只会多出一条误导性的错误）。
- 通用教训：**测试用例钉住的是"边界"，不是"某个具体函数名"**——实现推进
  时边界要跟着搬家，否则会出现"实现变好 → 门变红"的反向激励。
## 100. struct 有隐式无参构造器（宿主对 struct 跳过构造器检查）

- 坑（2026-10-21 实测）：`new Matrix4x4()`（`Matrix4x4` 是 **struct**，只有
  一个 16 个 float 的构造器）在自举里报 "no constructor of 'Matrix4x4' takes 0
  argument(s); a class that declares constructors has no implicit parameterless
  one"，宿主却编译通过。根因：宿主检查器的构造器可用性检查第一句就是
  `if (type->kind != TYPE_CLASS || !type->sym) return;`
  （`src/compiler/checker.c: check_ctor_available`）——**struct 根本不做这个
  检查**，于是 `new Vector3("x")`（1 个 string 传给 3 个 float 的构造器）在
  宿主也合法，产物里就是一个全零的 struct。
- 做法：自举在 `new` 的降级里，当没有构造器匹配时，若目标是 **struct** 就不
  报错——按"隐式无参构造器"处理（实例已经分配好且已清零，字段初始化器照跑）。
  "class 声明了构造器就没有隐式无参构造器"这条规则只对 class 成立。
- 通用教训：**"隐式无参构造器"是 class 与 struct 的分水岭**——照抄别家的
  报错文案时，要先确认那条规则的对象是"类型"还是"类"。
## 101. 浮点字面量的 `f` 后缀决定类型（宿主 lit_suffix ? float : double）

- 坑（2026-10-21 实测）：`Vector4.Multiply(a, 2.0f)` 在自举里选中了
  `Multiply(Vector4, Vector4)` 重载，把 `2.0f` 当成一个 Vector4 传下去 →
  clang 报 "floating point constant invalid for type"。根因：自举的词法器把
  `f/F/m/M` 后缀**吃掉**了（只留在文本之外），于是 `2.0f` 与 `2.0` 在类型
  眼里都是 double；候选里只有 `Multiply(Vector4, float)` 的形参是 float，
  精确匹配拿不到分，两个候选打平 → 取第一个。
- 做法：词法器把浮点后缀记进 token（`litSuffix`：1=f/F，2=m/M），解析器放进
  节点的 `suffix`，**后端的实参定型**（`ArgTypeNode` → `FloatLitType`）按后缀
  给 "float" 或 "double"。
- 通用教训：**后缀不是装饰**——它决定字面量的类型，而类型决定重载选择；
  词法器"顺手吃掉"后缀时，后面所有按类型选候选的逻辑都会悄悄偏一格。
## 102. float 与 double 互相可隐式转换：重载评分不算"不匹配"

- 坑（2026-10-21 实测）：`Foo(double)` 与 `Foo(float)` 两个重载，传 `2.0f`
  时若按"名字不同即不匹配"扣分，float 那个重载会被判成不可用。
- 做法：`ScoreCand` 里 float/double 这一对不再扣分（只是拿不到精确匹配的
  加分）——宿主允许 float 实参进 double 形参（加宽），也允许反向。
- 通用教训：**"不匹配"和"不是精确匹配"是两件事**：能隐式转换的类型对不该
  被判出局，只是排序靠后。
## 103. `await` 有三种形态：协程帧 / 任务句柄 / 普通值透传

- 坑（2026-10-21 实测）：`await sqlite3_step(stmt)`（一个返回 int 的
  extern 调用）在自举里报 "'await' expects an async call in the self-hosted
  compiler -- this operand is not lowered as a coroutine frame"，宿主却编译
  通过。宿主的三种形态在 `src/compiler/irgen_expr.c: emit_expr_await_expr`：
  ① 被 await 的调用开了一个**协程帧** → 挂起等它完成；② 被 await 的是
  **任务句柄** → 轮询到完成再取结果；③ **其它任何值** → 函数的最后一句是
  `return sub`，值原样透传（等于"await 了但没等待什么"）。
  自举原来只认第 ① 种。
- 做法：`EmitAwait` 分流——不是协程帧时，Task 句柄走"泵调度器到 done 再读
  结果槽 6 并回收帧"（与 `t.Result` 同一套协议），其它类型直接透传。
- 通用教训：**"await"不是"必须等"**——它先是一种**类型规则**（Task<T> → T），
  再是一种**降级**（帧 / 句柄 / 透传）；只实现其中一种就会把合法程序判死。

## 104. 实例方法组要绑定接收者（`obj.Method` 当委托值）

- 坑（2026-10-21 实测）：`Action<int> f = probe.Probe;`（把实例方法当委托
  值传出去）在自举里报 "member 'Probe' of type 'Probe' is not supported"，宿主
  却编译通过。宿主的 `emit_method_group_closure`
  （`src/compiler/irgen_expr.c`）把方法组变成一条**带接收者的闭包记录**：
  `{fn, dtor, target}`——fn 是一个从 env 槽 0 取接收者的 thunk。
- 做法：自举生成 thunk `@__mg_<target>`：从 env 槽 0 读接收者，调目标方法，
  返回值按方法返回类型转换。ARC 类的接收者在 thunk 内部 zretain/zrelease
  （自举的闭包记录不会释放成员，所以释放责任落在 thunk 上）。
- 通用教训：**"方法组"不是一个方法指针**——它是一段"等接收者到齐才调用"的
  代码；只把函数地址塞进闭包、丢了接收者，调用时会拿随机值当 `this`。

## 105. `nint` 是关键字：词法、语法、类型名三处都要有

- 坑（2026-10-21 实测）：`(nint)-1` 在自举报 "unresolved identifier
  'nint'"，宿主编译通过。宿主有 `TK_NINT`（`src/compiler/lexer.c`），绑定器
  也认识 `nint` 这个类型名（`binder.c` 的 `type_nint`）；自举的绑定器早就能
  解析 `nint` 这个类型名，缺的是**词法与语法**把它当类型关键字。
- 做法：`token.zan` 加 `static int Nint()`；`lexer.zan` 的关键字表加
  `if (s == "nint") { return TK.Nint(); }`；`parser.zan` 的 `IsTypeKeyword`
  / `TypeKeywordName` 加上 Nint → "nint"（注意 `decimal` 在
  `TypeKeywordName` 里映射成 "double"，`nint` 映射成 "nint"）。
- 通用教训：**类型名认识 ≠ 类型能用**——词法、语法、绑定三张表要一起补；
  只补绑定器，最先炸的是"把它当表达式用的地方"（强制转换）。

## 106. 数组级优先于元素名：`List<string>[]` 的元素是 `List<string>`

- 坑（2026-10-21 实测）：`List<string>[] a; a[0].Add("x");` 在自举把
  `a[0]` 定型成 **string**（报 "string method 'Add' is not supported"），
  宿主正常。两处根因：① 检查器 `InferType` 的 `AK.Index` 分支先判
  `sval == "List"` 就返回类型实参（`kids[0]`），没先看数组级——`List<string>[]`
  的类型节点是 `sval="List"` + `kids=[string]` + `ival=1`，于是命中了 List
  特例。② 后端 `IsListType` / `IsDictType` 只看 `sval` 不看 `ival`，
  `List<string>[]` 被当成 List 走 `zlist_get`；`ArrayElemType` / `GenNew` 的
  数组分支还把类型实参（`kids`）丢了。
- 做法：**数组级（`ival`）先判**——`ival >= 1` 就是数组，元素类型是
  `sval` + `kids`（类型实参）+ `ival - 1`；`IsListType` / `IsDictType` /
  `IsSbType` 与 `IsStr` / `IsSpanType` 一样要先排除 `ival >= 1`；派生元素
  类型时把 `kids` 一起带走。
- 通用教训：**类型节点上的"名字"只是类型的一部分**——名字后面还挂着数组级、
  可空标志和类型实参；凡是"从类型 A 推出类型 B"的地方，只抄名字就会丢信息。

## 107. 裸 `throw;` 是重抛

- 坑（2026-10-21 实测）：`catch { ...; throw; }`（把捕获的异常原样再抛）
  在自举里被降成 `bitcast i64 0 to i8*`——clang 报 "invalid cast opcode for
  cast from 'i64' to 'ptr'"。自举原来只有 `throw <expr>`，裸 `throw;` 的
  表达式是 null，被当成"抛 0"处理。
- 做法：后端在 catch 子句体里把当时的 `@__exc_val` / `@__exc_tid` 存进
  自己的槽（每个子句一份），裸 `throw;` 重新写回这两个全局再 `EmitRaise()`；
  检查器加 `catchDepth`，在 catch 块之外用裸 `throw;` 报宿主同款诊断
  "a rethrow (`throw;`) is only valid inside a catch block"。
- 通用教训：**"重抛"不是"抛 null"**——它重抛的是**在飞的那一个异常**；
  子句体里可能又跑过嵌套 try（在飞的全局已被覆盖），所以要按子句各存一份。

## 108. `await` 的隐藏内建：`Socket.ResolveSockAddr` / `Socket.ResolveAsync`

- 坑（2026-10-21 实测）：标准库的 `Socket.BuildSockAddrAsync` 里
  `await Socket.ResolveSockAddr(ip, port, addr, 28)` 在自举里编译出对
  未定义函数 `@Socket_ResolveSockAddr` 的调用；`Socket.ResolveAsync` 同理。
  这两个不是普通方法：宿主在 `emit_expr_await_expr` 里把它们**识别成
  编译器内建**（`src/compiler/irgen_expr.c`），降级成
  `zan_io_resolve_sa_co` / `zan_io_resolve_co` 的挂起调用；标准库自己写的
  `public static async int ResolveAsync(string hostname) { return await
  Socket.ResolveAsync(hostname); }` 就是靠这条内建才不递归。
- 做法：自举的 `IsSocketIntrinsic` 加上这两个名字，`EmitSocketIntrinsic`
  按运行时契约发调用（`zan_io_resolve_sa_co(name, port, buf, cap, frame,
  resume, out)`，结果长度写在帧的结果槽 6 上，是个 **i32**），并在
  `EmitAsyncRuntime` 里补上对应的 `declare`。
- 通用教训：**"标准库自己调用的方法"往往不是普通方法**——宿主可能把它当
  编译器内建；自举补齐内建时，要按运行时的**真实契约**（参数顺序、结果
  槽位宽度）来，不能照抄别处内建的形状。
## 109. 插值的格式说明符要走 printf 语义（`x.ToString("D4")`）

- 坑（2026-10-21 实测）：`$"{v:D4}"` 在自举里报 "call to 'v.ToString' is
  not supported by the self-hosted compiler yet"——自举的词法器把插值洞
  `{v:D4}` 拼成 `v.ToString("D4")`，而 irgen 后端只认**无参**的
  `ToString`。宿主则把说明符映射成 printf 模式
  （`src/compiler/irgen_expr.c: interp_format_to_printf`）：D/X 要位数
  （位数 ≤ 0 或 > 512 → 不套格式）、F/E 把位数夹到 512、G 是"有效位数"、
  `0`/`#` 是自定义模式（点前 0 的个数 = 整数补零，点后 0/# 的个数 = 小数位）。
- 做法：irgen 后端补 `FormatSpecToPrintf`（与 ngen 后端同名函数同规则）+
  `SpecWantsDouble` + `EmitScalarToStringFmt`（snprintf 两段式：先问长度，
  再写进 `zstr_new` 的缓冲）；`ToString(<字符串字面量>)` 在标量接收者上
  走这条路。**注意 `x` 小写要映射成 `llx`**（宿主按大小写给 `llX`/`llx`），
  ngen 后端写死 `llX` 是错的。
- 通用教训：**"格式化"不是一个字符串，是一个语义表**——同一个洞里的
  `D4`/`X2`/`F1`/`G3`/`E2`/`0.00` 走的是六条不同的规则；照抄"看起来像"的
  映射表会在大小写、位数边界上出错。

## 110. 插值里的 `{{` / `}}` 是转义花括号

- 坑（2026-10-21 实测）：`$"{{{n}}}"` 在自举里报 "syntax error: unexpected
  ':'"——自举的词法器（`lexer.zan` 的 `InterpStr`）见到 `{` 就开一个洞，
  不认 `{{` 是转义的左花括号，于是把 `{n` 当成洞的内容。
  宿主在 `lexer_interp_string_segment` 里明确：`{{` 是一个字面 `{`，
  `}}` 是一个字面 `}`，单个 `}` 是字面 `}`。
- 做法：自举的 `InterpStr` 在文本段里遇到 `{{` / `}}` 时，把前面的文本
  切一段、补一个字面花括号段，再继续。
- 通用教训：**"分隔符"要先问有没有转义形式**——C# 的插值里 `{` 有两种
  含义（洞的开始 / 字面花括号），词法器不先处理转义，后面的语法分析
  看到的就是完全不同的东西。

## 111. 自举的插值是 `+` 链，bool/nullable 空洞因此对不上（已知缺口）

- 坑（2026-10-21 实测）：`$"{flag}"`（bool）在宿主输出 "true"，在自举输出
  "1"；`$"{u}"`（`ulong?`）在宿主输出载荷或空串，在自举直接生成坏 IR
  （把可空槽当 i64 喂给 snprintf）。根因：自举的词法器把插值**降成
  `+` 拼接链**，于是空洞和普通拼接操作数无法区分——而宿主的
  `emit_expr_string_interp` 是逐段判断的（bool → "true"/"false"，
  char → 字符本身，可空 → 载荷或 ""，其余按类型）。
- 做法（未做）：自举要有独立的插值节点（宿主是 `AST_STRING_INTERP`），
  词法/语法/检查器/两个后端都要认它；检查器还得跟宿主一样拒绝插值类
  实例（"cannot interpolate 'Box' -- convert the operand explicitly
  instead of formatting the reference"）。
- 通用教训：**"降级成更简单的表达式"会丢掉语义**——`+` 链能表达拼接，
  表达不了"这一段按 bool 渲染"；凡是宿主有专门节点的地方，自举抄成
  通用表达式就会在边角上分叉。

## 112. `%E` 的指数位数是 C 运行时的差异，不是编译器的差异

- 坑（2026-10-21 实测）：`$"{f:E2}"` 在宿主输出 "3.14E+000"（指数三位），
  在自举产物里输出 "3.14E+00"（指数两位）。两边发射的格式串都是
  `%.2E`，差别在**链接时的 C 运行时**：宿主 zanc 的链接行是
  `-lmingw32 -lgcc -lmoldname -lmingwex -lmsvcrt`（`src/compiler/main.c`），
  产物走 `msvcrt.dll`（老 CRT，指数至少三位）；用 VS clang 链出来的产物
  走 UCRT（指数至少两位）。
- 做法：这是**链接层面**的差异，编译器两边一致；对拍要么让两侧用同一个
  C 运行时，要么把它记成环境差异（差分门的 84 个用例都不含 `%E`）。
- 通用教训：**"逐字一致"要区分"编译器的语义"和"工具链的环境"**——
  printf 家族的细节（指数位数、`%g` 的尾零）由 C 运行时决定，链接器换了
  运行时，输出就变；先确认差异出在哪一层，再决定改哪边。
## 113. extern（DllImport）实参要传原始缓冲：byte[] 不是字符串

- 坑（2026-10-21 实测）：自举编译的程序一碰 SQLite 就崩（0xC0000005），
  而 stdout 因为块缓冲全丢，看起来像"跑到退出时才崩"——实际是崩在
  `SqliteConnection.Open` 里。根因：自举把 `byte[]` 实参**复制成一个
  新的托管字符串**（`zstr_new` + 逐字节拷贝）再交给 C，宿主却是
  **原样传指针**（`src/compiler/irgen_arc.c: coerce_args_to_params`
  只对指针类型做 bitcast，从不复制）。于是
  `sqlite3_open(filename, ppDb)` 把 8 字节的连接句柄写进了那个临时
  字符串的载荷区（压在字符串头上），程序自己的缓冲区仍然是空的，
  字符串头也被写坏——后面一释放就崩。
- 做法：自举的 `CallArg` 加 `rawStr` 开关，extern 被调方**不做
  byte[]→string 打包**，直接传数组的数据指针；`Coerce` 补上通用的
  指针→指针 bitcast（数组是 i64*，C 的 `char*` 是 i8*）。自举的
  `StrSlotConv`（赋值/返回时的打包）保持不变——那是托管语义。
- 剩余差异（已知）：宿主把 byte[] **别名**成字符串（同一个指针，
  `strbuf` 认得数组头，长度取元素个数），自举是**复制**一份；所以
  "赋值之后再改数组"在宿主会看到新值（`after: ZBC`），在自举还是旧值
  （`after: ABC`）。要对齐得让自举的字符串运行时支持"数组视图"。
- 通用教训：**托管侧的"隐式转换"到了 ABI 边界必须退回零拷贝指针**。
  byte[]→string 在托管语言里通常是拷贝，但 C 的出参缓冲区是**给 C
  写的**（句柄、长度、指针都往里塞），一复制就等于把结果写进临时
  对象。判据：实参那块内存**会不会被 C 侧写**——会，就不能复制。
## 114. 宿主把 `Math.PI` 和 `Cpu.*` 当内建：自举照抄，连宿主的"恒 0"一起照抄

- 坑（2026-10-21 实测）：`packages/Zan.Gui` 里的 `Math.PI` 和
  `stdlib` 里的 `Cpu.HasSse42` 让自举**直接拒绝编译**两个用例
  （"Math is a built-in type and cannot be used as a value" +
  "unresolved identifier 'Cpu'"）。宿主这边：
  - `Math.PI` 被折成**编译期 double 常量**（`src/compiler/irgen_expr.c:6783`，
    3.14159265358979323846），压根不走成员查找；
  - `Cpu.HasXxx` / `Cpu.CpuFeature(id)` 被降成编译器内部的
    `__zan_cpu_feature()`（`irgen_expr.c:1488` 调用、`irgen_expr.c:6651`
    属性），**完全绕开 stdlib 的 `System.Runtime.Intrinsics.Cpu` 类**。
- 关键事实：原生构建时 `g->target_triple` 是**空串**（只有 `--target`
  交叉编译才填，`src/compiler/irgen.c:1568`），而 `cpu_feature_fn` 用
  `strstr(triple, "x86")` 判架构——空串既不是 x86 也不是 arm → 走
  "其它平台"回退 → 发射出来的 IR 就是
  `define internal i32 @__zan_cpu_feature(i32) { ret i32 0 }`。
  于是宿主在**任何机器上**都报"没有 popcnt / sse42 / avx2"，
  `Cpu.CpuFeature(n)` 恒为 0。同一文件里的 `emit_target_is_x86`
  反而正确处理了空串——两个函数对空串的判断不一致，宿主选了保守的那个。
- 做法：自举照抄这个行为——`Math.PI` 折成同一个 double（打印出来是
  `3.141592653589793`），`Cpu.HasXxx` 折成 `false`，
  `Cpu.CpuFeature(n)` 折成 `0`。**不要**去问真正的 CPU：自举若走 stdlib 的
  `zan_cpu_feature` 会拿到真实值（1），和宿主对不上。
- 通用教训：**"宿主是规格"包括宿主的 bug**。自举对齐的目标是"两边输出
  逐字相同"，不是"两边都正确"。遇到宿主明显不合常理的行为（空 triple →
  恒 0），先确认它确实是当前宿主的行为，再照抄，并在注释和 skill 里
  写明"这是宿主的保守回退"，免得后人当成自举的缺陷去"修"。
