# ZanWeb.Model

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Crud/Model/CrudConf.zan`


## CrudConf (class)

- string table;

- string title;

- string path;

- string orderCol;

- bool orderDesc;

- string labelCol;

- string toggleField;

- string stampCreated;

- string stampUpdated;

- List<ListFilter> fs;

- List<ListButton> bs;

- List<ListCol> cols;

- List<ListOps> ops;

- List<StrMap> uniques;

- List<FormField> forms;

- List<string> vcols;

- string editDefs;

- string inlineDefs;

- string fName;

- string fLabel;

- string fKind;

- string fOpts;

- string fW;

- bool fMuted;

- bool fRo;

- bool fVir;

- bool fInline;

- string fSearch;

- string fPick;

- string fPattern;

- bool fReq;

- int fMax;

- int fMin;

- string fPh;

- string fDef;

- bool fSpan;

- int fRows;

- string fUnique;

- CrudConf()

- static CrudConf Of(string table, string title, string path)

- CrudConf Col(string name, string label)

- CrudConf Int()

- CrudConf Area()

- CrudConf Pass()

- CrudConf Opts(string opts)

- CrudConf W(string width)

- CrudConf Muted()

- CrudConf Time()

- CrudConf Ro()

- CrudConf V()

- CrudConf Inline()

- CrudConf Search(string ph)

- CrudConf Pick(string url)

- CrudConf Pattern(string re)

- CrudConf Unique(string msg)

- CrudConf Req()

- CrudConf Max(int n)

- CrudConf Min(int n)

- CrudConf Ph(string p)

- CrudConf Def(string v)

- CrudConf Span()

- CrudConf Rows(int n)

- CrudConf Order(string col, bool desc)

- CrudConf Label(string col)

- CrudConf Toggle(string field)

- CrudConf Created(string col)

- CrudConf Updated(string col)

- CrudConf TbarAdd(string name)

- CrudConf TbarReload()

- CrudConf TbarEnable()

- CrudConf TbarDisable()

- CrudConf TbarDelete()

- CrudConf TbarBatch(string name, string value, string confirm)

- CrudConf OpsEdit()

- CrudConf OpsDel(string confirm)

- CrudConf OpsCustom(string name, string action, string confirm, string args, string perm)

- CrudConf Ops(string name, string url, string confirm, string args, string perm)

- CrudConf Btn(string type, string name, string url, string value, string confirm)

- string Act(string action)

- string Title()

- void Seal()

- ListPage Build(bool inline)

- FormPage ToFormPage(string idValue)

- static string SelectOpts(string opts)
