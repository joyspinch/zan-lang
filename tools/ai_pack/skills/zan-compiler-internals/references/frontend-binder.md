# 编译器前端解析、绑定与语义检查契约

## 1. 扩展方法候选筛选与重载评分

- **接收者兼容性双向同步**：
  扩展方法的接收者兼容性判定必须同时应用于候选筛选（Filtering）与重载评分（Scoring）。当具体类实现接口、派生接口继承基础接口时，非泛型接口扩展方法应当能够被调用。
- **评分优先级准则**：
  ① 精确类型匹配（Exact Type Match）优于接口兼容匹配；
  ② 接口兼容匹配优于未受约束的裸类型参数（Type Parameter）。
- **泛型接口实参区分**：
  单纯通过符号层次遍历 `implements` 无法区分泛型实参（例如 `I<int>` 与 `I<string>`），必须保留完整的泛型类型实例化匹配路径，防止误匹配或漏匹配。

## 2. 语法解析器（Parser）分派契约

- **变量声明探测（`looks_like_var_decl`）**：
  在解析歧义位置（如泛型调用 vs 比较运算、局部变量声明 vs 表达式）时，前瞻分派必须严格核验类型指示符与后续标识符。
- **类型位置的节点分类**：
  语法树中位于类型位置（如字段类型、返回类型、泛型实参、局部变量类型）的标识符是 `AST_TYPE_REF`，而非通用的表达式标识符 `AST_IDENTIFIER`。后续 Binder 和 Checker 遍历时必须区分节点语义，严禁将类型引用降级为变量查找。

## 3. 调用形状与静态性判断

- **接收者槽位与静态性强相关**：
  在发射调用指令时，调用形状的接收者参数槽必须严格根据目标方法是否为静态方法来确定。实例方法必须提供有效的 `this` 接收者指针；静态方法和纯函数不得占用接收者槽位。
- **属性访问静态与实例区分**：
  访问属性时，若属性声明为 static，禁止通过实例接收者直接读写；反之实例属性必须绑定至有效实例对象，防止语义层绕过导致 IRGen 阶段解引用空接收者指针。

## 4. 闭包捕获与控制流分析

- **捕获变量而不是捕获值快照**：
  Lambda 表达式和局部函数捕获外部局部变量时，捕获的是变量的存储单元（Cell），以便在闭包内外实现共享修改。对闭包对象的保活必须遵循引用计数协议。
- **可空分析穿透复合控制流**：
  在进行可空性检查（Nullability Check）和空值守卫扫描时，静态分析必须穿透 `lock` 等同步块语句，同时核验锁体内外表达式与返回语句，避免误报合法的空值守卫逻辑。
## 5. 前端模糊测试的 harness 纪律

- **harness 必须和被测程序一样预留大栈**：编译 fuzz harness 时带上与 `zanc`
  一致的栈预留（Windows MSVC 目标：`-Xlinker /STACK:33554432`；GNU ld：
  `-Wl,--stack,33554432`）。
  - 坑出处：不带时 harness 用的是进程默认 1MB 栈，**约 50 层括号嵌套就爆栈**，
    1560 个变异体里 23 个报 stack-overflow，看着像编译器有内存缺陷；实际上解析器
    有 `ZAN_PARSER_MAX_EXPR_DEPTH=512` 守卫（`src/compiler/parser.c:1573`），
    真 `zanc` 上同样的输入报的是 "expression nesting too deep (max 512)"。
  - 判定：肇事输入原样喂给真 `zanc`——真程序给诊断而 harness 崩，是 harness 的
    栈配置问题；真 `zanc` 也崩才是产品缺陷。
- **先拿 artifact 再下结论**：libFuzzer 把肇事输入写在 `-artifact_prefix` 指定的
  目录；先拿那个文件单点复现，再判断是产品缺陷还是 harness 假阳性。
- **Windows 下 ASan 是动态库**：`clang_rt.asan_dynamic-<arch>.dll` 必须与宿主架构
  一致（x64 机器拷 aarch64 的会得到 0xC0000135 DLL 找不到），并放在 harness
  可执行文件旁边或加入 PATH。
- 现成入口：`tests/fuzz/fuzz_frontend.c`（libFuzzer 入口，覆盖
  lexer/parser/nsresolve/binder/checker）+ `scripts/fuzz_frontend.ps1`
  （编译 + 播种语料 + 跑 + 收集 artifact）。