# Gui.Component.CodeEditor

> 源码: `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Analysis.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Completion.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Debug.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Intelli.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Render.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.Symbols.zan`, `packages/Zan.Gui.CodeEditor/src/Gui/Component/CodeEditor/CodeEditor.zan`


## Caret (class)

- int line;

- int col;

- Caret(int line, int col)


## CeAnalysisLease (class)


## CeAnalysisLensResult (class)

- Dict <int, int> counts;

- Dict <int, List<int>> references;


## CeAnalysisResult (class)

- List<Diagnostic> errors;

- List<QuickFix> fixes;

- List<CeBufType> bufferTypes;

- List<ProjType> projectTypes;

- List<CeCompletionCandidate> candidates;

- List<CeHlLine> highlights;

- CeFileIndex references;

- Dict <string, string> signatures;

- List<string> imports;

- string fileNamespace;


## CeAnalysisSnapshot (class)

- CeAnalysisLease lease;

- List<string> lines;

- List<string> projectSources;

- List<ProjType> projectTypes;

- List<CeSymbolType> symbolTypes;

- string path;

- int version;

- int projectVersion;

- int symbolVersion;

- int language;

- int themeSignature;

- int keywordColor;

- int typeColor;

- int stringColor;

- int commentColor;

- int numberColor;

- int textColor;

- AtomicInt cancelled;


## CeBufType (class)

- string name;

- string members;

- CeBufType(string name, string members)


## CeCandidateItem (class)

- string item;

- string kind;

- CodeCompletionItem semantic;

- CeCandidateItem(string item, string kind)


## CeCompletionCandidate (class)

- string item;

- string kind;

- CeCompletionCandidate(string item, string kind)


## CeExtRefCount (class)

- string key;

- int count;

- CeExtRefCount(string key, int count)


## CeExtRefDecl (class)

- string owner;

- string name;

- CeExtRefDecl(string owner, string name)


## CeFileIndex (class)

- List<string> stripped;

- List<string> owners;

- List<string> decls;

- static CeFileIndex Of(List<string> ls)

- static CeFileIndex OfText(string body)


## CeHlLine (class)

- string src;

- List<CodeSpan> spans;

- bool blockIn;

- bool blockOpen;


## CeLensMemo (class)

- string name;

- int count;

- int fresh;

- CeLensMemo(string name, int count, int fresh)


## CeLineBracketCache (class)

- string src;

- int brace;

- int paren;

- int usingBad;

- CeLineBracketCache(string src, int brace, int paren, int usingBad)


## CeMissingUsingLine (class)

- string src;

- string ns;

- string word;

- CeMissingUsingLine(string src, string ns, string word)


## CeSemanticCompletionIntent (class)

- int token;

- string path;

- string text;

- int stamp;

- int line;

- int col;

- int start;

- bool dot;

- bool forced;


## CeSemanticTextMemo (class)

- string path;

- int version;

- int line;

- int col;

- string name;

- string text;

- bool Matches(string path, int version, int line, int col, string name)


## CeStdlibTypeEntry (class)

- string name;

- string ns;

- CeStdlibTypeEntry(string name, string ns)


## CeSymbolMember (class)

- public string owner;

- public string name;

- public string kind;

- public string sig;

- public bool isStatic;

- public CeSymbolMember(string owner, string name, string kind, string sig, bool isStatic)


## CeSymbolType (class)

- public string name;

- public string baseTypes;

- public CeSymbolType(string name, string baseTypes)


## CeTypeScope (class)

- public string typeName;

- public int depth;

- public CeTypeScope(string typeName, int depth)


## CeVisibleRowLayout (class)

- int line;

- int top;

- int lensY;

- string decl;

- CeVisibleRowLayout(int line, int top, int lensY, string decl)


## CodeCompletionItem (class)

- string label;

- string kind;

- string detail;

- string doc;

- string insertText;

- bool hasTextEdit;

- int startLine;

- int startCol;

- int endLine;

- int endCol;

- CodeCompletionItem(string label, string kind, string detail, string doc)


## CodeEditor (class)

- App analysisApp;

- CeAnalysisLease analysisLease;

- CeAnalysisSnapshot analysisRunning;

- CeAnalysisSnapshot analysisApplied;

- AtomicInt analysisWorkers=new AtomicInt(0);

- bool analysisDetached;

