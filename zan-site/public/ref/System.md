# System

> 源码: `packages/Zan.Desktop/src/System/Audio.zan`, `stdlib/System/AppPath.zan`, `stdlib/System/Binding.zan`, `stdlib/System/ConsoleColor.zan`, `stdlib/System/Convert.zan`, `stdlib/System/DateTime.zan`, `stdlib/System/Environment.zan`, `stdlib/System/Exception.zan`, `stdlib/System/Guid.zan`, `stdlib/System/IDisposable.zan`, `stdlib/System/Interop.zan`, `stdlib/System/ListExtensions.zan`, `stdlib/System/MemoryExtensions.zan`, `stdlib/System/NativeMemory.zan`, `stdlib/System/OperatingSystem.zan`, `stdlib/System/Random.zan`, `stdlib/System/RandomNumberGenerator.zan`, `stdlib/System/Stopwatch.zan`, `stdlib/System/StringExtensions.zan`, `stdlib/System/TaskJoin.zan`, `stdlib/System/TimeSpan.zan`, `stdlib/System/ZanVersion.zan`


## AppPath (class)

- [DllImport("kernel32", EntryPoint="GetModuleFileNameW")]static extern int GetModuleFileNameW(nint module, nint buf, int size);

- [DllImport("kernel32", EntryPoint="GetCurrentProcessId")]static extern int GetCurrentProcessId();

- [DllImport("crt")]static extern int readlink(string path, byte[]buf, int size);

- [DllImport("crt", EntryPoint="getpid")]static extern int GetPid();

- [DllImport("crt", EntryPoint="_NSGetExecutablePath")]static extern int NSGetExecutablePath(nint buf, nint size);

- static string Exe()

- static int Pid()


## ArgumentException (class)

- public ArgumentException(string message)


## ArgumentNullException (class)

- public ArgumentNullException(string paramName):base("Value cannot be null.(Parameter '"+paramName+"')")


## ArgumentOutOfRangeException (class)

- public ArgumentOutOfRangeException(string message):base(message)


## Audio (class)

- static bool Open()

- static bool IsOpen()

- static void SetVolume(double volume)

- static double Volume()

- static string DriverName()

- static int ActiveVoices()

- static void StopAll()

- static void Close()

- static string LastError()


## Binding (class)

- object target;

- BindGet<T> getter;

- BindSet<T> setter;

- bool live;

- T constVal;

- int version;

- T Get()

- void Set(T v)

- int Version()

- bool IsLive()


## Com (class)

- static nint Ole()

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

- static int SlotAddRef()

- static int SlotRelease()

- static int Apartment()

- static int InProc()

- static bool Ok(int hr)

- static int Initialize()

- static void Shutdown()

- static nint Guid(string text)

- static int Hex(string s, int off, int len)

- static int Digit(string ch)

- static void FreeGuid(nint g)

- static nint Create(string clsid, string iid)

- static nint Slot(nint iface, int index)

- static int Call0(nint iface, int index)

- static int Call1(nint iface, int index, nint a)

- static int Call2(nint iface, int index, nint a, nint b)

- static int Call3(nint iface, int index, nint a, nint b, nint c)

- static int Call4(nint iface, int index, nint a, nint b, nint c, nint d)

- static int Call5(nint iface, int index, nint a, nint b, nint c, nint d, nint e)

- static int Fail()

- static nint Get(nint iface, int index)

- static string GetString(nint iface, int index)

- static int GetInt(nint iface, int index)

- static nint Query(nint iface, string iid)

- static nint Keep(nint iface)

- static void Release(nint iface)

- static void Free(nint p)

- static string TakeString(nint p)


## ComVtbl (class)

- nint vtbl;

- nint obj;

- int slots;

- int count;

- static ComVtbl Create(int slots)

- ComVtbl Add(nint method)

- nint Build()

- void SetState(int v)

- static int State(nint self)

- void Destroy()


## Convert (class)

- static string BASE64_ALPHA()

- static string ToBase64String(byte[]inArray)

- static int DecB64(int c)

- static byte[]FromBase64String(string s)

- static string ToHexString(byte[]inArray)

- static string ToHexString(byte[]inArray, int offset, int length)

- static int Nibble(int c)

- static byte[]FromHexString(string hex)


## DateTime (class)

- [DllImport("crt")]static extern long time(nint ptr);

