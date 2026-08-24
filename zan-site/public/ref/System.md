# System

> 源码: `stdlib/System/Binding.zan`, `stdlib/System/ConsoleColor.zan`, `stdlib/System/DateTime.zan`, `stdlib/System/Exception.zan`, `stdlib/System/Guid.zan`, `stdlib/System/IDisposable.zan`, `stdlib/System/Interop.zan`, `stdlib/System/ListExtensions.zan`, `stdlib/System/MessageBox.zan`, `stdlib/System/NativeMemory.zan`, `stdlib/System/Random.zan`, `stdlib/System/StringExtensions.zan`, `stdlib/System/TaskJoin.zan`, `stdlib/System/TimeSpan.zan`, `stdlib/System/ZanVersion.zan`


## ArgumentException (class)

当传给方法的参数无效时抛出。

- public ArgumentException(string message)


## Binding (class)

到类型 T 值的双向数据绑定。

组件作者将可绑定属性声明为 `Binding<T>` 字段。
将字段左值赋给这类属性时，编译器会将其降级为
实时绑定（Get 读取字段，Set 回写）；赋
其他表达式则生成存储值的常量绑定。

class User { string name; }
input.data = user.name;   // 实时：model <-> UI
input.data = "hello";     // 常量：固定值，仍可设置

每次 Set 都会使 `version` 递增，控件可低成本检测写入。

- object target;

- BindGet<T> getter;

- BindSet<T> setter;

- bool live;

- T constVal;

- int version;

- T Get()
  - 绑定字段的当前值（live）或存储值（const）。

- void Set(T v)
  - 通过绑定写回（live 绑定时为 UI -> model）。

- int Version()
  - 通过此绑定执行的写入次数。

- bool IsLive()
  - 绑定到模型字段时为 true（区别于常量值）。


## Com (class)

无需 C shim 的 COM：接口指针即地址，方法存在于
对象的 vtable 中，槽位通过
delegate 转换变为可调用对象。

nint shell = Com.Create("00021401-0000-0000-C000-000000000046",
"00000000-0000-0000-C000-000000000046");
Com.Call1(shell, 5, argument);   // vtable 槽位 5，`shell` 作为 `this`
Com.Release(shell);

用 Zan 实现的 COM 对象（异步 COM API 的完成处理器都需要它）
通过 ComVtbl 构建：将回调地址写入 vtable
（从 `(nint)handler` 获取），并把对象交还回去。

- static nint Ole()
  - ole32 在运行时解析而非链接时解析：
    从不接触 COM 的程序既不会加载也不依赖它。

- static nint ole;

- static int comTls=Com.AllocComTls();

- static int AllocComTls()

- static int ComTls()

- static bool ComInitialized()

- static bool SetComInitialized(bool initialized)

- static int CoInit(nint reserved, int flags)

- static void CoUninit()

- static int CoCreate(nint clsid, nint outer, int ctx, nint iid, nint result)

- static void TaskFree(nint p)

- static int SlotQueryInterface()
  - IUnknown vtable 布局，所有 COM 接口共用。

- static int SlotAddRef()

- static int SlotRelease()

- static int Apartment()
  - COINIT_APARTMENTTHREADED——UI/WebView2 对象所需的模式。

- static int InProc()
  - CLSCTX_INPROC_SERVER.

- static bool Ok(int hr)

- static int Initialize()
  - 在调用线程上初始化 COM。S_FALSE 表示成功，且
    与一次 Shutdown 配对；apartment 模式变更仍视为失败。

- static void Shutdown()
  - 在调用线程上平衡本类成功的 Initialize。

- static nint Guid(string text)
  - 将 `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx` 解析为新建的
    16 字节 GUID（前三个字段为小端，符合 COM 预期）。

- static int Hex(string s, int off, int len)

- static int Digit(string ch)

- static void FreeGuid(nint g)

- static nint Create(string clsid, string iid)
  - 使用文本 GUID 调用 CoCreateInstance；类不可用时返回 0。

- static nint Slot(nint iface, int index)
  - 接口指针 vtable 槽位 `index` 处方法的地址。

