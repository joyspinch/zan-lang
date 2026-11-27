# 运行时数据布局、ARC 与发布链接契约

## 三、运行时数据布局与内存所有权（Runtime / ARC）

27. **Dict 内建内存布局、保序约束与整数键自适应哈希**：
   - 动作：Dict 的 keys/values 并行缓冲区必须严格保持插入顺序；Remove 整体前移条目并就地修索引（墓碑 + 下标减一），墓碑到负载上限才整表重建。索引 = `ctrl[cap+16] | slots[cap] | tomb | plim`（`builtin_dict.c`）。整数类键（`ZAN_DICT_KEY_INT`）的索引先用**恒等定位**（h1 = 键），`plim` 记任一插入的最大探测组序号，查找探过 `plim` 组即判缺失；某次插入超过 `ZAN_DICT_IDENT_MAX_PROBE` 组就整表改用种子哈希重建（`plim = ZAN_DICT_PLIM_SEEDED`，扩容继承）。哈希种类永远以索引的 `plim` 为准，调用点传的种类只定新索引起始模式；`__zan_dict_key_eq` 只认种类 1 为字符串。
   - 理由：枚举顺序与析构顺序依赖插入序。种子哈希把顺序整数键打散成随机访存，`Dict<int,int>` 200 万键 + 1000 万查找比 `std::unordered_map`（恒等哈希、顺序流式访存）慢 4–6×——恒等实验证明表结构本身只差 1.3×，差距全在定位策略。纯恒等在 2 的幂表上会被步长 2^k / 打包键打成长链、缺失查找扫整段稠密跑道，所以必须带 plim 截断 + 探测超限回退，HashDoS 下每次操作代价仍有常数上界（`case_dict_int_identity_fallback`）。
   - 边界：`d[k] = v` 只调一次 `__zan_dict_set`（命中返回下标、追加返回 ~下标），不要在外面再 find 判命中；显式 `out` 的 TryGetValue 未命中写 default，裸标识符旧写法未命中保留原值（`case_tryget_pinyin`）。
28. **FFI 外部指针借用与内建所有权白名单**：
   - 动作：从 `DllImport` 外部返回的字符串是借用指针，运行时不释放、不下标；内建 API 若返回借用或托管字符串必须显式在借用白名单中登记。
   - 理由：防止跨 FFI 边界的野指针析构或托管堆内存泄漏。
29. **临时所有权实参自动回收（Lookup Builtins）**：
   - 动作：在集合查找类内建（如 `Contains`, `IndexOf`, `Dict.Remove`, `d[key]`）计算完成后，必须调用 `emit_release_owned_call_temp` 释放内联求值的临时对象。
   - 理由：防止查找表达式内部求值的临时字符串或对象在每次执行时泄露。
30. **异常展开栈与实参临时平衡注册（EH Unwind）**：
   - 动作：调用函数时若求值产生托管临时对象，必须在压栈前注册至每线程 unwind 栈；调用返回后 pop 次数必须与 push 次数严格相等。
   - 理由：保证被调方发生 setjmp/longjmp 展开时临时对象被完整释放，且展开栈深度平衡。
31. **委托双形态与闭包捕获保活**：
   - 动作：区分纯函数指针与携带上下文环境的闭包（Tagged Closure）；闭包跨调用帧逃逸或传递至异步线程时必须调用 ARC retain，并在执行入口校验 `ZAN_CLOSURE_TAG`。
   - 理由：确保被捕获的上下文变量在异步或脱钩执行期间生命周期完整。