- bool analysisQueued;

- bool analysisWanted;

- int projectVersion;

- string analysisFailure="";

- Dict <int, int> analysisLensCounts;

- Dict <int, List<int>> analysisReferenceLines;

- Dict <string, string> analysisSignatures;

- List<string> analysisImports;

- string analysisNamespace="";

- int analysisKeywordColor;

- int analysisTypeColor;

- int analysisStringColor;

- int analysisCommentColor;

- int analysisNumberColor;

- int analysisTextColor;

- int analysisThemeSignature;

- CodeEditorTextLoad textLoadRequest;

- CeAnalysisSnapshot workerSnapshot;

- bool workerBufferTypesReady;

- void AttachAnalysis(App app)

- void DetachAnalysis()

- async void DrainAnalysisAsync()

- CodeEditorTextLoad BeginTextLoad(string path, int language)

- static CodeEditorPreparedText PrepareText(string text)

- bool ApplyPreparedText(CodeEditorTextLoad request, CodeEditorPreparedText prepared)

- bool AnalysisSnapshotCurrent(CeAnalysisSnapshot snapshot)

- CeAnalysisSnapshot CaptureAnalysisSnapshot()

- void RequestAnalysis()

- void StartAnalysis()

- static CodeEditor AnalysisWorker(CeAnalysisSnapshot snapshot)

- static bool SnapshotCancelled(CeAnalysisSnapshot snapshot)

- bool AnalysisCancelled()

- void AnalysisMemberRecvs(CeFileIndex index, string owner, List<string> family, List<string> receivers)

- static CeAnalysisResult ComputeAnalysis(CeAnalysisSnapshot snapshot)

- static void CollectAnalysisSignatures(List<string> source, Dict <string, string> signatures)

- static CeAnalysisLensResult ComputeAnalysisLenses(CeAnalysisSnapshot snapshot, CeFileIndex index)

- bool ApplyAnalysis(CeAnalysisSnapshot snapshot, CeAnalysisResult result)

- void CompleteAnalysis(CeAnalysisSnapshot snapshot, CeAnalysisLensResult lenses, string failure)

- string AnalysisFailure()

- bool LocalDiagnosticsCurrent()

- string LocalDiagnosticMessage(int line)

- int CopyCachedReferenceLines(string name, int declaration, List<int> output)

- int CachedLensRefCount(int line)

- void UpdateAnalysisPalette(Theme theme, int defaultText)


## CodeEditor (class)

- static bool Has(List<string> xs, string v)

- static bool StartsWithP(string s, string pre)

- static void KeywordList(List<string> outk)

- static void StdlibList(List<string> outk)

- static void CollectWords(List<string> ls, List<string> outw)

- static void CollectWordsRange(List<string> ls, int from, int to, List<string> outw)

- void CaptureCompletionSource()

- bool CompletionSourceCurrent()

- void ApplyKindFilter()

- bool HasKind(string kind)

- static int MatchScore(string cand, string pfx)

- static bool HumpMatch(string cand, string pfx)

- static bool SubseqMatchI(string cand, string pfx)

- static bool EqI(string a, string b)

- static void SortCandidatesByScore(List<CeCandidateItem> items, List<int> scores)

- static void SortCandidatesByScoreTop(List<CeCandidateItem> items, List<int> scores, int limit)

- void ForceCompletion()

- int SemanticCompletionToken()

- void CancelSemanticCompletion()

- void ClearSemanticTextMemos()

- void ResetSemanticProviders()

- CodeSemanticProviderLease AcquireSemanticProviders()

- bool SemanticProvidersCurrent(CodeSemanticProviderLease lease)

- bool RefreshSemanticResult(string method, string path, int stamp, int line, int col, string name, int token)

- bool ClearSemanticTextMemo(string method, string path, int stamp, int line, int col, string name)

- bool ScoreExternalCompletion(string prefix, Dict <string, bool> taken, List<int> scores)

- void UpdateCompletion()

- void ScoreCandidateEntities(List<CeCompletionCandidate> cands, string pfx, Dict <string, bool> taken, List<int> scores)

- void ScoreWords(List<string> words, string defKind, string pfx, Dict <string, bool> taken, List<int> scores)

- void ScoreCandidates(List<string> items, List<string> kinds, string defKind, string pfx, Dict <string, bool> taken, List<int> scores)

- void EnsureCandBase()