- static int Call0(nint iface, int index)
  - 调用 vtable 槽位 `index`，以接口指针作为 `this`。
    槽位为空时返回 E_FAIL，缺失的方法会像调用失败一样
    优雅降级，而不是跳到地址零。

- static int Call1(nint iface, int index, nint a)

- static int Call2(nint iface, int index, nint a, nint b)

- static int Call3(nint iface, int index, nint a, nint b, nint c)

- static int Call4(nint iface, int index, nint a, nint b, nint c, nint d)

- static int Call5(nint iface, int index, nint a, nint b, nint c, nint d, nint e)

- static int Fail()
  - E_FAIL.

- static nint Get(nint iface, int index)
  - 调用单个参数为 out 指针的槽位，返回
    被调用者存入的值（失败时返回 0）——所有 COM getter 的通用形式。

- static string GetString(nint iface, int index)
  - 返回 CoTaskMemAlloc 分配宽字符串的 getter，封送后释放。

- static int GetInt(nint iface, int index)
  - 通过 out 指针返回 32 位值（BOOL、枚举、计数）的 getter。

- static nint Query(nint iface, string iid)
  - 以文本 IID 调用 QueryInterface；不支持时返回 0。

- static nint Keep(nint iface)
  - 对借用的接口指针（即回调收到的）
    持有引用，使其在调用结束后依然存活。

- static void Release(nint iface)

- static void Free(nint p)
  - 释放被调用者用 CoTaskMemAlloc 分配的缓冲区（字符串
    出参的常用约定）。

- static string TakeString(nint p)
  - 读取 CoTaskMemAlloc 分配的宽字符串出参并释放。


## ComVtbl (class)

用 Zan 实现的 COM 对象：`Add` 按 vtable 顺序追加方法地址
（从三个 IUnknown 槽位开始），`Build` 返回
可供原生代码回调的接口指针。

ComVtbl v = ComVtbl.Create(4);
v.Add((nint)Handler.QueryInterface);
v.Add((nint)Handler.AddRef);
v.Add((nint)Handler.Release);
v.Add((nint)Handler.Invoke);
nint handler = v.Build();

对象为 `{ vtable, state }`：`State()` 为调用者提供一个槽位，
用于存放其关联 id，静态 handler 可从中
通过传入的 `self` 指针读回。

- nint vtbl;

- nint obj;

- int slots;

- int count;

- static ComVtbl Create(int slots)

- ComVtbl Add(nint method)
  - 追加下一个 vtable 条目；超过 `slots` 的多余条目会被忽略。

- nint Build()
  - 分配对象（vtable 指针 + 一个状态字）并返回
    接口指针。

- void SetState(int v)
  - 由调用者持有、随对象携带的状态字。

- static int State(nint self)

- void Destroy()


## DateTime (class)

UTC 时刻，精度为 1 秒，存储为
Unix 纪元（1970-01-01 00:00:00 UTC）以来的秒数。

日历字段由 civil-from-days 算法推导，因此
该类完全跨平台，仅需 libc <c>time()</c> 来获取
当前时刻。`Now` 与 `UtcNow` 都返回 UTC；
本机时区的墙钟时间由 `LocalNow` /
`ToLocalTime` 给出，偏移取自 libc 的时区库
（`LocalOffsetMinutes`），因此含夏令时。

- [DllImport("crt")]static extern long time(nint ptr);

- [DllImport("crt", EntryPoint="localtime")]static extern nint plat_localtime(nint tptr);
  - 本机时区换算全部交给 libc 的时区库：localtime 把纪元秒展开成本地
    墙钟字段，再用 timegm（Windows 上是 _mkgmtime）把这组字段当作 UTC
    折回纪元秒，两者之差就是该时刻的实际偏移（自带夏令时）。

- [DllImport("crt", EntryPoint="_mkgmtime")]static extern long plat_timegm(nint tm);

- [DllImport("crt", EntryPoint="localtime")]static extern nint plat_localtime(nint tptr);

- [DllImport("crt", EntryPoint="timegm")]static extern long plat_timegm(nint tm);

- long epoch;

- int year;

- int month;

- int day;

- int hour;

