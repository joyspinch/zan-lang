# System.Scripting

> 源码: `stdlib/System/Scripting/Lua.zan`, `stdlib/System/Scripting/Python.zan`


## Lua (class)

Zan 的进程内 Lua 嵌入（Lua 5.3 / 5.4）。

运行时定位 Lua 动态库，并通过 `Interop`
（LoadLibrary/GetProcAddress）在运行期解析所有入口点，因此
从未用到 <c>System.Scripting</c> 的程序不会链接 Lua；
没有 Lua 的机器只会得到 `Lua.IsAvailable` == false，
而不是加载失败。这与 `Python`、SDL3 等其他可选
原生依赖的处理方式一致。

using System.Scripting;

if (!Lua.IsAvailable()) { ... fall back ... }
Lua.Initialize();

// 直接调用，自动封送 double/long/string：
double v = Lua.CallD("math.floor", 22.3);
string s = Lua.CallS("string.upper", "zan");

// 脚本与表达式：
Lua.Exec("answer = 6 * 7");
LuaValue r = Lua.Eval("answer");           // 42

// 表既是数组也是字典：
LuaValue t = Lua.Eval("{1, 4, 9}");
long second = t[2].ToLong();
t[3] = Lua.FromLong(99L);
LuaValue p = Lua.Eval("{name = 'Zan'}");
p["lang"] = "Lua";

回调反向运行，通过 `Lua.RegisterFunction`：
静态、非捕获的 Zan 方法被包装为 lua_CFunction 并发布为全局
名字，使 Lua 代码可以调用 Zan 代码。

生命周期：Lua 是栈式 API，而 Zan 的值要活过单次调用，所以
`LuaValue` 把值存进 Lua 注册表（registry）的一个整数
槽位里，并在 ARC 回收包装时把该槽位置 nil——于是 Lua 的 GC
恰好在最后一个 Zan 引用消失后回收该值。所有 API 都在返回前
把栈顶恢复到进入时的高度，因此 `Lua.Top` 在成对
使用下始终回到 0。

- static int REGISTRY=0-1001000;

- static int TNIL=0;

- static int TBOOLEAN=1;

- static int TNUMBER=3;

- static int TSTRING=4;

- static int TTABLE=5;

- static int TFUNCTION=6;

- static nint mod;

- static nint state;

- static nint addr_newstate;

- static nint addr_openlibs;

- static nint addr_close;

- static nint addr_loadstring;

- static nint addr_pcallk;

- static nint addr_gettop;

- static nint addr_settop;

- static nint addr_type;

- static nint addr_toboolean;

- static nint addr_tonumberx;

- static nint addr_tointegerx;

- static nint addr_tolstring;

- static nint addr_pushnumber;

- static nint addr_pushinteger;

- static nint addr_pushstring;

- static nint addr_pushboolean;

- static nint addr_pushnil;

- static nint addr_pushvalue;

- static nint addr_pushcclosure;

- static nint addr_getglobal;

- static nint addr_setglobal;

- static nint addr_getfield;

- static nint addr_setfield;

- static nint addr_geti;

- static nint addr_seti;

- static nint addr_rawgeti;

- static nint addr_rawseti;

- static nint addr_rawlen;

- static nint addr_next;

- static nint addr_createtable;

- static bool resolved;

- static bool initialized;

- static long nextSlot;

- static bool IsAvailable()
  - 能加载可用的 Lua 动态库时为 true。

- static bool IsInitialized()
  - `Initialize` 运行过且状态仍存活时为 true。

- static void Initialize()
  - 加载可用的 Lua 动态库、创建状态并打开标准库。幂等。
    没有 Lua 运行时则抛出异常。

- static void LoadPath(string libPath)
  - 加载指定的 Lua 动态库（如 "C:/lua/lua54.dll"）并初始化状态。
    Lua 不在 PATH 或库搜索路径中时，请调用它而不是
    `Initialize`。

- static void Finalize()
  - 关闭 Lua 状态并释放运行时库。

- static nint State()
  - 底层 lua_State*（未初始化时为 0）。

- static int Top()
  - 当前 Lua 栈高度；成对使用的 API 之后应为 0。

- static void Exec(string code)
  - 执行一段 Lua 代码（编译或运行出错时抛出异常）。