- static string MemberKind(string name)

- static bool IsControlWord(string w)

- static string DeclIdentBefore(string trimmed, int at)

- static string PropNameAt(string trimmed)

- static bool MightBeDecl(string line)

- static string DeclNameAt(string line)

- static string StripCode(string line)

- static int FindCallSite(string code, string name, int from)

- int ScanRefs(string name, int declLine, List<int> outLines)

- void EnsureExternRefs()

- void EnsureRefIndex()

- void BeginExternRefs()

- void StepExternRefs(int kbBudget)

- int ExternRefCount(string owner, string member)

- int LensRefCount(string name, int declLine)

- int LensRefCountBudgeted(string name, int declLine)

- void BeginLensFrame(App app)

- bool LensPending()

- void JumpToNextRef(string name, int declLine)

- void CollectRefs(string name, int declLine, List<int> outLines)

- void JumpToRefLine(string name, int targetLine)

- static int FindFrom(string s, string sub, int start)

- static string TrimStr(string s)

- string WordAtMouse(App app, Rect area, int gutterW, int fontSize)

- static void KeywordListK(List<string> outk, List<string> kinds)

- static void CollectWordsK(List<string> ls, List<string> outw, List<string> kinds)

- static void CollectWordsRangeK(List<string> ls, int from, int to, List<string> outw, List<string> kinds)

- static int WordScanRadius()

- static int LowerByte(int b)

- static bool StartsWithI(string s, string pre)

- void AcceptCompletion(bool viaTab)

- bool TryCompleteWord()

- string GhostExtension()

- string ReceiverBefore(string ln, int dotPos)

- string OwnerTypeAtCaret()

- string EnclosingTypeOfCaretDecl()

- void DotCompletion()

- void UsingDotCompletion()

- static void ChildNamespaces(string prefix, List<string> out2)

- static void AddMembersIn(List<string> out2, string typeName, int wantStatic)

- static void AddMembers(List<string> out2, string typeName)

- static void AddLinqMembers(List<string> out2)

- static void AddWords(List<string> out2, string s)

- static void AddKindWords(List<string> cand, List<string> kinds, string s, string kind)

- static string StripModifiers(string trimmed)

- static string TypeDeclName(string trimmed)

- static bool DeclHeadOk(string head)

- static string MemberDeclName(string trimmed)

- static void ScanTypesInto(string src, List<string> names, List<string> membersJoined)

- static void ScanTypeLinesInto(List<string> ls, List<string> names, List<string> membersJoined)

- void SetProjectFiles(List<string> contents)

- void EnsureProjectScan()

- static bool HasProjType(List<ProjType> ps, string name)

- void ParseUsingsInto(List<string> outNs)

- string FileNamespace()

- bool NsVisible(string ns)

- bool UsesLinq()

- static bool IsKnownStdlibType(string name)

- static string NormalizeStdlibType(string name)

- static bool IsCollectionType(string name)

- static string StdlibNsData()

- static void StdlibTypesForNs(string ns, List<string> out2)

- static List<CeStdlibTypeEntry> nsTypeTable;

- static void EnsureNsTable()

- static void AddNsGroup(List<CeStdlibTypeEntry> table, string ns, string words)

- static string NsForStdlibType(string name)

- bool IsProjectType(string name)

- bool FindTypeMembers(string typeName, List<string> outMem)

- string ResolveVarType(string name)

- void AddStdlibCandidates(List<string> cand, List<string> kinds)

- static Dict <string, bool> SeenOf(List<string> cand)

- static void AddCand(List<string> cand, List<string> kinds, Dict <string, bool> seen, string w, string kind)

- void AddProjectCandidates(List<string> cand, List<string> kinds)

- bool InAttributeContext()

- void EnsureBufTypes()

- bool BufTypesStructuralChange()

- static int BraceKey(string s)

- static bool IdentByte(int b)

- void AnalyzeMissingUsings()

- bool ReplayMissingUsings()

- void ScanLineMissingUsing(int li, List<string> usings, string fileNs)

- string QuickFixNs()

- bool HasQuickFix()

- void AddUsing(string ns)

- void ApplyQuickFix()


## CodeEditor (class)

- static int Find(string s, string sub)

- static string KindWord(string kind)

- static string SigFromLine(string trimmed)

- static string SigInSource(string src, string name)

