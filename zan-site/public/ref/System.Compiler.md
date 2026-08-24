# System.Compiler

> 源码: `stdlib/System/Compiler/GenCommon.zan`, `stdlib/System/Compiler/GenDb.zan`, `stdlib/System/Compiler/GenDbEmit.zan`, `stdlib/System/Compiler/GenForm.zan`, `stdlib/System/Compiler/GenJson.zan`, `stdlib/System/Compiler/GenRoute.zan`, `stdlib/System/Compiler/GenScene.zan`, `stdlib/System/Compiler/ZanGen.zan`


## DbField (class)

一个实体字段的编译期模型(对应 dbgen.c 的 dg_field_t)。

- string Name;

- string TypeName;

- int Kind;

- string Col;

- bool IsPk;

- bool IsIdent;

- bool IdentSet;

- bool NotNull;

- int StrLen;


## GenCommon (class)

- static JsonValue NewReply()
  - 新回复: { "sources": [], "rewrites": [], "errors": [], "warnings": [] }

- static JsonValue ArrOf(JsonValue reply, string key)
  - 回复里名为 key 的数组;缺失时补一个,调用方永远拿到可追加的数组。

- static void AddSource(JsonValue reply, string name, StringBuilder text)
  - 追加一段生成源码(name 用于诊断)。

- static void AddRewrite(JsonValue reply, JsonValue op)
  - 追加一条调用点重写指令(按 genmeta 的调用点 id 协议)。

- static void Fail(JsonValue reply, string msg)
  - 标记失败:回复变成 { "error": msg },C 端读取并报错。

- static bool HasError(JsonValue reply)

- static string Esc(string s)
  - 把 s 转义为可嵌入 .zan 字符串字面量正文的文本。
    规则与旧 formgen 的 fg_esc 一致:反斜杠翻倍、双引号转义、
    LF -> \n、CR 丢弃、TAB -> \t,其余原样。

- static bool IsIdent(string s)
  - 标识符安全检查(与旧 formgen 的 fg_is_ident 一致)。

- static void Line(StringBuilder b, int indent, string text)
  - 追加一行到输出缓冲(indent 个 4 空格缩进)。


## GenDb (class)

- static JsonValue Unit;

- static JsonValue Reply;

- static Dict <string, JsonValue> ClassOf;

- static List<string> NeedNames;

- static Dict <int, string> Rewrote;

- static Dict <int, JsonValue> CallById;

- static int RwCount;

- static bool Any;

- static bool SyncAll;

- static List<JsonValue> Projs;

- static JsonValue ClassGet(string name)

- static bool EnumGet(string name)

- static JsonValue AttrOf(JsonValue cls, string name)
  - 属性读取(与 dbgen.c 的 dg_attr 一致,名字精确匹配)。

- static JsonValue AttrArg(JsonValue attr, string key)
  - 具名属性参数(`Name = expr`)的表达式树,或 null。

- static string AttrStr(JsonValue attr, string key)

- static bool AttrBool(JsonValue attr, string key, bool dflt)

- static int AttrInt(JsonValue attr, string key, int dflt)

- static bool IsIntName(string n)

- static bool IsFloatName(string n)

- static bool Is64(string type)
  - 声明类型是否是 64 位整数(列走 BIGINT 与 64 位绑定/读取)。

- static int ClassifyKind(string type)

- static List<DbField> FieldsOf(string cls, int depth)
  - 收集一个实体的全部字段(基类递归,与 dbgen.c 的 dg_collect_fields
    一致:跳过 static 与 DF_SKIP)。

- static void FieldsInto(string cls, int depth, List<DbField> outl)

- static DbField FieldFind(List<DbField> fs, string name)

- static string TableOf(string cls)

- static List<JsonValue> IndexAttrs(string cls)
  - 实体上的 `[Index(Name = "...", Fields = "a,b", IsUnique = true)]`,
    按声明顺序;一个实体可以带多条。

- static bool IsTableEntity(string cls)

- static string PkOf(string cls)
  - 主键列:显式 identity 优先,其次叫 id 的字段,没有返回 ""。

- static int NeedAdd(string name)

- static JsonValue CallAt(int id)

- static JsonValue TreeStr(string s)

- static JsonValue TreeNull()

- static JsonValue TreeId(string name)