- static LuaValue Eval(string code)
  - 求值一个 Lua 表达式并返回结果，如
    <c>Lua.Eval("{1, 4, 9}")</c>；表达式可引用
    `Exec` 定义的全局名字。

- static LuaValue GetGlobal(string name)
  - 读取全局名字（不存在则为 nil 值）。

- static void SetGlobal(string name, LuaValue v)
  - 写入全局名字，使脚本可以读到它。

- static LuaValue Call(string name, params LuaValue[]args)
  - 以任意数量的参数调用 Lua 函数。名字可以是点分路径
    （<c>"math.floor"</c>、<c>"string.upper"</c>）：第一段从全局表取，
    其余逐层取字段。

- static double CallD(string name, params double[]args)
  - Call + double 封送：<c>Lua.CallD("math.floor", 22.3)</c>。

- static long CallL(string name, params long[]args)
  - Call + long 封送：<c>Lua.CallL("math.max", 3L, 7L)</c>。

- static string CallS(string name, params string[]args)
  - Call + string 封送：<c>Lua.CallS("string.upper", "zan")</c>。

- static bool RegisterFunction(string name, LuaCFunction fn)
  - 把静态 Zan 方法发布为 Lua 全局函数。方法必须是
    lua_CFunction 形式：
    
    static int MyFn(nint L) {
    double a = Lua.ArgDouble(L, 1);
    return Lua.ReturnDouble(L, a * 2);
    }
    
    且不能捕获状态（Zan 委托为非捕获型），与 Input.Hook、
    Thread.Start 的回调一致。Lua 未就绪时返回 false。

- static LuaValue Nil()
  - nil 值。

- static LuaValue FromDouble(double v)
  - 把 double 包装为 Lua number。

- static LuaValue FromLong(long v)
  - 把 64 位整数包装为 Lua integer。

- static LuaValue FromStr(string s)
  - 把字符串包装为 Lua string（UTF-8 原样传递）。

- static LuaValue FromBool(bool b)
  - 把 bool 包装为 Lua boolean。

- static LuaValue FromList(List<LuaValue> items)
  - 把 LuaValue 列表包装为 Lua 数组表（下标从 1 起）。

- static LuaValue FromListDouble(List<double> items)
  - 把 double 列表包装为 Lua 数组表。

- static LuaValue FromListLong(List<long> items)
  - 把 long 列表包装为 Lua 数组表。

- static LuaValue FromListStr(List<string> items)
  - 把字符串列表包装为 Lua 数组表。

- static LuaValue FromDict(Dictionary <string, LuaValue> map)
  - 把字符串键映射包装为 Lua 表。

- static LuaValue FromDictStr(Dictionary <string, string> map)
  - 把字符串到字符串的映射包装为 Lua 表。

- static double ArgDouble(nint L, int index)
  - 把第 <paramref name="index"/> 个参数（从 1 起）读为 double。

- static long ArgLong(nint L, int index)
  - 把第 <paramref name="index"/> 个参数读为 64 位整数。

- static string ArgStr(nint L, int index)
  - 把第 <paramref name="index"/> 个参数读为字符串。

- static bool ArgBool(nint L, int index)
  - 把第 <paramref name="index"/> 个参数读为真值。

- static int ArgCount(nint L)
  - 回调参数个数。

- static int ReturnDouble(nint L, double v)
  - 回调返回一个 number（返回值即压入的结果个数）。

- static int ReturnLong(nint L, long v)
  - 回调返回一个 integer。

- static int ReturnStr(nint L, string s)
  - 回调返回一个 string。

- static int ReturnBool(nint L, bool b)
  - 回调返回一个 boolean。

- static int ReturnNil(nint L)
  - 回调返回 nil。

- static void EnsureReady()

- static void OpenState()

- static void SetTop(int n)

- static void Load(string code)

- static void Protected(int nargs, int nres, int mark, string what)

- static void Fail(string prefix)

- static string TopString()

- static void PushPath(string name, int mark)

- static bool TryResolve()

- static bool ResolveSymbols()

- static void FreeModule()

- static long StoreTop()

- static void PushSlot(long slot)

- static void DropSlot(long slot)

- static int TypeOfSlot(long slot)


## LuaValue (class)