- string RealSigFor(string name)

- string LspHoverFor(string name, int line, int col)

- string SigHelpForHover(string name)

- string SigHelpFor(string name)

- string CallAtCaret(List<int> outArg)

- static void ParamSpan(string sig, int idx, List<int> out2)

- static string DescSig(string name)

- static string DescDoc(string name)

- static string DescKeyword(string name)

- static void WrapText(string s, int maxW, int fontSize, List<string> outl, int maxLines)

- static void SnippetListK(List<string> outk, List<string> kinds)

- void ExpandSnippet(string name)

- static bool HasInt(List<int> xs, int v)

- bool HasBreakpoint(int line)

- void ToggleBreakpoint(int line)

- List<int> Breakpoints()

- void SetExecLine(int line)

- void SetFoldRegions(List<CodeFoldRegion> regions)

- void SetFoldRanges(List<int> starts, List<int> ends)

- int FoldRegionCount()

- bool AnyFolded()

- bool FoldRegionValid(int i)

- bool FoldableAt(int line)

- bool FoldedAt(int line)

- bool FoldHides(int line)

- void ToggleFold(int line)

- void UnfoldAll()

- void UnfoldContaining(int line)

- int FoldVisibleTotal()

- int FoldDocLineOfRow(int row)

- int FoldVisibleRowOf(int docLine)

- int FoldNextVisible(int ln)

- void HandleInput(App app)

- void HandleCharKey(App app)

- void HandleNavKey(App app)

- static int ColFromX(string s, int px, int fontSize)


## CodeEditor (class)

- static List<string> s_lineNumPool;

- string hoverDwellPath;

- int hoverDwellStamp;

- int hoverDwellScrollY;

- static string GetLineNumStr(int line1)

- static bool InSet(string chars, string ch)

- static bool ContainsSub(string hay, string needle)

- static bool IsKeyword(string w)

- static bool IsLuaKeyword(string w)

- static bool IsPyKeyword(string w)

- static bool IsKeywordLang(string w, int lang)

- static int BlockEnd(string line, int from, int lang)

- int HlThemeSig(Gui.Theme t, int defaultText)

- CeHlLine GetHl(int ln, string text, Theme t, int defaultText)

- bool Highlight(string line, Theme t, int defaultText, bool inBlock, List<CodeSpan> spans)

- static bool HighlightSpans(string line, int kwC, int tyC, int strC, int comC, int numC, int def, bool inBlock, List<CodeSpan> spans)

- static bool HighlightSpansLang(string line, int kwC, int tyC, int strC, int comC, int numC, int def, bool inBlock, int lang, List<CodeSpan> spans)

- static int SelColor(Gui.Theme t)

- static void DrawSelHighlight(Canvas c, App app, int aL, int aC, int cL, int cC, int ln, string text, int textX, int rowY, int lineH, int fontSize, Rect area)

- static void DrawWordOccurrences(Canvas c, string text, string targetWord, int textX, int rowY, int lineH, int fontSize, int color)

- static void DrawBracketBox(Canvas c, string text, int col, int textX, int rowY, int lineH, int fontSize, int color)

- static string DiagMsg(List<Diagnostic> ds, int line)

- static bool DiagHas(List<Diagnostic> ds, int line)

- static bool QuickFixHas(List<QuickFix> qs, int line)

- int Render(App app, Rect area)

- void RenderLines(App app, Rect area, int lineH, int fontSize, int gutterW, int textX, int visLines, bool focused, int hoverColor, int breakpointColor, int lineNumberColor, int caretColor, int defaultText)

- void RenderOverviewRuler(App app, Rect area, int visLines)

- void RenderAcPopup(App app, Rect area, int lineH, int textX, int fontSize, bool focused, int visLines)

- void RenderSigHelp(App app, Rect area, int lineH, int textX, int fontSize, bool focused, int visLines)

- static string WordAt(string line, int col)

- static bool IsIdentChar(string ch)

- static bool IsIdentByte(int b)

- static string LookupVar(List<DbgVar> vars, string name)

- static DbgVar LookupVarRaw(List<DbgVar> vars, string name)

- static string LeadWs(string line)

- static string Indent(int n)

- static void PrettyJson(string s, List<string> out2, int maxLines)

- static string Clip(string s)

- static void BuildHoverLines(DbgVar v, List<string> out2)


## CodeEditor (class)