32. **协程帧只许被 reap 一次（帧所有权契约）**：
   - 动作：给帧设置"自 awaiter"（`ASYNC_FRAME_AWAITER = 自身`、`AWAITER_STEP = __zan_co_reap`，即"任务完成即释放"）之前，必须确认没有第二条路径会再释放同一帧；带结果（keep_result）的帧不设自 awaiter，由用户侧 `reap` 释放，**两类帧的所有权契约不可混用**。
   - 理由：运行时 `__zan_co_frame_free` 的幂等守卫读帧首 8 字节的 `CO_DEAD` 位，但小对象分配器回收时把用户块首 8 字节写成 freelist 指针，守卫读到的已是垃圾 → 二次释放直接 double free（进程 abort）。实例：lambda 形态的 `Task.Run` 走 action 帧并自 reap，用户再读 `.Result`/`.Wait()` 又 reap 一次（该缺陷已按类型侧收口闭账，运行时仍缺幂等兜底，见 TASKS.md A380）。
   - 边界：运行时的守卫位不要放在分配器会回写的槽位（帧首 8 字节）；若要让 reap 幂等，标记必须落在分配器不覆盖的字段上。
33. **释放路径上的"已释放"标记不能放在分配器会回写的槽位**：
   - 动作：给任何"释放后仍要能被识别"的对象加幂等标记时，标记位必须放在**分配器不会覆盖**的字段上；小对象分配器回收时把用户块首 8 字节写成 freelist 指针（`src/runtime/rt_mem.c`），块首的任何标记在 free 之后立刻变成垃圾。
   - 理由：把守卫放在块首 → 二次释放读到的不是"已释放"而是 freelist 指针 → 幂等判断失效 → double free abort；`__wrap_free` 自己的双释放检测（块头 magic）才是真正兜底，它一命中就 fatal。
   - 边界：真要让 reap 幂等，用**注册表仲裁**（活协程表 `zan_co_live_del` 返回"本次是否真的摘掉了这个帧"），别指望对象里的标记位。
34. **同一块内存有两条释放路径时，先定单一所有者，再谈幂等**：
   - 动作：动手改帧/缓冲区的释放协议前，先把"谁拥有它、什么时候交棒"写在纸上：完成路径与用户路径同时存在时，必须选一个做唯一 owner，另一个改成只观测（等状态、不释放）。把"两边都释放但加了守卫"当解决方案，等于把 double free 换成偶发 UAF。
   - 理由：`Task.Run(lambda)` 的 action 帧此前"完成路径 reap 一次 + 用户侧 reap 一次"，靠时序侥幸没炸；真正的收口是让类型区分两种帧（无结果 action 帧 vs 带结果 keep_result 帧），使误用在编译期被拒。
35. **集合元素槽位按「字」而非「个」计**：
   - 动作：`List<T>` 的元素槽步长是 `elem_slot_words(T)` 个 8 字节字（值结构体 >1 字，
     指针/整型/浮点为 1 字）；任何走槽下标的路径（索引读、Add/Insert/RemoveAt、
     AddRange、ToArray、Dict.Keys/Values、数组元素赋值）都必须用
     `slot_word_index(idx, words)` 换算，缓冲区字节数用 `cap * 8 * words`。
   - 理由：按「每元素 1 字」硬编码的路径在结构体元素上会只搬第一个字，
     读出来是全零（`List<Vector3>.AddRange` 的元素全 0），且缓冲区会越界写。
   - 边界：`elem_slot_words` 由结构体的 LLVM 布局算出；布局算不出来时
     （`llvm_scalar_size` 为 0）必须报错拒绝，而不是悄悄退化成 1 字。
