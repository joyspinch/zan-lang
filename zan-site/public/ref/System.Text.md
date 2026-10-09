# System.Text

> 源码: `packages/Zan.Text/src/System/Text/Bm25Index.zan`, `packages/Zan.Text/src/System/Text/FuzzyMatching.zan`, `packages/Zan.Text/src/System/Text/Markdown.zan`, `packages/Zan.Text/src/System/Text/Pinyin.zan`, `packages/Zan.Text/src/System/Text/Template.zan`, `packages/Zan.Text/src/System/Text/TextTable.zan`, `packages/Zan.Text/src/System/Text/TextTokenizer.zan`, `stdlib/System/Text/Encoding.zan`


## Bm25Candidate (class)

- int id;

- double score;

- long order;

- Bm25Candidate(int id, long order)


## Bm25Document (class)

- int id;

- int length;

- long order;

- Dictionary <string, Bm25Term> terms=new Dictionary <string, Bm25Term>();

- List<Bm25Term> termList=new List<Bm25Term>();

- Bm25Document(int id, long order)


## Bm25Hit (class)

- int id;

- double score;

- Bm25Hit(int id, double score)


## Bm25Index (class)

- Dictionary <int, Bm25Document> documents=new Dictionary <int, Bm25Document>();

- Dictionary <string, Bm25PostingList> postings=new Dictionary <string, Bm25PostingList>();

- TextTokenizer tokenizer;

- long totalLen;

- long nextOrder;

- int visitedPostings;

- double k1=1.2;

- double b=0.75;

- double auxiliaryWeight=0.2;

- Bm25Index()

- Bm25Index(TextTokenizer tokenizer)

- void Add(int id, string text)

- void Remove(int id)

- int Count()

- int LastVisitedPostings()

- List<int> Search(string query)

- List<Bm25Hit> SearchTop(string query, int k)

- List<Bm25Hit> SearchFiltered(string query, int k, Bm25Filter filter)

- JsonValue ExportState()

- static Bm25Index ImportState(JsonValue state)

- static long StateInteger(JsonValue state, string key)

- static bool ValidStateTerm(string text)

- List<Bm25QueryTerm> QueryTerms(string query)

- void Attach(Bm25Document document)

- void Detach(Bm25Document document)

- static bool Better(Bm25Candidate a, Bm25Candidate b)

- static void HeapPush(List<Bm25Candidate> heap, Bm25Candidate candidate)

- static void HeapDown(List<Bm25Candidate> heap, int parent)

- static Bm25Candidate HeapPop(List<Bm25Candidate> heap)


## Bm25Posting (class)

- int id;

- Bm25Term frequency;

- Bm25Posting(int id, Bm25Term frequency)


## Bm25PostingList (class)

- List<Bm25Posting> entries=new List<Bm25Posting>();


## Bm25QueryTerm (class)

- string text;

- double weight;

- Bm25QueryTerm(string text, double weight)


## Bm25Term (class)

- string text;

- int primary;

- int auxiliary;

- int slot;

- Bm25Term(string text)


## Encoding (class)

- [DllImport("crt")]static extern long strlen(string str);

- static string DoubleToString(double val)

- static int ParseInt(string text)

- static double ParseDouble(string text)

- static int GetByteCount(string text)

- static Encoding UTF8 { get }

- static byte[]GetBytes(string text)

- static string GetString(byte[]bytes, int offset, int count)

- static string GetString(byte[]bytes)

- static string ByteToHex(int b)

- static int HexDigit(int c)

- static string CharFromCode(int code)

- static string Utf8FromCodePoint(int code)

- static int DecodeCodePoint(string s, int byteIndex, int[]cpOut)

- static string RuneAt(string s, int byteIndex)

- static int CodePointCount(string s)

- static string RuneFromIndex(string s, int runeIndex)

- static string UrlEncode(string text)

- static string UrlDecode(string text)

- static string HtmlEncode(string text)

- static string JsonEscape(string text)

- static string Base64Encode(string text)

- static string Base64EncodeBytes(string data, int len)

- static int Base64Digit(int c)

- static byte[]Base64Decode(string text)

- static long Rotl32(long x, int n)

- static void Sha1(string data, int len, byte[]digest)

- static string WebSocketAccept(string key)


## Fuzzy (class)