- int minute;

- int second;

- int dayOfWeek;

- DateTime(long unixSeconds)
  - 根据 Unix 时间戳（秒，UTC）构造时刻。

- static DateTime UtcNow()
  - 当前时刻（UTC）。

- static DateTime Now()
  - `UtcNow` 的别名；不应用本地时区偏移。

- static int LocalOffsetMinutesAt(long unixSeconds)
  - 本机时区在 <paramref name="unixSeconds"/> 这一时刻相对 UTC 的偏移
    （分钟，东为正，如东八区 480），含该时刻是否处于夏令时。
    时区信息不可用时返回 0（等同 UTC）。

- static int LocalOffsetMinutes()
  - 本机时区当前相对 UTC 的偏移（分钟，东为正）。

- DateTime ToLocalTime()
  - 同一时刻的本地墙钟表示：年月日时分秒都是本机时区读数。
    注意它的 `ToUnixSeconds` 已被偏移，不再是纪元秒；
    需要纪元秒请用原来的 UTC 时刻。

- static DateTime LocalNow()
  - 本机时区的当前墙钟时间（见 `ToLocalTime`）。

- static DateTime FromUnixSeconds(long seconds)
  - 根据 Unix 时间戳（自纪元起的秒数）构造时刻。

- static DateTime FromDate(int year, int month, int day)
  - 给定日历日期当天的 UTC 午夜零点。

- static DateTime FromParts(int year, int month, int day, int hour, int minute, int second)
  - 给定的日历日期和墙上时钟时间，按 UTC 处理。

- static int DaysFromCivil(int year, int month, int day)
  - 公历日期到 Unix 纪元的天数——与构造器所用的
    civil-from-days 算法互为逆运算。
    1970 年之前的日期为负。

- static int DaysInMonth(int year, int month)
  - <paramref name="year"/> 年 <paramref name="month"/> 月的天数。

- static bool IsLeapYear(int year)
  - <paramref name="year"/> 是否为公历闰年。

- int Year()

- int Month()

- int Day()

- int Hour()

- int Minute()

- int Second()

- int DayOfWeek()
  - 星期几，0=星期日 .. 6=星期六。

- long ToUnixSeconds()
  - Unix 纪元以来的秒数（UTC）。

- DateTime AddSeconds(long n)

- DateTime AddMinutes(long n)

- DateTime AddHours(long n)

- DateTime AddDays(long n)

- TimeSpan Subtract(DateTime other)
  - 此时刻与 <paramref name="other"/> 之间的间隔。

- bool Equals(DateTime other)

- bool IsBefore(DateTime other)

- bool IsAfter(DateTime other)

- string ToString()
  - 类 ISO 文本："YYYY-MM-DD HH:MM:SS"。

- string ToDateString()
  - 仅日期："YYYY-MM-DD"。

- string ToTimeString()
  - 仅时间："HH:MM:SS"。

- static string Pad2(long n)
  - 将 0..99 的值补零到两位。

- static long FloorDivL(long a, long b)
  - 对 64 位秒数做向下取整的整数除法。

- static int FloorDiv(int a, int b)
  - 向下取整的整数除法（向负无穷取整）。

- static int FloorMod(int a, int b)
  - 向下取整的模运算；结果符号与除数一致。


## Exception (class)

所有异常的基础类型，携带人类可读的消息，通过
<c>throw new Exception("...")</c> 抛出，并由 <c>catch (Exception e)</c> 捕获。

- public string Message;

- public Exception(string message)

- public string ToString()


## FileNotFoundException (class)

当必须存在的文件无法打开时抛出。

- public FileNotFoundException(string message)


## Guid (class)

UUID（GUID）的生成与解析。

string id = Guid.New();          // e.g. "f47ac10b-58cc-4372-a567-0e02b2c3d479"
Guid g = Guid.Parse(id);         // round-trips
bool same = Guid.Equals(g, g2);

- static string ToString(Guid g)
  - `g` 的规范形式：36 字符小写（"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"）。

- static Guid Parse(string text)
  - 解析 36 字符的规范 GUID（允许连字符，不区分大小写）。
    文本不是合法的 GUID 时返回 null。

