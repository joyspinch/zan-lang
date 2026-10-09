# System.Knowledge

> 源码: `packages/Zan.Knowledge/src/System/Knowledge/GalleryIndex.zan`


## GalleryIndex (class)

- static GalleryResult Generate(string root, string seedPath)

- static void AddRefs(JsonValue e, List<string> refs)

- static void ScanTemplates(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)

- static void ScanExamples(string dir, string root, List<string> refs, List<JsonValue> outp, GalleryResult r)

- static string Manifest(string path, string key)

- static string TemplateKind(string family, bool hasDesign)

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