- static List<CeSymbolMember> symMembers;

- static List<CeSymbolType> symTypes;

- static int symbolVersion;

- static bool SymbolIndexLoaded()

- static void ClearSymbolIndex()

- static CodeEditorPreparedSymbolIndex PrepareSymbolIndex(string text)

- static bool ApplyPreparedSymbolIndex(CodeEditorPreparedSymbolIndex prepared)

- static bool LoadSymbolIndex(string path)

- static bool HasSymbolType(string typeName)

- static string TypeNameOn(string line)

- static int FindWordFrom(string s, string word, int from)

- static int BraceDelta(string line)

- static int BraceDeltaPre(string s)

- static void OwnerPerLine(List<string> ls, List<string> outOwners)

- static void StripCodeInto(List<string> ls, List<string> outStripped)

- static void OwnerPerLinePre(List<string> stripped, List<string> outOwners)

- static string EnclosingTypeAt(List<string> ls, int at)

- static string IdentBefore(string s, int end)

- static void CollectTypedVars(List<string> ls, string typeName, List<string> outNames)

- static void CollectTypedVarsPre(List<string> stripped, string typeName, List<string> outNames)

- static int CountMemberRefsIn(List<string> ls, string member, string owner, List<int> outLines)

- static int CountMemberRefsPre(CeFileIndex idx, string member, string owner, List<int> outLines)

- static void MemberRecvsPre(List<string> stripped, string owner, List<string> outFamily, List<string> outRecvs)

- static void MemberRecvsWithTypes(List<string> stripped, string owner, List<string> outFamily, List<string> outRecvs, List<CeSymbolType> types)

- static int CountMemberRefsWith(CeFileIndex idx, List<string> family, List<string> recvs, string member, List<int> outLines)

- static int FindMemberDeclLineIn(List<string> ls, string owner, string member)

- static bool IsMemberDeclOf(string line, string member)

- static string IdentAt(string s, int at)

- static string SymbolDeclaringType(string typeName, string member)

- static void SymbolMembers(string typeName, List<string> out2)

- static void SymbolMembersIn(string typeName, List<string> out2, int wantStatic)

- static bool SymbolTypeLacksMember(string typeName, string member)

- static void SymbolSubtypesOf(string typeName, List<string> out2)

- static void SymbolSubtypesIn(List<CeSymbolType> types, string typeName, List<string> out2)

- static string SymbolFirstBase(string typeName)

- static string SymbolFirstBaseIn(List<CeSymbolType> types, string typeName)

- static string SymbolSignature(string typeName, string member)

- static string SymbolSignatureAny(string member)

- static void SymbolOverloads(string typeName, string member, List<string> out2)

- static string SymbolKind(string typeName, string member)

- static void SymbolTypesStartingWith(string prefix, List<string> out2)

- static void SymbolExtensionsFor(string typeName, List<string> out2)


## CodeEditor (class)

- static string lang="zh";

- List<string> lines;

- int curLine;

- int curCol;

- int scrollLine;

- int scrollY;

- int targetScrollY;

- int maxScrollY;

- int ensuredCaretLine;

- int lastId;

- public string documentPath="";

- public CodeCompletionProvider externalCompletion;

- public CodeSignatureProvider externalSignature;

- public CodeHoverProvider externalHover;

- List<CeVisibleRowLayout> visibleRows;

- int lensRowH;

- List<string> acItems;

- List<CeCandidateItem> acVisibleCandidates;

- string acSourceText;

- int acSourceVersion;

- string acSourcePath;

- int acSourceLine;

- int acSourceCol;

- List<CeCandidateItem> acAllCandidates;

- string acFilter;

- int filtX;

- int filtY;

- int filtW;

- int filtH;

- List<string> acFull;

- List<CodeLensRef> codeLens;

- bool refPopOpen;

- string refPopName;

- List<int> refPopLines;

- int refPopX;

- int refPopY;

- int refPopSel;

- int refPopGX;

- int refPopGY;

- int refPopGW;

- int refPopGH;

- int refPopRowH;

- int refPopHeadH;

- int refPopShown;

- bool gotoRequested;

- int acSel;

- bool acActive;

- int acStart;

- bool acDot;

- bool acForced;

- List<string> acKinds;

- int popX;

- int popY;

- int popW;

- int popH;

- int popItemH;

- int popFirst;

