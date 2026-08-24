# System.Knowledge

> 源码: `stdlib/System/Knowledge/GalleryIndex.zan`, `stdlib/System/Knowledge/ZformSchema.zan`


## GalleryIndex (class)

- static GalleryResult Generate(string root, string seedPath)

- static void AddRefs(JsonValue e, List<string> refs)

- static void ScanTemplates(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)

- static void ScanExamples(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)

- static string Manifest(string path, string key)

- static string TemplateKind(string family, bool hasZform)

- static string TemplateSummary(string manifest)

- static string Summary(string path, string dir, string fn)

- static bool Decoration(string s)

- static string SameName(string path, string ext)

- static string Rel(string root, string path)

- static string ParentName(string path)

- static string DirOf(string path)

- static string BaseName(string path)

- static string WithoutExt(string path)

- static void SortEntries(List<JsonValue> a)

- static void CheckTopics(JsonValue a)

- static List<string> Lines(string s)

- static bool Has(List<string> a, string s)

- static bool HasPrefix(List<string> a, string prefix)

- static string Trim(string s)

- static string Lower(string s)

- static bool Starts(string s, string p)

- static bool Ends(string s, string p)

- static int Find(string s, string p, int start)

- static string Replace(string s, string a, string b)


## GalleryResult (class)

Source driven gallery generation shared by the CLI and MCP server.

- string json;

- int entries;

- int discovered;

- int templates;

- List<string> warnings;

- GalleryResult()


## KnowledgeControl (class)

- string name;

- string file;

- int line;

- string styleKind;

- bool designer;

- bool creatable;

- bool generic;

- bool inheritsBase;

- string defaultOf;

- List<KnowledgeProp> props;

- List<string> events;

- KnowledgeControl()


## KnowledgeProp (class)

- string variable;

- string key;

- string label;

- string type;

- string aliases;

- string tip;

- string options;

- int step;

- int lo;

- int hi;

- bool range;

- KnowledgeProp()


## ZformResult (class)

- string json;

- string controlsText;

- int controls;

- List<string> warnings;

- ZformResult()


## ZformSchema (class)

Static source reader for the .zform schema and control catalogue.

- static ZformResult Generate(string root, string stdlib, string docPath)

- static void ScanDir(string dir, string root, string stdlib, List<KnowledgeControl> outp, ZformResult result)

- static void ScanFile(string path, string root, string stdlib, List<KnowledgeControl> outp, ZformResult result)

- static KnowledgeControl ReadControl(string name, int line, string path, string root, string stdlib, string header, string body, ZformResult result)

- static KnowledgeProp ReadProp(string line, int at)

- static void ReadChainText(string line, KnowledgeProp p)

- static JsonValue ControlJson(KnowledgeControl c)

- static string factoryKinds="";

- static string factoryCreate="";

- static void ReadFactories(string stdlib)

- static void ReadFactories(KnowledgeControl c)

- static string DefaultOf(string n)

- static string PropType(string k)

- static bool Common(string e)

- static string Manifest(List<string> a)

- static void SortStrings(List<string> a)

- static KnowledgeProp ByVar(List<KnowledgeProp> a, List<KnowledgeProp> b, string v)

- static void AddOrdered(List<KnowledgeProp> a, KnowledgeProp p)

- static void Warn(ZformResult result, string path, string root, string stdlib, int classLine, int offset, string line, string reason)

- static string ReturnString(string body, string method)

- static string MethodBody(string body, string name)

- static int MethodLineOffset(string body, string name)

- static bool HasProps(string body)

- static string Rel(string root, string stdlib, string path)

- static List<int> DepthLines(string s)

- static string Mask(string s)

- static int Match(string s, int open)

- static List<string> Lines(string s)

- static string Trim(string s)

- static bool Starts(string s, string p)

- static bool Ends(string s, string p)

- static int Find(string s, string p, int at)

- static string Replace(string s, string a, string b)

- static string AfterWord(string s, string w)

- static bool Word(string s, string w)

- static bool IsIdentChar(string c)

- static string WordAt(string s, int p)

- static string Identifier(string s, int p)

- static string IdentifierBack(string s, int p)

- static string QuotedArg(string s, int open, int n)

- static string QuotedAfter(string s, string key)

- static List<string> SplitComma(string s)

- static List<string> Split(string s, string sep)

- static int Int(string s)

- static string Without(string s)

- static bool Has(List<string> a, string x)

- static int LineAt(string s, int p)

- static int NextLine(string s, int p)