- static Guid NewV4()
  - 使用操作系统 CSPRNG 生成 version-4（随机）UUID：
    122 个随机位，版本号 0100，变体位 10。
    仅当操作系统 RNG 失败时才返回 null。

- static string New()
  - 快捷方法：返回新的随机 GUID 字符串，RNG 失败时返回 ""。

- static bool Equals(Guid a, Guid b)
  - 两个 GUID 相等时返回 true（可安全处理 null）。

- static string Format(byte[]b)
  - 将 16 字节格式化为规范 GUID 字符串。

- static int CodeOf(string ch)

- public string text;


## HttpRequestException (class)

当 HTTP 请求无法发送或完成时抛出
（连接失败、TLS 握手失败）。

- public HttpRequestException(string message)


## IOException (class)

当 I/O 操作（打开、读取、写入、复制）失败时抛出。

- public IOException(string message)


## Interop (class)

动态原生链接：在运行时解析库及其入口点，
并通过 delegate 调用，而不声明 `[DllImport]`
（加载时即需存在）。

nint user32 = Interop.Load("user32.dll");
nint addr   = Interop.Symbol(user32, "MessageBoxW");
MessageBoxW box = (MessageBoxW)addr;      // 地址 -> 可调用
box(0, Wide.Of("hi"), Wide.Of("zan"), 0);

`(Delegate)address` 转换及其逆操作 `(nint)handler` 是编译器的
原语，因此可选依赖（一个可能未安装的运行时）
无需 C shim：先探测，缺失时回退即可。

- [DllImport("kernel32", EntryPoint="LoadLibraryA")]static extern nint WinLoad(string name);

- [DllImport("kernel32", EntryPoint="GetProcAddress")]static extern nint WinSymbol(nint mod, string name);

- [DllImport("kernel32", EntryPoint="FreeLibrary")]static extern int WinUnload(nint mod);

- [DllImport("kernel32", EntryPoint="GetLastError")]static extern int WinLastError();

- [DllImport("crt", EntryPoint="dlopen")]static extern nint PosixLoad(string name, int flags);

- [DllImport("crt", EntryPoint="dlsym")]static extern nint PosixSymbol(nint mod, string name);

- [DllImport("crt", EntryPoint="dlclose")]static extern int PosixUnload(nint mod);

- [DllImport("crt", EntryPoint="dlerror")]static extern string PosixError();

- static int RtldNowLocal()
  - RTLD_NOW | RTLD_LOCAL 的整数值：Linux/musl 上 RTLD_LOCAL 为 0
    （RTLD_NOW=2），Darwin 上 RTLD_LOCAL=4（RTLD_NOW=2），所以
    不能只写一次硬编码常量。

- static string lastError="";

- static string LastError()
  - 上次失败加载的原生加载器错误文本（dlerror / Win32 错误码）；
    没有失败时为空字符串。

- static nint Load(string name)
  - 按名称或路径加载共享库；不存在时返回 0。

- static nint Symbol(nint mod, string name)
  - 导出入口点的地址；库中没有时返回 0
    （一种以 ABI 兼容方式探测新运行时特性的做法）。

- static void Unload(nint mod)

- static nint Entry(string name, string entry)
  - 一步加载 `name` 并解析 `entry`；任一失败返回 0。

- static extern string getenv(string name);

- static string EnvVar(string name)
  - 从活动进程块读取环境变量，未设置时返回
    空字符串。


## InvalidOperationException (class)

当操作对对象当前
状态无效时抛出（例如对空序列调用 First()）。

- public InvalidOperationException(string message)


## JsonException (class)

当 JSON 文本格式错误时抛出（JsonValue.Parse）。

- public JsonException(string message)


## ListExtensions (class)

编译器不会原生降低的 List<T> 操作。

以扩展方法（`this List<T>` 接收方）的形式编写，以便
像成员一样调用：`items.Remove(x)`。此前这些调用会被编译成
常量 0 / 静默空操作，把普通的 Remove 或 Sort 变成
一个没有诊断信息的 bug。