- static bool SameByte(int a, int b)

- static bool Matches(string query, string candidate)

- static double Score(string query, string candidate)

- static bool IsBoundary(string s, int i)

- static bool IsLetter(string ch)

- static bool IsUpper(string ch)

- static bool IsLower(string ch)

- static bool SameChar(string a, string b)

- static bool SameStr(string a, string b)

- static bool SameStrIgnoreCase(string a, string b)


## Markdown (class)

- static string ToHtml(string text)

- static bool Block(StringBuilder html, bool first, string block)

- static string Inline(string text)

- static string InlineAt(string text, int depth)

- static bool SafeUrl(string url)

- static string EscapeHtml(string text)

- static bool IsBlockStart(string line)

- static int HeadingLevel(string line)

- static bool IsQuote(string line)

- static int ListKind(string line)

- static int ListBody(string line, int kind)

- static List<string> Lines(string text)

- static string Trim(string text)

- static bool IsSpace(string ch)

- static bool StartsWith(string text, string prefix)

- static int Find(string text, string needle, int from)


## Pinyin (class)

- static Dictionary <string, string> cache;

- static string ToPinyin(string text)

- static string Initials(string text)

- static string ToPinyinChar(string ch)

- static Dictionary <string, string> Table()

- static Dictionary <string, string> Build()

- static string Lookup(string han)

- static int Utf8ByteWidth(int c0)

- static int Utf8Width(string ch)

- [DllImport("crt")]static extern string getenv(string name);

- [DllImport("crt", EntryPoint="zan_file_fopen")]static extern nint zan_pkg_fopen(string path, string mode);

- [DllImport("crt")]static extern int fclose(nint fp);

- [DllImport("crt")]static extern long fread(byte[]buf, long size, long count, nint fp);

- [DllImport("crt")]static extern int zan_embed_has(string name);

- [DllImport("crt")]static extern string zan_embed_read(string name);

- static string Env(string name)

- static string ReadSmallFile(string path)

- static string ExeDir()

- [DllImport("kernel32", EntryPoint="GetModuleFileNameW")]static extern int GetModuleFileNameW(nint hModule, nint buf, int size);

- [DllImport("kernel32", EntryPoint="WideCharToMultiByte")]static extern int ToMulti(int page, int flags, nint wide, int wideLen, nint mb, int mbLen, nint defChar, nint usedDef);

- static string WideToStr(nint p)

- [DllImport("crt")]static extern int readlink(string path, byte[]buf, int size);

- static bool ReadableFile(string path)

- static bool EmbedExists(string path)

- static string LoadData()


## Template (class)

- static string Render(string text, Dictionary <string, string> vars)

- static string RenderRaw(string text, Dictionary <string, string> vars)

- static string EscapeHtml(string s)

- static string Expand(string text, Dictionary <string, string> vars, bool escape)

- static string Lookup(string key, Dictionary <string, string> vars)

- static string Trim(string s)

- static int Find(string hay, string needle, int from)


## TextTable (class)

- List<string> headers;

- List <List<string>> rows=new List <List<string>>();

- int[]widths;

- TextTable(List<string> headers)

- void AddRow(List<string> row)

- string Render()

- static string Border(int[]widths)

- static string Line(List<string> cells, int[]widths)


## TextToken (class)

- string text;

- int kind;

- int position;

- TextToken(string text, int kind, int position)


## TextTokenizer (class)

- Dictionary <long, int> edges=new Dictionary <long, int>();

- List<int> terminals=new List<int>();

- List<string> words=new List<string>();

- string fingerprint="";

- TextTokenizer()

- void AddWord(string word)

- int LoadDictionary(string path)

- string Fingerprint()

- JsonValue ExportState()

- static TextTokenizer ImportState(JsonValue state)

- List<TextToken> Tokenize(string text)

- static bool IsAsciiWord(int cp)

- static bool IsCjk(int cp)

- static int Fold(int cp)

- static string Normalize(string text)

- bool AcceptWord(string word)

- int ReadDictionary(string data)

- void LongestMatch(string source, int start, int[]scalar, int[]result)

- static void EmitAuxiliary(List<TextToken> tokens, string source, int start, int end, int position, string primary, int[]scalar)


## bool (delegate)

`delegate bool Bm25Filter(int id);`