- static JsonValue TreeGenericId(string name, string targ)
  - 泛型标识符:`Expr<T>` 的 "Expr" 带 targs 数组(反序列化器据此
    构建 inst_type_ref)。

- static JsonValue TreeMem(JsonValue obj, string name)

- static JsonValue TreeCall(JsonValue callee, JsonValue args)

- static JsonValue TreeCall1(JsonValue callee, JsonValue arg)

- static JsonValue OneArg(JsonValue arg)

- static JsonValue TreeBin(string op, JsonValue l, JsonValue r)

- static JsonValue TreeCast(string t, JsonValue e)

- static JsonValue TreeLam(List<string> ps, JsonValue body)

- static int ArgsCount(JsonValue call)

- static string LamParam(JsonValue lam)
  - lambda 节点的第一个形参名;结构不完整时给空串。

- static int ArrLen(JsonValue o, string key)
  - o[key] 数组的长度;缺失算 0。

- static JsonValue ArgAt(JsonValue call, int i)

- static JsonValue NewOp(string op, JsonValue call)

- static void AddRewrite(JsonValue op)

- static void MarkChain(JsonValue call, string entity, int ck)
  - 链重写成功后登记调用点:下游调用点沿 `call#<id>` 回溯链根时,每一环
    都必须在 Rewrote 里。children-first 分配 id、按 id 升序处理,内层
    先于外层登记,与 dbgen.c 直接改 AST 的语义一致。

- static void Diag(JsonValue call, string msg)

- static string SqlOp(string op, bool flip)

- static string ExprOp(string op)

- class Wf

- static void WfText(Wf w, string s)

- static void WfFlush(Wf w)

- static void WfExpr(Wf w, JsonValue e)

- static void WfBind(Wf w, int kind, JsonValue expr)

- static void WfErr(Wf w, string msg)

- static bool MentionsParam(JsonValue e, string pname)
  - 表达式树里是否出现 lambda 参数名。

- static DbField AsColumn(Wf w, JsonValue e)
  - `p.field` -> 字段条目,否则 null。

- static void WfCol(Wf w, DbField f)

- static string LitMismatch(DbField f, JsonValue e)
  - 字面量/字段类型匹配(编译期)。不匹配返回消息,匹配返回 ""。

- static bool CheckValueType(Wf w, DbField f, JsonValue e)

- static void WfValue(Wf w, DbField f, JsonValue e)
  - 标量比较值:`?` 标记 + 按列类型的绑定。枚举包 (int) 转换,
    double 除纯字面量外包 (double) 转换。

- static string AggCall(Wf w, JsonValue e, out int outKind)
  - 条件里的聚合调用 `p.Count` / `p.Sum(x => x.col)` 等。返回
    聚合名(COUNT/SUM/AVG/MAX/MIN),不是聚合调用时返回 ""。
    畸形聚合发诊断并返回非空,但 w.Ok 已清。

- static string BindMethod(int kind)
  - 绑定类型 -> 链方法名。

- static JsonValue OpObj(string m, JsonValue args)

- static JsonValue OneOp(string m, JsonValue arg)

- static bool WhereMethod(Wf w, JsonValue e)
  - `p.f.Method(arg...)` 条件:空参 null 测试、两参范围测试、
    单参比较方法,以及字符串匹配与 In 系列。

- static void WhereCond(Wf w, JsonValue e)

- static JsonValue WhereFrag(JsonValue call, string cls, JsonValue lambda, Wf w)
  - 把 lambda 翻译成 SQL 片段树 + 绑定列表,返回片段树;不可翻译时
    发诊断并返回 null。

- class ExprSlot

- static List<ExprSlot> Eslots;

- static void CollectExprSlots()
  - 扫描方法声明的 `Expr<...>` 形参(形状检查即可,无需 binder)。

- static JsonValue ExprCall(string fn, JsonValue args)
  - `ExprNode.<fn>(args)` 构造调用树。

- static bool PnameChain(JsonValue e, string pname)
  - 接收者链是否最终落在 lambda 参数上(`o` in `o.region` / ...)。

- static bool ExprIsValue(JsonValue e, string pname)

- static DbField MemberField(List<DbField> fs, JsonValue e, string pname)

- static int LitKind(JsonValue e)

- static bool KindCompat(int fk, int lk)
  - 字面量 lk 能否与列类型 fk 比较(与 dbgen.c 的 dg_kind_compat 一致)。

