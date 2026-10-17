# crt-transition — 过渡期链接用 C 运行时（非自举源码）

这两个 `.c` 文件不是自举编译器的一部分；它们只是把回归/记分牌产出的目标码
链接成可执行文件时所需的过渡期外部符号实现（`native_regression.py
--runtime`、`native_bootstrap.sh RT_OBJS`）：

    cc -c -o zanstubs.o zanstubs.c
    cc -c -o zanhost.o  zanhost.c
    cc -o zanc zanc.o zanstubs.o zanhost.o -lSystem

## Zan 运行时第一批~第十三批(runtime_core.zan)

ZAN_TARGET 条件编译(v18o-25):lexer 的平台 define 表跟目标走——
`ZAN_TARGET=aarch64-linux` 时定义 LINUX+ARM64(不再 MACOS/APPLE),
stdlib 的 `#if LINUX`/`#else` POSIX 臂(Platform/Runtime 平台报告、
AppPath /proc 路径、Threading POSIX 信号量、Directory 的 Linux dirent
偏移 18/19)随之编入,兑现 stdlib"交叉编译的程序报告目标系统而非构建
主机"的契约;无环境(或其它值)时与 oracle 宿主表逐位一致。guest 探针
实证:Directory 枚举命中(此前 guest 用 Darwin 偏移 20/21 读 musl 的
glibc 布局 dirent,枚举名必错)、GetPlatform 报 "linux";宿主无环境
逐位不变(电池 47/47 × 双配置、crossboot 80/80)。永久金标
native_retarget(平台谓词恰好一真 + dirent 枚举命中,两车道同金标)
入电池 46→47。runtime_core 的 Darwin 常数(O_CREAT/O_NONBLOCK、
struct stat 布局)条件化以此为基础,仍属后续批次。

`runtime_core.zan` 用 Zan 重新实现了过渡 C 运行时的十批符号:
`zan_monotonic_ns` / `zan_monotonic_us` / `zan_sha256` / `zan_sha512`(第一批)、
`zan_alloc` / `zan_free` / `zan_crc32`(第二批)、
`zan_pkg_fopen` / `zan_file_fopen` / `remove` / `rename` / `read_path`、
FileStream 八件套(open/read/write/seek/tell/flush/close/eof)、FileInfo 五件套
(length/attributes/time/set_readonly/set_time)、`try_lock` / `unlock`、
`zan_embed_*`(诚实空集)、`zan_mmap_*`(诚实桩)——第三批;
`zan_plat_net_interfaces`(getifaddrs 走查/地址折叠/MAC)与
`zan_plat_icmp_ping`(DGRAM→RAW 回退、poll 状态机)——第四批,
**至此 zanhost.c 在 `-DZAN_RT_CORE_ZAN` 下整体清零**(链接进
`zanhost_rest.o` 的只剩空翻译单元);
`zan_io_*` 纯函数族 17 个符号(socket send/recv/alive/ready/cleanup、
peer_ip(+into)、sockaddr_ip_str(+into)、sockaddr_family/is_safe、
resolve_sa/ipv4/all(+async)、connect_sa/status)——第五批;
`zan_atomic_int_*` 七符号(create/destroy/load/store/add/exchange/
compare_exchange)——第六批;
`zan_audio_*` 21 桩、`zan_monitor_enter/exit`、`zan_dispatch_*`(CAS
自旋锁环)、`zan_eh_tls_state`(pthread key)——第七批;
`zan_shared_table_*` 36 符号(命名/匿名表的建开挂删、schema 解析、
int/float/string 三型列读写、_at 哈希族、hash/exists/count/stat、
increment/extreme、expire 三件、rate_allow 窗口、lock 租约两件、
clear/destroy)——第八批;
native io reactor 8 符号(io_wait_co/io_recv_co/io_recv_to_co/
io_accept_co/io_poll/io_close_notify/resolve_sa_co/resolve_ipv4_co:
watch 表、零超时探测、DNS 自管道 + 脱离 worker 互斥交棒)——第九批;
缺口五批次(第十批)收回线程与恢复链:`zan_set_ready_hook`(
编译器发射的 _zan_co_ready 自注册经 setter,不再直存 C 数据符号)、
`zan_thread_start` / `zan_thread_current_id`(原 C zan_thread_*)、
`[ThreadEntry] zan_thread_trampoline`(x19-x28 序言/收尾,替代
zanstubs.c 的 naked asm 屏障)、DNS worker 体/自管道(F_SETFL 非阻塞)/
互斥摘链——详见下文"缺口五批次"一节。
至此 C 侧(Zan 车道)`#ifndef ZAN_RT_CORE_ZAN` 编出面上这一族清零:
4 个内部 thunk(zt_io_resume / zt_dns_worker_fn / zt_dns_drain /
zt_dns_pipe_rd)与 naked trampoline 一并退役;C 基线车道
(zanstubs_full)原样保留全部 C 实现供 A/B。缺口一的
zan_open_creat 包装已在缺口一批次随变参降级修复退役(锁定 C 符号账面
21→20,缺口一批次详见下文)。
它由原生自举编译器(ngen)编译成对象,再用 `localize_syms.py` 把 API 面
之外的符号本地化——ngen 会把整套运行时帮助函数以全局符号发射进每个
对象,不本地化则与程序对象撞符号。缺口五批次起导出面 136 个强符号
(133 + set_ready_hook/thread_start/thread_current_id)+ 4 个弱
符号(embed 族经 `--weaken` 置 N_WEAK_DEF,带内嵌资源的程序对象自带的
强定义在链接时获胜):

    SEED=/path/to/stage2 bash crt-transition/build_zan_core.sh