- [DllImport("kernel32", EntryPoint="GetSystemTimeAsFileTime")]static extern void GetSystemTimeAsFileTime(nint lpFileTime);

- [DllImport("crt", EntryPoint="clock_gettime")]static extern int clock_gettime(int clkId, nint tp);

- [DllImport("crt", EntryPoint="_localtime64_s")]static extern int plat_localtime(nint tmOut, nint tptr);

- [DllImport("crt", EntryPoint="_mkgmtime")]static extern long plat_timegm(nint tm);

- [DllImport("crt", EntryPoint="localtime_r")]static extern nint plat_localtime(nint tptr, nint tmOut);

- [DllImport("crt", EntryPoint="timegm")]static extern long plat_timegm(nint tm);

- long epochMs;

- long epoch;

- int year;

- int month;

- int day;

- int hour;

- int minute;

- int second;

- int millisecond;

- int dayOfWeek;

- static long GetCurrentUnixMilliseconds()

- DateTime(long unixSeconds):this(unixSeconds*1000, 0)

- DateTime(long unixMs, int dummy)

- static DateTime UtcNow()

- static DateTime Now()

- static int LocalOffsetMinutesAt(long unixSeconds)

- static int LocalOffsetMinutes()

- DateTime ToLocalTime()

- static DateTime LocalNow()

- static DateTime FromUnixSeconds(long seconds)

- static DateTime FromUnixMilliseconds(long milliseconds)

- static DateTime FromDate(int year, int month, int day)

- static DateTime FromParts(int year, int month, int day, int hour, int minute, int second)

- static int DaysFromCivil(int year, int month, int day)

- static void CivilFromDays(int days, out int year, out int month, out int day)

- static int DaysInMonth(int year, int month)

- static bool IsLeapYear(int year)

- int Year()

- int Month()

- int Day()

- int Hour()

- int Minute()

- int Second()

- int DayOfWeek()

- int Millisecond()

- long ToUnixSeconds()

- long ToUnixMilliseconds()

- DateTime AddMilliseconds(long ms)

- DateTime AddSeconds(long n)

- DateTime AddMinutes(long n)

- DateTime AddHours(long n)

- DateTime AddDays(long n)

- TimeSpan Subtract(DateTime other)

- bool Equals(DateTime other)

- bool IsBefore(DateTime other)

- bool IsAfter(DateTime other)

- string ToString()

- string ToStringWithMillis()

- string ToDateString()

- string ToTimeString()

- static DateTime ParseDate(string value)

- static int ParseDigits(string s, int at, int count)

- static int Digit(string c)

- static string Pad2(long n)

- static long FloorDivL(long a, long b)

- static int FloorDiv(int a, int b)

- static int FloorMod(int a, int b)


## Environment (class)

- [DllImport("kernel32", EntryPoint="SetEnvironmentVariableW")]static extern int SetEnvironmentVariableW(nint name, nint val);

- [DllImport("crt", EntryPoint="setenv")]static extern int setenv(string name, string val, int overwrite);

- [DllImport("crt", EntryPoint="unsetenv")]static extern int unsetenv(string name);

- [DllImport("crt", EntryPoint="exit")]static extern void crt_exit(int code);

- public static int ProcessId { get }

- public static string ProcessPath { get }

- public static string NewLine { get }

- public static string GetEnvironmentVariable(string variable)

- public static void SetEnvironmentVariable(string variable, string value)

- public static void Exit(int exitCode)

- public static long TickCount64 { get }


## Exception (class)

- public string Message;

- public Exception InnerException;

- public Exception(string message)

- public Exception(string message, Exception innerException)

- public string ToString()


## FileNotFoundException (class)

- public FileNotFoundException(string message)


## Guid (class)

- static string ToString(Guid g)

- static Guid Parse(string text)

- static Guid NewV4()

- static string New()

- static bool Equals(Guid a, Guid b)

- static string Format(byte[]b)

- static int CodeOf(string ch)

- public string text;


## HttpRequestException (class)

- public HttpRequestException(string message)


## IOException (class)

- public IOException(string message)


## Interop (class)

- [DllImport("kernel32", EntryPoint="LoadLibraryA")]static extern nint WinLoad(string name);

- [DllImport("kernel32", EntryPoint="GetProcAddress")]static extern nint WinSymbol(nint mod, string name);

- [DllImport("kernel32", EntryPoint="FreeLibrary")]static extern int WinUnload(nint mod);