36. **静态（immortal）对象与堆对象共用头部布局时，哨兵判定必须覆盖每一条 ARC 路径**：
   - 动作：字符串字面量等编译期常量不是堆对象，它们的引用计数用哨兵值
     `ZAN_STRING_SENTINEL_RC`（不参与计数）。凡是会读写计数头
     （2026-11-25 起：`ZAN_RC_OFF = -8` 的 **u32**，immortal = `ZAN_RC_IMMORTAL`
     0xffffffff；静态串前缀是 `ZAN_STR_LIT_W0/W1`）的 ARC
     路径——通用 retain / 通用 release、字符串 retain、动态释放——都必须
     **先判哨兵再动计数**。静态对象在只读段，对它做原子加一/减一就是访问冲突。
   - 理由：判一半就会崩。字符串 retain 与动态释放都判了哨兵，唯独通用
     retain/release 漏了；而"字面量存进 `object` 槽"走的正是通用 retain
     （槽的静态类型是 object 不是 string），于是对只读段做原子加一 → 0xC0000005。
     实例：`case_ternary_is_type` / `case_cast_string_object` /
     `case_cs_b05_pattern_var` 三条同时崩，根因同一（见 TASKS.md A412）。
   - 排查手法：访问冲突先看**写入目标地址落在哪个映像段**——写 exe 的只读段
     = 在改静态对象；再看寄存器里的对象指针是不是"目标地址 + 16"（那就是 ARC
     计数头）。崩溃现场在 `<exe_dir>/zan_crash.log`（含 fault/rip/rax/raw stack），
     先读它再决定要不要上调试器；最小探针二分（`object o = "字面量"` vs
     `string s = "字面量"`）通常一步就切到根因。

**ARC 头：对象头 8 字节、RC 是 [p-8] 的 u32，字符串头 12 字节——绝不碰串的 [p-16]**（实证，2026-11-25，设计 `docs/OBJECT_LAYOUT.md`）：
- 布局（`src/common/zan_abi.h`）：对象 `[-8]` u32 rc | `[-4]` u32 tw（bit31 栈提升、
  bit30 对象标记、bit29 前垫 8 字节、低 29 位站点号）；字符串 `[-12]` u32 len |
  `[-8]` u32 rc | `[-4]` "ZANS"。[-8] 的 u64 头字：对象 `(tw<<32)|rc`、串
  `(ZANS<<32)|rc`，"高 32 位 == ZANS 即串""头字 < 0 即栈对象"照旧。数组不变。
- 描述符不再在头里：默认模式 `zan_hdr_desc` 按站点号查 `__zan_desc_tbl`（收尾定型，
  须早于 `refl_compact_member_tables`，否则只经表引用的反射记录被当死记录清零——
  GetType().Name 变空）；C 运行时经 `zan_rt_register_desc_table` 查。读头字取站点号
  一律用 `zan_hdr_site` / `ZAN_HDR_SITE` 且先看对象标记（数组头字里可能是描述符指针）。
- 对齐 16 的类（decimal/i128/SIMD 字段）：站点实参带 `ZAN_OBJ_TW_PAD16`，块前垫 8 字节
  让 p 16 对齐，`__zan_arc_free` 按这一位传 p-16；漏了它 decimal 字段落在 8 mod 16 上。
- 串长度读写走 `zan_str_len_ptr`（[-12] 的 u32），`emit_string_len_set` 只写 [-12] 与
  [-4] 标签、不碰 rc；旧式整写 `[-8] = ZANS<<32|len` 会把 rc 改成长度。
- 收益：两指针节点 40 → 24 字节，binary-trees 43 → 28 MB（C 18 MB）。
- 链了 rt_mem（`g->str_pages`，Apple/ELF/Windows 原生且非 `--emit-lib`）时串经
  `zan_mem_str_malloc` 进 8 字节粒度"串页"：块真起点在 4 mod 8，交出的是虚基址
  V = 用户指针 - 16（释放照旧传 p-16）。**V[0..3] 是相邻串的尾巴**：任何对串写
  `[-16]`（旧式 `store i64 1` 初始化 rc、i64 原子加减）都会悄悄改坏邻居的最后
  4 字节——表现为别的串尾字符变了，不崩。新增串分配/RC 路径一律用 `zan_rc_ptr`
  （i32），C 侧用 `ZAN_RC_PTR`/`ZAN_RC_LOAD`。
- 收益：`"item"+i` 一千万个串 392 MiB → 315 MiB（每串 32 → 24 字节）。钉子用例
  `tests/conformance/string_header_pages.zan`。
- 自举后端自有串/对象 ABI（串头 32 字节、对象 rc i64 在 -16、delegate 钩子强覆盖），
  不受影响；改 C 运行时读写 RC 的代码时别把它当成同一 ABI。