该脚本同时产出五个对象:Zan 配置用 `runtime_core.o`(第十三批起即全部——
`zanstubs_rest.o + zanhost_rest.o` 是 `-DZAN_RT_CORE_ZAN` 编出的 C 余量,
全局符号已清零,仅为账面保留),C 基线用
`zanstubs_full.o + zanhost_full.o`(无定义符,供 A/B 对照)。
Zan 配置链接面:

    python3 scripts/native_regression.py --seed "$SEED" \
      --runtime "crt-transition/runtime_core.o" \
      tests/selfhost/native_rt_core.zan          # 九批端到端(366 行金标)

47 项默认电池(kernel 27 + 指定 20,含缺口四批次加入的 native_float_shapes、
缺口一批次加入的 native_varargs / native_varargs_elf、缺口五批次加入的
native_fnptr、第十一批加入的 native_sync、第十二批加入的 native_spans 与
第十三批加入的 native_memwide、v18o-25 加入的 native_retarget)
同样接受上述
`--runtime`。两个配置(A/B)都已全量验证:fixture(366 行金标,双配置字节一致)、
默认电池 43/43、自举固定点
(含 stage1.o 引用的全部 24 个文件族符号由 Zan 对象供给;第四批后另含
net/ping 两符号,第五批后另含 17 个 io 符号,第六批后另含 7 个原子符号,
第七批后另含 audio/monitor/dispatch/eh 28 个符号,第八批后另含
shared_table 36 个符号,第九批后另含 io reactor 8 个符号;两路
run.JGPg3t / run.E3RqVN 均 stage2.o == stage3.o)。第九批验证矩阵还
显式跑了 4 个 async 端到端(native_async_{basic,detach,locals,throw},
hook!=0 全链:编译器发射的 _zan_co_ready 注册 hook → reactor 停泊 →
zt_io_resume 恢复),双配置 4/4。第四批的 net 文本还与 C oracle
做过逐字节 A/B(同一台机器 probe 程序直接 dump zan_plat_net_interfaces
返回缓冲,两路 diff 为空)。

缺口一批次(ngen 变参调用降级,目标感知;顺带退役缺口一绕行件,
锁定 C 符号账面 21→20):考古定案——`[DllImport(Variadic = true)]`
机器四站俱全(ast 属性/parser MOD/ngen PushCallArgs 宽 arity 与
CallStaticNg Apple 栈式尾参),macOS 车道端到端可用但零文档零用户
(纯声明调变参函数拿到寄存器垃圾,file_lock 的 open 翻车即此);ELF
车道则整段错误——ngen 无视目标一律按 Apple arm64 把尾参落栈变参区,
而 aarch64-linux 实测定案(clang -target aarch64-none-linux-gnu 编
companion C 反汇编为 oracle):变参按自然类型继续常规分配,int 尾参
续 w 序、double 直接入 d 序、float 按 C 默认提升为 double,Apple 式
落栈在 ELF 上被被调方按寄存器读→垃圾。修复(ngen CallStaticNg 三处,
Apple 车道既有代码形状不动):(a) ELF 车道(dllVar 且
ZAN_TARGET=aarch64-linux)尾参进自然类分配循环:fp 类(IsDblExpr 或
float 型)续 cf<8→d 序否则溢出槽,其余续 ci<8→x 序否则溢出槽,sres
计入尾参溢出;求值仍写 8*sres+8*vk 暂存槽(Apple 同槽即 ABI 变参区,
ELF 仅暂存),新增 ELF 读回段把暂存搬进寄存器/溢出槽(fmov dN,x10 /
str 双字;无单精准化——提升已保证 double 位型)。(b) Apple 车道尾参
基址 8*sres→8*cs(未填充溢出数):va_list 从具名栈参之后开始,奇数个
定长溢出参时 16 对齐垫把尾参整体推后 8 字节——对抗探针 probe9
(9 定长 int + 尾参)修复前 macOS 1058≠2160,修复后双车道 2160;
现有 fixture 全是零溢出变参调用故从未暴露。(c) 金标:金标化
tests/selfhost/native_varargs.zan(变参 open 尾参 mode→stat 回读
33188=S_IFREG|0644、snprintf 5/8 尾参、fcntl F_GETFL;纯声明 open
负效不钉——结果按定义非确定)入默认电池;新增
tests/selfhost/native_varargs_elf.zan(snprintf/printf:混合类 5 尾参、
8 int 尾参寄存器耗尽+3 溢出槽、double/float 提升尾参、类交错、
定长-only 返回值)入电池与 crossboot 默认清单(qemu 执行,与 macOS
车道逐字节一致);对抗探针 probe9/probef(d0..d7 耗尽 + double 尾参
溢出 + int 尾参 x0)双车道 2160/1020.0 一致。绕行件退役:runtime_core
的 copen2 改 [DllImport(Variadic=true)] open 三参调用(mode 0644 尾参,
vamode=33188 经 file_lock fixture 间接验证),zanstubs.c 的
zan_open_creat 定义删除。全矩阵:fixture 366 行双车道字节一致、电池
42/42 × 双配置、async 4/4 × 双配置、负例诊断文本原样、crossboot
40 fixture(39 + native_varargs_elf)全绿;双收尾固定点
run.Tlt11M(Zan 道)/ run.UPw8Fy(C 基线道)均 stage2.o == stage3.o,
且涵盖最终头注与退役件;收尾 stage2.o 与全矩阵所用编译器对象逐字节
同一(cmp 验证,矩阵结果对收尾编译器原样成立)。