- static bool Remove<T>(this List<T> src, T item)
  - 移除第一个等于 `item` 的元素；返回是否
    确实移除了某个元素。

- static List<T> GetRange<T>(this List<T> src, int start, int count)
  - 从 `start` 开始的 `count` 个元素的副本。范围会
    被限制在列表范围内。

- static void Sort(this List<int> src)
  - 原地升序排序整数。

- static void Sort(this List<double> src)
  - 原地升序排序数值。

- static void Sort(this List<string> src)
  - 原地按序数升序排序字符串。


## MessageBox (class)

桌面提示框。Windows 用 MessageBoxA；Linux 用 zenity 或 kdialog；
macOS 用 osascript 的 display dialog。返回值沿用 Win32 的
IDOK=1 / IDCANCEL=2 / IDYES=6 / IDNO=7，因此三个平台的判断代码一致。

提示文本与标题都通过临时文件/参数文件传给后端，不拼进 shell 命令，
因此内容里的引号和 `$(...)` 不会被解释。

- [DllImport("user32")]static extern int MessageBoxA(nint hwnd, string text, string caption, int flags);

- static int Ok()
  - IDOK。

- static int Cancel()
  - IDCANCEL。

- static int Yes()
  - IDYES。

- static int No()
  - IDNO。

- static int Show(string text, string caption)
  - 只有"确定"的提示框；始终返回 IDOK。

- static int ShowYesNo(string text, string caption)
  - 是/否提示框；返回 IDYES 或 IDNO。

- static int ShowOkCancel(string text, string caption)
  - 确定/取消提示框；返回 IDOK 或 IDCANCEL。

- static bool Posix(string text, string caption, int kind)
  - kind: 0 = 只有确定，1 = 是/否，2 = 确定/取消。
    返回用户是否选择了肯定按钮。

- static bool Have(string tool)

- static string Sanitize(string s)
  - 标题要进单引号命令行，因此去掉单引号（标题是短标签，
    丢掉引号比冒被解释的风险好）。

- static string OsaQuote(string s)

- static bool RunOsa(string script)


## NativeMemory (class)

二进制 IO 用的堆外原始内存（ByteBuffer、数据库页、网络
组帧）。地址是普通 nint，不进入 ARC 对象
机制，因此热路径的字节级循环开销与等效的 C 代码相同。

这里每个方法都是编译器内建函数：这些声明是为了
让检查器能对调用做类型检查（ARC 也能跟踪 GetString 返回的字符串），
但 irgen 将每个调用降低为单个 libc 调用——所命名的
库实际上从未被调用或链接。

标量访问是 <c>Span<T></c> 的职责：<c>new Span<int>(p, n)</c>
将原始地址视图化，并以小端、无对齐要求的
类型化读写进行索引。

安全约定：地址必须来自 `Alloc`（或
ByteBuffer.Raw() 之类的固定缓冲区），并且不能在
`Free` 之后继续使用。偏移/长度不做边界检查——由调用方
负责边界，与任何 FFI 缓冲区一样。

- [DllImport("crt")]static extern nint Alloc(int size);
  - 分配 `size` 字节并清零；返回其地址。

- [DllImport("crt")]static extern void Free(nint p);
  - 释放从 Alloc 获得的地址。释放 0 为无操作。

- [DllImport("crt")]static extern void Copy(nint dst, nint src, int n);
  - 将 n 字节从 src 复制到 dst（允许重叠，memmove 语义）。

- [DllImport("crt")]static extern void Fill(nint p, int v, int n);
  - 用 v 的低 8 位填充 p 处的 n 字节。

- [DllImport("crt")]static extern int Compare(nint a, nint b, int n);
  - memcmp：a 与 b 前 n 字节的比较序 <0 / 0 / >0。

- [DllImport("crt")]static extern int Find(nint p, int off, int b, int n);
  - memchr：在 p+off 起的 n 字节内查找字节 b（取低 8 位），
    命中返回相对 p 的偏移，未命中返回 -1。

- [DllImport("crt")]static extern string GetString(nint p, int off, int len);
  - 将 p+off 处的 len 字节复制为新的 ARC 托管字符串。