- rt_mem 分配器性能坑：空闲链空时曾每次都进慢路径做 8 次远端条带原子交换
  （全新分配为主的程序每次分配白付）；现快路径先切当前页、慢路径先读后换。
  `zan_mem_cache_get` 拆成内联 TLS 快路径 + noinline 慢路径——它被多处内联
  后编译器会把整函数外提，分配热路径多一次调用。

**串助手一律以串头长度为界，新串必须记长度**（实证，2026-11-25）：
strops（Split/IndexOf/Replace）0.13 → 0.07 s（C++ 0.07），numfmt 0.08 → 0.05 s。病根：
- 产串助手（`__zan_list_push_strn` 即 Split 的每段、Replace、Trim、ToUpper/ToLower）
  只补 `\0` 不调 `emit_string_len_set`：头里是"未测量"，此后每个 `.Length` /
  `IndexOf` 都 strlen 一遍再盖戳。新增任何产串路径：分配 → 拷贝 → `\0` →
  **`emit_string_len_set`**，缺一步就是隐性 O(n)。
- Split/Replace 曾走 strlen + strstr，单字符实参（`Split(',')`）先物化成堆上 1 字节串。
  现在 `builtin_str_scan.c`：调用点用 `emit_string_length` 取长度传进助手，查找走
  `__zan_mem_find`（`__zan_memchr` 首字节 + memcmp 余下，支持串内 `\0`、无需 memmem），
  `__zan_memchr` 跨度 < 32 字节逐字节扫（libc memchr 调用 + SIMD 起步在短字段上更贵）。
  字符**字面量**实参在 `emit_string_like_arg` 直接取不朽串字面量（owned=2，不释放）。
- `.Length` 的 `__zan_str_len_arr` 拆成小入口（空/页首/缓存命中）+ noinline `.slow`，
  LLVM 才会把命中路径内联——整块助手太大，热循环里每次 `.Length` 一次调用。
- `int.Parse`/`Convert.ToInt*` 的 strtoll 走 locale（numfmt 45% 采样）：`__zan_strtoll10`
  吃掉"可选 '-' + ≤18 位"，其余（空白、'+'、≥19 位、非数字）原样交 strtoll，endp/errno 契约不变。
- `zan_rt_str_release` 曾无条件对 `__zan_live` 原子减（加一只在 `--check-leaks`）；
  rt_mem `free` 的块边界校验 `off % sz` 是一条 udiv（改 Lemire 整除判定表
  `k_class_divm`），`zan_mem_fatal_block` 内联进 free 带来栈保护帧（改 noinline cold）。
- 钉子用例 `tests/conformance/string_scan_fastpaths.zan`（多字节/重叠分隔符、UTF-8 字符
  分隔符、短/长区间两条扫描路、等长/变长替换、解析快慢路径边界）。

**分配密集代码的四道税：S-UNWIND 最小不动点、实参后于分配、构造体旧值读、48 字节类**（实证，2026-11-24）：
binary-trees（`new Node(Make(d-1), Make(d-1))`）1.86 s → 0.9 s（C malloc/free 1.1 s），
53 → 45 MiB。逐层病根：
- **收缩 pass 取了最小不动点**：被调者全部已证 nothrow 才记 nothrow，递归函数
  （Make 调 Make）永远停在 UNKNOWN，invoke、landing pad 与 EH 临时栈簿记全保留。
  改为最大不动点（先记 nothrow，只沿调用边传播"可抛"）。同时三处假"可抛"源：
  `mrs` 内联汇编（读 TSD）被当成未知间接调用；`pthread_*`/`getenv` 不在 native 名表；
  释放链（`zan_rt_release(_dyn)`、类/集合/数组释放、`__zan_arc_free`、经描述符/闭包
  头的析构间接调用）没标 nounwind——析构器被 E1016 拒收，释放链不跑用户代码，
  现由 `zan_mark_nounwind` 统一挂属性，证得 nothrow 的函数也挂 nounwind。