- int popShown;

- bool selActive;

- int selAnchorLine;

- int selAnchorCol;

- bool mouseSelecting;

- bool barDragging;

- int lastClickMs;

- int lastClickLine;

- int lastClickCol;

- int clickCount;

- int pageRows;

- int textVersion;

- int lastScrollMs;

- int wheelAccum;

- List<Caret> carets;

- int bpALine;

- int bpACol;

- int bpMLine;

- int bpMCol;

- string bpMemoKey;

- string clipboard;

- bool ateCtrlKey;

- List<EditSnapshot> undoStack;

- List<EditSnapshot> redoStack;

- int undoEditKind;

- int undoEditLine;

- int undoEditCol;

- List<Diagnostic> errors;

- int lastDiagLine;

- List<Diagnostic> compErrors;

- int cErrStamp;

- int diagStaleFrame;

- List<CeBufType> muTypes;

- List<string> btLineSrc;

- List<CeCompletionCandidate> cbCandidates;

- int cbGen;

- int candBaseGen;

- List<ProjType> projTypes;

- List<string> projFileSrc;

- bool projScanDirty;

- List<CeExtRefCount> extRefCounts;

- bool extRefDirty;

- int extRefAt;

- List<CeExtRefDecl> extRefDecls;

- CeFileIndex extRefFileIdx;

- int extRefDeclAt;

- string extRefOwnerCur;

- List<string> extRefFamily;

- List<string> extRefRecvs;

- CeFileIndex refIdx;

- int refStamp;

- string sigMemoName;

- string sigMemoText;

- int sigMemoStamp;

- CeSemanticTextMemo signatureLspMemo;

- CeSemanticTextMemo hoverLspMemo;

- CodeSemanticProviderLease semanticProviderLease;

- CeSemanticCompletionIntent semanticCompletionIntent;

- int semanticCompletionEpoch;

- List<CeLensMemo> lensMemos;

- int lensStamp;

- int lensBudget;

- bool lensPending;

- int lensQuietMs;

- List<CeHlLine> hlCache;

- int hlThemeSig;

- List<QuickFix> fixes;

- List<CeMissingUsingLine> muLineCaches;

- string muCtx;

- int muStamp;

- List<CeLineBracketCache> ceLineCaches;

- List<int> bps;

- int execLine;

- List<DbgVar> dbgVars;

- DbgVar hoverTip;

- int hoverTipX;

- int hoverTipY;

- int lastHoverMoveMs;

- int lastHoverX;

- int lastHoverY;

- List<CodeFoldRegion> foldRegions;

- bool ctxOpen;

- int ctxX;

- int ctxY;

- string ctxCmd;

- int edFont;

- int synKw;

- int synTy;

- int synStr;

- int synCom;

- int synNum;

- int synText;

- int cssKw;

- int cssTy;

- int cssStr;

- int cssCom;

- int cssNum;

- int syntaxLang;

- int textMemoStamp=0-1;

- string textMemo="";

- void SetSyntaxLang(int lang)

- int SyntaxLang()

- static int LangForName(string path)

- static string LowerAscii(string s)

- string LineComment()

- void ApplyTheme(int fontPx, int kw, int ty, int str, int com, int num, int text)

- int FontSize(Gui.Theme t)

- CodeEditor()

- static List<string> SplitLines(string s)

- void SetLines(List<string> ls)

- void BumpTextVersion()

- void LoadText(string s)

- string GetText()

- int TextStamp()

- static int ByteColumnToUtf16(string text, int byteCol)

- static int Utf16ColumnToByte(string text, int column)

- int LineCount()

- int CurLine()

- int CurCol()

- string LineAt(int i)

- int LastWidgetId()

- bool HasSelection()

- int ScrollLine()

- void SetScrollLine(int line)

- int ScrollY()

- void SetScrollY(int y)

- bool CompletionActive()

- int CompletionCount()

- int CompletionSel()

- string CompletionItem(int i)

- string CompletionKind(int i)

- string WordAtCaret()

- bool ConsumeGotoRequest()

- void SetDebugVars(List<DbgVar> vars)

- void GoToLine(int line)

- int BufLen()

- void SetCompilerDiagnostics(List<Diagnostic> diagnostics)

- bool CompilerDiagStale()

- void InvalidateDiagStale()

- string CompilerErrMsg(int ln)

