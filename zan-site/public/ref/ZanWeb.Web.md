# ZanWeb.Web

> 源码: `packages/Zan.Mvc/src/ZanWeb/Framework/AdminController.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/ApiController.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/AppController.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/AppServices.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/CrudOps.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Fmt.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/FormPage.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Lang.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/ListPage.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Prose.zan`, `packages/Zan.Mvc/src/ZanWeb/Framework/Rows.zan`


## AdminController (class)

- protected SysUser who;

- protected string headingText;

- protected DataScope scope;

- protected bool IsFragment()

- protected async SysUser Admin()

- override async bool OnBeforeAsync()

- protected async DataScope Scope()

- protected async ViewData Layout(string heading)

- protected void I18n(ViewData d)

- protected async void Buttons(ViewData d)

- protected async int Mask()

- protected async bool Can(int bit)

- protected async void AddDesigned(List<MenuNode> nodes)

- protected void Page(ViewData d)

- protected void Dialog(ViewData d)

- protected void FormDialog(ViewData d)

- void Saved(string msg)

- async void Note(string title, string target, bool ok, string message)

- protected void Pager(ViewData d, ListQuery q, int total)

- protected List<int> Ids(string name)

- protected bool PageReady(ViewData d)

- protected bool DataReady()


## ApiController (class)

- protected string TraceId()

- protected void Ok(string dataJson)

- protected void Fail(int code, string msg)

- protected void ServerError(string userMsg)


## AppController (class)

- IDbConnection lease;

- IDbConnection readLease;

- bool inTx;

- bool readOnlyIntent;

- bool leaseIsRead;

- override void __UseReadOnly()

- private DbContext Db()

- protected CacheContext Cache()

- protected async IDbConnection Conn()

- protected IDbConnection Held()

- protected async IDbConnection ReadConn()

- protected IDbConnection HeldRead()

- public IDbConnection __Conn()

- override async bool OnBeforeAsync()

- protected bool DbReady()

- private void DiscardLease()

- protected async bool Begin()

- protected async void Commit()

- protected async void Rollback()

- override async bool __TxBegin()

- override async void __TxCommit()

- override async void __TxRollback()

- override async void OnAfterAsync()

- virtual void OnDone()

- public string InAny(string name)

- public int InAnyInt(string name)


## AppServices (class)

- static AppServices current;

- DbContext db;

- CacheContext cache;

- AppServices(DbContext db, CacheContext cache)

- static void Use(AppServices services)

- static AppServices Current()

- DbContext Db()

- CacheContext Cache()


## CrudOps (class)

- static async void Field(AdminController c, ListPage lp, string title, string field, string value, List<int> allowed)

- static async void Batch(AdminController c, ListPage lp, string field, string value, List<int> allowed, string title)

- static async void Delete(AdminController c, ListPage lp, List<int> allowed, string title)

- static StrMap Find(ListPage lp, string field)

- static void Conf(AdminController c, ListPage lp)

- static string WidthNum(string width)


## Fmt (class)

- static int offsetMinutes;

- static void SetOffsetMinutes(int minutes)

- static int OffsetMinutes()

- static long LocalDayStart(int year, int month, int day)

- static long TodayStart(long seconds)

- static long ParseDay(string ymd)

- static string DayInput(long seconds)

- static string Dash(string v)

- static string MaskMobile(string mobile)

- static string MaskEmail(string email)

- static string Text(string v, string fallback)

- static string FirstChar(string s)

- static string Flag(bool on)

- static string Stamp(long seconds)

- static string Day(long seconds)

- static string Uptime(long ms)

- static string Bytes(long v)

- static string Dur(long us)

- static string Frac(long v, long unit, int digits)

- static string Percent(long v)

- static string Pad2(long v)

- static string PublishedText(int published)

- static string PublishedClass(int published)

- static string ReviewText(int status)

- static string ReviewClass(int status)

- static string EnabledText(int status)

- static string EnabledClass(int status)

- static string OkText(int ok)

- static string OkClass(int ok)

- static string LoginText(int ok)


## FormField (class)

- string kind;

- string type;

- string label;

- string name;

- string value;

- string ph;

- string options;

- string blank;

- string pattern;

- List<StrMap> rows;

- int max;

- int min;

- int areaRows;

- bool required;

- bool span;

- bool onlyNew;