- [DllImport("kernel32", EntryPoint="GetLastError")]static extern int WinLastError();

- [DllImport("crt", EntryPoint="dlopen")]static extern nint PosixLoad(string name, int flags);

- [DllImport("crt", EntryPoint="dlsym")]static extern nint PosixSymbol(nint mod, string name);

- [DllImport("crt", EntryPoint="dlclose")]static extern int PosixUnload(nint mod);

- [DllImport("crt", EntryPoint="dlerror")]static extern string PosixError();

- static int RtldNowLocal()

- static string lastError="";

- static string LastError()

- static nint Load(string name)

- static nint Symbol(nint mod, string name)

- static void Unload(nint mod)

- static nint Entry(string name, string entry)

- static extern string getenv(string name);

- static string EnvVar(string name)


## InvalidOperationException (class)

- public InvalidOperationException(string message)


## JsonException (class)

- public JsonException(string message)


## ListExtensions (class)

- static bool Remove<T>(this List<T> src, T item)

- static List<T> GetRange<T>(this List<T> src, int start, int count)

- static void Sort(this List<int> src)

- static void QuickSortInt(List<int> a, int lo, int hi)

- static void Sort(this List<double> src)

- static void QuickSortDouble(List<double> a, int lo, int hi)

- static void Sort(this List<string> src)

- static void QuickSortString(List<string> a, int lo, int hi)

- static void Sort<T>(this List<T> src, Comparison<T> comparison)

- static void QuickSortCustom<T>(List<T> a, int lo, int hi, Comparison<T> cmp)


## MemoryExtensions (class)

- public static bool SequenceEqual(this byte[]first, byte[]second)

- public static int IndexOf(this byte[]source, byte value, int startIndex, int count)

- public static int IndexOf(this byte[]source, byte value, int startIndex)

- public static int IndexOf(this byte[]source, byte value)

- public static int LastIndexOf(this byte[]source, byte value)

- public static bool Contains(this byte[]source, byte value)

- public static bool IsAscii(this byte[]source, int offset, int length)

- public static bool IsAscii(this byte[]source)

- public static int IndexOfAny(this byte[]source, byte value1, byte value2, int startIndex, int count)

- public static int IndexOfAny(this byte[]source, byte value1, byte value2, int startIndex)

- public static int IndexOfAny(this byte[]source, byte value1, byte value2)

- public static void ToUpperAscii(this byte[]source, int offset, int length)

- public static void ToUpperAscii(this byte[]source)

- public static void ToLowerAscii(this byte[]source, int offset, int length)

- public static void ToLowerAscii(this byte[]source)

- public static string ToHexString(this byte[]source)

- public static string ToHexString(this byte[]source, int offset, int length)


## NativeMemory (class)

- [DllImport("crt")]static extern nint Alloc(int size);

- [DllImport("crt")]static extern void Free(nint p);

- [DllImport("crt")]static extern void Copy(nint dst, nint src, int n);

- [DllImport("crt")]static extern void Copy2D(nint dst, int dstStride, nint src, int srcStride, int rowBytes, int height);

- [DllImport("crt")]static extern void Fill(nint p, int v, int n);

- [DllImport("crt")]static extern int Compare(nint a, nint b, int n);

- [DllImport("crt")]static extern int Find(nint p, int off, int b, int n);

- [DllImport("crt")]static extern string GetString(nint p, int off, int len);

- [DllImport("crt")]static extern void PutString(nint p, int off, string s, int len);

- [DllImport("crt")]static extern long AsI64(double value);

- [DllImport("crt")]static extern double AsF64(long bits);

- [DllImport("crt")]static extern int Crc32(nint p, int len);

- [DllImport("crt")]static extern int Crc32C(nint p, int len);

- [DllImport("crt")]static extern long Crc32CUpdate(long crc, nint p, long len);

- [DllImport("crt")]static extern string Sha256(nint p, int len);

- [DllImport("crt")]static extern string Sha1(nint p, int len);

- [DllImport("crt")]static extern string Sha512(nint p, int len);

- [DllImport("crt")]static extern long AesCbcEncrypt(nint dst, nint src, long size, nint key, int keybits, nint iv);

- [DllImport("crt")]static extern long AesCbcDecrypt(nint dst, nint src, long size, nint key, int keybits, nint iv);

- [DllImport("crt")]static extern long AesEcbBlock(nint key, int keybits, nint in16, nint out16);

