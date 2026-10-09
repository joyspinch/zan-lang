# System.Compiler

> 源码: `stdlib/System/Compiler/GenCommon.zan`, `stdlib/System/Compiler/GenDb.zan`, `stdlib/System/Compiler/GenDbEmit.zan`, `stdlib/System/Compiler/GenForm.zan`, `stdlib/System/Compiler/GenHtml.zan`, `stdlib/System/Compiler/GenIndex.zan`, `stdlib/System/Compiler/GenJson.zan`, `stdlib/System/Compiler/GenRoute.zan`, `stdlib/System/Compiler/GenScene.zan`, `stdlib/System/Compiler/ZanGen.zan`


## DbField (class)

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

- static JsonValue ArrOf(JsonValue reply, string key)

- static void AddSource(JsonValue reply, string name, StringBuilder text)

- static void AddRewrite(JsonValue reply, JsonValue op)

- static void Fail(JsonValue reply, string msg)

- static bool HasError(JsonValue reply)

- static string Esc(string s)

- static bool IsIdent(string s)

- static void Line(StringBuilder b, int indent, string text)


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

- static JsonValue AttrArg(JsonValue attr, string key)

- static JsonValue AttrPositional(JsonValue attr)

- static string AttrStr(JsonValue attr, string key)

- static bool AttrBool(JsonValue attr, string key, bool dflt)

- static int AttrInt(JsonValue attr, string key, int dflt)

- static bool IsIntName(string n)

- static bool IsFloatName(string n)

- static bool Is64(string type)

- static int ClassifyKind(string type)

- static List<DbField> FieldsOf(string cls, int depth)

- static void FieldsInto(string cls, int depth, List<DbField> outl)

- static DbField FieldFind(List<DbField> fs, string name)

- static string TableOf(string cls)

- static List<JsonValue> IndexAttrs(string cls)

- static bool IsTableEntity(string cls)

- static string PkOf(string cls)

- static int NeedAdd(string name)

- static JsonValue CallAt(int id)

- static JsonValue TreeStr(string s)

- static JsonValue TreeNull()

- static JsonValue TreeId(string name)

- static JsonValue TreeGenericId(string name, string targ)

- static JsonValue TreeMem(JsonValue obj, string name)

- static JsonValue TreeCall(JsonValue callee, JsonValue args)

- static JsonValue TreeCall1(JsonValue callee, JsonValue arg)

- static JsonValue OneArg(JsonValue arg)

- static JsonValue TreeBin(string op, JsonValue l, JsonValue r)

- static JsonValue TreeCast(string t, JsonValue e)

- static JsonValue TreeLam(List<string> ps, JsonValue body)

- static int ArgsCount(JsonValue call)

- static string LamParam(JsonValue lam)

- static int ArrLen(JsonValue o, string key)

- static JsonValue ArgAt(JsonValue call, int i)

- static JsonValue NewOp(string op, JsonValue call)

- static void AddRewrite(JsonValue op)

- static void MarkChain(JsonValue call, string entity, int ck)

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

- static DbField AsColumn(Wf w, JsonValue e)

- static void WfCol(Wf w, DbField f)

- static string LitMismatch(DbField f, JsonValue e)

- static bool CheckValueType(Wf w, DbField f, JsonValue e)

- static void WfValue(Wf w, DbField f, JsonValue e)

- static string AggCall(Wf w, JsonValue e, out int outKind)

- static string BindMethod(int kind)

- static JsonValue OpObj(string m, JsonValue args)

- static JsonValue OneOp(string m, JsonValue arg)

- static bool WhereMethod(Wf w, JsonValue e)

- static void WhereCond(Wf w, JsonValue e)

- static JsonValue WhereFrag(JsonValue call, string cls, JsonValue lambda, Wf w)

- class ExprSlot

- static List<ExprSlot> Eslots;

- static void CollectExprSlots()

- static JsonValue ExprCall(string fn, JsonValue args)

- static bool PnameChain(JsonValue e, string pname)

- static bool ExprIsValue(JsonValue e, string pname)

- static DbField MemberField(List<DbField> fs, JsonValue e, string pname)

- static int LitKind(JsonValue e)

- static bool KindCompat(int fk, int lk)

- static bool ExprTypeOk(List<DbField> fs, JsonValue l, JsonValue r, string pname)

