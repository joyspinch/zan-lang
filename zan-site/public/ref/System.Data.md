# System.Data

> 源码: `packages/Zan.Data/src/System/Data/DbException.zan`, `packages/Zan.Data/src/System/Data/DbParams.zan`, `packages/Zan.Data/src/System/Data/DbPool.zan`, `packages/Zan.Data/src/System/Data/DbProvider.zan`, `packages/Zan.Data/src/System/Data/DbResult.zan`, `packages/Zan.Data/src/System/Data/DbTrace.zan`, `packages/Zan.Data/src/System/Data/DbValues.zan`, `packages/Zan.Data/src/System/Data/IDbConnection.zan`, `packages/Zan.Data/src/System/Data/IDbConnector.zan`, `packages/Zan.Data/src/System/Data/IDbExecutor.zan`, `packages/Zan.Data/src/System/Data/PoolCore.zan`, `packages/Zan.Data/src/System/Data/TracedDbConnection.zan`, `packages/Zan.DataFrame/src/System/Data/DataFrame.zan`


## DataCol (class)

- string name;

- int kind;

- List<string> text;

- List<long> ints;

- List<double> nums;

- static DataCol Create(string colName, int colKind)

- string Name()

- int Kind()

- int Count()

- void Append(string v)

- void AppendLong(long v)

- void AppendDouble(double v)

- void AppendFrom(DataCol src, int srcRow)

- void PutStr(int row, string v)

- void PutLong(int row, long v)

- void PutDouble(int row, double v)

- string GetStr(int row)

- long GetLong(int row)

- double GetDouble(int row)


## DataFrame (class)

- List<DataCol> cols;

- int rowCount;

- DataFrame()

- static DataFrame New()

- static DataFrame FromCsv(string text)

- static int InferKind(CsvTable t, int at)

- static bool LooksInt(string s)

- static bool LooksFloat(string s)

- DataFrame AddColumn(string name, int kind)

- int Rows()

- int ColCount()

- List<string> Names()

- int ColIndex(string name)

- int Kind(string name)

- int AddRow()

- void AddStrRow(params string[]cells)

- void AddFloatRow(params double[]cells)

- DataCol ColOrThrow(string name)

- string GetStr(int row, int col)

- string GetStr(int row, string col)

- long GetInt(int row, int col)

- long GetInt(int row, string col)

- double GetFloat(int row, int col)

- double GetFloat(int row, string col)

- void SetStr(int row, int col, string v)

- void SetStr(int row, string col, string v)

- void SetInt(int row, int col, long v)

- void SetInt(int row, string col, long v)

- void SetFloat(int row, int col, double v)

- void SetFloat(int row, string col, double v)

- DataFrame Filter(RowFn fn)

- List<int> AllRows()

- DataFrame SortBy(string col, bool desc)

- static void MergeSort(List<int> idx, List<int> tmp, int lo, int hi, RowCmpFn cmp)

- List<RowGroup> GroupBy(string col)

- double Aggregate(List<int> rows, string col, int op)

- double Sum(List<int> rows, string col)

- double Avg(List<int> rows, string col)

- double Min(List<int> rows, string col)

- double Max(List<int> rows, string col)

- DataFrame Join(DataFrame right, string keyCol, string rightKeyCol)

- string ToCsv()


## DbException (class)

- public int Code;

- public int Provider;

- public DbException(string message, int code, int provider)

- static DbException Of(string message)


## DbParam (class)

- int kind;

- string text;

- long ival;

- double dval;

- DbParam(int kind, string text, long ival, double dval)


## DbParams (class)

- static int KindNull=0;

- static int KindInt=1;

- static int KindDouble=2;

- static int KindText=3;

- List<DbParam> items;

- DbParams()

- void push(int kind, string t, long i, double d)

- DbParams Add(string val)

- DbParams AddInt(int val)

- DbParams AddLong(long val)

- DbParams AddDouble(double val)

- DbParams AddNull()

- int Count()

- int KindAt(int i)

- string TextAt(int i)

- long IntAt(int i)

- double DoubleAt(int i)

- string AsText(int i)

- bool IsNull(int i)


## DbPool (class)

- IDbConnector connector;

- PoolCore<IDbConnection> core;

- string lastOpenError;

- DbPool(IDbConnector connector, int maxSize)

- async IDbConnection OpenOne()

- async IDbConnection AcquireAsync()

- void Release(IDbConnection c)

- IDbConnection TryAcquire()

- async int WarmupAsync(int count)

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- bool IsClosed()

- int GetProvider()

- void Close()


## DbProvider (class)

- static int Unknown=0-1;

- static int ODBC=0;

- static int SQLite=1;

- static int MySQL=2;

- static int PostgreSQL=3;

- static int SqlServer=4;

- static int Oracle=5;

- static int ClickHouse=7;

- static int QuestDB=8;

- static int Firebird=9;

- static int DuckDB=10;

- static int TDengine=11;

- static int MariaDB=12;

- static int Kingbase=13;

- static int OpenGauss=14;

- static int DM=15;


## DbResult (class)

- List<DbRow> rows;

- List<string> columnNames;

- int columnCount;

- Dict <string, int> colMap;

- DbResult()

- static DbResult Empty()

- void SetColumnCount(int count)

- void AddColumnName(string name)

- void AddRowCells(List<DbCell> cells, List<string> texts)

- void AddRow(List<string> row)

- void AddRowNulls(List<string> row, List<bool> nullFlags)

- bool IsNull(int row, int col)

- bool IsNullByName(int row, string columnName)

- int RowCount()

- int ColumnCount()