一个 Lua 值。底层值存放在 Lua 注册表的一个槽位里，包装被 ARC
回收时该槽位被置 nil，因此 Lua 的 GC 与 Zan 的生命周期对齐。
nil 值的槽位号为 0，不占用注册表。

- long slot;

- static LuaValue Wrap(long slot)
  - 包装一个已分配的槽位（0 表示 nil）。

- static LuaValue CaptureTop()
  - 把栈顶的值移进注册表并包装（弹出栈顶）。

- ~LuaValue()

- long Slot()
  - 注册表槽位号（nil 为 0）。

- void Push()
  - 把该值压到 Lua 栈上。

- int Type()
  - lua_type 值。

- bool IsNil()
  - 该值为 nil 时为 true。

- bool IsTable()
  - 该值为表时为 true。

- bool IsFunction()
  - 该值为函数（Lua 函数或 C 函数）时为 true。

- double ToDouble()
  - 读为 double。

- long ToLong()
  - 读为 64 位整数。

- string ToStr()
  - 读为字符串（number 会按 Lua 规则转成字符串）。

- bool ToBool()
  - 读取真值（Lua 语义：只有 nil 和 false 为假）。

- long Length()
  - 表的数组长度（# 运算符的 raw 版本）。

- LuaValue At(long index)
  - 读表的整数下标（下标从 1 起）。

- void SetAt(long index, LuaValue val)
  - 写表的整数下标。

- LuaValue Get(string key)
  - 读表的字符串键。

- void Set(string key, LuaValue val)
  - 写表的字符串键。

- LuaValue Call(params LuaValue[]args)
  - 以任意数量的参数调用该值（必须是可调用的）。

- List<LuaValue> ToList()
  - 把 Lua 数组表拆成 LuaValue 列表。

- List<double> ToListDouble()
  - 把 Lua 数组表拆成 double 列表。

- List<long> ToListLong()
  - 把 Lua 数组表拆成 long 列表。

- List<string> ToListStr()
  - 把 Lua 数组表拆成字符串列表。

- Dictionary <string, LuaValue> ToDict()
  - 把 Lua 表拆成以字符串为键的映射。键按 Lua 的转换规则读为
    字符串，因此数字键也能取到（"1"、"2" ...）。

- static LuaValue op_call(LuaValue self, string a)
  - 运算符重载：<c>fn(a, b)</c> 调用 Lua 函数。由编译器的 op_call
    降级启用；标量重载自动封送 string/double/long/bool 参数。

- static LuaValue op_call(LuaValue self, double a)

- static LuaValue op_call(LuaValue self, long a)

- static LuaValue op_call(LuaValue self, bool a)

- static LuaValue op_call(LuaValue self, LuaValue a)

- static LuaValue op_call(LuaValue self, params double[]args)

- static LuaValue op_call(LuaValue self, params long[]args)

- static LuaValue op_call(LuaValue self, params string[]args)

- static LuaValue op_call(LuaValue self, params bool[]args)

- static LuaValue op_call(LuaValue self, params LuaValue[]args)

- static LuaValue op_index(LuaValue self, long index)
  - 运算符重载：<c>t[2]</c>，同 `At`。

- static LuaValue op_index(LuaValue self, string key)
  - 运算符重载：<c>t["k"]</c>，同 `Get`。

- static void op_index_set(LuaValue self, long index, LuaValue val)
  - 运算符重载：<c>t[2] = v</c>，同 `SetAt`。

- static void op_index_set(LuaValue self, long index, string val)
  - 运算符重载：<c>t[2] = "s"</c>，封送字符串值。

- static void op_index_set(LuaValue self, string key, LuaValue val)
  - 运算符重载：<c>t["k"] = v</c>，同 `Set`。

- static void op_index_set(LuaValue self, string key, string val)
  - 运算符重载：<c>t["k"] = "s"</c>，封送字符串值。


## PyObject (class)

对 Python 对象的强引用。底层 PyObject* 会在
该包装被 ARC 回收时释放（Py_DECREF）。

- nint handle;

- static PyObject Wrap(nint h)
  - 包装一个自有引用（绝不会是借用引用）。

- nint Raw()
  - 原始 PyObject*；0 表示 null（Python None 不是 null）。

- bool IsNull()
  - 该对象是 Python 空指针时为 true（罕见）。

- ~PyObject()