- [DllImport("crt")]static extern long AesCtrCrypt(nint dst, nint src, long size, nint key, int keybits, nint counter);

- [DllImport("crt")]static extern long GhashBlock(nint h, nint x, nint y);

- [DllImport("crt")]static extern long GhashUpdate(nint h, nint data, long len, nint y);

- [DllImport("crt")]static extern long AesGcmEncrypt(nint key, int keybits, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint outBuf, nint tag16);

- [DllImport("crt")]static extern long AesGcmDecrypt(nint key, int keybits, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint tag16, nint outBuf);

- [DllImport("crt")]static extern long AesGcmInit(nint ctxBuf, long ctxLen, nint key, int keybits);

- [DllImport("crt")]static extern long AesGcmEncryptCtx(nint ctxBuf, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint outBuf, nint tag16);

- [DllImport("crt")]static extern long AesGcmDecryptCtx(nint ctxBuf, nint iv, nint aad, long aadLen, nint inBuf, long inLen, nint tag16, nint outBuf);

- [DllImport("crt")]static extern long RsaModPow(nint baseVal, long bLen, nint exp, long eLen, nint mod, long mLen, nint outBuf);

- [DllImport("crt")]static extern long RsaCrtModPow(nint msg, long mLen, nint p, long pLen, nint q, long qLen, nint dp, long dpLen, nint dq, long dqLen, nint qinv, long qinvLen, nint outBuf, long outLen);

- [DllImport("crt")]static extern long X25519(nint scalar, nint point, nint outBuf);

- [DllImport("crt")]static extern string Sm3(nint p, int len);

- [DllImport("crt")]static extern long Sm4CbcEncrypt(nint dst, nint src, long size, nint key, nint iv);

- [DllImport("crt")]static extern long Sm4CbcDecrypt(nint dst, nint src, long size, nint key, nint iv);

- [DllImport("crt")]static extern string Base64Encode(nint ptr, long size);

- [DllImport("crt")]static extern long Base64Decode(nint dst, nint src, long size);

- [DllImport("crt")]static extern long JsonSkipWhitespace(nint ptr, long pos, long len);

- [DllImport("crt")]static extern long JsonScanString(nint ptr, long pos, long len);

- public static bool SequenceEqual(nint a, nint b, int len)

- public static int IndexOf(nint p, int len, byte target)

- public static bool IsAscii(nint p, int len)

- public static int IndexOfAny(nint p, int len, byte val1, byte val2)

- public static int MatchGroup16(nint groupPtr, byte targetH2)

- public static int MatchEmpty16(nint groupPtr)


## NotSupportedException (class)

- public NotSupportedException(string message)


## ObjectDisposedException (class)

- public ObjectDisposedException(string objectName):base("Cannot access a disposed object. Object name: '"+objectName+"'.")


## OperatingSystem (class)

- public static bool IsWindows()

- public static bool IsLinux()

- public static bool IsMacOS()

- public static bool IsWasi()

- public static bool IsWasm32()

- public static bool IsMusl()

- public static bool IsRiscv64()

- public static bool IsIos()

- public static bool IsAndroid()

- public static bool IsOhos()

- public static string Platform { get }

- public static bool IsOSPlatform(string platform)


## PlatformNotSupportedException (class)

- public PlatformNotSupportedException(string message)


## Pump (class)

- static nint user32;

- static nint peek;

- static nint translate;

- static nint dispatch;

- static nint sleep;

- static void Resolve()

- static void Drain()

- static bool Until(PumpGate gate, int timeoutMs)


## PumpGate (class)

- bool done;

- PumpGate()

- void Signal()

- bool IsDone()

- void Reset()


## Random (class)

- [DllImport("crt")]static extern long time(nint ptr);

- long state;

- Random()

- static Random Seeded(long seed)

- long NextRaw()

- int Next()

- int NextBelow(int bound)

- int Between(int lo, int hi)

- double NextDouble()

- bool NextBool()


## RandomNumberGenerator (class)

- [DllImport("advapi32")]static extern int SystemFunction036(string buf, int len);

- [DllImport("crt", EntryPoint="__wasi_random_get")]static extern int WasiRandomGet(string buf, int len);

- [DllImport("crt", EntryPoint="fopen")]static extern nint urandOpen(string path, string mode);

- [DllImport("crt", EntryPoint="fread")]static extern long urandRead(string buf, long size, long count, nint fp);