- **`new T(args)` 曾先分配再求实参**：新对象要进 EH 临时栈，实参调用又"看得见"它，
  内联构造体 `this.l = a` 的 `old = load` 折不成 null，`release_dyn(old)` 卡住
  retain(a)/release(临时量) 配对。现实参先求值（C# 同序；`emit_new_storage`），
  分配点索引仍在原位置预留（`emit_new_site`，站点编号不漂）。
- **GVN 折不掉新对象字段旧值**：活对象注册表调用与返回值捕获了指针，AA 认为任何
  调用都可能写它。晚期 ARC 收尾补了块内精确折叠 `late_fold_fresh_loads`（只跨
  不重叠的同对象 store、对分配前已存在值的 retain、不写内存的指令）。同处：
  `late_drop_tmp_pairs` 迭代到不动点（嵌套临时量一趟只配最内层）；nounwind 且无
  invoke/landingpad/tmp_unwind 的函数整体删 push/pop（`late_drop_dead_tmp`，跨块）；
  窗口允许"存 x 本身"（所有权转移）；`retain/release(null)` 直接删。
- **对象 40 字节进 48 字节类**：rt_mem 加 8 字节粒度"对象页"类（24..120 的奇数倍 8），
  只经 `zan_mem_obj_malloc`（`g->str_pages` 时 zan_rt_alloc 调它）。判据靠 LLVM 不变式：
  对齐 16 的类型尺寸必是 16 的倍数，`n % 16 == 8` 的对象天然只要 8 对齐。
- 内存地板（2026-11-25 已降）：8 字节头（u32 rc + u32 站点号）+ 仅多态树才有的 vptr 槽
  + 载荷，两指针节点 24 字节 vs C 16 字节（见上文"ARC 头"条）。
- 顺带修了 `zan_rt_defer_push` IR 弱回退把 (fn, obj) 调反成 obj(fn) 的潜伏崩溃。
- 钉子用例 `tests/conformance/arc_new_args_first.zan`（实参/构造体抛异常的释放）、
  `tests/runtime/rt_mem_apple_zone_test.c`（对象页）。排查手法：`ZAN_DUMP_LATE_ARC_IR`
  看晚期收尾前的 IR，`ZAN_LATE_ARC_STATS=1` 看配对数；分配器本身的份额用
  "C 版基准改链 zanrt_mem.o"单独量（0.68 s，比 libc 快一倍——瓶颈不在分配器）。

**弱字段存储不接管所有权——右值是 +1 临时时存储后必须释放**（实证，2026-11-02）：
`emit_rc_store_field` 的 weak 路径经 `emit_weak_store` 落槽后，若右值属于 owned
形态（`expr_yields_owned_rc_value`：弱字段读取提升出的 +1 强引用、`new`、无主
调用结果），必须补一次 `emit_rc_release_for_type`。强路径靠"消费 owned 右值"
吸收这个 +1，弱路径没有消费者，漏释放 = 这个 retain 永远还不掉：`a.weakF =
b.weakF` 每执行一次泄漏一个对象，stdlib `LinkedList.Remove` 的
`cur.next.prev = cur.prev` 曾因此让未 Clear 的链表整条释放不掉。二分时先区分
"弱读做接收者"（不泄漏）与"弱读做右值"（泄漏）——钉子用例
`tests/conformance/weak_store_owned_rhs.zan`。
`tests/conformance/weak_store_owned_rhs.zan`。

**guard 异常关键词分流次序与 out 实参落槽**（实证，2026-11-02）：
- `infer_guard_exception_sym` 按消息子串分流，次序敏感：division by zero →
  "not a valid"（→FormatException）→ "outside the range"（→Overflow）→
  "out of bounds"/"index"/"range"（→IndexOutOfRange）。新增 guard 消息先查子串
  污染——解析消息曾带 "(or out of range)" 尾巴，被 "range" 分支整体吃进
  IndexOutOfRange。int.Parse/Convert.ToInt* 的格式错与越界必须两条
  emit_runtime_check 分开发射才能分流成两种异常。
