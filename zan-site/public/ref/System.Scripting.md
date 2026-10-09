# System.Scripting

> 源码: `packages/Zan.Scripting/src/System/Scripting/Lua.zan`


## Lua (class)

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

- static nint addr_loadbufferx;

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

- static bool IsInitialized()

- static void Initialize()

- static void LoadPath(string libPath)

- static void Finalize()

- static void Sandbox()

- static nint State()

- static int Top()

- static void Exec(string code)

- static void ExecBytes(byte[]chunk, string chunkName)

- static LuaValue Eval(string code)

- static LuaValue GetGlobal(string name)

- static void SetGlobal(string name, LuaValue v)

- static LuaValue Call(string name, params LuaValue[]args)

- static double CallD(string name, params double[]args)

- static long CallL(string name, params long[]args)

- static string CallS(string name, params string[]args)

- static bool RegisterFunction(string name, LuaCFunction fn)

- static LuaValue Nil()

- static LuaValue FromDouble(double v)

- static LuaValue FromLong(long v)

- static LuaValue FromStr(string s)

- static LuaValue FromBool(bool b)

- static LuaValue FromList(List<LuaValue> items)

- static LuaValue FromListDouble(List<double> items)

- static LuaValue FromListLong(List<long> items)

- static LuaValue FromListStr(List<string> items)

- static LuaValue FromDict(Dictionary <string, LuaValue> map)

- static LuaValue FromDictStr(Dictionary <string, string> map)

- static double ArgDouble(nint L, int index)

- static long ArgLong(nint L, int index)

- static string ArgStr(nint L, int index)

- static bool ArgBool(nint L, int index)

- static int ArgCount(nint L)

- static int ReturnDouble(nint L, double v)

- static int ReturnLong(nint L, long v)

- static int ReturnStr(nint L, string s)

- static int ReturnBool(nint L, bool b)

- static int ReturnNil(nint L)

- static void EnsureReady()

- static void OpenState()

- static void SetTop(int n)

- static void Load(string code)

- static void LoadBytes(byte[]chunk, string chunkName)

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

- long slot;

- static LuaValue Wrap(long slot)

- static LuaValue CaptureTop()

- ~LuaValue()

- long Slot()

- void Push()

- int Type()

- bool IsNil()

- bool IsTable()

- bool IsFunction()

- double ToDouble()

- long ToLong()

- string ToStr()

- bool ToBool()

- long Length()

- LuaValue At(long index)

- void SetAt(long index, LuaValue val)

- LuaValue Get(string key)

- void Set(string key, LuaValue val)

- LuaValue Call(params LuaValue[]args)

- List<LuaValue> ToList()

- List<double> ToListDouble()

- List<long> ToListLong()

- List<string> ToListStr()

- Dictionary <string, LuaValue> ToDict()

- static LuaValue op_call(LuaValue self, string a)

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

- static LuaValue op_index(LuaValue self, string key)

- static void op_index_set(LuaValue self, long index, LuaValue val)

- static void op_index_set(LuaValue self, long index, string val)

- static void op_index_set(LuaValue self, string key, LuaValue val)

- static void op_index_set(LuaValue self, string key, string val)


## double (delegate)

`delegate double LuaNumFn(nint L, int idx, nint isnum);`


## int (delegate)

`delegate int LuaCFunction(nint L);`


## int (delegate)

`delegate int LuaIntFn(nint L);`


## int (delegate)

`delegate int LuaIdxIntFn(nint L, int idx);`


## int (delegate)

`delegate int LuaLoadFn(nint L, string code);`


## int (delegate)

`delegate int LuaLoadBufFn(nint L, string buf, long sz, string chunkName, string mode);`


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


## long (delegate)

`delegate long LuaIntegerFn(nint L, int idx, nint isnum);`


## long (delegate)

`delegate long LuaRawLenFn(nint L, int idx);`


## nint (delegate)

`delegate nint LuaNewStateFn();`


## nint (delegate)

`delegate nint LuaPushStrFn(nint L, string s);`


## string (delegate)

`delegate string LuaToStrFn(nint L, int idx, nint len);`


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