- static bool ExprTypeOk(List<DbField> fs, JsonValue l, JsonValue r, string pname)

- static bool ExprCompatible(List<DbField> fs, JsonValue e, string pname)
  - lambda 体能否降级为 ExprSql 能构建 SQL 的树:列读、字面量、
    支持的二元运算(含操作数类型检查)、取反、p 链上
    StartsWith/EndsWith/Contains。其余(Eq/Gt/Between/IsNull/Like/In
    方法调用、捕获变量、聚合…)回退经典 SQL 片段路径。

- static JsonValue ExprTree(JsonValue e, string pname)
  - 构建 DbExpr 构造树(用户表达式树 -> ExprNode 调用树)。

- static JsonValue ExprWrap(string entity, string pname, JsonValue tree)
  - 包一层 `Expr<T>.From(DbExpr.Lambda(pname, tree))`。

- static bool ExprRewriteCall(JsonValue call)
  - 参数在 Expr slot 上的调用点:`M(p => body)` 降级为
    `M(Expr<T>.From(DbExpr.Lambda(...)))`。

- static void RewriteWhere(JsonValue call, string cls, bool exprOk)
  - `db.Query<T>().Where(...)`:Expr 路径替换参数;否则把调用点替换
    成 `.W(frag).P(v)...` 链(db_chain 指令)。

- static JsonValue BuildWhereChain(JsonValue recv, string cls, JsonValue lambda, JsonValue call)
  - 把 `recv.W(...)` 构建为 `.W(frag).P(v)...` 链树(Read 访问器用,
    不经过指令)。诊断挂在 w.Call 上。

- static void RewriteWhereIf(JsonValue call, string cls)
  - `WhereIf(cond, p => ...)`:片段与参数只在 cond 成立时应用。

- static void RewriteHaving(JsonValue call, string cls)
  - `Having(p => ...)`:同 Where,但落在 HAVING 子句。

- static void RewriteSet(JsonValue call, string cls, bool incr)
  - `Set(a => a.col, value)` / `SetIncr(a => a.col, delta)`。

- static void RewriteAgg(JsonValue call, string cls, string fn)
  - `Sum/Avg/Max/Min(a => a.col)`:终端返回列自身类型(AVG 恒 double)。

- static void RewriteCols(JsonValue call, string cls, bool only)
  - `InsertColumns/UpdateColumns(a => a.col)` / `IgnoreColumns(...)`。

- static JsonValue GbTree(JsonValue e, string pname, List<DbField> fields, out bool anyCol)
  - GroupBy 的标量表达式 -> 运行时拼出 SQL 片段的树:
    `a => a.minute / step * step` 变成
    `"(t.minute / " + Convert.ToString(step) + " * " + ... + ")"`。
    列出自实体字段,数值出自业务侧的整数表达式(由 ORM 自己格式化成
    十进制,业务侧仍然不出现 SQL)。不是这个形状时返回 null。

- static void RewriteGroupBy(JsonValue call, string cls)
  - `GroupBy(p => p.col)` -> `.GB("t.col")`。

- static void RewriteConflict(JsonValue call, string cls)
  - Insert 链上的 `OnConflict(a => a.col)` /
    `OnConflict(a => new { a.c1, a.c2 })` -> 每个键列一个 `.OC("col")`。
    不写 OnConflict 时冲突键取实体主键(生成侧的 Keys())。

- static void RewriteUpsertSet(JsonValue call, string cls, string what)
  - Insert 链上的冲突动作 `SetIncr/SetMax/SetMin(a => a.col)`:
    该列在冲突时累加 / 取大 / 取小,其余列被新值覆盖。

- static void RewriteDoNothing(JsonValue call)
  - Insert 链上的 `IfExistsDoNothing()` -> `.NOP()`。

- static void RewriteDistinct(JsonValue call)
  - `Distinct()` -> `.DISTINCT()`。

- static void RewriteOrderBy(JsonValue call, string cls, bool desc)
  - `OrderBy(p => p.col)` -> `.OB("t.col")`(desc -> OBD)。

- static string AggText(JsonValue call, JsonValue e, string pname, List<DbField> fields)
  - 聚合调用 `a.Sum(x => x.col)` / `a.Count()` 的 SQL 文本(不是聚合
    时返回 "";畸形聚合已发诊断,同样返回 "")。聚合不带参数,因此
    投影列表不会打乱 WHERE 的参数顺序。