- bool HasCompilerErr(int ln)

- void GoToLineCol(int line, int col)

- void InsertStr(string ins)

- void NewLine()

- void BackspaceEdit()

- void DeleteEdit()

- void ClampCol()

- void MoveLeft()

- void MoveRight()

- void MoveUp()

- void MoveDown()

- void MoveWordLeft()

- void MoveWordRight()

- int SmartHomeCol()

- void MoveLineUp()

- void MoveLineDown()

- void IndentSelection()

- void OutdentSelection()

- void OutdentCurrentLine()

- static List<string> CloneLines(List<string> src)

- void EndUndoGroup()

- void PushUndoSnapshot()

- void SaveUndo()

- void SaveUndoTyping()

- void SaveUndoBackspace()

- void RestoreSnapshot(EditSnapshot s)

- void Undo()

- void Redo()

- void SelectAll()

- void ClearSelection()

- void SelectRange(int aLine, int aCol, int bLine, int bCol)

- void SelectWordCaret()

- void SelectLineCaret()

- void SelPrepare(bool shift)

- void BuildRowLayout(Rect area, int lineH, int lensH)

- int VisibleRowCount()

- List<int> rowLine { get }

- int LineAtY(int lineH, int yPix)

- int LineBottomY(App app, int ln)

- int LineTopY(int ln)

- void CaretFromMouse(App app, Rect area, int gutterW, int fontSize)

- void ClearExtraCarets()

- void AddCaretBelow()

- void AddCaretAbove()

- void McInsertStr(string ins)

- void McBackspace()

- void StartSelection()

- string GetSelectedText()

- void DeleteSelection()

- void CopySelection()

- void CutSelection()

- void PasteClipboard()

- void InsertBlock(string ins)

- void DeleteAtCaret()

- void ToggleComment()

- void DuplicateLine()

- void DeleteLine()

- static string LTrim(string s)

- static string RTrim(string s)

- static string Uncomment(string s)

- static string UncommentTok(string s, string tok)

- string TakeCommand()

- static string T(string en, string zh)

- static List<ContextMenuItem> BuildCtxMenu()

- void RunCtxAction(App app, int act)

- bool CtxMenuOpen()

- void RenderCtxMenu(App app)

- void CheckErrors()

- void EnsureLineScan()

- void ScanLineBrackets(int li)

- bool BracketInsideLexeme(int line, int col)

- bool ScanPairForward(int line, int col, int open, int close)

- bool ScanPairBackward(int line, int col, int open, int close)

- void UpdateBracketPair()


## CodeEditorPreparedSymbolIndex (class)

- List<CeSymbolMember> members;

- List<CeSymbolType> types;


## CodeEditorPreparedText (class)

- List<string> lines;

- string text;

- bool applied;


## CodeEditorTextLoad (class)

- string path;

- string previousPath;

- int version;

- int language;


## CodeFoldRegion (class)

- int startLine;

- int endLine;

- bool folded;

- CodeFoldRegion(int startLine, int endLine, bool folded)


## CodeLensRef (class)

- int line;

- string name;

- int x;

- int y;

- int w;

- int count;

- CodeLensRef(int line, string name, int x, int y, int w, int count)


## CodeSemanticProviderLease (class)


## CodeSpan (class)

- string text;

- int color;

- int width;

- CodeSpan(string text, int color)


## ContextMenuItem (class)

- string label;

- string key;

- int action;

- ContextMenuItem(string label, string key, int action)


## DbgVar (class)

- string name;

- string type;

- string val;

- DbgVar(string name, string type, string val)


## Diagnostic (class)

- int line;

- string msg;

- Diagnostic(int line, string msg)


## EditSnapshot (class)

- List<string> lines;

- string text;

- int line;

- int col;

- EditSnapshot(List<string> lines, int line, int col)

- EditSnapshot(string text, int line, int col)


## ProjType (class)

- string name;

- string members;

- ProjType(string name, string members)


## QuickFix (class)

- int line;

- string ns;

- QuickFix(int line, string ns)


## List (delegate)

`delegate List<CodeCompletionItem> CodeCompletionProvider(string path, int line0, int col0);`


## string (delegate)

`delegate string CodeSignatureProvider(string path, int line0, int col0, string callee);`


## string (delegate)

`delegate string CodeHoverProvider(string path, int line0, int col0, string word);`
