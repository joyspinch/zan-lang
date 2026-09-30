# ZanWeb.Admin

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Crud/Controller/Admin/CrudScreenController.zan`


## CrudScreenController (class)

- List<string> rowKeys;

- protected virtual CrudConf Def()

- virtual async Task DoList()

- virtual async Task DoListData()

- virtual async Task DoEdit()

- virtual void DoFormConf()

- void CrudPage(ViewData d)

- virtual void DoConf()

- virtual async Task DoForm()

- virtual async Task DoSave()

- virtual async Task DoDelete()

- virtual async Task DoBatch()

- virtual async Task DoField()

- virtual async Task DoOptions()

- virtual string OnSaving(DbValues row, bool isNew)

- protected virtual async string OnDeleting(long id)

- protected virtual async List<StrMap> OnRows(List<StrMap> rows)

- virtual async Task OnSaved(bool isNew)

- virtual async Task OnFielded(string field, long id)

- virtual async Task OnBatched(string field, string value, List<int> ids)

- protected virtual async Task OnDeleted(long id)

- string CheckValue(CrudConf conf, string field, string value)

- async long CountRows(CrudConf conf, List<OrmCond> conds)

- async List<StrMap> LoadRows(CrudConf conf, List<OrmCond> conds, ListQuery q)

- void LoadKeys(CrudConf conf, DbResult r)

- ListQuery PagedAny()

- List<int> IdsAny()

- static void Env(AdminController c, JsonValue data)

- static JsonValue JsonRows(List<StrMap> rows, List<string> keys)

- static JsonValue SelectOptsJson(string opts)

- static JsonValue RulesJson(FormField f)

- string RowTarget(CrudConf conf, int id)

- static DbTableQuery Apply(DbTableQuery q, List<OrmCond> conds)

- static List<StrMap> StrMaps(DbResult r)