- [DllImport("crt")]static extern void PutString(nint p, int off, string s, int len);
  - 将 s 的前 len 字节复制到 p+off（不写 NUL 终止符）。

- [DllImport("crt")]static extern int Crc32(nint p, int len);
  - p 处 len 字节的 CRC-32（IEEE，poly 0xEDB88320）。


## PlatformNotSupportedException (class)

当跨平台 API 在当前平台
没有实现时抛出。接口在所有平台都存在，因此代码各处都能编译运行；
不支持的那一半会显式报错，而不是静默
不做事。

- public PlatformNotSupportedException(string message)


## Pump (class)

嵌套的 Win32 消息循环。异步 COM 完成以投递消息的形式
到达调用（apartment）线程，因此等待完成就需要
泵消息：同步包装器通过 `Pump.Until(flag, 5000)` 等待
回调而不死锁 UI 线程。

- static nint user32;

- static nint peek;

- static nint translate;

- static nint dispatch;

- static nint sleep;

- static void Resolve()

- static void Drain()
  - 排空当前线程上所有已排队的消息。

- static bool Until(PumpGate gate, int timeoutMs)
  - 泵消息直到 `gate` 报告完成或超过 `timeoutMs`；
    闸门打开时返回 true。


## PumpGate (class)

回调设置、Pump.Until() 等待的完成标志。

- bool done;

- PumpGate()

- void Signal()

- bool IsDone()

- void Reset()


## Random (class)

通用（非加密）伪随机数生成器。
使用 64 位线性同余序列（Knuth MMIX 常数）；
速度快，可由种子复现，但绝不能用于安全用途——参见
<c>System.Security.Cryptography.RandomNumberGenerator</c>。

- [DllImport("crt")]static extern long time(nint ptr);

- long state;

- Random()
  - 以当前时间作为种子。

- static Random Seeded(long seed)
  - 用显式种子创建生成器（可复现）。

- long NextRaw()
  - 推进状态并返回原始 64 位结果。

- int Next()
  - 非负伪随机整数。

- int NextBelow(int bound)
  - [0, bound) 区间内的非负整数。

- int Between(int lo, int hi)
  - [lo, hi) 区间内的整数。

- double NextDouble()
  - [0.0, 1.0) 区间内的 double。

- bool NextBool()
  - 伪随机布尔值。


## RegexBudgetException (class)

当正则匹配用尽回溯步骤预算时抛出。病态模式
（`(a+)+$` 之类）在某些输入上代价会爆炸，引擎宁可报错也不把
超时伪装成"无匹配"。

- public RegexBudgetException(string message)


## SocketException (class)

当套接字操作失败时抛出（创建、绑定、监听、
连接失败），消息携带平台错误码（Windows 的
WSAGetLastError / POSIX 的 errno）。<paramref name="code"/> 以
数值形式携带同一错误码，供程序检查而非解析消息文本。

- public int code;

- public SocketException(int code, string message)


## StringExtensions (class)

编译器不会原生简化的字符串操作。

它们是扩展方法（`this string` 接收者），因此调用起来
与普通成员一样：`name.PadLeft(8)`。这里的每个名字以前都会编译成
常量 0（该成员根本不存在），所以 C# 风格的代码
会静默产生垃圾结果；把它们实现为真正的 Zan 代码，可以让
编译器、IDE 和语言服务器共用同一套实现。

- static bool IsNullOrEmpty(this string s)
  - 与 C# 的 `string.IsNullOrEmpty` 一致：字符串为 null 或
    长度为 0 时返回 true。先判 null（对 null 的引用比较是安全的），
    因此对 null 字符串调用它不会解引用，也就不会崩溃。用它替代
    `s == ""`——后者会在原生层对空指针做字符串比较直接崩溃。

- static bool IsNullOrWhiteSpace(this string s)
  - 与 C# 的 `string.IsNullOrWhiteSpace` 一致：null、空串或
    仅由空白字符组成时返回 true。

- static string PadLeft(this string s, int width)
  - 在 `width` 个空格宽的字段中右对齐字符串。

- static string PadLeft(this string s, int width, string pad)
  - 右对齐字符串，用 `pad` 填充（取其第一个
    字符；`pad` 为空时原样返回字符串）。

