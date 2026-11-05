# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。领账按批次：批次 1 根修优先（闭一条解锁一片），批次 2 独立账可并行，批次 3 等前置闭了再领，批次 4 大工程须独占窗口；同批内按编号序。每条正文前的〔…〕是依赖标签：〔前置 X〕= X 不闭本条闭不了；〔软前置 X〕= 没 X 也能做但成本高；〔根修·解锁 Y〕= 本条闭了 Y 的前置随之解除；〔串行前置 X〕= 与 X 同改一个文件，等 X 落地再动。根修闭账时同批摘掉下游条目的〔前置〕标签并上调批次。领账前 `git status` + 文件 mtime 查在飞——并发会话不一定走调度器注册（2026-11-03 B-ID107 即靠 mtime 抓到撞车）。
>
> 2026-11-05 平账：移出已闭账 [x] 七条（B-ID149/150/151/154/156/157/158），结论与验证证据由对应提交正文承担。
>
## 未完成（按平账顺序分批）

### 批次 0 · 在飞（持有会话未提交；其他会话勿碰同文件）

- [ ] B-ID159〔批次 2 独立账〕窗口声明面标签化三刀：刀1 已提交（data-kind 属性旁路废除、TagToKind 折入别名 + ZanKind zan-<kebab> 还原、ProjectBody 单跳直挂、Form.FindDoc 磁盘回退修真内容、69 html 标签化、5 测试重切 Html.Parse 通道 + html_runtime/form_dynamic 补 MarkDirty 存量红）；刀2 JSON 管线拆除——DesignerHtml.ToJsonDoc + 读侧 orphan 函数删除、设计器 writer 去 data-kind legacy 分支、TagKind 共享映射收口、DesignLoad/Designer.Html.LoadHtmlText/Designer.Prepared/Preview/PvCompCtl 重切 Html 通道、UserComponentRegistry 与 .zcomp 换 html、FormBuilder Build(JsonValue) 子图 + FbBuildContext 删除、NormalizeEventName 归一化行为新家、form_grid/genform/design_form_size/form_control/compref/batchjob/designer_wysiwyg/formbuild/rich_events 测试重切或删、roundtrip golden 更新；刀3 过渡态清仓——docs（HTML_UI/STRUCTURE/STDLIB_COMPONENT_STANDARDS）与 skills（gui-design/html-window/zan-development/zan-workflow/game-dev）三副本同提交改写、html_gallery 及 gui_charts/gui_excel_export/gui_browser/gui_cef_browser/gui_wallpaper 声明化、intellisense.c zan-* tag 解析、gen_design_halves.py 改读标签、gallery.json 与 examples-catalog regen、_scratch 无 README 探针目录清扫。


### 批次 1 · 根修项（闭它解锁下游，优先领）

（暂空——B-ID75 实测定性为大工程移入批次 4，见该条 11-04 规模勘察注记。）

### 批次 2 · 独立账（无前置，任一会话可领，按编号序）

（暂空——本批次全部闭账移出。）

### 批次 3 · 有前置（前置闭了再领）

（暂空——本批次全部闭账移出。）

### 批次 4 · 大工程 / 独占窗口（无其他 irgen/runtime 改动在飞时领）

- [ ] B-ID71〔大工程 · 独占窗口；关联 B-ID64 异常路径帧析构〕SJLJ（setjmp/longjmp）异常机制与 ARC 栈展开资源泄漏/锁死锁：跨函数 throw 直接恢复栈寄存器，绕过被穿越中间帧的析构逻辑（ARC 漏释放、using 不关、zan_monitor_enter 无 exit 全局死锁）；exception_handler_depth 真回归同源——O2 DCE 把 rethrow 路径写入的局部当 dead store 删（setjmp/longjmp 对 LLVM 是不可见控制流边；已证全量 volatile alloca 无效，longjmp 后 volatile store 仍被 CPU 级跳过）。须演进为零开销栈展开（LandingPad）根治；双轨保持一致。
- [x] B-ID103〔大工程 · 独占窗口 · main.c 并发禁动区〕irgen unity build 拆分：irgen.c 把 21 个 irgen_*.c 文本 include 进单 TU（约 52K 行），静态函数靠跨"文件"前向声明网维系插入顺序；main.c 在 main() 函数体内 #include "main_options_parse.c"（驱动不可单测）。修法方向：真 TU 化 + 声明网收编进头文件 + 选项解析独立编译单元。main.c 属并发禁动区，待窗口。
- [ ] B-ID152〔裁定搁置〕ZanDB 刀8 段 zstd 压缩：stdlib 无压缩绑定，需引入外部库链接与三平台分发——与自包含分发架构相抵；且压缩块缓存仍需解压驻留，对 2G 内存机目标无益。数据面收益已被 tag 11 行编码（4×↓）替代大半，除非出现大文本字段压缩需求，不领。
- [ ] B-ID153〔裁定搁置〕ZanDB 刀9 SIMD 距离内核：运行时只有 CPU 特性探测（System.Runtime.Intrinsics.Cpu）无向量算术内建，落地须编译器/运行时新增 AVX2/NEON 内建——超出 ZanDB 数据面范围。评估前置已满足：发布档基线实测 approx 2ms/20k 库（debug 9.9ms，约 5×，见 ZanDb README 量化检索实测），而真实查询延迟由重打分文档行点查主导（12ms，IO 不随构建档变），SIMD 对端到端收益被摊薄；除非出现纯 ADC 高 QPS 场景，不领。
- [ ] B-ID155〔裁定搁置〕zanc 编译缓存重做：旧缓存架构因产物散落源码树、无界堆积成巨量垃圾文件被移除；每次全量重编（GUI 程序 726 文件，Zan.Gui 包 387 文件/23MB 源码）是其接受代价，不是遗漏。重提必须先满足四约束：产物只进单一 git-ignored 固定目录、容量有界（LRU 封顶驱逐）、内容哈希键失效（杜绝陈旧产物）、带一键清理入口。实测 hello+stdlib 30 文件峰值 18MB——进程基线轻，内存大头是包编译面；包缓存收益随 Gui 包肥大持续增长。
