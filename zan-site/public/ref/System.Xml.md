# System.Xml

> 源码: `packages/Zan.Xml/src/System/Xml/XmlDocument.zan`, `packages/Zan.Xml/src/System/Xml/XmlException.zan`, `packages/Zan.Xml/src/System/Xml/XmlNode.zan`, `packages/Zan.Xml/src/System/Xml/XmlParser.zan`


## XmlAttribute (class)

- public string name;

- public string value;


## XmlDocument (class)

- static int MaxDepth=512;

- public List<XmlNode> children=new List<XmlNode>();

- static XmlDocument Parse(string xml)

- static XmlDocument Load(string path)

- XmlNode RootElement()

- XmlNode Element(string name)

- string ToXml()

- string ToPrettyXml()

- void Save(string path)

- static void WriteNode(StringBuilder sb, XmlNode n, int depth, bool pretty)

- static void WriteAttrs(StringBuilder sb, XmlNode n)

- static void EmitCdata(StringBuilder sb, string text)

- static void EscapeText(StringBuilder sb, string t)

- static void EscapeAttr(StringBuilder sb, string t)

- static void EscapeRuns(StringBuilder sb, string t, bool isAttr)

- static void Indent(StringBuilder sb, int depth, bool pretty)

- static void Newline(StringBuilder sb, bool pretty)


## XmlException (class)

- public XmlException(string message)


## XmlNode (class)

- public int kind;

- public string name;

- public string text;

- public List<XmlAttribute> attrs;

- public List<XmlNode> children;

- static XmlNode NewElement(string name)

- static XmlNode NewText(string text)

- static XmlNode NewCdata(string text)

- static XmlNode NewComment(string text)

- static XmlNode NewPi(string target, string body)

- static XmlNode NewDecl()

- static XmlNode NewDoctype(string raw)

- bool IsElement()

- void AddChild(XmlNode child)

- void MergeText(string piece)

- void AddText(string text)

- void AddAttr(XmlAttribute a)

- void SetAttr(string attrName, string value)

- string Attr(string attrName)

- string AttrOr(string attrName, string fallback)

- string LocalName()

- static string LocalNameOf(string qname)

- bool MatchesName(string want)

- XmlNode Element(string name)

- List<XmlNode> Elements(string name)

- List<XmlNode> ElementChildren()

- string InnerText()

- void AppendTextInto(StringBuilder sb)

- static int IndexOf(string s, string needle, int from)


## XmlParser (class)

- string s;

- int pos;

- int len;

- static int MaxDepth=512;

- static XmlDocument Parse(string src)

- XmlDocument ReadDocument()

- XmlNode ReadElement(int depth)

- bool ReadAttrs(XmlNode elem, bool isDecl)

- void ReadChildren(XmlNode elem, int depth)

- XmlNode ReadComment()

- XmlNode ReadCdata()

- XmlNode ReadPiOrDecl()

- XmlNode ReadDoctype()

- string ReadName()

- string DecodeText(string raw)

- int ReadEntity(string raw, int i, StringBuilder sb)

- static int DigitVal(int c, bool hex)

- void SkipWs()

- int CurB()

- bool Has(string lit)

- void Err(string msg)
