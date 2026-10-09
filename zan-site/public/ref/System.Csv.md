# System.Csv

> 源码: `packages/Zan.Csv/src/System/Csv/Csv.zan`


## Csv (class)

- public static int MaxFieldsPerRecord=1000000;

- public static int MaxRecords=1000000;

- static void FinishRecord(CsvTable t, List<string> rec)

- static CsvTable Parse(string text)

- static CsvTable ParseSep(string text, string separator)

- static List<string> ParseRow(string line)

- static string Escape(string field)

- static string EscapeSep(string field, string separator)

- static string RowToText(List<string> fields)

- static string RowToTextSep(List<string> fields, string separator)

- static string Serialize(List <List<string>> rows)

- static string SerializeSep(List <List<string>> rows, string separator)


## CsvTable (class)

- List<string> header;

- List <List<string>> rows;

- CsvTable()

- List<string> Header()

- List <List<string>> Rows()

- int RowCount()

- List<string> Column(string name)