缺口二批次(ngen 静态 Span 视图,不改 runtime 符号面):残余面是
`Span<T>` 类型的**静态字段**(裸名与类限定)——读写、`Length`、
`Slice`、IsDblExpr 六个消费点全部漏静态(`SpanContainerTy` 只认局部
与实例字段),一律落 "unsupported (assignment|index) target" 编译
错。修复:`SpanContainerTy` 对齐 `ArrContainerTy` 补静态两分支(类
限定接收者沿用写路径的"接收者必须命名类"守卫)。考古:当年
probe_va2 的"静默错写"(写后原缓冲为零)与"跨 DllImport 持视图不可
靠"症状已被既往批次顺带治好——探针实测静态直入 Span ctor 写读
421422、视图跨两次 DllImport 持有 99098 全对,本批只剩响亮的编译错
面。探针 A/B:同源三形态在旧编译器 6 个编译错,新编译器
421422/7/99098 全对;扩展形(裸名读写、类限定、`Length`、
`Slice(5,2)` 写、越界守卫照数组同款触发)全绿。边界:`this.静态`
全车道不支持(普通静态同报错,一贯口径);async 方法内的 Span 存储
是独立既有局限(await 穿越臂从未支持任何 Span 存储,连局部视图也
不),本批未打开。runtime_core.zan 的"静态只存块指针、用点取新局
部"写法保留(大面积改写零收益),头注缺口 2 已改已修。全矩阵:
fixture 366 行双车道字节一致、默认电池 40/40 × 双配置(电池本批起
含 native_float_shapes——缺口四 FloatLit 形态守护,覆盖字面量立即
数/可空装箱 GenNulWrap 各站/float 窄化/内部 double 形参返回)、
async 4/4 × 双配置、crossboot 39 fixture 双道 78 PASS、自举固定点
run.jdFH6P(Zan 道)/ run.FmBUg7(C 基线道)均 stage2.o == stage3.o,
且收尾 stage2 与全矩阵所用字节同一(cmp 验证)。