- out 实参取**槽位地址**不是值：AST_REF_ARG 走 `emit_ref_arg` 直接返回地址
  （内联 `out int v` 声明在该函数里建 alloca），裸标识符查 `local_find` 的
  alloca + `ol->type`，落槽统一走 `emit_typed_out_store`（处理整型宽度换算与
  ARC 旧值释放）。`emit_expr` 对标识符返回的是加载后的值，指针守卫静默跳写——
  int.TryParse 的 out 曾永不更新就是这一坑。
- long.Parse 全宽越界只能靠 strtoll 饱和置的 errno=ERANGE 判（`emit_errno_preset`
  按**目标** triple 选 TLS errno 地址），按 INT32 边界判会误杀合法值
  （3000000000、MaxValue 曾被拒）。


---

## 四、编译发布与驱动链接（Packaging & Cross-Linking）

37. **驱动运行时打包与 LLVM 模块生命周期顺序**：
   - 动作：驱动 manifest 中的条件依赖符号前缀（如 `WebView`, `Cef` 等）必须在释放 LLVM 模块前预缓存至 `prefix_cache`，禁止在 `zan_irgen_release_llvm` 后调用模块遍历 API。
   - 理由：LLVM 模块销毁后指针置空，再次遍历会导致进程异常崩溃。
38. **按需拉入与运行时依赖前缀触发**：
   - 动作：标准库与驱动的按需拉入应基于符号消费触发，避免无条件的粗粒度导入引入沉重的多线程或特定平台运行时依赖。
   - 理由：减少单执行文件的发布体积，避免无用依赖拖慢启动性能。
39. **iOS Mach-O 交叉编译平台标记与原生外壳驱动内嵌**：
   - 动作：交叉编译生成 iOS ARM64 运行时对象时，必须指定 `-target aarch64-ios`（严禁使用 `aarch64-macos` 代替），避免 `ld64.lld` 因平台头标识不符（`has platform macOS, which is different from target platform iOS`）拒绝合流。针对 iOS 上的 `zan_gui` 与 `zan_image` 驱动，编译器禁止生成桩函数，而是直接静态内嵌 `zanrt_gui.o` 与 `zanrt_image.o`。
   - 理由：iOS 设备禁止动态加载未签名的第三方 dylib，所有 GUI 视图生命周期、Retina 触控、2D 软件光栅与图像解码器（PNG/WebP/SVG）驱动均以静态目标文件合流进可执行 Mach-O，确保输出的标准 `.ipa` 即装即用。
   - 边界：交叉目标引用 libc 符号必须按**目标**平台选名字，不许按宿主
     `#if defined(_WIN32)` 选——Windows 宿主编 macOS 时 IR 名 `_errno`
     会被 Mach-O 再加一个前导下划线变成 `__errno`，而 libSystem 导出的
     C 符号是 `__error`。这类错链接期才炸、且炸在离病灶最远的调用点上
     （实例：`Convert.ToInt*` 字符串路径的 ERANGE 探针发 `__errno`，
     ff_bidi_demo 交叉编 macos-arm64 链接失败；已改 irgen_call.c 按
     target_triple/目标标志选 `__error`/`__errno`/`__errno_location`/
     `_errno`，分支次序对齐 Socket.zan 的 DllImport 守卫）。