- string GetString(int row, int col)

- int GetInt(int row, int col)

- long GetLong(int row, int col)

- double GetDouble(int row, int col)

- bool GetBool(int row, int col)

- string GetByName(int row, string columnName)

- int GetColumnIndex(string name)

- string GetColumnName(int index)

- List<string> GetColumnNames()

- bool HasRows()

- DbRow RowAt(int index)

- List<string> GetRow(int index)

- List<string> First()

- void Print()


## DbRow (class)

- List<DbCell> cells;

- List<string> texts;

- DbRow(List<DbCell> cells, List<string> texts)

- int Count()

- string GetString(int col)

- bool IsNull(int col)

- int GetInt(int col)

- long GetLong(int col)

- double GetDouble(int col)

- bool GetBool(int col)


## DbTrace (class)

- static SqlTraceSink sink=null;

- static void Sink(SqlTraceSink fn)

- static bool Enabled()

- static long NowUs()

- static void Report(string sql, long startUs)


## DbValue (class)

- string name;

- int kind;

- string text;

- long ival;

- double dval;

- DbValue(string name, int kind, string text, long ival, double dval)


## DbValues (class)

- List<DbValue> items;

- DbValues()

- DbValues Set(string name, string val)

- DbValues Set(string name, int val)

- DbValues Set(string name, long val)

- DbValues Set(string name, double val)

- DbValues Set(string name, bool val)

- DbValues SetNull(string name)

- DbValues push(string name, int kind, string t, long i, double d)

- int Count()

- string NameAt(int i)

- int KindAt(int i)

- int IndexOf(string name)

- bool Has(string name)

- DbValues Remove(string name)

- void BindTo(DbParams ps, int i)

- void BindAs(DbParams ps, int i, int want)

- long parseInt(DbValue v)

- double parseDouble(DbValue v)

- void reject(DbValue v, string want)


## PoolCore (class)

- int maxSize;

- List<T> idle;

- List<T> borrowed;

- int liveCount;

- int waiting;

- Gate gate;

- bool closed;

- bool openFailed;

- int skipped;

- int retryEvery;

- PoolCore(int maxSize)

- T TakeIdle()

- void Pool(T c)

- void Drop(T c)

- void Borrow(T c)

- bool TryReserve()

- void UndoReserve()

- bool RemoveBorrowed(T c)

- bool OpenBlocked()

- void NoteOpenFailed()

- void NoteOpenOk()

- bool OpenFailing()

- Gate Waiter()

- void EnterWait()

- void LeaveWait()

- int IdleCount()

- int LiveCount()

- int WaitingCount()

- int MaxSize()

- bool IsClosed()

- void MarkClosed()

- void WakeAll()

- async bool WaitForRelease(PoolWait w)


## PoolWait (class)

- int gated;

- int budget;

- PoolWait()

- static int GateWaitMs=250;

- static int MaxGateWaits=64;

- static int PollStepMs=20;

- static int PollBudgetMs=10000;


## RowGroup (class)

- string key;

- List<int> rows;

- static RowGroup Create(string groupKey)

- string Key()

- List<int> Rows()

- int Count()


## TracedDbConnection (class)

- IDbConnection inner;

- TracedDbConnection(IDbConnection inner)

- IDbConnection Inner()

- static IDbConnection Wrap(IDbConnection db)

- static IDbConnection Unwrap(IDbConnection db)

- DbResult Query(string sql)

- DbResult Query(string sql, DbParams prms)

- int Execute(string sql)

- int Execute(string sql, DbParams prms)

- string ExecuteScalar(string sql)

- string ExecuteScalar(string sql, DbParams prms)

- async DbResult QueryAsync(string sql)

- async DbResult QueryAsync(string sql, DbParams prms)

- async int ExecuteAsync(string sql)

- async int ExecuteAsync(string sql, DbParams prms)

- async string ExecuteScalarAsync(string sql)

- async string ExecuteScalarAsync(string sql, DbParams prms)

- int GetProvider()

- bool IsConnected()

- void Close()

- void Dispose()

- void BeginTransaction()

- void Commit()

- void Rollback()

- async bool BeginTransactionAsync()

- async bool CommitAsync()

- async bool RollbackAsync()


## bool (delegate)

`delegate bool RowFn(int row);`


## int (delegate)

`delegate int RowCmpFn(int a, int b);`


## void (delegate)

`delegate void SqlTraceSink(string sql, long us);`


## Agg (enum)

- Sum

- Avg

- Min

- Max

- Count


## ColKind (enum)

- Str

- Int

- Float


## IDbConnection (interface)

- bool IsConnected();

- void Close();

- void BeginTransaction();

- void Commit();

- void Rollback();

- async bool BeginTransactionAsync();

- async bool CommitAsync();

- async bool RollbackAsync();

- void Dispose();


## IDbConnector (interface)

- async IDbConnection ConnectAsync();

- int GetProvider();


## IDbExecutor (interface)

- DbResult Query(string sql, DbParams prms);

- int Execute(string sql, DbParams prms);

- DbResult Query(string sql);

- int Execute(string sql);

- string ExecuteScalar(string sql);

- string ExecuteScalar(string sql, DbParams prms);

- int GetProvider();

- async DbResult QueryAsync(string sql, DbParams prms);

- async int ExecuteAsync(string sql, DbParams prms);

- async DbResult QueryAsync(string sql);

- async int ExecuteAsync(string sql);

- async string ExecuteScalarAsync(string sql);

- async string ExecuteScalarAsync(string sql, DbParams prms);


## DbCell (struct)

- public int kind;

- public long ival;