缺口三批次(ngen 字面量直接物化,不改 runtime 符号面):原状 LoadStr
经 `l_.g.strp.N` 数据槽间接寻址,槽由 `_zan_rt_strpool_init` 在启动垫
片里盖章,而该 init 只从持有 Main 的对象被调用——第二个 ngen 对象的
槽恒 0。litlib 二对象探针实证:库对象导出的 Greet 返回 null、
Length/索引报 null reference(旧编译器),同源同 link 的新编译器
4/4(greet/len/chr/join 全对)。修复(ngen 四处):(a) `DGlob` 增
`bytes` 载荷;(b) `RtEmitStrpool` 重写为逐字面量发 8 对齐的
`l_.g.strd.N` 初始化块(rc=−1 不朽哨兵、64 位域,同反射 blob 先例
——本车道无任何 rc 递减路径,哨兵纯文档化;+8 处 SNAZ<<32|len 头,
载荷=blob+16,字节+NUL+对齐垫),`_zan_rt_strpool_init` 保留为空函
数(启动垫片仍调,过渡符号);(c) `LoadStr` 改 adrp+add 后 `add #16`
直取载荷;(d) 三个目标写手(macho/elf/coff)的 `__data` 载荷改经
`EmitDataPayload` 发已初始化字节(零填充电位 + 逐块覆盖)。原始
`l_.str.N` cstring 与 ~12 处直用点不变;FindNotAnyOf 的 strcspn 路径
从"依赖 init 补 NUL"变为 blob 自带 NUL,更稳。过程教训:首轮自举
stage3 全程异常串打印丢开头 4 字符("Unhandled exception:
KeyNotFoundException"→"ndled exception: NotFoundException")——快照
早于 rc 扩 8 字节的修正,12 字节头与 blob+16 载荷指针错位 4 字节,
每串打印从头跳 4;16 字节头(/rc qword + /头 qword)对齐后全绿。
blob 磁盘布局经 otool+nm 直读验证(rc/len/magic/字节逐一对)。
全矩阵:fixture 366 行双车道字节一致、39/39 × 双配置(本批当时电池
口径;native_float_shapes 加入后为 40 项)、async 4/4 ×
双配置、自举固定点 run.LIgI93(Zan 道)/ run.E8bVwz(C 基线道)均
stage2.o == stage3.o(收尾轮,含最终头注;Zan 道过程轮为
run.SbZmUP,基线道过程轮 run.jUjjFl);ELF/COFF 车道由 crossboot
39 fixture 全绿(qemu 执行 + elfcheck/coffcheck 结构验证;其
-mstrict-align 无 MMU 环境同时钉住 blob 的 8 对齐)。

缺口四批次(ngen double 边界修复,不改 runtime 符号面):编译器三处
改动——(a) 字面量惰性 `dbl.N` 槽改编译期 strtod 位型 + LoadImm 直发
(`DblBitsOf`:编译器自进程经 DllImport 调 strtod,Span 往返取回位型,
任意对象序成立);(b) 方法体统一改名 `_Cls_meth__body`(35 处内部
引用:BLInternal/vtable/委托/方法组/反射 thunk/async ramp/$resume
经 MethSymNg 自动跟随),导出名上发 C-ABI 垫片——整形槽降序搬移、
`fmov x_q, d_vi` 重排(float 形参经 `fcvt d16`)、`fmov d0, x0` /
`fcvt s0, d16` 收返回,>8 槽或聚合签名回退纯分支(与旧布局字节同形);
垫片自存 x29/x30(`bl body` 会打 lr);(c) `IsDblExpr` 的 Index 分支
补 `SpanContainerTy` 情形——原 Span<double> 元读被当 long,赋 double
局部再 scvtf 一次,即第八批 get_float 的潜伏值 bug。垫片编码本身踩掉
四个坑(bl 打 lr 未保存 → 自举 stage3 单指令自旋;ldp 后索引 #32/#16
错配 → sp 漂移段错误;`0x9E66`/`0x9E67` 的 fmov 方向与 fcvt 操作数
次序写反 → double 形参恒 0、float 返回恒 0)。ABI 逐位探针:C 驱动
直调 Zan 导出 7/7(double/float 形参返回、int/double 混排双向位移、
5 参三浮点),float 专项 4/4,Zan 经真实 double 型 DllImport 调
fmin/fabs/fmod 5/5。runtime 对象以新编译器重建后导出面不变(133 强
+ 4 弱,411 本地化——多出的 78 个即 `__body` 符号)。第八批 fixture
的 float 段升级为逐位值往返(1.5/0.1/-0.75/π,359→366 行金标,双
配置字节一致)。全矩阵:40/40 × 双配置、async 4/4 × 双配置、自举
固定点 run.Ct1l0a(Zan 道)/ run.j0eAnc(C 基线道)均
stage2.o == stage3.o(收尾轮,含最终头注;Zan 道过程轮为
run.ZZM9QN / run.wuA9Cs,基线道过程轮 run.9EyNuC)。

第二批的改名机制：stdlib 的 `NativeMemory.Alloc/Free/Crc32` 声明加
`EntryPoint = "zan_alloc" / "zan_free" / "zan_crc32"`——DllImport 的
EntryPoint 就是符号改名机制，无需动编译器。C host（LLVM 车道）对这些
调用直降 libc（`irgen_expr.c`），不受影响。`NativeMemory.Crc32` 的双车道
oracle 语义是**零扩展成 i64 返回**（"123456789" → 3421780262，
见 `irgen_expr.c` 的 `nm_crc32_fn` 末尾 ZExt 与 `ngen.zan` 的 Crc32→long
特例）；fixture 里自带的 `int` 返回 DllImport 变体才打印有符号读数
（-873187034）——两条都钉在 golden 里。

过渡期遗留别名：旧代 seed 是对着改名前的 stdlib 快照编译的，其 stage1
产物仍引用 `_Alloc/_Free/_Crc32`。`zanstubs.c` 里这三个转发别名**不加
守卫**（两个配置都参与链接），等所有钉住的 seed 都晚于本次改名后即可
删除。注意：native 车道的 stdlib
查找是**编译器同目录优先**（`main.zan` 回退 `./stdlib`）——seed 旁边的
历史快照会遮蔽仓库 stdlib，改 stdlib 后必须用新 bootstrap 产出的 seed
重跑回归，否则改动静默不生效。

约束与已知代价(详见 `runtime_core.zan` 头注):不 using stdlib(Span 是
binder 内建,libc 直调);数值一律 long + `>>>` + 掩码;每次调用为 Span
视图泄漏 1~2×16 字节(视图按设计短命不回收);公开 ABI 方法名必须与
libc 导入别名错开(同名会触发重载 mangling 破坏符号契约,如内部的
`cfree` 别名)。第三批追加:stdlib 声明的 string/byte[] 形参在 Zan 侧
一律收 nint(同槽位,隐式 string→nint 已探针验证);struct stat 按
macOS arm64 硬编码(144 字节,mode@4 time@32/48/64 size@96,经
offsetof 验证);embed 族为强符号(ngen 无弱符号发射;native 车道无
embed 烘焙器,gen0 的 embedres.c 不进 native 链接,无人与之争);
try_lock 代际表是进程内 calloc 静态、单线程假设与 C 版一致,锁本体
BSD flock;read_path/embed_list 返回 malloc 的 C 串(C strdup 契约),
该 string 返回 ABI 在托管侧目前是休眠路径(fixture 按字节校验)。
`utimensat` 在部分受限环境拒绝相对路径——金标钉"返回值与回读一致"
而非具体值,任何环境下正确实现均为 true。

第四批追加:`sa_family_t` 是 u8(Darwin),只能读 offset 1——offset 2
起 `sockaddr_in` 是端口、`sockaddr_dl` 是 sdl_index,并读会分别把
AF_LINK 判成假、MAC 列判空。`freeifaddrs` 必须收链表头(`*listp`)而
非持有者指针,否则持有者随后 `free` 即 double free。

第五批追加:公开方法名即导出名(mangling `_{Class}_{Method}`)——
stdlib 的 `EntryPoint="zan_io_*"` 要求方法名自带 io_ 前缀;按
`socket_send` 命名的第一版导出 `_zan_socket_send` 被 localize,
症状就是 kept 计数停在 37。`addrinfo` 48 字节按 `Span<long>(p,6)`
读(family 在 ph[0] 高 32 位、addr=ph[4]、next=ph[5]);getaddrinfo
的 service 收十进制字节串(uderc_into 不写 NUL,调用方补);fd_set
是 128 字节/16 long,select 后一律取新 Span 重读;v6 字节取反用
`255-x`(`^` 未验证);SIGPIPE 惰性忽略 = `csignal(13,1)` 取旧
handler、非 0 则装回。

第六批追加:`zan_atomic_int_*` 的 RMW 不经 ngen(无 LSE 代码生成),
走 libSystem 仍在导出的 `OSAtomicAdd64Barrier`(恰为 add_fetch)与
`OSAtomicCompareAndSwap64Barrier`;exchange 用 CAS 循环、失败路径取
load-after-fail 当"看到的旧值"——单线程车道与 oracle 的 `__atomic`
等价,多线程竞争是文档化偏差(线程族本身仍在 C)。原子布尔返回值
声明 nint 并掩 `&255` 再判(小整型 ABI 只保证 w0);load/store 是
对齐 8 字节访问,arm64 单拷贝原子。

第七批追加:audio 桩的 `"stub"` 串在 lit_buf[32..36](lit_init 追加,
不动既有偏移)。monitor 是条纹计数器而非真锁——本车道单线程,递归
即 entry++/--,重入合法、退出钳 0;条纹折叠用乘法散列(`^` 未验证)。
dispatch 用 CAS 自旋锁(0 闲 1 忙)替代 pthread mutex,环状态在
calloc 控制块、静态只存块指针、用点取新局部,grow 的头尾迁移与 C 版
逐字对齐(head=0、tail=存活数),200 元素跨两次翻倍的 FIFO 保序钉在
fixture。eh_tls_state 的 `__thread` 槽位换成 pthread specific key
(`pthread_key_create` 无析构 + get/setspecific;`pthread_key_t` 是
unsigned long)。`double` 形参/返回的 DllImport 可用:0.0 走
`(double)0L` 显式转换,不写字面量;audio 的 A/B 签名以 C 桩为准
(stdlib AudioNative 声明与 C 桩本就有漂移,桩忽略多余实参)。

第八批追加:shared table 的块布局是自定的(表 3152B:[0]名串 [1]容量
[2]键长 [3]列数 [4]行距 [5]行链头 [6]行数 [7]在册 [8]自旋锁,列区从
80 起每列 96B;行 48B 六槽),不必对齐 C 的 zt_table——A/B 只对
符号级行为负责。注册表/匿名表/锁单元沿用"静态存指针 + 惰性建块"
单线程假设,互斥本身是 OSAtomic CAS 自旋。三个 C 行为怪癖按 oracle
逐字保留并钉进 fixture:墓碑复用不更新键副本(复用行对
find-by-key 不可见);lock_acquire 先 purge(传入 now)→ 到期租约
即被清、重夺返回 1;lock_release 用墙钟 purge → 未到期租约也被清、
后续 release 返回 0。schema 解析的 `*p++ != ':'` 无论成败都消耗
冒号,列判重是 strlen 等长 + memcmp;FNV-1a 的 XOR 以
`(a|b)-(a&b)` 恒等式实现。指针算术一律经 `st_ptr(long)` 两步
——`(nint)(表达式)` 括号强转被解析器当方法调用(缺口三的旁证)。

第九批追加:native io reactor 的 watch 表是私有布局 80B/项(fd/kind/
active/buf/len/deadline/frame/step/outn/垫),静态只存 io_ws/io_n/
io_cap,grow 用 calloc+memcpy+free。co 助手系统调用前一律先零超时
poll 探测——accept 出的套接字常被调用方留在阻塞态,直接阻塞会冻死
单线程引擎而不是只停这一根协程。DNS 与 C worker 的分工:worker 线程
体留在 C(ngen 无函数指针调用,地址经 `zt_dns_worker_fn()` thunk
取),作业块 72B 是共享契约(next@0 name@8 port@16 buf@24 cap@32
outn@40 frame@48 step@56 result@64 v4@68),完工链经互斥
(`zt_dns_drain()`)+ 自管道(读端 fd 经 `zt_dns_pipe_rd()`,非阻塞)
交棒,ready 队列只被主线程触碰;恢复经 `zt_io_resume` thunk(hook
指针的间接调用,缺口五)。fixture 是非 async 程序,hook==0,恢复为
空操作——reactor 语义只经 outn/outfd 槽与 poll 返回值观察(停泊探针
-999/交付值/退休后归 0);4 个 async 端到端补 hook!=0 全链。金标里
accept 交付用"等待环"写法(fd_ready 偶发慢一拍时 accept 停泊、首轮
poll 交付,两种交错输出一致);ipv4 解析值按 s_addr 内存序(LE 读
u32:127.0.0.1 → 0x0100007F = 16777343),"localhost" 可能 ::1 在前,
只断言交付发生不断言族。

**已发现编译器缺口**(ngen,均有探针/反汇编证据,详见
`runtime_core.zan` 头注,本文件全部绕行):
1. 【缺口一,已修】变参调用降级——原状:`[DllImport(Variadic=true)]`
   机器全在(ast/parser/ngen)且 Apple 车道实测可用,但零文档零覆盖;
   ELF 车道错把尾参按 Apple arm64 落栈变参区(aarch64-linux 实测定案:
   变参按自然类型继续常规分配,int 续 w 序、double 入 d 序、float 提升为
   double)。第二受害者是三参 open(open 本是变参函数,mode 由 va_arg
   读出):x2 落寄存器残留值,创建文件拿到随布局漂移的垃圾模式
   (file_lock 翻车根因,v18o-13 根治)——绕行 zanstubs.c 二参包装
   zan_open_creat,并要求变参 libc 函数一律不得从 ngen 代码 DllImport。
   修复见下"缺口一批次"节;绕行件已退役,变参 libc 现可直接 DllImport。
2. 静态字段持有的指针上 Span 索引写不可靠(probe_va2 写后原缓冲
   仍为零)——模式一律"静态存、用点取新局部";
3. 字符串字面量经数据槽间接寻址、槽由本对象静态初始化填充,第二个
   ngen 对象的 init 不被调用→槽恒 0——runtime 内禁写字面量。
4. 【缺口四,已修,全矩阵验证绿】double 边界的原状三层:字面量惰性
   `dbl.N` 槽 + 运行时 strtod(字面串槽),非首对象 fastParse64(NULL)
   段错误;导出方法体直收 C ABI 调用(double 形参在 d 寄存器被按 x
   寄存器读);Span<double> 元读被静态当 long、赋 double 局部再
   scvtf 一次——第八批 get_float 的潜伏值 bug(fixture 当时只验行
   管理语义;第七批 audio 的 `== 0.0` 断言只是出口前无调用、scvtf 的
   0.0 恰好留在 d0 才通过)。修复见下"缺口四批次"节;runtime 内
   `(double)0L` 写法保留(无害)。
5. 无间接调用/函数指针:`LoadSymRef` 能取全局地址,但没有经寄存器的
   `blr`。受害形态:协程恢复 hook(`zan_co_ready_hook`)、线程体
   (`pthread_create` 的 fn)。绕行:C 侧包装 thunk(与已退役的
   zan_open_creat 同形态,属缺口五)——`zt_io_resume(frame,step)` 内部
   判空调 hook,线程体地址
   经 `zt_dns_worker_fn()` 取,互斥下摘完工链经 `zt_dns_drain()`。


- `zanstubs.c`:gen0 主机原语(Alloc/Free/Copy/…/Crc32 legacy 别名)、
  Win 代码页 API、`zan_thread_*`(naked asm 屏障 + 函数指针调用,
  等编译器函数指针特性)、
  原生 io reactor 的 4 个内部 thunk(zan_co_ready_hook + zt_io_resume
  + zt_dns_worker_fn + zt_dns_drain;watch 表/close_notify/poll/
  DNS worker/五个 await 形态第九批已移入 runtime_core.zan,`-D` 时
  编出;语义对照 oracle `src/runtime/rt_io.c`/`rt_co.c`:零超时探测、
  recv 的 EINTR/EAGAIN 三态、POLLNVAL/ERR/HUP 交付、recv_to 数据赢
  过将过的截止、DNS 自管道排干 + 互斥摘链;audio 桩、monitor、
  dispatch、eh_tls_state 第七批移入,io 纯函数族第五批移入:send 的
  -1/-2 分类与 SIGPIPE 惰性忽略、resolve_sa/resolve_all 的 v4 优先与
  stride-32 去重、sockaddr_is_safe 的内嵌 v4 分类、connect_sa 非阻塞
  +select 截止;close_notify 因车道无 reactor 为 no-op)。第一批~第二
  批符号(zan_monotonic_*/sha256/sha512/alloc/free/crc32)与
  `zan_atomic_int_*`(第六批)已移入
  runtime_core.zan,`-DZAN_RT_CORE_ZAN`
  时编出;语义无歧义的用 libc 实现;语义未移植的路径 abort()(宁可炸
  也别静默错)。
- `zanhost.c`:在 `-DZAN_RT_CORE_ZAN` 下整体清零(文件半边第三批移入
  runtime_core.zan,network 半边第四批移入);无定义符的
  `zanhost_full.o` 仍是 A/B 对照的 C oracle。注意:本文件
  是 2026-09-17 /tmp 清空后按符号需求重建的,与更早的失传版本不保证
  逐行一致;功能以 43/43 回归 + bootstrap 定点 + parity sweep 为准。

由本编译器产出的程序另引用 `_zan_ext_*`/`_zan_dt_*`/`_zan_dir_*` 帮助
函数族（见 src/selfhost/ngen_host.zan）——那些由 ngen_host 直接生成
机器码到目标文件里，不在这里。

## Linux 用户态车道(v18o-20 打开)

执行载体:qemu-system-aarch64 -M virt + Alpine v3.20 aarch64 内核
(netboot vmlinuz-virt)+ 自组 initramfs(minirootfs + stty -onlcr 的
init,payload stderr 剥离后串口线即程序 stdout 原字节)。宿主侧
`scripts/linux_vehicle.sh` 全流程:zanc(ZAN_TARGET=aarch64-linux)→
ELF 对象 → `scripts/elf_localize.py`(ELF 版 localize:局部化非 API
全局、重排 symtab 使局部符号先于非局部并重映射 .rela 索引,ld.lld 严
查 sh_info)→ 与 ELF 版 runtime(runtime_core.elf.o 定点编译器重编 +
localize)+ musl libc.a + Alpine libgcc.a 静态链接 → guest 引导 → 串口
输出对 .out 金标逐字节 diff。第十四批起链接里**零项目 C**
(zanstubs/zanhost 余量与 zanlinuxshims.c 全部退役,见下)。验收(默认
十夹具,第十五批起 SKIP 清单为空):kernel1、native_extern、
native_string_ops、native_float_shapes、native_varargs_elf、
native_digests、native_memwide、native_rt_core(366 行全 IO 面,
第十五批收编)、native_sync(第十五批收编)、native_varargs(第十五
批解钉收编:open 标志与 st_mode 偏移按目标 #if 双臂,单一 golden
两平台;至此"macOS 布局钉"清零)全绿——变参 ELF 车道金标与摘要/
内存宽面金标都在真 Linux 用户态复现。第十五批的两条根因:guest
init 补 `ip link set lo up`(回环 down 的级联雪崩:bind/connect
失败 → 解析器/套接字/worker 交付全挂);AF_INET6 编号 darwin 30 /
Linux 10 读侧归一(io_sockaddr_family、io_resolve_sa、
io_sockaddr_ip_str、resolve_sa_co pton 双臂;进 libc 的 domain 一律
原生值)。诊断探针存 tests/diag/probe_guest.zan(裸值人工对拍,非
电池夹具)。

crt-transition/linux/(第十四批起整目录退役,macOS 车道素来零引用):
- zanlinuxshims.c(558 行,已退役):曾垫 runtime 所引 macOS 面——
  __error、arc4random_buf、pthread_threadid_np、_NSGetExecutablePath、
  OSAtomic 原子、os_unfair_lock_*、mach_task_self_、
  mach_vm_read_overwrite、CommonCrypto 四件。第十四批把这张面整个
  搬进 runtime_core.zan 的 zan_* 目标无关符号(平台实现在 #if 内:
  musl 直调 gettid/getentropy/process_vm_readv/readlink,CAS 自旋锁
  走 [AtomicCas] 内建),发射器与 stdlib 同批改调,ELF 链接自此零
  项目 C。文件保留作史档,勿再扩展;摘要向量金标迁至
  tests/selfhost/native_digests(电池 + guest 默认夹具)。
- zanlinux_prelude.h:-include 注入的原型(zanstubs.c 的
  pthread_threadid_np 隐式声明在 musl 下是错误)。
- compat/net/if_dl.h:shadow <net/if_dl.h>(-I 优先)——musl 无
  AF_LINK,以字节精确的 sockaddr_ll 视图给 LLADDR/sdl_alen。
- zanstubs.c 的线程族(`_zan_thread_trampoline` naked asm + 
  `_zan_thread_trampoline_body`)自缺口五批次起只在 C 基线车道编出;
  Zan 车道由 runtime_core.zan 的 `[ThreadEntry] thread_trampoline`
  接管(ELF 符号无前缀,early 的 `--defsym` 别名随之删除)。
- v18o-22 车道衔接垫片(zanlinuxshims.c 追加,两笔):
  (1) GCD 信号量面——stdlib Threading 的 `#elif MACOS` 臂直呼
  dispatch_semaphore_*(条件定义 per-HOST,ELF 对象在 macOS 主机上
  编译时同一臂被编进;musl 无 libdispatch),musl 未命名 POSIX 信号量
  逐位给全 create/wait/signal/release/dispatch_time(-1=FOREVER,
  dispatch_time(0,delta)=单调 ns 绝对期限,超时返 49)。
  (2) `pthread_mutexattr_settype` 翻译——第十一批 monitor 按 Darwin
  实测硬编码 RECURSIVE=2,musl 的 2 是 ERRORCHECK(头文件实测
  NORMAL/DEFAULT=0、RECURSIVE=1、ERRORCHECK=2),静态链接同名定义
  遮挡档案副本,2→1 直接写低 4 位(attr 在调用面恒 calloc 全零);
  native_sync 的重入/跨条纹断言 guest 逐位仲裁通过(除 try_lock 臂)。
  try_lock 臂在 guest 全数返回 0:先在 Darwin open 常数家族
  (copen2 的 0x200=O_CREAT 在 aarch64 Linux 不是 O_CREAT,无
  O_CREAT 的 open 对新路径 ENOENT;cc91da8 同值证实,与 rt_core 的
  struct stat 布局同族)——per-TARGET 常数条件化仍是 known_open。
- 每夹具 guest 引导 ~7s;下载缓存于 build/linux-vehicle/dl(需网络,
  首次 ~60MB)。known_open 第一项(Linux 用户态宿主层)自此解锁,
  余下是覆盖面扩展(全电池 guest 化、plat_net_interfaces 的 MAC
  字段保真)。

## 缺口五批次(v18o-21):语言级函数指针 + [ThreadEntry] + C thunk 清算

编译器半边:委托值 bit0 定形态(oracle 双形态考古定案)——
`(委托类型)nint` 转换打 `|1` 裸标记,调用点 tbnz 分岔:裸形态剥标记后
按声明签名直接 blr(无 env),even 走 interned 成对读取;泛型 cast
解析 `(Ident<…>)operand`(配平 `>` 后必须是 `)`+操作数头);
`[ThreadEntry]` 方法属性:序言帧 +96 成对保存 x19-x28(libpthread 把
线程自指针放在被调者保存寄存器里,线程入口是 C→Zan 边界),收尾
`mov sp, x29` 后逐对恢复;async 注册改 BL `_zan_set_ready_hook`。
运行时半边(runtime_core.zan 第十批):set_ready_hook / io_resume(裸调
hook)/ thread_trampoline / thread_start / thread_current_id /
dns_worker_job / dns_pipe_ensure / dns_mu_ensure 全部 Zan 实现;
fcntl 声明修正为 Variadic=true(第三参 va_arg,纯声明会设错标志——
此项修复后 A/B 首次逐位一致);新增静态一律零值哨兵(库对象数据段恒
零,静态初始化器只对持有 Main 的对象生效)。永久金标 native_fnptr
入电池(42→43)。

独立验收(本仓库门禁,zanc_v21 = 闭包定点编译器):自举两路定点
(旧 seed×C 基线造 stage1 → Zan 道定点 run.xrUewG;终树 runtime 自举
闭包 run.cCLFzx,均 stage2.o == stage3.o 字节同一)、gate probes 绿、
默认电池 43/43 × 双配置(Zan 道 + C 基线道)、native_rt_core 双车道显
式绿、负例诊断逐字同旧编译器、crossboot 80/80(Mach-O + qemu ELF,
SEED 钉死)、linux_vehicle 默认 5/5 guest 逐字节、parity sweep
见 docs/native-parity-baseline.json v18o-21 记录。扩展 guest 集
(55 夹具,超出电池面)暴露的两处差异均经 cc91da8 worktree A/B 证为
先在状态:file 族的 struct stat 布局(musl 偏移不同,rt_core 第 25 行
起)与 guest 无 PATH 环境(cstring_read 第 2 行)——非本批回归;
plat_net_interfaces 的 MAC 保真与 Linux 常数条件化仍是 known_open。

## 第十一批(v18o-22):多线程正确性清算(真锁 + 真 RMW + 惰性初始化串行化)

编译器半边(占位体即文档的内建):`[AtomicCas]` → 单条 casal x1,x2,[x0]
(armv8.1 LSE,返回比较时刻旧值,oracle seen 语义逐位对齐,第六批
load-after-fail 文档化偏差退役)、`[AtomicLoad]` → ldar、`[AtomicStore]`
→ stlr(真 SEQ_CST)、`[StaticAddr("fld")]` → adrp+add 取本类静态数据
地址。运行时半边(runtime_core.zan 第十一批):monitor 从条纹计数器换
真 64 条纹递归 pthread 锁(缺口五起 DNS worker 真在跑,计数器不提供
互斥);rt_init_lk 唯一串行化点双检锁盖住九处惰性静态;三处自旋解锁
平铺 store 0 → barrier 减一;atomic load/store/CX 走内建;SIGPIPE 探测
入自旋锁;sockaddr 文本与网卡快照合并 rt_tls 每线程刮擦块。keep 账面
不变(136)。永久金标 native_sync(同锁两写方 5000 精确、递归重入、
跨条纹独立、CAS 环 2500 精确、worker EH 500、try_lock 代际表、线程 id
互异)入电池 43→44。

独立验收(本仓库门禁,zanc_v22 = 闭包定点编译器):自举两路定点
(v21×终树 stage1 → Zan 道定点 run.jHDotS;终树 runtime 自举闭包
run.4H0uBY,均 stage2.o == stage3.o 字节同一)、gate probes 绿、
默认电池 44/44 × 双配置、native_rt_core 双车道显式绿(旧金标逐字
不动)、负例诊断逐字同 v21、crossboot 80/80(SEED 钉死)、
linux_vehicle 默认 5/5 guest 逐字节;guest 扩展:16 夹具集 15 绿 +
native_sync 10/12 行绿(try_lock 臂 = 先在 O_CREAT 常数家族,cc91da8
同值证实)、27 kernels 全绿;parity sweep 见
docs/native-parity-baseline.json v18o-22 记录。

## 第十二批(v18o-23):内存管理清算(Span 视图零堆分配)

编译器半边:三个装箱点(`new Span<T>(b,n)`、`arr.AsSpan(...)`、
`span.Slice(...)`)的 malloc(16) {base,len} 堆箱全部退役——每构造点
在 ReserveSpanBoxes 预序预留私有 16B 帧槽,发射点按节点身份查表取
`x29±off`;同夹具 _malloc 重定位 33→18,视图箱无界泄漏账面清零。
值语义:Span 局部赋值/形参传递按 {base,len} 对拷贝(by-value,重绑不
出 callee);逃逸禁令六处响亮诊断(返回/字段/Lambda 捕获/容器元素/
ref out——C# ref-struct 规则;缺口二的静态 Span 读写能力随之退役,
全库零用量)。runtime_core 零源码行为改动即受益(183 处 new Span 站点
重编译后落帧槽)。永久金标 native_spans(拷贝 4040、形参下传+重绑
3408、热循环双站点 5,000,000、三元两臂 20/50、越界软钳 111 等)入
电池 44→45。

独立验收(本仓库门禁,zanc_v23 = 闭包定点编译器):自举两路定点
(v22×终树 stage1 → Zan 道定点 run.dWxIzk;终树 runtime 自举闭包
run.NwCSpH,均 stage2.o == stage3.o 字节同一)、gate probes 绿、
默认电池 45/45 × 双配置、native_rt_core 双车道显式绿、负例诊断逐字
同 v22、crossboot 80/80(SEED 钉死)、linux_vehicle 默认 5/5 + 27
kernels + fnptr/spans guest 逐字节(native_sync 仍 10/12,try_lock 臂
= 先在 O_CREAT 常数家族);parity sweep 见
docs/native-parity-baseline.json v18o-23 记录。

## 第十三批(v18o-24):最后一个 C 族清算(darwin 车道零项目 C)

runtime_core 第十三批(11→0):mem 六件(Copy/Fill/Compare/Find/
GetString/PutString)落 class zan(libc 走 zmemmove/zmemcmp/zmemchr
别名避开 ABI 撞名;copy 免重叠自转,fill/compare/find 带 n<=0 与空指针
卫);GetString/PutString 真实现([rc:8][0x5A414E53|len:8][data][NUL]
布局与编译器内联展开逐位一致);Win32 文本两件按 CP_UTF8 语义(非法
序列替 U+FFFD,合法 FFFD 不误替;sl/wl<0 计 NUL;零缓冲查尺寸;缓冲
不足返 0;宽侧代理对拆合)。EntryPoint 改名面收账:NativeMemory 六件 +
Interop.Wide/Process/Directory/Pinyin 全库自此不再声明任何裸名 C 导入。
zanstubs_rest.o 全局符号清零(纯 C 余量为空,物保留作账面);C 基线
双面(裸名 + zan_*)原样。自举机制入账:自举 SEED 优先用二进制旁的
stdlib 快照——旧定点旁是改名前快照,其 stage1 引用裸名、零 C 链接必
炸;解法是老种子 + 现行 stdlib 拷贝件作 SEED 一次自举,新定点的快照即
含全部改名。永久金标 native_memwide(重叠 memmove/回拷比较/memchr/
ARC 串长/Slice 视图/宽往返)入电池 45→46。

独立验收(本仓库门禁,zanc_v24 = 闭包定点编译器):自举两路定点
(v23×终树 → Zan 道定点 run.9iH4T1,RT_OBJS=runtime_core.o 单对象;
终树 runtime 自举闭包 run.AXTX2c,均 stage2.o == stage3.o 字节同一;
zanstubs_rest.o 全局符号 nm 实测 0)、gate probes 绿、默认电池 46/46
× 双配置(zero-C 单对象 + C 基线双对象;C 基线 native_sync 一次
w0lk=9 为 C 道遗留代际表竞态的统计命中——batch 11 串行化只在 Zan 道,
重跑 5/5 绿)、native_rt_core 双车道显式绿、负例诊断逐字同 v23、
crossboot 80/80(SEED 钉死)、linux_vehicle 默认 5/5 + 27 kernels +
fnptr/spans/memwide guest 逐字节(native_sync 仍 10/12,try_lock 臂 =
先在 O_CREAT 常数家族);parity sweep 573/624 平历史最高(om 3 =
fileinfoex_mmap + http_forwarder_stream + tdengine_pool,后两者定向
重跑即 pass,均 oracle 侧计时 flapper),见
docs/native-parity-baseline.json v18o-24 记录。

独立验收(本仓库门禁,zanc_v25 = 闭包定点编译器):自举定点
run.DTx5Ud(种子暂存法:run.B34Of5 stage2 + 现行 stdlib 拷贝件,
stage2.o == stage3.o 逐字节一次过);定点 stage2.o 未定义符号 nm 实测
恰为十个 zan_* 面(zan_md5/sha1/sha256/hmac_sha256/urandom/vm_read/
lock_enter/lock_exit/exe_path/thread_current_id),CC_/mach/OSAtomic/
_NSGetExecutablePath/pthread_threadid_np 零残留;默认电池 48/48 × 双
配置(Zan 单对象 + C 基线双对象,C 基线带同语义 zan_* 别名);
crossboot 80/80(stub.c 哨兵与实现同步换 zan_* 面,mach_task_self_
数据符号退役);linux_vehicle **零项目 C 链接**(zanstubs_rest/
zanhost_rest/zanlinuxshims 三对象自编链段删除)7/7 guest 逐字节
(默认五 + native_digests + native_memwide 新入默认集);负例诊断
逐字;runtime/fixture 对象级确定性逐字节。至此 darwin 与 linux 双
车道链接均为零项目 C:过渡期 C 运行时只剩 C 基线车道(A/B oracle)
与史档(zanstubs.c -U 面、zanlinuxshims.c)。