- [DllImport("crt", EntryPoint="fclose")]static extern int urandClose(nint fp);

- static int Fill(string buf, int n)

- static byte[]GetBytes(int n)


## RegexBudgetException (class)

- public RegexBudgetException(string message)


## SocketException (class)

- public int code;

- public SocketException(int code, string message)


## Stopwatch (class)

- [DllImport("crt", EntryPoint="zan_stopwatch_frequency")]static extern long ZanStopwatchFrequency();

- [DllImport("crt", EntryPoint="zan_stopwatch_ticks")]static extern long ZanStopwatchTicks();

- static long cachedFrequency=0;

- long startTicks;

- long accumulated;

- bool running;

- static Stopwatch StartNew()

- void Start()

- void Stop()

- void Reset()

- void Restart()

- double ElapsedMilliseconds()

- double ElapsedSeconds()

- long ElapsedTicks()

- bool IsRunning()

- static long Frequency()

- static long GetMicroseconds()

- static long GetMilliseconds()

- static long MonoScaled(long unit)

- static long NowTicks()


## StringExtensions (class)

- [DllImport("crt")]static extern long strtoll(string s, nint endp, int base_);

- [DllImport("crt")]static extern double strtod(string s, nint endp);

- static bool IsNullOrEmpty(this string s)

- static bool IsWhitespaceByte(int b)

- static bool IsWhitespaceCodePoint(int cp)

- static bool IsNullOrWhiteSpace(this string s)

- static string PadLeft(this string s, int width)

- static string PadLeft(this string s, int width, string pad)

- static string PadRight(this string s, int width)

- static string PadRight(this string s, int width, string pad)

- static string TrimStart(this string s)

- static string TrimEnd(this string s)

- static string Insert(this string s, int index, string text)

- static string Remove(this string s, int start)

- static string Remove(this string s, int start, int count)

- static int CompareTo(this string s, string other)

- static bool EqualsOrdinal(this string s, string other)

- static List<string> ToCharList(this string s)

- static List<string> ToByteCharList(this string s)

- static int CountOf(this string s, string needle)

- static int ToInt32(this string s)

- static long ToInt64(this string s)

- static double ToDouble(this string s)

- static int ByteAt(this string s, int index)

- static string RuneAt(this string s, int byteIndex)

- static int RuneCount(this string s)

- static string CharAt(this string s, int runeIndex)

- static byte[]FromHexString(this string s)

- static string ToUpperAscii(this string s)

- static string ToLowerAscii(this string s)


## TaskJoin (class)

- static int MAX_BACKOFF=4;

- static int YIELD_SPINS=8;

- static async int WhenAll(List<long> handles)

- static async int WhenAny(List<long> handles)

- static async int WhenAllPoll(List<long> handles)

- static async int WhenAnyPoll(List<long> handles)

- static void CancelAll(List<long> handles)

- static int DoneCount(List<long> handles)


## TimeSpan (class)

- long totalMs;

- long total;

- TimeSpan(long seconds)

- static TimeSpan FromMilliseconds(long ms)

- static TimeSpan FromSeconds(long s)

- static TimeSpan FromMinutes(long m)

- static TimeSpan FromHours(long h)

- static TimeSpan FromDays(long d)

- long TotalMilliseconds()

- long TotalSeconds()

- long TotalMinutes()

- long TotalHours()

- long TotalDays()

- int Days()

- int Hours()

- int Minutes()

- int Seconds()

- int Milliseconds()

- bool IsNegative()

- TimeSpan Add(TimeSpan other)

- TimeSpan Subtract(TimeSpan other)

- TimeSpan Negate()

- string ToString()


## TimeoutException (class)

- public TimeoutException(string message)


## Wide (class)

- [DllImport("kernel32", EntryPoint="MultiByteToWideChar")]static extern int ToWide(int page, int flags, nint mb, int mbLen, nint wide, int wideLen);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int ToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- static int Utf8()

- static nint Of(string s)

- static string Read(nint p)

- static int Length(nint p)

- static void Free(nint p)


## ZanVersion (class)

本标准库随附的工具链版本，程序可直接打印
它（服务启动横幅、关于对话框、`--version`
输出）而无需查询构建系统。

- static string Text()
  - 点分格式的发布版本字符串，例如 "0.2.0"。


## T (delegate)

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


## int (delegate)

`delegate int Comparison<T>(T x, T y);`


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
