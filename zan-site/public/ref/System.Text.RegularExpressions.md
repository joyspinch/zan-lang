# System.Text.RegularExpressions

> 源码: `packages/Zan.Text/src/System/Text/RegularExpressions/Match.zan`, `packages/Zan.Text/src/System/Text/RegularExpressions/Regex.zan`, `packages/Zan.Text/src/System/Text/RegularExpressions/RegexProgram.zan`


## Match (class)

- bool success;

- string input;

- List<int> caps;

- int groups;

- Match()

- static Match Failure()

- static Match FromCaps(string input, List<int> caps, int groups)

- bool Success()

- int Index()

- int Length()

- string Value()

- int GroupCount()

- int GroupIndex(int n)

- int GroupLength(int n)

- string Group(int n)

- bool GroupSuccess(int n)


## Regex (class)

- RegexProgram prog;

- string pattern;

- Regex()

- static Regex Compile(string pattern)

- static Regex CompileOptions(string pattern, bool ignoreCase)

- static string Validate(string pattern)

- string Pattern()

- int GroupCount()

- int GroupNumber(string name)

- List<string> GroupNames()

- bool IsMatch(string input)

- Match Find(string input)

- Match FindFrom(string input, int start)

- int ScanFrom(nint buf, int len, int start, List<int> caps)

- void Overflow(int start)

- List<Match> Matches(string input)

- int Count(string input)

- string Replace(string input, string replacement)

- string ReplaceFirst(string input, string replacement)

- string Expand(string replacement, Match m)

- string ExpandFor(string replacement, Match m)

- int RefNumber(string body)

- static int DigitValue(string d)

- static bool IsDigit(string d)

- List<string> Split(string input)

- static bool Matched(string input, string pattern)

- static string Replaced(string input, string pattern, string replacement)

- static bool IsMetaChar(int c)

- static string Escape(string text)


## RegexNamedGroup (class)

- public string name;

- public int groupNum;

- public RegexNamedGroup(string name, int groupNum)


## RegexProgram (class)

- static int OpChar()

- static int OpAny()

- static int OpClass()

- static int OpMatch()

- static int OpJmp()

- static int OpSplit()

- static int OpSave()

- static int OpBol()

- static int OpEol()

- static int OpWordB()

- static int OpBackref()

- static int OpBos()

- static int OpEos()

- static int OpGpos()

- static int OpMark()

- static int OpEmptyChk()

- static int OpLook()

- static int OpLookEnd()

- static int LookAhead()

- static int LookAheadNeg()

- static int LookBehind()

- static int LookBehindNeg()

- static int LookAtomic()

- static int MemRange()

- static int MemNotSet()

- List<int> op;

- List<int> ax;

- List<int> ay;

- List<int> az;

- List<int> clsKind;

- List<int> clsLo;

- List<int> clsHi;

- List<int> clsStart;

- List<int> clsCount;

- List<int> clsNeg;

- List<RegexNamedGroup> namedGroups;

- int groups;

- int marks;

- bool ignoreCase;

- static int MaxRecursionDepth=10000;

- bool multiline;

- bool dotAll;

- string pattern;

- string error;

- nint pbuf;

- int plen;

- int ppos;

- RegexProgram()

- static RegexProgram Compile(string pattern, bool ignoreCase)

- string Error()

- int GroupCount()

- string Pattern()

- int GroupNumber(string name)

- List<string> GroupNames()

- static int DecodeAt(nint buf, int len, int sp)

- static int StepWidth(nint buf, int len, int sp)

- static int Utf8Len(int cp)

- int Emit(int o, int x, int y, int z)

- int Here()

- static bool XIsPc(int o)

- static bool YIsPc(int o)

- int CopyRange(int from, int to)

- int PeekAt(int i)

- int Peek()

- int Next()

- int NextCp()

- void ParseInlineFlags()

- void ParseAlt()

- void ParseSeq()

- void ParseTerm()

- void ParseQuantifier(int start)

- int ParseInt()

- void ApplyQuantifier(int start, int min, int max, bool lazy)

- void AppendBody(List<int> bo, List<int> bx, List<int> by, List<int> bz, int delta)

- void Truncate(int n)

- void StarAt(int start, int len, bool lazy)

- void PlusAt(int start, int len, bool lazy)

- void OptionalAt(int start, int len, bool lazy)

- void Shift(int start, int len, int n)

- void ParseAtom()

- void ParseGroup()

- string CharText(int c)

- static string Hex2(int v)

- int NewGroup(string name)

- string ParseGroupName(int closer)

- void ParseCaptureBody(int gi)

- void ParseGroupTail(int gi)

- void ParseLook(int kind)

- int FixedWidth(int from, int to)

- bool ClassIsAscii(int ci)

- int BeginClass(bool negate)

- void AddMember(int ci, int kind, int lo, int hi)

- void AddRange(int ci, int lo, int hi)

- void AddShorthand(int ci, int kind)

- bool AddPosixClass(int ci, string name, bool negate)

- void ParseClass()

- bool ParsePosixInClass(int ci)

- int EscapeValue(int e)

- static int HexDigit(int c)

- int ParseHexEscape()

- int ParseFixedHex(int digits)

- int ParseControlEscape()

- void ParseEscape()

- static int Fold(int c)

- static bool IsWordCp(int c)

- static bool InShorthand(int kind, int c)

- bool ClassHas(int ci, int c)

- int RunAt(nint input, int len, int start, int origin, List<int> caps)

- bool Step(RegexRun run, int pc, int sp, int depth)


## RegexRun (class)

- nint input;

- int len;

- List<int> caps;

- List<int> marks;

- int budget;

- int end;

- int origin;

- int subEnd;

- bool overflow;

- RegexRun()