40. **iOS CoreText 字体生命周期、主线程呈现与触控事件流纪律**：
   - 动作：(1) 通过 UIKit `[UIFont systemFontOfSize:]` 桥接获取系统字体后，必须显式 `CFRetain` 并常驻全局字号索引表（`g_font_table[128]`），其 ascent/height 度量必须持久化缓存，禁止在文本测量或光栅化完成时直接调用 `CFRelease` 释放系统单例，防止内部度量数据归零引发排版坍塌；(2) CoreText 绘制采用正向坐标系光栅化，基线锚定 `descent + 1.0`，内存行索引直接按 `mask[py * tw]` 正向写入 Zan 软件表面，严禁在写入时做垂直翻转（`th - 1 - py`），否则会导致字形 180 度上下倒置；(3) iOS CALayer 的 `setContents:` 与 `CATransaction` 提交必须通过 `dispatch_sync_f` 派发至主线程（`_dispatch_main_q`）执行，禁止在后台工作线程直接 flush；(4) 触控滑动判定阈值（slop）设定为 `6.0f * scale`（6pt），滚轮事件在队列中实施增量合并（coalescing），并在拖拽释放时标记 `flag = 1` 抑制误触。
   - 理由：UIFont 在 iOS 内部由 UIKit 统一管理引用计数，过度释放破坏单例内存引发度量归零与中文降级回退（问号乱码/像素风）；CGBitmapContext 在 iOS 内存布局中第 0 行为顶部，反向 blit 会造成字形上下颠倒；CoreAnimation 事务在非主线程提交会被系统延迟至未知主循环周期，导致拖动画面延迟极高（卡顿迟钝）。
41. **批量构建不要把文件列表摊在命令行上**：
   - 动作：编译"一个目录/一个包"的全部源文件时，把**目录**交给编译器
     （`zanc <dir>`，编译器自己递归展开 `*.zan`），或用 `@响应文件`；不要在
     调用方枚举出成千上万个路径再拼进命令行。
   - 理由：Windows 命令行长度上限约 32K。5618 个文件的包把路径摊上去，
     **进程根本起不来**——而这个失败是**静默的**：报错文本里没有任何编译
     诊断，调用方只看"输出里没有 error"就判成编译成功，于是一个从没被编译
     的包看起来是健康的（`scripts/pkg_sweep.ps1` 长期这样误报，微信 SDK 缺
     枚举的缺陷因此藏了很久）。
   - 边界：目录展开必须**按路径字典序排序**——目录遍历的原始顺序在 Windows
     上不确定，而输入顺序影响合并单元里 decl 的先后，可复现构建与差分门都
     要求稳定。