- static string PadRight(this string s, int width)
  - 在 `width` 个空格宽的字段中左对齐字符串。

- static string PadRight(this string s, int width, string pad)
  - 左对齐字符串，用 `pad` 填充（取其第一个
    字符；`pad` 为空时原样返回字符串）。

- static string TrimStart(this string s)
  - 返回去掉前导空白字符的副本。

- static string TrimEnd(this string s)
  - 返回去掉尾部空白字符的副本。

- static string Insert(this string s, int index, string text)
  - 在 `index` 处插入 `value` 的副本。越界的索引
    会被钳制到字符串两端。

- static string Remove(this string s, int start)
  - 返回删除自 `start` 起全部字符的副本。

- static string Remove(this string s, int start, int count)
  - 返回从 `start` 处删除 `count` 个字符的副本。

- static int CompareTo(this string s, string other)
  - 序号比较：返回负数、0 或正数，同 C# 的
    string.CompareTo。

- static bool EqualsOrdinal(this string s, string other)
  - 序号相等比较（字符串的 `==` 本身也按值比较；
    此方法是为了让 C# 风格的代码继续可用）。

- static List<string> ToCharList(this string s)
  - 把字符串拆成单字符字符串列表。Zan 没有
    char[] 值类型，因此这是 C# ToCharArray 的
    List<string> 等价实现。

- static int CountOf(this string s, string needle)
  - `needle` 不重叠出现的次数。


## TaskJoin (class)

扇出汇合（fan-out join）：<c>Task.WhenAll</c> / <c>Task.WhenAny</c> 的
实现。两者不是编译器内建——解析器只把接收者 <c>Task</c> 改写为
<c>TaskJoin</c>（见 parser.c 的 desugar_task_join），因此汇合本身就是
普通的 async 标准库代码，像其他被 await 的调用一样挂起与恢复。

List<long> hs = new List<long>();
hs.Add(Task.Spawn(Work(1)));
hs.Add(Task.Spawn(Work(2)));
int n = await Task.WhenAll(hs);      // 全部完成后返回句柄个数
int i = await Task.WhenAny(hs);      // 最先完成者的下标

完成性由 <c>Task.IsDone(handle)</c> 观察（协程完成或被取消后为真），
等待期间用 <c>Task.Delay</c> 让出：汇合方挂起到 reactor，不占线程、
也不忙等。轮询间隔从 1 毫秒起指数退避到 4 毫秒——短任务几乎立即
汇合，长任务不会每毫秒都唤醒一次。

- static int MAX_BACKOFF=4;
  - 轮询退避上限（毫秒）。

- static async int WhenAll(List<long> handles)
  - 挂起直到 `handles` 中每个句柄都已完成（或被取消），
    返回汇合的句柄个数。空列表立即返回 0，已完成的批次
    不会挂起。

- static async int WhenAny(List<long> handles)
  - 挂起直到 `handles` 中任一句柄完成，返回其下标；
    同一轮里多个都已完成时返回最小下标。空列表返回 -1。
    其余协程继续运行——需要一起收尾就先
    `CancelAll` 再 `WhenAll`。

- static void CancelAll(List<long> handles)
  - 请求取消每个句柄（已完成的句柄无副作用）。取消是
    协作式的：协程在下一个挂起点观察到请求，因此取消后通常
    紧跟一次 <c>WhenAll</c> 等它们真正收尾。

- static int DoneCount(List<long> handles)
  - `handles` 中已完成的句柄个数（不挂起）。


## TimeSpan (class)

一秒精度的时间间隔，以带符号的
总秒数存储。由 `DateTime.Subtract` 或
<c>From*</c> 工厂方法产生。

- long total;

- TimeSpan(long seconds)
  - 从带符号的秒数构造一个时间间隔。

- static TimeSpan FromSeconds(long s)

- static TimeSpan FromMinutes(long m)

- static TimeSpan FromHours(long h)

- static TimeSpan FromDays(long d)

- long TotalSeconds()

- long TotalMinutes()

- long TotalHours()

- long TotalDays()