- PyObject GetAttr(string name)
  - 读取属性（如 math 模块上的 "floor"）。

- PyObject TryGetAttr(string name)
  - 不抛异常地读取属性；属性不存在时返回 null PyObject
    （Python 错误指示器会被清除）。

- void SetAttr(string name, PyObject val)
  - 写入属性：<c>obj.SetAttr("name", value)</c>。

- PyObject At(long index)
  - 读取序列（list/tuple）的下标：<c>obj.At(2)</c>。

- void SetIndex(long index, PyObject val)
  - 写入 list 的下标：<c>obj.SetIndex(2, v)</c>。

- PyObject GetItem(string key)
  - 用 Python 键读取元素（dict、sequence）：<c>obj.GetItem("k")</c>。

- void SetItem(string key, PyObject val)
  - 用 Python 键写入元素（dict、sequence）：<c>obj.SetItem("k", v)</c>。

- static PyObject op_call(PyObject self, string a)
  - 运算符重载：<c>fn(a, b)</c> 调用 Python callable。由编译器
    的 op_call 降级启用；标量重载自动封送
    string/double/long/bool 参数，PyObject 重载接收
    已包装的值，params 重载则
    覆盖多个 PyObject 参数。

- static PyObject op_call(PyObject self, double a)

- static PyObject op_call(PyObject self, long a)

- static PyObject op_call(PyObject self, bool a)

- static PyObject op_call(PyObject self, PyObject a)

- static PyObject op_call(PyObject self, params double[]args)

- static PyObject op_call(PyObject self, params string[]args)

- static PyObject op_call(PyObject self, params long[]args)

- static PyObject op_call(PyObject self, params bool[]args)

- static PyObject op_call(PyObject self, params PyObject[]args)

- static PyObject op_index(PyObject self, long index)
  - 运算符重载：<c>list[2]</c> 读取下标，同 `At`。

- static PyObject op_index(PyObject self, string key)
  - 运算符重载：<c>dict["k"]</c> 读取元素，同 `GetItem`。

- static void op_index_set(PyObject self, long index, PyObject val)
  - 运算符重载：<c>list[2] = v</c> 写入下标，同 `SetIndex`。

- static void op_index_set(PyObject self, long index, string val)
  - 运算符重载：<c>list[2] = "s"</c> 封送字符串值。

- static void op_index_set(PyObject self, string key, PyObject val)
  - 运算符重载：<c>dict["k"] = v</c> 写入元素，同 `SetItem`。

- static void op_index_set(PyObject self, string key, string val)
  - 运算符重载：<c>dict["k"] = "s"</c> 封送字符串值。

- PyObject Call0()
  - 不带参数调用对象。

- PyObject Call1(PyObject a)
  - 带一个参数调用对象。

- PyObject Call2(PyObject a, PyObject b)
  - 带两个参数调用对象。

- PyObject Call3(PyObject a, PyObject b, PyObject c)
  - 带三个参数调用对象。

- PyObject Call(params PyObject[]args)
  - 以任意数量的参数调用对象：
    <c>fn.Call(Python.FromDouble(1.0), Python.FromStr("x"))</c> 或传入
    预构建的列表。取代 Call0..3；它们为兼容性保留。

- PyObject CallTuple(nint tuple)

- double ToDouble()
  - 将对象读为 double。

- long ToLong()
  - 将对象读为 64 位整数。

- string ToStr()
  - 将对象读为字符串（str(obj)）。

- bool ToBool()
  - 读取对象的真值。

- List<PyObject> ToList()
  - 将 Python list 拆为 PyObject 列表。

- List<double> ToListDouble()
  - 将 Python list 拆为 double 列表。

- List<string> ToListStr()
  - 将 Python list 拆为字符串列表（逐个 str()）。

- Dictionary <string, PyObject> ToDict()
  - 将 Python dict 拆为以字符串为键的 PyObject 映射。键
    通过 str() 读取，因此非字符串键也能转换。


## PyRegisteredDef (class)

- nint def;

- nint name;

- PyRegisteredDef(nint def, nint name)


## Python (class)

Zan 的进程内 CPython 嵌入。