- static string ProjAdd(string cls, string target, JsonValue items)
  - 投影形状登记:返回生成侧的方法名前缀 `Pj<N>`。
    items 每项是 { col: SQL 片段, field: 目标字段名, get: 读取器 }。

- static void RewriteProj(JsonValue call, string cls, string term)
  - `ToList<T>(a => new T { f = a.col, g = a.Sum(x => x.col), ... })`:
    分组聚合投影。列与聚合都由实体字段推导,业务侧不出现 SQL。
    终端:ToList/ToListAsync/ToOne/ToOneAsync。

- static void RewriteToList(JsonValue call, string cls, bool isAsync)
  - `ToList(a => a.field)` -> `.ToListCol("t.field")`(async 变体走
    `ToListColAsync`)。带类型实参的 `ToList<T>(...)` 走 RewriteProj。

- static void RewriteInclude(JsonValue call, string cls)
  - `Include(p => p.nav)`:LEFT JOIN 导航实体。

- static DbField SelColumn(JsonValue call, string cls, List<DbField> fields, string what)
  - 选择器列:`p => p.field` 的列,失败时发诊断并返回 null。

- static bool SelectHead(string name)

- static string ChainEntity2(JsonValue call, out int kind)
  - 沿 fluent 链接收者回溯到重写后的 `__DbBind.X_<T>` 根,返回实体名
    ("" 表示不是 db 链)。kind: 1 select, 2 insert, 3 update, 4 delete。
    与 dbgen.c 的 dg_chain_entity2 一致:未重写的链方法(SetDict/Page
    等真实方法)直接穿过,直到找到登记过的根。

- static bool IsRoot(JsonValue call, out string cls, out int kind, out bool sync)
  - `<recv>.Query<T>()` / `Select<T>()` / `Insert<T>(..)` / `Update<T>()`
    / `Delete<T>()` / `SyncStructure<T>()` 且 T 是已知实体类。

- static JsonValue DbBindCall(string name, JsonValue args)
  - `__DbBind.<name>(args...)` 调用树。

- static JsonValue AccConn(JsonValue acc)
  - 访问器 `obj.__Conn()` 调用树。

- static void VisitCall(JsonValue call)

- static void ChainMethods(JsonValue call, string entity, int ck)
  - 链上方法分派(调用点已被识别为 db 链)。

- static JsonValue BuildWhereTree(JsonValue wCall, string cls, JsonValue lambda, JsonValue call)
  - Where 调用树(Read 访问器内部用):Expr 路径替换参数,否则返回
    `.W(frag).P(v)...` 链树。

- static void Run(JsonValue req, JsonValue reply)


## GenDbEmit (class)

- static bool Is64(string type)
  - 声明类型是否是 64 位整数(需要 BIGINT 列与 64 位绑定/读取)。

- static string Getter(int kind)
  - 行的 Getter 名(按字段类型)。

- static string GetterOf(DbField f)
  - 行的 Getter 名(64 位整数列走 GetLong,否则同 Getter(kind))。

- static void Projections(StringBuilder b, string cls)
  - 聚合/列投影终端:GenDb.Projs 里属于本实体的每个形状生成
    `Pj<N>()` / `Pj<N>Async()` / `Pj<N>One()` / `Pj<N>OneAsync()`,
    结果映射到业务侧声明的目标类型(类型安全,业务侧不出现 SQL)。

- static int TyCode(int kind)
  - __DbBind.Ty 的列类型码:0 int/enum, 1 double, 2 bool, 3 string,
    4 64 位整数。

- static int TyCodeOf(DbField f)
  - 字段的列类型码(在 TyCode 之上区分 64 位整数)。

- static void BindField(StringBuilder b, string ind, DbField f, string obj)
  - 输出 `ps.AddXxx(o.<field>);` 绑定语句(ind 为缩进)。

