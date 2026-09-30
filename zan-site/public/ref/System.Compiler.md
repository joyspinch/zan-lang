# System.Compiler

> 源码: `stdlib/System/Compiler/GenCommon.zan`, `stdlib/System/Compiler/GenDb.zan`, `stdlib/System/Compiler/GenDbEmit.zan`, `stdlib/System/Compiler/GenForm.zan`, `stdlib/System/Compiler/GenHtml.zan`, `stdlib/System/Compiler/GenIndex.zan`, `stdlib/System/Compiler/GenJson.zan`, `stdlib/System/Compiler/GenRoute.zan`, `stdlib/System/Compiler/GenScene.zan`, `stdlib/System/Compiler/ZanGen.zan`


## DbField (class)

一个实体字段的编译期模型(对应 dbgen.c 的 dg_field_t)。

- string Name;
  - 字段名。

- string TypeName;
  - 声明的类型名。

- int Kind;
  - 字段类别: 0 int, 1 double, 2 bool, 3 string, 4 enum, 5 nav

- string Col;
  - 列名([Column(Name=...)] 覆盖, 默认字段名)。

- bool IsPk;
  - 是否主键([Column(IsPrimary)] 或名为 id)。

- bool IsIdent;
  - 是否自增 identity 列。

- bool IdentSet;
  - IsIdentity 是显式写出来的(而不是按主键推断)

- bool NotNull;
  - 是否 NOT NULL(默认仅主键, [Column(IsNullable)] 覆盖)。

- int StrLen;
  - 字符串最大长度([Column(StringLength)])。


## GenCommon (class)

ZanGen 各生成器共享的工具集: 回复组装、字符串转义、诊断与输出行。

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
  - 是否已用 Fail 标记失败(error 键存在)。

- static string Esc(string s)
  - 把 s 转义为可嵌入 .zan 字符串字面量正文的文本。
    规则与旧 formgen 的 fg_esc 一致:反斜杠翻倍、双引号转义、
    LF -> \n、CR 丢弃、TAB -> \t,其余原样。

- static bool IsIdent(string s)
  - 标识符安全检查(与旧 formgen 的 fg_is_ident 一致)。

- static void Line(StringBuilder b, int indent, string text)
  - 追加一行到输出缓冲(indent 个 4 空格缩进)。


## GenDb (class)

编译期类型化 ORM 查询降级: 把类型化查询链重写为 __DbBind 参数化绑定链。

- static JsonValue Unit;
  - 输入的编译单元 AST(req.unit)。

- static JsonValue Reply;
  - 输出容器: 重写指令与诊断写进这里(reply)。

- static Dict <string, JsonValue> ClassOf;

- static List<string> NeedNames;

- static Dict <int, string> Rewrote;

- static Dict <int, JsonValue> CallById;

- static int RwCount;
  - 已发出的重写指令数(链登记用)。

- static bool Any;
  - 本轮是否发出过任何重写(决定是否触发绑定类文本生成)。

- static bool SyncAll;
  - 出现过 SyncStructureAll 调用(需为全部表实体生成绑定)。

- static List<JsonValue> Projs;
  - 聚合/列投影形状(生成 Pj<N>)。

- static JsonValue ClassGet(string name)
  - 按名字查类/枚举声明, 不存在返回 null。

- static bool EnumGet(string name)
  - 名字是否对应已知枚举。

- static JsonValue AttrOf(JsonValue cls, string name)
  - 属性读取(与 dbgen.c 的 dg_attr 一致,名字精确匹配)。

- static JsonValue AttrArg(JsonValue attr, string key)
  - 具名属性参数(`Name = expr`)的表达式树,或 null。

- static JsonValue AttrPositional(JsonValue attr)
  - 属性的第一个位置实参（`[Table("t_x")]` 的 "t_x"）。
    genmeta 把位置实参序列化为裸表达式树（不是 assign 包装），
    只认 str/int 字面量。

- static string AttrStr(JsonValue attr, string key)
  - 具名属性参数取字符串字面量, 缺失或类型不符返回 ""。

- static bool AttrBool(JsonValue attr, string key, bool dflt)
  - 具名属性参数取 bool 字面量, 缺失或类型不符返回 dflt。

- static int AttrInt(JsonValue attr, string key, int dflt)
  - 具名属性参数取 int 字面量, 缺失或类型不符返回 dflt。

- static bool IsIntName(string n)
  - 类型名是否属于整型族(int/long/short/byte/sbyte/uint/ushort/
    ulong/char/nint)。

- static bool IsFloatName(string n)
  - 类型名是否属于浮点族(double/float/decimal)。

- static bool Is64(string type)
  - 声明类型是否是 64 位整数(列走 BIGINT 与 64 位绑定/读取)。

- static int ClassifyKind(string type)
  - 类型名 -> 字段类别: 0 int、1 double、2 bool、3 string、4 enum、
    5 导航实体、6 跳过(数组/泛型/可空/未知)。

- static List<DbField> FieldsOf(string cls, int depth)
  - 收集一个实体的全部字段(基类递归,与 dbgen.c 的 dg_collect_fields
    一致:跳过 static 与 DF_SKIP)。

- static void FieldsInto(string cls, int depth, List<DbField> outl)
  - FieldsOf 的递归体: 先基类后自身收集字段, depth 防继承环(>8 停)。

- static DbField FieldFind(List<DbField> fs, string name)
  - 按字段名在字段列表中查找, 没有返回 null。

- static string TableOf(string cls)
  - 实体的表名([Table("t_x")] 或 [Table(Name = "t_x")]，缺省用类名)。
    位置实参形态曾被静默丢弃（AttrArg 只认 assign 包装），回落成
    类名当表名，运行期报 no such table——探针实锤后补上。

- static List<JsonValue> IndexAttrs(string cls)
  - 实体上的 `[Index(Name = "...", Fields = "a,b", IsUnique = true)]`,
    按声明顺序;一个实体可以带多条。

- static bool IsTableEntity(string cls)
  - 类是否带 [Table] 属性(即 ORM 实体)。

- static string PkOf(string cls)
  - 主键列:显式 identity 优先,其次叫 id 的字段,没有返回 ""。

- static int NeedAdd(string name)
  - 实体名进 NeedNames(待生成绑定类), 返回其在列表中的下标。

- static JsonValue CallAt(int id)
  - 按调用点 id 取调用点, 不存在返回 null。