- bool onlyEdit;


## FormPage (class)

- string action;

- string idValue;

- bool wide;

- List<FormField> fs;

- FormField last;

- FormPage()

- static FormPage Of(string submitAction)

- FormPage Id(string value)

- FormPage Wide()

- FormPage Text(string label, string name)

- FormPage Num(string label, string name)

- FormPage Pass(string label, string name)

- FormPage Area(string label, string name)

- FormPage Select(string label, string name, string options)

- FormPage SelectList(string label, string name, List<StrMap> rows)

- FormPage Hint(string text)

- FormPage Add(string kind, string label, string name)

- FormPage Field(string name)

- FormPage Val(string v)

- FormPage Ph(string p)

- FormPage Max(int n)

- FormPage Min(int n)

- FormPage Req()

- FormPage Span()

- FormPage Rows(int n)

- FormPage Pattern(string re)

- FormPage OnlyNew()

- FormPage OnlyEdit()

- FormPage Blank(string label)

- string Validate(AppController c)

- void Render(ViewData d)

- bool IsEdit()

- bool Visible(bool isEdit, FormField f)


## Lang (class)

- static string loaded="zh-CN";

- static StrMap pack=new StrMap();

- static bool Known(string lang)

- static async void Load()

- static string T(string zh)


## ListButton (class)

- string type;

- string name;

- string url;

- string value;

- string confirm;


## ListCol (class)

- string field;

- string title;

- string kind;

- string width;

- string opts;


## ListFilter (class)

- string type;

- string title;

- string name;

- string col;

- string ph;

- string options;

- string url;


## ListOps (class)

- string name;

- string url;

- string confirm;

- string args;

- string perm;


## ListPage (class)

- List<ListFilter> fs;

- List<ListButton> bs;

- List<ListCol> cols;

- List<ListOps> ops;

- List<StrMap> edits;

- string table;

- string key;

- ListPage()

- static ListPage Of()

- ListPage Text(string title, string name, string col, string ph)

- ListPage Select(string title, string name, string col, string options)

- ListPage Time(string title, string name, string col)

- ListPage Pick(string title, string name, string col, string url)

- ListPage TbarAdd(string name, string url)

- ListPage TbarReload()

- ListPage TbarSep()

- ListPage TbarEnable(string url)

- ListPage TbarDisable(string url)

- ListPage TbarDelete(string url)

- ListPage TbarCustom(string name, string url, string confirm)

- ListPage TbarBatch(string name, string url, string value, string confirm)

- ListPage Btn(string type, string name, string url, string value, string confirm)

- ListPage Col(string field, string title, string kind, string width)

- ListPage Tag(string field, string title, string opts, string width)

- ListPage Flag(string field, string title, string width)

- ListPage Ops(string name, string url, string confirm, string args, string perm)

- ListPage OpsEdit(string url)

- ListPage OpsDel(string url, string confirm)

- ListPage Table(string physicalName)

- ListPage EditFields(string defs)

- List<OrmCond> Collect(AppController c)

- static OrmCond Cond(string name, string col, string kind, string value)

- string Search(AppController c, ViewData d, string action)

- string Toolbar(ViewData d, bool canCreate, bool canUpdate, bool canDelete)

- string TableHtml(ViewData d, List<StrMap> rows, bool canUpdate, bool canDelete)

- static string Cell(ListCol c, StrMap row)

- static string OpsCell(ListPage lp, StrMap row, bool canUpdate, bool canDelete)

- static string Fill(string tpl, StrMap row)

- string PagerHtml(string action, List<OrmCond> conds, ListQuery q, int total)

- void Screen(AppController c, ViewData d, string path, string action, List<OrmCond> conds, ListQuery q, int total, List<StrMap> rows, bool canCreate, bool canUpdate, bool canDelete)

- string Query(List<OrmCond> conds, int page, int limit)

- static string Esc(string s)


## Prose (class)

- static int RuneLen(string s, int i)

- static int Runes(string s)

- static string Excerpt(string s, int max)

- static int Minutes(string body)

- static List<string> Paragraphs(string body)

- static List<string> Tags(string tags)

- static bool HasTag(string tags, string tag)


## Rows (class)

- static void Project<T>(List<T> rows, List<StrMap> into)where T:RowView


## RowView (interface)

- void ToRow(StrMap row);