运行时定位 runtime DLL，并通过 `Interop`
（LoadLibrary/GetProcAddress）在运行期解析所有入口点，因此
从未用到 <c>System.Scripting</c> 的程序不会链接 Python；
没有 Python 的机器只会得到 `Python.IsAvailable`
== false，而不是加载失败。这与 stdlib 对待
SDL3 等其他可选原生依赖的方式一致。

using System.Scripting;

if (!Python.IsAvailable()) { ... fall back ... }
Python.Initialize();

// 直接调用，自动封送 double/string：
double v = Python.CallD("math.floor", 22.3);
string s = Python.CallS("greet", "Zan");

// 表达式与模块级访问：
Python.Exec("answer = 6 * 7");
PyObject r = Python.Eval("[x * x for x in range(4)]");
List<double> squares = r.ToListDouble();        // [0, 1, 4, 9]

// 对象式调用，显式封送：
PyObject math = Python.Import("math");
double f = math.GetAttr("floor").Call1(Python.FromDouble(22.3)).ToDouble();

// 容器可双向跨边界：
PyObject pyList = Python.FromListStr(new List<string>{ "a", "b" });
Dictionary<string, PyObject> pyDict = Python.Eval("{'k': 1}").ToDict();

回调反向运行，通过 `Python.RegisterFunction`：
静态、非捕获的 Zan 方法被包装为 PyCFunction，
并发布到 __main__ 模块，使 Python 代码可以调用 Zan 代码。

引用计数：`PyObject` 持有其 PyObject* 的强引用
（Py_INCREF），并在 finalizer 中释放（Py_DECREF），
因此 ARC 恰好在最后一个 Zan
引用消失时回收 Python 对象。借用引用（如来自
`Python.GetArg` 的元组项）直接读取，从不包装。

线程：每次经本模块的调用期间都持有 GIL。
只要解释器已初始化过一次，从非初始化线程调用 Python 就是安全的。
重度使用时，计划在长时间运行的 Zan 计算
期间释放 GIL（规划中）。

- static nint mod;

- static nint addr_init;

- static nint addr_isInit;

- static nint addr_finalize;

- static nint addr_runSimple;

- static nint addr_importModule;

- static nint addr_getAttr;

- static nint addr_call;

- static nint addr_tupleNew;

- static nint addr_tupleSet;

- static nint addr_tupleGet;

- static nint addr_floatAsDouble;

- static nint addr_floatFromDouble;

- static nint addr_longAsLong;

- static nint addr_longFromLong;

- static nint addr_unicodeAsUtf8;

- static nint addr_unicodeFromString;

- static nint addr_objStr;

- static nint addr_objIsTrue;

- static nint addr_boolFromLong;

- static nint addr_decRef;

- static nint addr_errPrint;

- static nint addr_errOccurred;

- static nint addr_errClear;

- static nint addr_cfuncNew;

- static nint addr_setAttrString;

- static nint addr_incRef;

- static nint addr_runString;

- static nint addr_moduleGetDict;

- static nint addr_listSize;

- static nint addr_listGetItem;

- static nint addr_listNew;

- static nint addr_listSetItem;

- static nint addr_getItem;

- static nint addr_setItem;

- static nint addr_dictSize;

- static nint addr_dictNext;

- static nint addr_dictNew;

- static nint addr_dictSetItemString;

- static bool resolved;

- static bool initialized;

- static List<PyRegisteredDef> registeredDefs;

- static bool IsAvailable()
  - 可加载可用的 CPython DLL 时为 true。

- static bool IsInitialized()
  - `Initialize` 运行过后为 true。

- static void Initialize()
  - 加载可用的最新 CPython（python3xx.dll）并初始化
    解释器。幂等。没有 Python 运行时则抛出异常。

- static void LoadPath(string dllPath)
  - 加载指定的 CPython DLL（如 "C:/Python311/python311.dll"）并
    初始化解释器。当 Python 不在 PATH 中时，请调用它而不是
    `Initialize`。

- static void Finalize()
  - 关闭解释器（释放 runtime DLL）。

- static void Exec(string code)
  - 在 __main__ 模块的命名空间中执行 Python 代码。

- static PyObject Eval(string code)
  - 在 __main__ 命名空间中求值 Python 表达式并返回
    结果（如 <c>Python.Eval("[x * x for x in range(4)]")</c>）。使用
    PyRun_String 与 Py_eval_input；表达式可引用
    `Exec` 定义的名称。