- static JsonValue TreeStr(string s)
  - 字符串字面量节点。

- static JsonValue TreeNull()
  - null 字面量节点。

- static JsonValue TreeId(string name)
  - 标识符节点。

- static JsonValue TreeGenericId(string name, string targ)
  - 泛型标识符:`Expr<T>` 的 "Expr" 带 targs 数组(反序列化器据此
    构建 inst_type_ref)。

- static JsonValue TreeMem(JsonValue obj, string name)
  - 成员访问节点 o.n。

- static JsonValue TreeCall(JsonValue callee, JsonValue args)
  - 调用节点 f(a)。

- static JsonValue TreeCall1(JsonValue callee, JsonValue arg)
  - 单参调用节点; arg 为 null 时即无参调用。

- static JsonValue OneArg(JsonValue arg)
  - 单元素参数数组; arg 为 null 时空数组。

- static JsonValue TreeBin(string op, JsonValue l, JsonValue r)
  - 二元运算节点 op l r。

- static JsonValue TreeCast(string t, JsonValue e)
  - 类型转换节点 (t)e。

- static JsonValue TreeLam(List<string> ps, JsonValue body)
  - lambda 节点 (p1..pn) => body。

- static int ArgsCount(JsonValue call)
  - 调用点实参个数(缺 args 算 0)。

- static string LamParam(JsonValue lam)
  - lambda 节点的第一个形参名;结构不完整时给空串。

- static int ArrLen(JsonValue o, string key)
  - o[key] 数组的长度;缺失算 0。

- static JsonValue ArgAt(JsonValue call, int i)
  - 调用点第 i 个实参, 越界返回 null。

- static JsonValue NewOp(string op, JsonValue call)
  - 针对调用点的重写指令头(op + id)。

- static void AddRewrite(JsonValue op)
  - 把重写指令追加进 reply 并计数(RwCount 供链登记判断)。

- static void MarkChain(JsonValue call, string entity, int ck)
  - 链重写成功后登记调用点:下游调用点沿 `call#<id>` 回溯链根时,每一环
    都必须在 Rewrote 里。children-first 分配 id、按 id 升序处理,内层
    先于外层登记,与 dbgen.c 直接改 AST 的语义一致。

- static void Diag(JsonValue call, string msg)
  - 在调用点位置向 reply.errors 追加一条错误诊断。

- static string SqlOp(string op, bool flip)
  - Zan 比较运算符 -> SQL 文本(两侧各一个空格); flip 时反转
    < > 方向; 不支持的运算符返回 ""。

- static string ExprOp(string op)
  - 表达式树支持的运算符白名单(原样返回), 不支持返回 ""。

- class Wf
  - 单个 Where/Having lambda 的翻译状态: 文本段与表达式段交替累积
    成片段树, Binds 记录按出现顺序的绑定值。

- static void WfText(Wf w, string s)
  - 追加一段 SQL 文本。

- static void WfFlush(Wf w)
  - 把累积文本折叠进片段树并清空缓冲。

- static void WfExpr(Wf w, JsonValue e)
  - 追加一个表达式段(先折叠未完的文本)。

- static void WfBind(Wf w, int kind, JsonValue expr)
  - 记录一个绑定值; 超过 64 个置 Ok=false。

- static void WfErr(Wf w, string msg)
  - 发一条 Where 翻译诊断并置 Ok=false。

- static bool MentionsParam(JsonValue e, string pname)
  - 表达式树里是否出现 lambda 参数名。

- static DbField AsColumn(Wf w, JsonValue e)
  - `p.field` -> 字段条目,否则 null。

- static void WfCol(Wf w, DbField f)
  - 输出限定列名 t.<col>。

- static string LitMismatch(DbField f, JsonValue e)
  - 字面量/字段类型匹配(编译期)。不匹配返回消息,匹配返回 ""。

- static bool CheckValueType(Wf w, DbField f, JsonValue e)
  - 字面量与列类型不匹配时发诊断返回 false; 匹配(或不涉及字面量)
    返回 true。

- static void WfValue(Wf w, DbField f, JsonValue e)
  - 标量比较值:`?` 标记 + 按列类型的绑定。枚举包 (int) 转换,
    double 除纯字面量外包 (double) 转换。

- static string AggCall(Wf w, JsonValue e, out int outKind)
  - 条件里的聚合调用 `p.Count` / `p.Sum(x => x.col)` 等。返回
    聚合名(COUNT/SUM/AVG/MAX/MIN),不是聚合调用时返回 ""。
    畸形聚合发诊断并返回非空,但 w.Ok 已清。

- static string BindMethod(int kind)
  - 绑定类型 -> 链方法名（`__` 前缀 = 降级专用，应用层不得手写）。

- static JsonValue OpObj(string m, JsonValue args)
  - 链操作 { m, args }。

- static JsonValue OneOp(string m, JsonValue arg)
  - 单参链操作; arg 为 null 时无参。

- static bool WhereMethod(Wf w, JsonValue e)
  - `p.f.Method(arg...)` 条件:空参 null 测试、两参范围测试、
    单参比较方法,以及字符串匹配与 In 系列。

- static void WhereCond(Wf w, JsonValue e)
  - 条件树递归翻译: &&/||、!、bool 列直读、方法形式与运算符形式的
    比较、null 测试、聚合比较。失败置 w.Ok=false。

- static JsonValue WhereFrag(JsonValue call, string cls, JsonValue lambda, Wf w)
  - 把 lambda 翻译成 SQL 片段树 + 绑定列表,返回片段树;不可翻译时
    发诊断并返回 null。

- class ExprSlot
  - 一个 `Expr<T>` 形参槽: 所在类/方法/参数位与实体名。

- static List<ExprSlot> Eslots;
  - 本单元全部 Expr 槽, 由 CollectExprSlots 填充。

- static void CollectExprSlots()
  - 扫描方法声明的 `Expr<...>` 形参(形状检查即可,无需 binder)。

- static JsonValue ExprCall(string fn, JsonValue args)
  - `ExprNode.<fn>(args)` 构造调用树。

- static bool PnameChain(JsonValue e, string pname)
  - 接收者链是否最终落在 lambda 参数上(`o` in `o.region` / ...)。

- static bool ExprIsValue(JsonValue e, string pname)
  - 表达式是否是"值侧": p 链读或 int/double/str/bool 字面量。

- static DbField MemberField(List<DbField> fs, JsonValue e, string pname)
  - `p.field` 形态的字段条目, 否则 null。