- static void GenCols(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - `<Entity>Cols`:表名 + 列名常量 + Has/Kind/Require。

- static void GenEntity(StringBuilder b, string cls)
  - 查询类 __DbQ_<E>。

- static void GenInsert(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - __DbI_<E>:INSERT(行列表 / DbValues 字典 / Only/Skip)。

- static void GenUpdate(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - __DbU_<E>:UPDATE(Set/SetIncr/Where 链)。

- static void GenDelete(StringBuilder b, string cls, string tbl)
  - __DbD_<E>:DELETE(Where 链)。

- static string PkList(List<DbField> fs)
  - 复合主键的列清单(两列及以上时才有:一列的主键仍写在列定义里)。
    identity 列自带 PRIMARY KEY,不参与表级约束。

- static bool IsIdent(string s)
  - 只有标识符字符的名字才写进 DDL:索引名来自实体上的字面量,
    但生成侧仍然自己把关,不把任何别的东西拼进语句。

- static List<string> IndexArgs(string cls, List<DbField> fs, string tbl)
  - 实体 `[Index(Name=, Fields=, IsUnique=)]` -> 每条索引一行
    `__DbBind.EnsureIndex(db, name, tbl, cols, unique)` 的实参文本。
    `Fields` 写的是实体字段名,进语句的是它们的物理列名;既有表也补建
    缺失的索引(存在性由 __DbBind 按方言判断)。

- static void GenCodeFirst(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - __DbCF_<E>:CodeFirst(缺表建表,缺列加列;同步 + 异步)。

- static void Run()
  - 汇总 __DbBind + 各实体类,产出整段生成源码。


## GenForm (class)

- static void Translate(JsonValue req, JsonValue reply)

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)
  - 翻译单个 .zform 文档。失败返回 null;具体诊断(非法 kind 等)
    已写入 reply 的 error,纯解析失败则不设 error(C 端报通用错误)。

- static void ValidateFields(JsonValue reply, JsonValue arr, string file_name)
  - 校验字段树:每个字段的 kind 必须是合法 Zan 标识符(递归 kids)。

- static void ValidateColumns(JsonValue reply, JsonValue o, string f0, int idx)
  - 校验 DataGrid 的声明式列:columns 必须是对象数组,每列的
    "field" 是行实体上的字段路径(点分标识符),"type" 只认
    text/num/real/bool/date。带列的网格还必须写 "of"——列访问器
    读的是 "of" 类型的字段,缺省的 string 上没有它们。

- static bool ValidFieldPath(string s)
  - 列的 "field" 是实体上的字段路径:点分的合法标识符
    ("name"、"addr.city")。

- static string KindOf(JsonValue o)

- static string TypeOf(JsonValue o)
  - 字段的 Zan 类型:泛型控件在 "of" 里写类型实参
    (`"kind": "ListView", "of": "string"` -> `ListView<string>`),
    多个实参用逗号分隔。非泛型控件就是 kind 本身。

- static string DefaultTypeArgs(string kind)
  - 泛型控件缺省的类型实参:设计器放置的控件只有 kind,
    没有 "of",而泛型控件不写实参无法声明。它们展示的
    都是文本行,所以缺省即 string。

- static bool ValidTypeArgs(string of)
  - "of" 的每个类型实参必须是合法 Zan 类型名(允许点分限定)。

- static string TrimWs(string s)

- static bool IsContainer(JsonValue o)

- static int ObjNum(JsonValue o, string key, int def)

- static bool ObjBool(JsonValue o, string key)

- static string ObjStr(JsonValue o, string key)

- static int PrefH(JsonValue o)

- static string JoinOpts(JsonValue o)
  - Join options with '|' (mirrors JoinOpts).

- static void FieldSetup(StringBuilder b, JsonValue o, string vn)
  - 应用 schema 级属性(options/placeholder/required/defOn)。

- static void EmitColumns(StringBuilder b, JsonValue o, string vn)
  - DataGrid 的声明式列:columns 数组按现有 DataGrid<T> 泛型列 API
    展开,访问器直接读取 "of" 实体的字段
    ({"field":"name"} -> `__r => __r.name`),不经过任何字符串行
    中间层;字段名或列类型与实体不符时由编译器在生成代码上报错。
    数据本身由 code-behind 用 `grid.Bind(list)` 传入 List<of>。

- static void EmitColumn(StringBuilder b, JsonValue c, string vn)

- static void EmitHandlers(StringBuilder wire, JsonValue o, string vn, string dispatch)
  - 发射 on<Event> 处理器绑定(事件名 = 去掉前导 "on" 的键)。

- static string ParentExpr(JsonValue o, string parent)
  - 子控件归属的容器表达式：设计把它放在容器的某个“位”
    上时（`childTab`：标签页的第几页、分栏的哪个窗格），真正
    的父节点是那个内部容器（见 Control.SlotHost），而不是容器
    自身——直接挂在容器上的子控件会全部重叠在一起或压根
    就不参与布局。

- static int ChildrenCount(JsonValue o)

- static int EmitField(StringBuilder decls, StringBuilder body, StringBuilder wire, StringBuilder valid, JsonValue o, string parent, int id, List<string> used, bool freeMode, bool parentIsFlow, string dispatch, bool instanceFields)
  - 发射一个字段的声明/构造/事件接线/校验,递归容器 kids。
    返回下一个局部 id。与旧 formgen 的 fg_emit_field 逐字节等价。


## GenJson (class)

- static Dict <string, JsonValue> ClassOf;
  - 类名 → 类 JSON(含 kind/bases/fields)。

- static List<string> NeedNames;

- static List<bool> NeedRootList;

- static JsonValue Reply;

- static JsonValue ClassGet(string name)
  - Dict 无 Get 成员:用 TryGetValue 封装。

- static string FElemName;

- static int FElemKind;

- static void Run(JsonValue req, JsonValue reply)

- static void Err(JsonValue call, string msg)

- static void Warn(JsonValue field, string msg)

- static void Diag(JsonValue arr, JsonValue at, string msg)

- static void VisitCall(JsonValue call)

- static int NeedFind(string name)

- static int NeedAdd(string name)

- static bool IsIntName(string n)

- static bool IsLongName(string n)

- static bool IsFloatName(string n)

- static int BaseKind(string name)
  - 基元/枚举/类的基类分类(FK 常量)。

- static int FieldKind(string tname)
  - 字段整体分类;List 元素经 FElemName/FElemKind 输出。

- static string Fmt(string tpl, string arg)
  - %s 占位符替换(对应 C 端 snprintf 模板)。

- static int GenFields(StringBuilder out0, string cls, int mode, int depth, int emitted)
  - mode: 1 = binder(JsonValue -> entity),0 = tree(entity -> JsonValue),
    2 = writer(entity -> JSON 文本,emitted 计数字段数跨基类递归)。
    返回新的 emitted(仅 mode 2 有意义)。

- static void GenFieldW(StringBuilder out0, int fk, string fname, string tname)

- static void GenFieldB(StringBuilder out0, int fk, string fname, string tname)

- static void GenFieldT(StringBuilder out0, int fk, string fname, string tname)

- static void EmitBindClass()

- static void GenClass(StringBuilder out0, int idx)


## GenRoute (class)

- static JsonValue Reply;

- static Dict <string, JsonValue> ClassOf;

- static JsonValue Unit;

- static Dict <string, int> MetaKind;

- static Dict <string, string> MetaVal;

- static List<string> MetaOrder;

- static List<string> PName;

- static List<string> PType;

- static List<bool> PRequired;

- static List<string> PDef;

- static List<string> PDesc;

- static List<string> PWhere;

- static List<string> PmParam;

- static List<string> PmProp;

- static int ConstKind;

- static string ConstSval;

- static string RType;

- static bool RRequired;

- static bool RHasDefault;

- static bool RHasLabel;

- static string RWhere;

- static JsonValue ClassGet(string name)

- static void Run(JsonValue req, JsonValue reply)

- static string Esc(string s)
  - 转义为 Zan 字符串字面量正文(与 routegen 的 rg_put_qstr 一致:
    反斜杠/双引号/LF/CR/TAB 转义,其余原样)。

- static string Lower(string s)
  - 小写(对应 C 的 lower_inplace)。

- static int Atoi(string s)
  - 十进制解析(rg_cval 的 sval 反推 ival)。

- static string AttrHttpVerb(string n)
  - HTTP 动词属性名 → 动词,否则 ""。

- static bool AttrIsStructural(string n)
  - 结构性属性(按结构解释,不进入用户元数据)。

- static bool AttrNamed(JsonValue d, string nm)

- static string AttrStringArg(JsonValue d, string nm)
  - 属性类上第一个 <name>(...) 的字符串参数;无则 ""。

- static bool DerivesFrom(JsonValue cls, string basename)

- static bool IsController(JsonValue cls)

- static string ControllerDisplay(JsonValue cls)
  - 原始(未改名)简单类名:nsresolve 会给重名控制器改名,但
    orig_name 保留源码名,路由/视图键保持可读。

- static string ControllerModule(JsonValue cls)
  - 模块 = 应用根命名空间之下的部分:第一个 '.' 之后的段
    ("ZanWeb.Admin.System" → "Admin.System";根命名空间为空)。

- static string ControllerActionName(JsonValue cls, string disp, string mod)
  - action 名的控制器半段:模块 + 源码名,与视图键同一个拼法。
    
    始终带模块前缀,即使当前程序里没有重名控制器:名字是权限的持久标识,
    存进库、写进日志。若只在重名时才加前缀,以后新增一个同名控制器就会把
    已有的 "Users.Save" 集体改成 "Admin.System.Users.Save",库里的旧授权
    全部对不上 —— 静默失权。前缀恒定,加控制器就只是加控制器。

- static string ControllerToken(JsonValue cls)
  - 控制器 token = 原始类名去掉 "Controller" 后缀并小写。

- static string BuildPath(string clsTpl, string mTpl, string ctrlTok, string actionTok)

- static bool EvalConst(JsonValue e)
  - 枚举成员求值(仅需要名字;数值无消费者)。

- static void MetaSet(string key, bool onlyAbsent)

- static int MetaGetKind(string key)

- static string MetaGetVal(string key)

- static string PmapProp(string param)

- static void ApplyAttr(JsonValue attr)
  - 应用一次属性用法到元数据:属性类默认值(仅缺省时)先,
    用法的位置/命名参数后(总是覆盖)。

- static bool ReaderFor(string name)
  - reader 名 → (type, required, has_default, has_label, where)。
    返回 false 表示不是 reader。

- static string Literal(JsonValue e)
  - 字面量参数原样输出;非字面量(动态默认值是代码,不是文档)为 ""。

- static void ParamsAdd(JsonValue call)

- static bool PNameSeen(string nm)

- static void ParamAddOne(string nm, string ty, string desc, string def)

- static void ParamsAddPaged(JsonValue call)
  - this.Paged(defOrder, defLimit) 读每个列表屏共享的五个参数
    (Controller.Paged → ListQuery.From)。读发生在框架内部,因此
    在这里展开唯一调用点——否则 /api/docs 会把列表端点描述成
    不取任何参数。

- static void ScanCalls(string cls, string fn)
  - 扫描一个 action 方法体里的输入调用点(经 genmeta 的 calls 数组,
    按所属类/方法过滤)。

- static void ParamsFromPath(string path)
  - 路由路径段也是参数,即使 action 从未在读取中命名它
    ("{id:int}" → id/int/path)。

- static void EmitParams(StringBuilder out0)

- static void EmitFluent(StringBuilder out0, string title)

- static int GenController(JsonValue cls, StringBuilder handlers, StringBuilder reg)
  - 生成一个控制器的全部路由;返回路由数。

- static string FirstVerbAttr(JsonValue m)
  - 方法上第一个 HTTP 动词属性名,无则 ""。

- static string MethodVerb(JsonValue m)

- static bool BindableParams(JsonValue ps)
  - 参数类型能否由蹦床从请求绑定(string/int/long/double/bool)。

- static bool BindableType(string t)
  - 单个参数类型是否可绑定。

- static string BindLine(string t, string n)
  - 一条绑定语句:`long id = __c.NeedLong("id", "id");`。必填语义:
    缺失与非法同罪,都抛 ApiError(400),由蹦床统一应答——签名即文档,
    文档表在注册段以同一份名字先行播种。


## GenScene (class)

- static void Translate(JsonValue req, JsonValue reply)

- static string Esc(string s)
  - 转义为可嵌入 .zan 字符串字面量正文的文本。
    与旧 scenegen 的 sg_esc 一致:反斜杠/双引号前置反斜杠、
    LF -> \n、CR -> \r、TAB -> \t,其余原样(注意 CR 是保留的,
    与 GenCommon.Esc 不同)。

- static int ObjNum(JsonValue o, string key, int def)

- static string ObjStr(JsonValue o, string key)

- static bool ObjBool(JsonValue o, string key, bool def)

- static int ArrNum(JsonValue o, string key, int i, int def)
  - 数值数组属性的第 i 个分量("color"/"clearColor")。

- static bool UsedHas(List<string> used, string n)

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)
  - 翻译单个 .zscene 文档。解析失败返回 null(不设 error,
    由 C 端报通用错误)。


## ZanGen (class)

- static void Main()