- int Days()

- int Hours()

- int Minutes()

- int Seconds()

- bool IsNegative()
  - 时间间隔为负时返回 true。

- TimeSpan Add(TimeSpan other)

- TimeSpan Subtract(TimeSpan other)

- TimeSpan Negate()

- string ToString()
  - 文本格式为 "[-][D.]HH:MM:SS"。


## Wide (class)

针对 Win32/COM 宽字符（`W`）接口的 UTF-16 封送。
Zan 字符串是 UTF-8，因此宽字符参数是显式持有的缓冲区：

nint w = Wide.Of("https://example.com");
nav(webview, w);
Wide.Free(w);

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int ToWide(int page, int flags, nint mb, int mbLen, nint wide, int wideLen);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int ToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- static int Utf8()

- static nint Of(string s)
  - 分配 `s` 的 NUL 结尾 UTF-16 副本。用 Free() 释放。

- static string Read(nint p)
  - 将 NUL 结尾的 UTF-16 缓冲区复制回 Zan 字符串。

- static int Length(nint p)
  - 宽字符缓冲区中 NUL 终止符前的 UTF-16 码元数量。

- static void Free(nint p)


## ZanVersion (class)

本标准库随附的工具链版本，程序可直接打印
它（服务启动横幅、关于对话框、`--version`
输出）而无需查询构建系统。

- static string Text()
  - 点分格式的发布版本字符串，例如 "0.2.0"。


## T (delegate)

双向数据绑定背后的存取器对：读取/写入
`target` 的某个字段。当字段左值被赋给 `Binding<T>` 类型的槽时，
编译器会合成匹配的函数（如 `input.data = user.name;`）。

`delegate T BindGet<T>(object target);`


## int (delegate)

`delegate int GetEnvironmentVariableWFn(nint name, nint buf, int size);`


## int (delegate)

`delegate int PeekMessageFn(nint msg, nint hwnd, int min, int max, int remove);`


## int (delegate)

`delegate int TranslateMessageFn(nint msg);`


## int (delegate)

`delegate int SleepFn(int ms);`


## int (delegate)

vtable 方法的调用形式：每个 COM 方法都把接口
指针作为第一个参数并返回 HRESULT，宽于
寄存器的参数（RECT、结构体出参）以地址传递。

`delegate int ComCall0Fn(nint self);`


## int (delegate)

`delegate int ComCall1Fn(nint self, nint a);`


## int (delegate)

`delegate int ComCall2Fn(nint self, nint a, nint b);`


## int (delegate)

`delegate int ComCall3Fn(nint self, nint a, nint b, nint c);`


## int (delegate)

`delegate int ComCall4Fn(nint self, nint a, nint b, nint c, nint d);`


## int (delegate)

`delegate int ComCall5Fn(nint self, nint a, nint b, nint c, nint d, nint e);`


## int (delegate)

`delegate int CoInitializeExFn(nint reserved, int flags);`


## int (delegate)

`delegate int TlsAllocFn();`


## int (delegate)

`delegate int TlsSetValueFn(int index, nint data);`


## int (delegate)

`delegate int CoCreateInstanceFn(nint clsid, nint outer, int ctx, nint iid, nint result);`


## nint (delegate)

`delegate nint DispatchMessageFn(nint msg);`


## nint (delegate)

`delegate nint TlsGetValueFn(int index);`


## void (delegate)

`delegate void BindSet<T>(object target, T v);`


## void (delegate)

`delegate void CoUninitializeFn();`


## void (delegate)

`delegate void CoTaskMemFreeFn(nint p);`


## ConsoleColor (enum)

16 种标准控制台颜色，顺序（0..15）与 C# 的
System.ConsoleColor 一致。可赋给 Console.ForegroundColor 或
Console.BackgroundColor；运行时将其映射为 ANSI SGR 转义序列。

- Black

- DarkBlue

- DarkGreen

- DarkCyan

- DarkRed

- DarkMagenta

- DarkYellow

- Gray

- DarkGray

- Blue

- Green

- Cyan

- Red

- Magenta

- Yellow

- White


## IDisposable (interface)

- void Dispose();