42. **await 的目标必须先判「异步 / 普通」再取帧**：
   - 动作：`emit_expr_await_expr`（`src/compiler/irgen_expr.c`）在取子协程帧
     之前，先用 `await_target_is_async()` 判一次：目标是**普通值**就原样返回，
     不进协程帧那套。判定要覆盖六种形态——`Task`/`ValueTask` 类型、async
     **方法**（看 `MOD_ASYNC`）、async **委托**（对象是委托 / 被调者本身是
     委托 / 字段是委托）、**接口方法**（签名在接口上声明）、**类名静态接收者**
     （`Socket.SendStringAsync(...)`，类名不是值，`infer` 给不出类型，要按名
     查符号）。
   - 理由：普通值往往也是 i8 指针（字符串就是），与子协程帧的指针**同型**，
     被当成帧去读就静默读出垃圾——不报错，只是结果为空或乱码。实测两种症状：
     `await` 一个 async 方法拿到空结果（协程帧从没建立），`await` 一个同步
     委托同样拿到空。HTTP 请求的 await 被误判成普通值时，报的是
     `request write failed`——离根因十万八千里。
   - 边界：**不要靠 `infer_expr_type` 单独判**。async 方法的返回类型是它的
     真实返回类型（`List<AiMessage>`），不是 `Task`——类型上看不出来，只能
     看方法原型上的 `MOD_ASYNC`。同理接口方法、类名静态接收者也都推不出来，
     必须各自走一条路。
   - **运行时新增全局符号，必须过全部链接形态门**：rt_mem.o 只在原生 exe、
     跨端 linux exe 与 Apple（本机/交叉 bundle 有 zanrt_mem.o 时，发射期
     改名 zan_mem_*）链入——Windows DLL（ld.exe -shared）、macOS dylib
     （ld64.lld）、Android 静态链（bionic 上 --wrap=malloc 崩，刻意不链
     mem）、selfhost zanrt_sh 束都拿不到它。B-ID78 把 defer 队列落进
     rt_mem.c 后这些形态全部断链（ELF .so 靠默认容忍未定义符号**静音带
     病**，dlopen 才炸——比响的更危险）。修法沿 zan_io_pump 先例：
     irgen 对已建声明补 WeakAnyLinkage + 函数体（emit_defer_fallback），
     链入强定义覆盖、未链入退化 fail-open。
   - **Apple 体积与"静默弱桩"**：ld64 按整对象链入，Mach-O 只有
     `-dead_strip` 才按符号剥（本机/交叉 Apple 现一律带；hello 176→58 KB）。
     但 main 无条件调用的运行时入口会把整个子系统钉住、dead-strip 救不了
     ——`zan_co_sched_init` 曾让每个 exe 带全套 M:N 调度器（~20 KB 代码）。
     这类"启动即调"的入口要在**优化后**按模块引用面裁掉
     （`zan_strip_unused_co_sched_init`；优化前死 stdlib 函数还挂着引用，
     判不准），回归由 `size_budget_dev_macos` 的 FORBID_SYMBOLS 守。
     反面：rt_timer.o 的 `zan_mem_slabs/trim` 是恒 0 弱桩，分配器对象没链上
     时程序照跑、只是 GC 统计悄悄失真——只查"不崩、非负"的用例
     （gc_trim_memory）看不出，必须断言强实现的可观察效果
     （`case_gc_fast_alloc_linked`）。验证落点用 `nm`/`ld -why_live`。
   - **LLVMAddFunction 不查重名**：先建同名函数，后建者不会返回已有的，
     而是并存，发射时被 mangler 加 `.N` 后缀（如 `zan_rt_defer_push.5`）
     ——调用指空、链接 undefined reference。要给"延迟建的声明"补体/改
     链路，必须后挂在 `g->` 字段上（声明建成之后再 LLVMSetLinkage +
     AppendBasicBlock），不能抢跑。中段发体要用 `LLVMGetInsertBlock`
     保存恢复 builder 位置，否则后续 Br 插进别的函数。

---

**新增编译器 .c 文件要同时进 zan-lsp 的源文件表**（实证，2026-11-24）：
`zan-lsp` 内嵌同一套前端（lexer/parser/binder/checker/diag），但在 CMakeLists 里
是**另一份**源文件表。前端新调用了新文件里的函数，只加进 `zanc` 的表，`zanc` 照常
通过而 `zan-lsp` 链接失败（Undefined symbols），失败的链接还会删掉旧的
`build/zan-lsp`。改前端后验证要 `cmake --build build --target zanc zan-lsp`，
LSP 用例必须用**新编**的 zan-lsp 跑——旧产物照样"通过"，什么也证明不了。

**普通构建也是 closed-world 反射：只经 Invoke 按名调用的方法须显式保留**（实证，2026-11-25）：
非 `-g` 的普通构建与 `--publish` 同口径裁剪反射桩（`emit_target.c` 的 `closed_world`），
静态上没人调用、只在标记里 `@click="HitMe"` 按名出现的方法，桩被裁掉后 `HasMethod`
仍为真（对账通过），`Invoke` 却静默什么也不做——症状是"点了没反应"、不报错。
`On<大写>` 名字被 `refl_is_framework_hook_name` 兜住，所以多数窗体看不出来。
设计稿处理器名由 `zan_embed_collect_html_handlers_ex` 收集（工程 `src/` 递归 + 普通构建
另扫显式输入所在目录本层，对齐运行期 `HtmlDocs.Find` 的查找面）；新增"只按名调用"的
入口时同样要进这张保留表。探针写法注意：探针里顺手写一句 `obj.Invoke("名字")` 会
让该名字静态可见、问题消失——复现要去掉一切同名字面量。