- static bool ExprCompatible(List<DbField> fs, JsonValue e, string pname)

- static JsonValue ExprTree(JsonValue e, string pname)

- static JsonValue ExprWrap(string entity, string pname, JsonValue tree)

- static bool ExprRewriteCall(JsonValue call)

- static void RewriteWhere(JsonValue call, string cls, bool exprOk)

- static JsonValue BuildWhereChain(JsonValue recv, string cls, JsonValue lambda, JsonValue call)

- static void RewriteWhereIf(JsonValue call, string cls)

- static void RewriteHaving(JsonValue call, string cls)

- static void RewriteSet(JsonValue call, string cls, bool incr)

- static void RewriteAgg(JsonValue call, string cls, string fn)

- static void RewriteCols(JsonValue call, string cls, bool only)

- static JsonValue GbTree(JsonValue e, string pname, List<DbField> fields, out bool anyCol)

- static void RewriteGroupBy(JsonValue call, string cls)

- static void RewriteConflict(JsonValue call, string cls)

- static void RewriteUpsertSet(JsonValue call, string cls, string what)

- static void RewriteDoNothing(JsonValue call)

- static void RewriteDistinct(JsonValue call)

- static void RewriteOrderBy(JsonValue call, string cls, bool desc)

- static string AggText(JsonValue call, JsonValue e, string pname, List<DbField> fields)

- static string ProjAdd(string cls, string target, JsonValue items)

- static void RewriteProj(JsonValue call, string cls, string term)

- static void RewriteToList(JsonValue call, string cls, bool isAsync)

- static void RewriteInclude(JsonValue call, string cls)

- static DbField SelColumn(JsonValue call, string cls, List<DbField> fields, string what)

- static bool SelectHead(string name)

- static string ChainEntity2(JsonValue call, out int kind)

- static bool IsRoot(JsonValue call, out string cls, out int kind, out bool sync)

- static JsonValue DbBindCall(string name, JsonValue args)

- static JsonValue AccConn(JsonValue acc)

- static JsonValue DaoClass(string entity)

- static bool DaoAmbiguous(string entity)

- static string DaoName(JsonValue dao, string entity)

- static bool DaoHasMethod(JsonValue dao, string name, int argc)

- static JsonValue DaoCallTree(string dao, string method, JsonValue conn, JsonValue args)

- static void VisitCall(JsonValue call)

- static void ChainMethods(JsonValue call, string entity, int ck)

- static JsonValue BuildWhereTree(JsonValue wCall, string cls, JsonValue lambda, JsonValue call)

- static void Run(JsonValue req, JsonValue reply)


## GenDbEmit (class)

- static bool Is64(string type)

- static string Bl(bool v)

- static string Getter(int kind)

- static string ColArgs(DbField f)

- static void Projections(StringBuilder b, string cls)

- static bool Has(List<string> l, string v)

- static void GenAccess(StringBuilder b, string cls)

- static bool IsIdent(string s)

- static List<string> IndexArgs(string cls, List<DbField> fs, string tbl)

- static void GenCols(StringBuilder b, string cls, List<DbField> fs, string tbl)

- static void GenEntity(StringBuilder b, string cls)

- static void GenInsert(StringBuilder b, string cls, List<DbField> fs, string tbl)

- static void GenUpdate(StringBuilder b, string cls, List<DbField> fs, string tbl)

- static void GenDelete(StringBuilder b, string cls, List<DbField> fs, string tbl)

- static void GenCodeFirst(StringBuilder b, string cls, List<DbField> fs, string tbl)

- static void Run()


## GenForm (class)

- static List<GenFormCompEntry> compEntries;

- static void Translate(JsonValue req, JsonValue reply)

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)

- static JsonValue CompDoc(string name)

- static JsonValue DeepCopy(JsonValue v)

- static string ExpandRefs(JsonValue arr, int depth)

- static void ValidateFields(JsonValue reply, JsonValue arr, string file_name)

- static void ValidateColumns(JsonValue reply, JsonValue o, string f0, int idx)

- static bool ValidFieldPath(string s)

- static bool IsElement(JsonValue o)

- static string KindOf(JsonValue o)

- static void ScanHeavyKinds(JsonValue node, bool[]flags)

- static string TypeOf(JsonValue o)

- static string QualifyKind(string kind)