- static PyObject Import(string name)
  - 导入 Python 模块（如 "math"、"os.path"）。

- static PyObject GetMain()
  - __main__ 模块（`Exec` 在其中定义名称）。

- static PyObject Call(string name, params PyObject[]args)
  - 以任意数量的参数调用函数。名称可以是点分形式
    （<c>"math.floor"</c>、<c>"os.path.join"</c>）。第一段会
    尝试作为 import（因此 <c>"math.floor"</c> 无需显式
    <c>import math</c>）；否则在 __main__ 上查找：
    <c>Python.Call("math.floor", Python.FromDouble(2.5))</c>。

- static PyObject TryImport(string name)
  - 导入模块，不存在时返回 null PyObject。

- static double CallD(string name, params double[]args)
  - Call + double 封送：<c>Python.CallD("math.floor", 2.5)</c>。

- static string CallS(string name, params string[]args)
  - Call + string 封送：<c>Python.CallS("greet", "Zan")</c>。

- static long CallL(string name, params long[]args)
  - Call + long 封送：<c>Python.CallL("int", "3.9")</c>。

- static bool RegisterFunction(string name, PyCFunction fn)
  - 在 __main__ 模块上发布静态 Zan 方法，让 Python 代码可以调用它。
    该方法必须是 PyCFunction 形式：
    
    static nint MyFn(nint self, nint args) {
    double a = Python.GetArgDouble(args, 0);
    return Python.ReturnDouble(a * 2);
    }
    
    且不能捕获状态（Zan 委托为非捕获型），
    与 Input.Hook、Thread.Start 回调一致。
    Python 解释器未就绪时返回 false。

- static PyObject FromDouble(double v)
  - 将 double 包装为 Python float。

- static PyObject FromLong(long v)
  - 将 64 位整数包装为 Python int。

- static PyObject FromStr(string s)
  - 将字符串包装为 Python str（UTF-8）。

- static PyObject FromBool(bool b)
  - 将 bool 包装为 Python bool。

- static PyObject FromList(List<PyObject> items)
  - 将 PyObject 列表包装为 Python list。

- static PyObject FromListDouble(List<double> items)
  - 将 double 列表包装为 Python float 列表。

- static PyObject FromListStr(List<string> items)
  - 将字符串列表包装为 Python str 列表。

- static PyObject FromDict(Dictionary <string, PyObject> map)
  - 将字符串键映射包装为 Python dict。

- static PyObject FromDictStr(Dictionary <string, string> map)
  - 将字符串到字符串的映射包装为 Python dict。

- static double ToDouble(PyObject o)
  - 将 Python 对象读为 double。

- static long ToLong(PyObject o)
  - 将 Python 对象读为 64 位整数。

- static string ToStr(PyObject o)
  - 将 Python 对象读为字符串（UTF-8，经 str()）。

- static bool ToBool(PyObject o)
  - 读取 Python 对象的真值。

- static bool ErrOccurred()
  - Python 错误指示器已设置时为 true。

- static void ErrClear()
  - 清除 Python 错误指示器。

- static double GetArgDouble(nint args, long index)
  - 将 args 元组的第 `index` 个参数读为 double。

- static long GetArgLong(nint args, long index)
  - 将 args 元组的第 `index` 个参数读为 long。

- static string GetArgStr(nint args, long index)
  - 将 args 元组的第 `index` 个参数读为字符串。

- static nint ReturnDouble(double v)
  - 构造回调的返回值：新的 float。

- static nint ReturnLong(long v)
  - 构造回调的返回值：新的 int。

- static nint ReturnStr(string s)
  - 构造回调的返回值：新的字符串。

- static nint ReturnBool(bool b)
  - 构造回调的返回值：新的 bool。

- static nint ReturnNone()
  - 构造回调的返回值：None。

- static void EnsureReady()

- static bool TryResolve()

- static bool ResolveSymbols()

- static void FreeModule()

- static void DecRef(nint obj)

- static void IncRef(nint obj)


## double (delegate)

`delegate double LuaNumFn(nint L, int idx, nint isnum);`


## double (delegate)

`delegate double PyDoubleFn(nint obj);`


## int (delegate)

`delegate int LuaCFunction(nint L);`


## int (delegate)

`delegate int LuaIntFn(nint L);`