- static int LitKind(JsonValue e)
  - 字面量类别: 0 int、1 double、2 bool、3 string, 非字面量 -1。

- static bool KindCompat(int fk, int lk)
  - 字面量 lk 能否与列类型 fk 比较(与 dbgen.c 的 dg_kind_compat 一致)。

- static bool ExprTypeOk(List<DbField> fs, JsonValue l, JsonValue r, string pname)
  - 二元运算两侧的列/字面量类型是否可比较(任一侧不涉及列时恒真)。

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
  - 把 `recv.W(...)` 构建为 `.__Frag(frag).__P(v)...` 链树(Read 访问器
    用,不经过指令)。诊断挂在 w.Call 上。

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
  - 名字是否是查询链头方法(访问器上可直接接链的那批)。

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

- static JsonValue DaoClass(string entity)
  - 返回实体对应的手写 DAO 类。DAO 名称按实体的稳定短名约定为
    `<Entity>Dao`;生成器只选择非静态、非 private/protected 的实例方法。
    这样 `this.User` 的领域方法仍由项目 DAO 负责，GenDb 不复制业务 SQL。

- static bool DaoAmbiguous(string entity)
  - `<Entity>Dao` 是否有多个同名/同 orig 的声明。

- static string DaoName(JsonValue dao, string entity)
  - DAO 的真实类名(命名空间 mangling 后的名字); dao 为 null 时
    按约定名 `<entity>Dao`。

- static bool DaoHasMethod(JsonValue dao, string name, int argc)
  - DAO 是否有指定名字与参数个数的非静态、非 private/protected
    实例方法。

- static JsonValue DaoCallTree(string dao, string method, JsonValue conn, JsonValue args)
  - 构造 `new <Dao>(conn).Method(args...)` 的表达式树。此树只描述
    编译期已经确认的 DAO 调用，连接仍由 accessor 所属对象提供。

- static void VisitCall(JsonValue call)
  - 单个调用点分派: SyncStructureAll、Expr 槽参数、访问器根
    (Insert/Update/Delete/Read/DAO 调用/链头)、ORM 根、链方法。

- static void ChainMethods(JsonValue call, string entity, int ck)
  - 链上方法分派(调用点已被识别为 db 链)。

- static JsonValue BuildWhereTree(JsonValue wCall, string cls, JsonValue lambda, JsonValue call)
  - Where 调用树(Read 访问器内部用):Expr 路径替换参数,否则返回
    `.W(frag).P(v)...` 链树。

- static void Run(JsonValue req, JsonValue reply)
  - 入口: 建立类符号表, 收集 Expr 槽, 按 id 升序(children-first)
    访问全部调用点; 发生过重写且无诊断时调用 GenDbEmit 生成
    绑定类文本。


## GenDbEmit (class)

实体绑定类文本生成：`<Entity>Cols`、`__DbQ_/I_/U_/D_/CF_`
各门面、`__DbM_` 取值/赋值口与 `__DbB_` 行来源,以及汇总类
`__DbBind`。由 `GenDb` 在收集到实体后调用
`Run` 产出整段源码。

- static bool Is64(string type)
  - 声明类型是否是 64 位整数(需要 BIGINT 列与 64 位绑定/读取)。

- static string Bl(bool v)
  - 布尔值的 Zan 字面量("true"/"false"),拼进生成的代码。

- static string Getter(int kind)
  - 行的 Getter 名(按字段类型)——投影仍按目标类型直接取值。

- static string ColArgs(DbField f)
  - `m.Col(...)` 的实参文本：列名、字段名、类型码、是否 64 位、
    主键、自增、非空、字符串长度。

- static void Projections(StringBuilder b, string cls)
  - 聚合/列投影终端:GenDb.Projs 里属于本实体的每个形状生成
    `Pj<N>()` / `Pj<N>Async()` / `Pj<N>One()` / `Pj<N>OneAsync()`,
    结果映射到业务侧声明的目标类型(类型安全,业务侧不出现 SQL)。

- static bool Has(List<string> l, string v)
  - 实体的取值/赋值口 `__DbM_<T>` 与行来源 `__DbB_<T>`。列集合和 SQL
    仍由运行期决定；这里仅把列元数据分发到实体的直接字段访问，避免为每个
    实体拉入通用反射读写表。

- static void GenAccess(StringBuilder b, string cls)
  - 生成 `__DbM_<E>`(按列字段名分发到实体直接字段的
    Read/Bind)与 `__DbB_<E>`(IOrmRows:实体列表按行按列绑定)。
    导航字段(Kind 5)不参与列绑定。

- static bool IsIdent(string s)
  - 只有标识符字符的名字才写进 DDL:索引名来自实体上的字面量,
    但生成侧仍然自己把关,不把任何别的东西拼进语句。

- static List<string> IndexArgs(string cls, List<DbField> fs, string tbl)
  - 实体 `[Index(Name=, Fields=, IsUnique=)]` -> 每条索引一行
    `m.Index(name, cols, unique)` 的实参文本。`Fields` 写的是实体
    字段名,进语句的是它们的物理列名;既有表也补建缺失的索引
    (存在性由 OrmDialect 按方言判断)。