- static List<string> NonConstructibleKinds()

- static bool IsStdWidgetKind(string kind)

- static bool IsHmiKind(string kind)

- static string DefaultTypeArgs(string kind)

- static bool ValidTypeArgs(string of)

- static string TrimWs(string s)

- static bool IsContainer(JsonValue o)

- static int ObjNum(JsonValue o, string key, int def)

- static bool ObjBool(JsonValue o, string key)

- static string ObjStr(JsonValue o, string key)

- static int PrefH(JsonValue o)

- static string JoinOpts(JsonValue o)

- static void EmitDesignCss(StringBuilder b, string vn, string css)

- static void EmitUseAppCss(StringBuilder b, string appExpr, string css)

- static void EmitSetProp(StringBuilder b, string vn, string key, string v)

- static void FieldSetup(StringBuilder b, JsonValue o, string vn)

- static void EmitColumns(StringBuilder b, JsonValue o, string vn)

- static JsonValue ColEntryAt(JsonValue cols, int i)

- static string OptFieldOf(string opt)

- static string OptFieldAt(JsonValue opts, int i)

- static string OptTitleOf(string opt)

- static void EmitOptionColumn(StringBuilder b, string vn, string opt, JsonValue entry, string of)

- static void EmitColumn(StringBuilder b, JsonValue c, string vn)

- static bool HasHandlers(JsonValue o)

- static void EmitHandlers(StringBuilder wire, JsonValue o, string vn, string dispatch)

- static string ParentExpr(JsonValue o, string parent, string parentKind)

- static bool HasSizeClass(string classes)

- static bool IsModelKey(string key)

- static string DockName(int dock)

- static int DefaultDock(JsonValue o)

- static int ChildrenCount(JsonValue o)

- static int EmitField(StringBuilder decls, StringBuilder body, StringBuilder wire, StringBuilder valid, JsonValue o, string parent, string parentKind, int id, List<string> used, bool freeMode, bool parentIsFlow, string dispatch, bool instanceFields, bool anon, string defaultSizeClass)

- static int EmitFieldCore(StringBuilder decls, StringBuilder body, StringBuilder wire, StringBuilder valid, JsonValue o, string parent, string parentKind, bool parentIsElement, int id, List<string> used, bool freeMode, bool parentIsFlow, string dispatch, bool instanceFields, bool anon, string defaultSizeClass, bool suppressAttach)

- static string PredeclareKid(StringBuilder decls, StringBuilder body, JsonValue o, int id, List<string> used, bool instanceFields, bool anon)

- static void EmitAttach(StringBuilder body, JsonValue o, string vn, string host, string parent, string parentKind, bool parentIsElement, bool fm, bool flowCell)

- static int ContainerPrefH(JsonValue o)


## GenFormCompEntry (class)

- string name;

- JsonValue doc;

- GenFormCompEntry(string name, JsonValue doc)


## GenHtml (class)

- static void Translate(JsonValue req, JsonValue reply)

- static bool Emit(string path, WDoc wd, StringBuilder outp)

- static void EmitDecl(StringBuilder b, WNode n, int idx, string genCls, string zs)

- static void EmitContent(StringBuilder b, WNode n, int idx)

- static void EmitWiring(StringBuilder b, WNode n, int idx)

- static string ClassName(string path)

- static string BaseName(string path)

- static string ScopeClassOf(string path)


## GenIndex (class)

- static JsonValue unit;

- static List<string> files;

- static string Base;

- static void Seed(JsonValue u)

- static string FileOf(int id)

- static string Slashes(string p)

- static string Relative(string path)

- static JsonValue NamedArg(JsonValue attr, string key)

- static JsonValue AttrOf(JsonValue owner, string name)

- static string ExprText(JsonValue e)

- static string CustomText(JsonValue cls, JsonValue m, string key)

- static string AuthOf(JsonValue cls, JsonValue m)

- static JsonValue RoutesDoc()

- static JsonValue EntitiesDoc()

- static JsonValue ManifestDoc(int routes, int entities)

- static int WriteTo(JsonValue u, string dir)


## GenJson (class)

- static Dict <string, JsonValue> ClassOf;

- static List<GenJsonNeedEntry> Needs;

- static JsonValue Reply;

- static JsonValue ClassGet(string name)

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

- static int FieldKind(string tname)