## int (delegate)

`delegate int LuaIdxIntFn(nint L, int idx);`


## int (delegate)

`delegate int LuaLoadFn(nint L, string code);`


## int (delegate)

`delegate int LuaPcallFn(nint L, int nargs, int nres, int errfunc, nint ctx, nint k);`


## int (delegate)

`delegate int LuaGetGlobalFn(nint L, string name);`


## int (delegate)

`delegate int LuaGetFieldFn(nint L, int idx, string key);`


## int (delegate)

`delegate int LuaGetIFn(nint L, int idx, long n);`


## int (delegate)

`delegate int LuaNextFn(nint L, int idx);`


## int (delegate)

`delegate int PyIntFn();`


## int (delegate)

`delegate int PyIntStrFn(string s);`


## int (delegate)

`delegate int PyNint3RetFn(nint a, nint b, nint c);`


## int (delegate)

`delegate int PyTupleSetFn(nint t, long i, nint v);`


## int (delegate)

`delegate int PySetAttrFn(nint obj, string name, nint val);`


## int (delegate)

`delegate int PyListSetFn(nint list, long index, nint v);`


## int (delegate)

`delegate int PyDictNextFn(nint dict, nint pos, nint keyOut, nint valOut);`


## int (delegate)

`delegate int PyDictSetStrFn(nint dict, string key, nint val);`


## long (delegate)

`delegate long LuaIntegerFn(nint L, int idx, nint isnum);`


## long (delegate)

`delegate long LuaRawLenFn(nint L, int idx);`


## long (delegate)

`delegate long PyLongFn(nint obj);`


## long (delegate)

`delegate long PyListSizeFn(nint list);`


## long (delegate)

`delegate long PyDictSizeFn(nint dict);`


## nint (delegate)

`delegate nint LuaNewStateFn();`


## nint (delegate)

`delegate nint LuaPushStrFn(nint L, string s);`


## nint (delegate)

`delegate nint PyCFunction(nint self, nint args);`


## nint (delegate)

`delegate nint PyNintStrFn(string s);`


## nint (delegate)

`delegate nint PyNint1Fn(nint a);`


## nint (delegate)

`delegate nint PyNint2Fn(nint a, nint b, nint c);`


## nint (delegate)

`delegate nint PyNintPairFn(nint a, nint b);`


## nint (delegate)

`delegate nint PyAttrFn(nint obj, string name);`


## nint (delegate)

`delegate nint PyDoubleRetFn(double v);`


## nint (delegate)

`delegate nint PyLongRetFn(long v);`


## nint (delegate)

`delegate nint PyTupleNewFn(long size);`


## nint (delegate)

`delegate nint PyTupleGetFn(nint t, long i);`


## nint (delegate)

`delegate nint PyBoolRetFn(int v);`


## nint (delegate)

`delegate nint PyCfuncNewFn(nint def, nint self);`


## nint (delegate)

`delegate nint PyRunStringFn(string code, int start, nint globals, nint locals);`


## nint (delegate)

`delegate nint PyListGetFn(nint list, long index);`


## nint (delegate)

`delegate nint PyListNewFn(long size);`


## nint (delegate)

`delegate nint PyDictNewFn();`


## string (delegate)

`delegate string LuaToStrFn(nint L, int idx, nint len);`


## string (delegate)

`delegate string PyStrRetFn(nint obj);`


## void (delegate)

`delegate void LuaVoid1Fn(nint L);`


## void (delegate)

`delegate void LuaVoidIntFn(nint L, int n);`


## void (delegate)

`delegate void LuaPushNumFn(nint L, double v);`


## void (delegate)

`delegate void LuaPushIntFn(nint L, long v);`


## void (delegate)

`delegate void LuaPushCClosureFn(nint L, nint fn, int upvals);`


## void (delegate)

`delegate void LuaVoidStrFn(nint L, string name);`


## void (delegate)

`delegate void LuaSetFieldFn(nint L, int idx, string key);`


## void (delegate)

`delegate void LuaSetIFn(nint L, int idx, long n);`


## void (delegate)

`delegate void LuaCreateTableFn(nint L, int narr, int nrec);`


## void (delegate)

`delegate void PyVoidFn();`


## void (delegate)

`delegate void PyVoid1Fn(nint a);`