- static void GenCols(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - `<Entity>Cols`:表名 + 列名常量 + Has/Kind/Require + 运行期元数据。
    元数据只建一次(静态缓存),SQL 片段的缓存挂在它上面。

- static void GenEntity(StringBuilder b, string cls)
  - 查询门面 __DbQ_<E>:链式方法转发给 OrmSelect,只有行映射与终端
    需要实体类型。

- static void GenInsert(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - 插入门面 __DbI_<E>:行列表/动态列/Only/Skip/Upsert 都转发给
    OrmInsert,只有实体类型的重载需要在这里。

- static void GenUpdate(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - 更新门面 __DbU_<E>：Set/SetIncr/Where 链转发给 OrmUpdate；
    实体整体更新在运行期按元数据展开。

- static void GenDelete(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - 删除门面 __DbD_<E>：Where 链转发给 OrmDelete。

- static void GenCodeFirst(StringBuilder b, string cls, List<DbField> fs, string tbl)
  - CodeFirst 门面 __DbCF_<E>：建表/加列/补索引都由 OrmSync 按元数据做。

- static void Run()
  - 汇总 __DbBind + 各实体类,产出整段生成源码。


## GenForm (class)

设计文档到合成 partial class 的代码生成器。
与 src/compiler/formgen.c 等价(输出逐字节一致);入口是
`Translate`,由 design 请求分派 .html 设计稿进来
(.zform JSON 设计文档已删,旧项目先转 .html—— DesignerHtml
FromJsonDoc 是规范转换器)。

- static List<string> compNames;
  - 本轮 design 请求携带的用户组件文档（design 请求的
    components[i]: { name, text }，来自工程 components/*.zcomp）。
    组件名（文件基名）→ 已解析 JSON。引用展开（ExpandRefNode）
    按名查表；Translate 入口填一次。

- static List<JsonValue> compDocs;

- static void Translate(JsonValue req, JsonValue reply)
  - 翻译 design 请求里所有 .html 设计稿(.zscene 归 GenScene):
    逐个解析、校验、合成为 .zan 源码,经 GenCommon.AddSource
    追加进 reply 的 sources;失败时写 reply 的 error 后返回。

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)
  - 翻译单个设计文档(JSON 模型,来自 DesignerHtml.ToJsonDoc)。
    失败返回 null;具体诊断(非法 kind 等)已写入 reply 的 error,
    纯解析失败则不设 error(C 端报通用错误)。

- static JsonValue CompDoc(string name)
  - 组件名对应的组件文档；未登记返回 null。

- static JsonValue DeepCopy(JsonValue v)
  - JsonValue 深拷贝：对象/数组递归新建，标量节点原样共享
    （不可变）。组件文档缓存 per 实例展开必须用它。

- static string ExpandRefs(JsonValue arr, int depth)
  - 展开字段数组里的用户组件引用节点（带 "ref" 键）。就地
    改写：引用节点变成 Panel 外壳（名字/几何/停靠/绑定等
    自身键全部保留），组件文档的字段匿名挂进 kids——内部名
    不进宿主命名空间、内部事件不接线（组件内部是引用的黑
    盒；宿主要接的是引用节点自己的 on* 绑定）。组件文档是
    流式时，其顶层字段标 dock=1 让生成代码竖排；自由时保持
    绝对坐标。组件可引用组件（递归，防环）。返回 "" 或错误。

- static void ValidateFields(JsonValue reply, JsonValue arr, string file_name)
  - 校验字段树:每个字段的 kind 必须是合法 Zan 标识符(递归 kids)。

- static void ValidateColumns(JsonValue reply, JsonValue o, string f0, int idx)
  - 校验 DataGrid 的声明式列。列有两个声明面：手写文档的
    columns 对象数组，与设计器列编辑器写出的 options
    "标题:字段" 选项串（设计器以 options 为列序真相，columns
    条目只是同下标的声明属性跟随表）。任一面声明了带字段的
    列，"of" 就是必需的——列访问器读的是 "of" 类型的字段，
    缺省的 string 上没有它们。

- static bool ValidFieldPath(string s)
  - 列的 "field" 是实体上的字段路径:点分的合法标识符
    ("name"、"addr.city")。

- static string KindOf(JsonValue o)
  - 字段的控件 kind:取 "kind" 字符串值,缺省或非字符串返回 ""。

- static void ScanHeavyKinds(JsonValue node, bool[]flags)
  - 设计树重家族 kind 扫描（生成头部按需发射 using 用）：
    flags[0]=Chart（ChartHost）、flags[1]=DataTable（DataGrid/
    Transfer）、flags[2]=WebView（WebViewBox）、flags[3]=Cef
    （CefBrowserBox）。kind 别名与 TypeOf 同步归一
    （Table/DataTable→DataGrid、Chart→ChartHost）。递归数组与
    对象的 "kids"/"fields" 子树；其余节点忽略。

- static string TypeOf(JsonValue o)
  - 字段的 Zan 类型:泛型控件在 "of" 里写类型实参
    (`"kind": "ListView", "of": "string"` -> `ListView<string>`),
    多个实参用逗号分隔。非泛型控件就是 kind 本身。

- static string QualifyKind(string kind)
  - 控件 kind → 命名空间限定名（A314）。生成类落在全局命名空间，
    裸 `Panel`/`Button` 会优先绑定到用户项目的同名全局类（全局
    声明压过 using import），字段与构造必须发射限定名才能锁住
    stdlib 真身（遮蔽下的限定逃逸，见 A312）。名单与
    ControlFactory.Kinds() 对齐（外加泛型控件 ListView/Dropdown）；
    名单外的 kind 是用户自定义 Control 派生类，保持裸名让它解析
    到用户项目自己的类。

- static List<string> NonConstructibleKinds()
  - 设计文档里不允许实例化的 kind：抽象基类（ChoiceGroup 是
    RadioGroup/CheckboxGroup 的基类，自身无零参构造，GenForm 发射
    `new ChoiceGroup()` 会得到 labels/children 为 null 的半初始化实例，
    首帧即空引用——html_gallery 曾因此整卡崩溃）。名单内 kind 一律
    在校验期报错，具体子类不受影响。

- static bool IsStdWidgetKind(string kind)

- static bool IsHmiKind(string kind)
  - 声明在 Gui.Hmi 命名空间的保留控件（工业/告警族，见
    Hmi/Alarm、Indicator、Gauge、NumPad、Trend、EquipPanel）。
    QualifyKind 据此发射 `Gui.Hmi.X` 而不是 `Gui.Widget.X`。

- static string DefaultTypeArgs(string kind)
  - 泛型控件缺省的类型实参:设计器放置的控件只有 kind,
    没有 "of",而泛型控件不写实参无法声明。它们展示的
    都是文本行,所以缺省即 string。

- static bool ValidTypeArgs(string of)
  - "of" 的每个类型实参必须是合法 Zan 类型名(允许点分限定)。

- static string TrimWs(string s)
  - 剥掉 s 首尾的空格与制表符(不含换行)。

- static bool IsContainer(JsonValue o)
  - 字段是否为容器:带 "kids" 数组即视为容器,可递归放下级控件。

- static int ObjNum(JsonValue o, string key, int def)
  - 读对象的整数属性:存在且为数字时取其值,否则返回 def。

- static bool ObjBool(JsonValue o, string key)
  - 读对象的布尔属性:缺省或非布尔一律 false。

- static string ObjStr(JsonValue o, string key)
  - 读对象的字符串属性:缺省或非字符串返回 ""。

- static int PrefH(JsonValue o)
  - 控件的布局首选高度:取 "fh"(设计器里每控件可调),
    缺省或非正数回退 32 逻辑像素。

- static string JoinOpts(JsonValue o)
  - Join options with '|' (mirrors JoinOpts).

- static void EmitSetProp(StringBuilder b, string vn, string key, string v)
  - 发射一条字符串 SetProp：值超长（base64 资源等）时拆成
    多段字面量相加，绕开单字面量 4095 字符的词法上限。

- static void FieldSetup(StringBuilder b, JsonValue o, string vn)
  - 应用 schema 级属性(options/placeholder/required/defOn)。

- static void EmitColumns(StringBuilder b, JsonValue o, string vn)
  - DataGrid 的声明式列:columns 数组按现有 DataGrid<T> 泛型列 API
    展开,访问器直接读取 "of" 实体的字段
    ({"field":"name"} -> `__r => __r.name`),不经过任何字符串行
    中间层;字段名或列类型与实体不符时由编译器在生成代码上报错。
    数据本身由 code-behind 用 `grid.Bind(list)` 传入 List<of>。
    设计器文档的列序真相在 options（"标题:字段" 选项串，列编辑
    器写的就是它），columns[i] 只是同下标的声明属性跟随表——
    options 非空时按它合成列，两处声明在 EmitColumn 汇合。

- static JsonValue ColEntryAt(JsonValue cols, int i)
  - columns 数组第 i 个条目（缺失/非数组/越界/非对象返回 null）。

- static string OptFieldOf(string opt)
  - 设计器列选项 "标题:字段" 的字段部分（首个冒号之后；没有
    冒号返回 ""）。与 Designer.ColField 同约定。

- static string OptFieldAt(JsonValue opts, int i)
  - options 数组第 i 项的字段部分（opts 非数组/越界返回 ""）。

- static string OptTitleOf(string opt)
  - 列选项的标题部分（首个冒号之前；没有冒号是整串）。

- static void EmitOptionColumn(StringBuilder b, string vn, string opt, JsonValue entry, string of)
  - 把一条 options 列选项（"标题:字段"）连同同下标的 columns
    声明条目发射成列：field/title 以显式条目优先，缺省从选项串
    解析，条目的其余声明键（width/type/align/聚合…）原样并入，
    走与手写 columns 相同的 EmitColumn 通道。只有标题的列在无
    "of"（DataGrid<string>，行即文本）时发射 `__r => __r` 直读
    行文本；有 "of" 时无法构成访问器，跳过（与手写 columns 缺
    field 的行为一致）。

- static void EmitColumn(StringBuilder b, JsonValue c, string vn)
  - 把一列的声明展开成 DataGrid<T> 的链式列构造调用
    (标题/宽度/类型列/对齐/前缀后缀/聚合/冻结/编辑回写等),
    非法或缺失的 "field" 直接跳过不发射。

- static void EmitHandlers(StringBuilder wire, JsonValue o, string vn, string dispatch)
  - 发射 on<Event> 处理器绑定(事件名 = 去掉前导 "on" 的键)。

- static string ParentExpr(JsonValue o, string parent, string parentKind)
  - 子控件归属的容器表达式：设计把它放在容器的某个“位”
    上时（`childTab`：标签页的第几页、分栏的哪个窗格），真正
    的父节点是那个内部容器（见 Control.SlotHost），而不是容器
    自身——直接挂在容器上的子控件会全部重叠在一起或压根
    就不参与布局。slot-host 容器（Tabs/SplitPanel）的子级缺
    `childTab` 就是第 0 位：设计器模型里 0 值被 HTML 序列化
    跳过（data-child-tab="0" 不落盘），按“挂容器本体”处理会让
    这些子控件运行时整个不可见（Tabs 只渲染页容器），设计里
    却明明看得到。

- static bool HasSizeClass(string classes)
  - 是否已经由控件自己声明尺寸档。按空格分词，避免把
    `small-card` 这类普通语义类误判成尺寸覆盖。

- static int ChildrenCount(JsonValue o)
  - 直接子控件数:无 "kids" 数组则为 0。自动布局用它估容器高度。

- static int EmitField(StringBuilder decls, StringBuilder body, StringBuilder wire, StringBuilder valid, JsonValue o, string parent, string parentKind, int id, List<string> used, bool freeMode, bool parentIsFlow, string dispatch, bool instanceFields, bool anon, string defaultSizeClass)
  - 发射一个字段的声明/构造/事件接线/校验,递归容器 kids。
    返回下一个局部 id。与旧 formgen 的 fg_emit_field 逐字节等价。
    `anon`:组件引用展开的内部字段——不生成具名声明、不接内部
    事件(组件内部对宿主不透明);`compAbs` 经壳节点的
    "compAbs" 键折叠进 freeMode(自由组件文档的子树恒走绝对几何)。

- static int ContainerPrefH(JsonValue o)
  - 容器在自动堆叠（Dock.Top / 流式格）里的偏好高度：
    引用展开的壳带 "ph" 键（组件画布高），其余容器维持
    44 + 44 * 子数的旧估算。


## GenHtml (class)

GenHtml.zan -- .html 声明文档 → 建树类的代码生成器
（WEB_GUI_ROADMAP P5）。与运行时 App.LoadHtml 共用 System.Web 的
解析记录（HtmlParser.Parse 的 WDoc/WNode）：编译期把 .html 展开
成纯建树代码，发布不携带 HTML 文本与解析器；动态装载仍走
App.LoadHtml（同一解析器，同一几何）。

输入(design 请求的 files[i])：
{ "name": "ui.html", "text": "<html 文本>", "emitMain": 忽略 }
输出：reply.sources 里追加一段名为 name 的 .zan 源码，形态
（ui.html → 类 UiHtml；类名 = 文件基名帕斯卡化 + Html 后缀）：
public class UiHtml {
public static string Css = "...<style> 与 style 属性收集...";
public static Control Build(HtmlHandlers handlers) { ... }
}
调用方：`Control root = UiHtml.Build(handlers);` 之后自行
`app.UseAppCss(UiHtml.Css)`（或并进自己的样式表）再挂树。

样式 scoping（Vue <style scoped> 同语义）：多份文档的 Css 合装
进同一张 app 表时，裸标签/类选择器会跨文档互中。生成期给每个
元素混入文档作用域类 zs-<基名>，并把文档 CSS 每个复合块补上
该类——规则只命中本文档的元素（System.Web.HtmlScope）。运行期挂载件
（列表行模板等 Build 之外长出来的控件）不携作用域类，其样式
放皮肤层（全局通道）——与 Vue 的组件样式不泄漏子组件同一取舍。

生成依据是 WDoc.nodes 声明记录（tag/父下标/属性/文档序内容）——
与运行时同一次解析的产物，天然同构。本文件只依赖 System.Web
纯层（不能 using Gui：生成器 exe 的活名闭包会滤掉 Gui 的
传递依赖，UiDriver 的 Json.Serialize 编译不进来）。

- static void Translate(JsonValue req, JsonValue reply)
  - 翻译 design 请求里所有 .html/.htm 文件；失败写 reply 的 error。

- static bool Emit(string path, WDoc wd, StringBuilder outp)

- static void EmitDecl(StringBuilder b, WNode n, int idx, string genCls, string zs)
  - 一条记录 = 一个变量的声明 + 属性协议（构造；文本与挂树在
    EmitContent 按文档序发射）。

- static void EmitContent(StringBuilder b, WNode n, int idx)
  - 父节点 idx 的内容游走：文本片 AddText、子节点 AddChild，
    严格 items 文档序（只有容器记录带文本项——捕获控件的文本
    走自己的构造协议，void 元素无内容）。

- static void EmitWiring(StringBuilder b, WNode n, int idx)
  - data-on-* 接线（树建完统一挂，html 文档序不敏感）。节点声明了
    `data-arg` 时走带参 WireArg（实参闭包捕获）。

- static string ClassName(string path)
  - "src/ui.html" → "UiHtml"（基名帕斯卡化 + Html；非标识符字符
    当分节符丢弃，数字开头的段补 _）。

- static string BaseName(string path)

- static string ScopeClassOf(string path)
  - 文档路径 → 作用域类：基名去扩展名后小写、非字母数字折 '-'。
    与生成类名同源（同目录同名文档本来就会撞生成类名），
    作用域类继承该唯一性。改写本身在 System.Web.HtmlScope 共用。


## GenIndex (class)

项目语义索引导出器: 从单元元数据生成 routes/entities/index 三个产物。

- static JsonValue unit;
  - Seed 播种的单元元数据(classes/files 由此读取)。

- static List<string> files;
  - 单元源文件表:file_id -> 路径,来自 unit 的 "files"。

- static string Base;
  - 可选的路径前缀裁剪。工具为了避开 cwd 歧义常把绝对路径喂给编译器,
    但索引消费者要的是工作区相对路径;调用方设置本字段即可(Seed 不会
    重置它)。空串表示按编译器原样输出。

- static void Seed(JsonValue u)
  - 从单元元数据建立查表状态,并给 GenRoute / GenDb 播种,以便直接调用
    它们的判定函数(两者的 Run 会自己重播种,先后调用互不影响)。

- static string FileOf(int id)
  - file_id -> 源文件路径(受 Base 裁剪)。旧编译器不导出 files 表时
    回退为 ""。

- static string Slashes(string p)
  - 把路径里的反斜杠统一成正斜杠(索引进产物前规整)。

- static string Relative(string path)
  - 相对化:路径带 Base 前缀(忽略尾斜杠差异)时裁掉前缀,
    否则按原样返回。

- static JsonValue NamedArg(JsonValue attr, string key)
  - 命名实参 `Name = <expr>` 的右值表达式树,没有则 null。

- static JsonValue AttrOf(JsonValue owner, string name)
  - 属性名匹配的第一个属性(类或成员),没有则 null。

- static string ExprText(JsonValue e)
  - 表达式树 -> 可读文本("PermBit.Create|PermBit.Update"、"true"、"8")。
    索引给人和 AI 看,保留源码写法比求值成数字更有用。

- static string CustomText(JsonValue cls, JsonValue m, string key)
  - `Custom(...)` 上一个命名实参的文本,方法优先于类。

- static string AuthOf(JsonValue cls, JsonValue m)
  - 授权门:`Custom(Authorization = CustomAuthorization.X)` -> "X"。

- static JsonValue RoutesDoc()
  - 路由表。筛选与路径拼装跟 GenRoute.GenController 同一套判定:
    只有被 [Route]/[Http*] 标注(或 ApiController 的公开方法)、参数形状
    可绑定的方法才是 action。

- static JsonValue EntitiesDoc()
  - 实体表:每个 `[Table]` 类的物理表、列、导航、索引与生成门面名。

- static JsonValue ManifestDoc(int routes, int entities)
  - index.json 清单:版本、生成者、源文件数与路由/实体条数、
    产物文件名列表及全部源文件(相对路径)。

- static int WriteTo(JsonValue u, string dir)
  - 把三份产物写进 `dir`(目录不存在则创建)。返回写出的路由条数。


## GenJson (class)

System.Json 实体映射器: 为 Json.Deserialize/Serialize 生成 __JsonBind 绑定类与重写指令。

- static Dict <string, JsonValue> ClassOf;
  - 类名 → 类 JSON(含 kind/bases/fields)。

- static List<string> NeedNames;
  - 本单元实际引用到的类（生成 __JsonBind 的顺序）。

- static List<bool> NeedRootList;
  - 与 NeedNames 平行：该类是否需要 List 根形态（DL_/SL_ 入口）。

- static JsonValue Reply;
  - 当前编译单元的 reply：诊断与 sources 回填都经它写入。

- static JsonValue ClassGet(string name)
  - Dict 无 Get 成员:用 TryGetValue 封装。

- static string FElemName;

- static int FElemKind;

- static void Run(JsonValue req, JsonValue reply)
  - 入口：扫描 unit 的 classes/calls，收集 Json.Deserialize<T>/
    Serialize<T> 调用点，生成并回填 __JsonBind 源码与重写指令。

- static void Err(JsonValue call, string msg)
  - 调用点错误：写入 reply.errors（带 file/line/col/msg）。

- static void Warn(JsonValue field, string msg)
  - 字段警告（如不支持的类型被跳过）：写入 reply.warnings。

- static void Diag(JsonValue arr, JsonValue at, string msg)
  - 向诊断数组追加一条 {file,line,col,msg}（取自 at 的整数字段）；
    arr 为 null 时静默不产生诊断。

- static void VisitCall(JsonValue call)
  - 处理一个调用点：校验显式类型参数、类可达性、List<Class> 形态，
    通过后按 Deserialize/Serialize × 单个/List 组合决定 D_/S_/DL_/SL_ 前缀，
    下发 json_call 重写指令。不合法时写诊断并跳过。

- static int NeedFind(string name)
  - 类名在 NeedNames 中的下标，未收集过返回 -1。

- static int NeedAdd(string name)
  - 收集/去重一个被引用的类；已存在时返回原下标。

- static bool IsIntName(string n)
  - 是否映射为 FK 1（Int）：int/short/byte/sbyte/uint/ushort/char。

- static bool IsLongName(string n)
  - 是否映射为 FK 2（Long）：long/ulong。

- static bool IsFloatName(string n)
  - 是否映射为 FK 3（Float）：double/float/decimal。

- static int BaseKind(string name)
  - 基元/枚举/类的基类分类(FK 常量)。未知类型返回 0（SKIP，字段被跳过并告警）。

- static int FieldKind(string tname)
  - 字段整体分类;List 元素经 FElemName/FElemKind 输出。

- static string Fmt(string tpl, string arg)
  - %s 占位符替换(对应 C 端 snprintf 模板)。

- static int GenFields(StringBuilder out0, string cls, int mode, int depth, int emitted)
  - mode: 1 = binder(JsonValue -> entity),0 = tree(entity -> JsonValue),
    2 = writer(entity -> JSON 文本,emitted 计数字段数跨基类递归)。
    返回新的 emitted(仅 mode 2 有意义)。

- static void GenFieldW(StringBuilder out0, int fk, string fname, string tname)
  - writer（mode 2）字段行：把一个字段直写成 JSON 文本
    （数字/枚举转十进制、bool 写 true/false、string 经
    Encoding.JsonEscape、类嵌套调 W_、List/List2 按元素种类展开）。
    List 元素种类读 FElemName/FElemKind。

- static void GenFieldB(StringBuilder out0, int fk, string fname, string tname)
  - binder（mode 1）字段行：从 JsonValue 宽容地读回字段
    （缺键取类型缺省值；枚举按 int 直赋）。List 字段遇非数组的
    单个值时也接受，包装成单元素列表。

- static void GenFieldT(StringBuilder out0, int fk, string fname, string tname)
  - tree（mode 0）字段行：把字段转成 JsonValue 节点放入对象
    （null→NewNull，string 过 T_Str，类嵌套调 T_）。List 元素
    种类读 FElemName/FElemKind。

- static void EmitBindClass()
  - 合成 __JsonBind 类：先写通用 null 转换助手，再按引用顺序生成
    每个类的 B_/T_/W_/D_/S_（及 List 形态 DL_/SL_），回填到 reply.sources。

- static void GenClass(StringBuilder out0, int idx)
  - 为一个类生成全部 binder 代码：B_(JsonValue→实体，宽容缺省)、
    T_(实体→JsonValue，null→NewNull)、W_(实体→JSON 文本直写，
    快约 4 倍)、D_/S_ 字符串入口，NeedRootList 时加 DL_/SL_。


## GenRoute (class)

属性与约定路由生成器: 消费 genmeta 单元元数据, 合成 __AttrRoutes 路由注册源码。

- static JsonValue Reply;
  - 输出容器: 合成源码与诊断写进这里(reply)。

- static Dict <string, JsonValue> ClassOf;

- static JsonValue Unit;
  - 输入的编译单元元数据(req.unit)。

- static Dict <string, int> MetaKind;

- static Dict <string, string> MetaVal;

- static List<string> MetaOrder;

- static List<RouteParamDoc> ParamDocs;

- static Dict <string, string> ParamToProp;

- static int ConstKind;

- static string ConstSval;

- static Dict <string, string> ModTitle;

- static Dict <string, string> ModIcon;

- static Dict <string, string> ModOrder;

- static Dict <string, string> ModNs;

- static List<string> ModKeys;

- static string RType;

- static bool RRequired;

- static bool RHasDefault;

- static bool RHasLabel;

- static string RWhere;

- static JsonValue ClassGet(string name)
  - 按名字查类声明, 不存在返回 null。

- static void Run(JsonValue req, JsonValue reply)
  - 入口: 扫描全部控制器生成路由; 无路由或已有诊断时不产出。
    产物是名为 __AttrRoutes 的合成源码(蹦床方法 + Register)。

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
  - 声明 d(类/方法)上是否出现名为 nm 的属性。

- static List<string> AttrStringArgs(JsonValue d, string nm)
  - 属性类上 <name>(...) 的全部字符串字面量参数;无则空表。

- static string AttrStringArg(JsonValue d, string nm)
  - 属性类上第一个 <name>(...) 的字符串参数;无则 ""。

- static string AttrNamedArg(JsonValue d, string attrName, string propName)
  - 属性类上名为 attrName 的属性中指定命名参数 propName 的字符串值;无则 ""。

- static bool DerivesFrom(JsonValue cls, string basename)
  - 类的 bases 里是否直接列出 basename(不递归)。

- static bool IsController(JsonValue cls)
  - 类是否呈控制器形状: 名以 Controller 结尾、继承
    Controller/ApiController/CrudController, 或带 [Route]/[ApiController]。
    抽象类与泛型原型类不直接生成路由分发器。

- static string ControllerDisplay(JsonValue cls)
  - 原始(未改名)简单类名:nsresolve 会给重名控制器改名,但
    orig_name 保留源码名,路由/视图键保持可读。

- static string ControllerModule(JsonValue cls)
  - 模块 = 应用根命名空间之下的部分:第一个 '.' 之后的段
    ("ZanWeb.Admin.System" → "Admin.System";根命名空间为空)。

- static string ControllerActionName(JsonValue cls, string disp, string mod)
  - action 名的控制器半段:模块 + 源码名,与视图键同一个拼法。

- static string ControllerToken(JsonValue cls)
  - 控制器 token = 原始类名去掉 "Controller" 后缀并小写。

- static void ResolveModule(JsonValue cls, out string modKey, out string modTitle, out string modIcon, out string modOrder)
  - 自动解析控制器所属的管理模块（支持类自身标记、单一模块声明类匹配、命名空间与类名前缀匹配）

- static string DefaultClassRoute(JsonValue cls)
  - 计算控制器的约定路由前缀 (namespace + class 组合，零注解按约定生效)

- static string InferMethodVerb(string mname)
  - 约定动词推断：若未显式标注动词属性，按方法名语义推导

- static string BuildPath(string clsTpl, string mTpl, string ctrlTok, string actionTok)
  - 合并类/方法路由模板, 替换 [controller]/[action] token, 归一化
    斜杠; 方法模板不以 / 开头且无 [action] 时追加 [action] 段。

- static bool EvalConst(JsonValue e)
  - 枚举成员求值(仅需要名字;数值无消费者)。

- static void MetaSet(string key, bool onlyAbsent)
  - 把 ConstKind/ConstSval 写入元数据键 key; onlyAbsent 时键已存在
    则跳过。首次插入顺序记录在 MetaOrder。

- static void MetaSetDirect(string key, string val, int kind)
  - 直接写入已求值的元数据键值(kind: 0=int, 1=bool, 2=str, 3=id/enum)

- static int MetaGetKind(string key)
  - 键的元数据类别, 没有返回 -1。

- static string MetaGetVal(string key)
  - 键的元数据值, 没有返回 ""。

- static string PmapProp(string param)
  - ctor 参数名 → 属性名映射 (O(1) 哈希查询), 没有返回 ""。

- static void ApplyAttr(JsonValue attr)
  - 应用一次属性用法到元数据:属性类默认值(仅缺省时)先,
    用法的位置/命名参数后(总是覆盖)。

- static bool ReaderFor(string name)
  - reader 名 → (type, required, has_default, has_label, where)。
    返回 false 表示不是 reader。

- static string Literal(JsonValue e)
  - 字面量参数原样输出;非字面量(动态默认值是代码,不是文档)为 ""。

- static void ParamsAdd(JsonValue call)
  - 记录一次输入调用发现的参数: 同名重复读按更强声明合并
    (必填/具体类型/默认与描述取先到者), 超过 64 个静默丢弃。

- static bool ParamDocSeen(string nm)
  - 参数名是否已登记。

- static void ParamAddOne(string nm, string ty, string desc, string def)
  - 登记一个可选参数(query 来源; 同名已存在则跳过)。

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
  - 把参数文档表输出为注册链上的 .Param(...) 序列。

- static void EmitFluent(StringBuilder out0, string title)
  - 把路由元数据输出为注册链上的 fluent 调用(Title/Auth/Menu/Lock/Upload/Limit/Meta)。

- static int GenController(JsonValue cls, StringBuilder handlers, StringBuilder reg)
  - 生成一个控制器的全部路由;返回路由数。

- static string FirstVerbAttr(JsonValue m)
  - 方法上第一个 HTTP 动词属性名,无则 ""。

- static string MethodVerb(JsonValue m)
  - 方法上的 HTTP 动词(取第一个动词属性), 没有动词属性时返回 ""。

- static bool BindableParams(JsonValue ps)
  - 参数类型能否由蹦床从请求绑定(string/int/long/double/bool,
    或本单元内可表单化的类)。

- static bool BindableType(string t)
  - 单个参数类型是否可绑定。

- static JsonValue FormClassOf(string t)
  - 参数类型指向的表单类:本单元已知、是 class、能以无参构造
    (显式声明了无参构造,或根本没声明构造)。返回类 JSON,否则 null。

- static Dict <string, string> OptArgs(JsonValue m)
  - [Opt("page=1","kw=")]:可选签名参数 name → default。

- static string InCall(string t, string label, string def)
  - In* 读表达式:string 走 In,其余带类型化默认。

- static string OptBindLine(string t, string n, string def)
  - 可选参数的绑定语句:[Opt("page=1")] 下的 `int page` 生成
    `int page = __c.InInt("page", 1);`——缺失取默认值,不再 400。

- static double AtoD(string s)
  - 十进制小数字面量解析(默认值只可能是源码里写得出的简单形状)。

- static string BindLine(string t, string n)
  - 一条绑定语句:`long id = __c.NeedLong("id", "id");`。必填语义:
    缺失与非法同罪,都抛 ApiError(400),由蹦床统一应答——签名即文档,
    文档表在注册段以同一份名字先行播种。


## GenScene (class)

.zscene 场景设计文档到合成 partial class 的代码生成器。
与 src/compiler/scenegen.c 等价(输出逐字节一致);入口是
`Translate`,由 design 请求分派 .zscene 文件进来。

- static void Translate(JsonValue req, JsonValue reply)
  - 翻译 design 请求里所有 .zscene 文件(.html 设计稿归 GenForm):
    逐个解析、合成为 .zan 源码,经 GenCommon.AddSource 追加进
    reply 的 sources;失败时写 reply 的 error 后返回。

- static string Esc(string s)
  - 转义为可嵌入 .zan 字符串字面量正文的文本。
    与旧 scenegen 的 sg_esc 一致:反斜杠/双引号前置反斜杠、
    LF -> \n、CR -> \r、TAB -> \t,其余原样(注意 CR 是保留的,
    与 GenCommon.Esc 不同)。

- static int ObjNum(JsonValue o, string key, int def)
  - 读对象的整数属性:存在且为数字时取其值,否则返回 def。

- static string ObjStr(JsonValue o, string key)
  - 读对象的字符串属性:缺省或非字符串返回 ""。

- static bool ObjBool(JsonValue o, string key, bool def)
  - 读对象的布尔属性:缺省或非布尔返回 def(与 GenForm 的
    ObjBool 不同,这里允许指定缺省,如 visible 缺省 true)。

- static int ArrNum(JsonValue o, string key, int i, int def)
  - 数值数组属性的第 i 个分量("color"/"clearColor")。

- static bool UsedHas(List<string> used, string n)
  - 元素名是否已被用作静态字段名(重名元素改用局部变量)。

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)
  - 翻译单个 .zscene 文档。解析失败返回 null(不设 error,
    由 C 端报通用错误)。


## RouteParamDoc (class)

路由参数文档模型 (单一实体结构，彻底消灭并行 List 坏味道)

- public string name;

- public string type;

- public bool required;

- public string def;

- public string desc;

- public string source;

- public RouteParamDoc(string name, string type, bool required, string def, string desc, string source)


## ZanGen (class)

编译期代码生成器入口: 按 mode 把请求分派给各 Gen* 模块并回写 reply。

- static void Main()
  - 生成器进程入口：读入请求 JSON,按 mode 分派(design ->
    GenForm/GenScene,codegen -> GenJson/GenRoute/GenDb,设了
    ZAN_INDEX_DIR 再顺带 GenIndex.WriteTo),把 reply 写到 out.json。