- static string Fmt(string tpl, string arg)

- static int GenFields(StringBuilder out0, string cls, int mode, int depth, int emitted)

- static void GenFieldW(StringBuilder out0, int fk, string fname, string tname)

- static void GenFieldB(StringBuilder out0, int fk, string fname, string tname)

- static void GenFieldT(StringBuilder out0, int fk, string fname, string tname)

- static void EmitBindClass()

- static void GenClass(StringBuilder out0, int idx)


## GenJsonNeedEntry (class)

- string name;

- bool rootList;

- GenJsonNeedEntry(string name, bool rootList)


## GenRoute (class)

- static JsonValue Reply;

- static Dict <string, JsonValue> ClassOf;

- static JsonValue Unit;

- static Dict <string, int> MetaKind;

- static Dict <string, string> MetaVal;

- static List<string> MetaOrder;

- static List<RouteParamDoc> ParamDocs;

- static Dict <string, string> ParamToProp;

- static int ConstKind;

- static string ConstSval;

- static Dict <string, string> RouteKeys;

- static string RType;

- static bool RRequired;

- static bool RHasDefault;

- static bool RHasLabel;

- static string RWhere;

- static JsonValue ClassGet(string name)

- static void Run(JsonValue req, JsonValue reply)

- static string Esc(string s)

- static string Lower(string s)

- static int Atoi(string s)

- static string AttrHttpVerb(string n)

- static bool AttrIsStructural(string n)

- static bool AttrNamed(JsonValue d, string nm)

- static List<string> AttrStringArgs(JsonValue d, string nm)

- static string AttrStringArg(JsonValue d, string nm)

- static string AttrNamedArg(JsonValue d, string attrName, string propName)

- static bool DerivesFrom(JsonValue cls, string basename)

- static bool IsController(JsonValue cls)

- static string ControllerDisplay(JsonValue cls)

- static string ControllerModule(JsonValue cls)

- static string ControllerActionName(JsonValue cls, string disp, string mod)

- static string ControllerToken(JsonValue cls)

- static string DefaultClassRoute(JsonValue cls)

- static string InferMethodVerb(string mname)

- static string BuildPath(string clsTpl, string mTpl, string ctrlTok, string actionTok)

- static bool EvalConst(JsonValue e)

- static void MetaSet(string key, bool onlyAbsent)

- static void MetaSetDirect(string key, string val, int kind)

- static int MetaGetKind(string key)

- static string MetaGetVal(string key)

- static string PmapProp(string param)

- static void ApplyAttr(JsonValue attr)

- static bool ReaderFor(string name)

- static string Literal(JsonValue e)

- static void ParamsAdd(JsonValue call)

- static bool ParamDocSeen(string nm)

- static void ParamAddOne(string nm, string ty, string desc, string def)

- static void ParamsAddPaged(JsonValue call)

- static void ScanCalls(string cls, string fn)

- static void ParamsFromPath(string path)

- static void EmitParams(StringBuilder out0)

- static void EmitFluent(StringBuilder out0, string title)

- static int GenController(JsonValue cls, StringBuilder handlers, StringBuilder reg)

- static string FirstVerbAttr(JsonValue m)

- static string MethodVerb(JsonValue m)

- static bool BindableParams(JsonValue ps)

- static bool BindableType(string t)

- static JsonValue FormClassOf(string t)

- static Dict <string, string> OptArgs(JsonValue m)

- static string InCall(string t, string label, string def)

- static string OptBindLine(string t, string n, string def)

- static double AtoD(string s)

- static string BindLine(string t, string n)


## GenScene (class)

- static void Translate(JsonValue req, JsonValue reply)

- static string Esc(string s)

- static int ObjNum(JsonValue o, string key, int def)

- static string ObjStr(JsonValue o, string key)

- static bool ObjBool(JsonValue o, string key, bool def)

- static int ArrNum(JsonValue o, string key, int i, int def)

- static bool UsedHas(List<string> used, string n)

- static StringBuilder TranslateOne(JsonValue reply, string json, string file_name, bool emit_main)


## RouteParamDoc (class)

- public string name;

- public string type;

- public bool required;

- public string def;

- public string desc;

- public string source;

- public RouteParamDoc(string name, string type, bool required, string def, string desc, string source)


## ZanGen (class)

- static void Main()
