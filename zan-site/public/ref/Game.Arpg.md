# Game.Arpg

> 源码: `stdlib/Game/Arpg/Combat.zan`, `stdlib/Game/Arpg/Config.zan`, `stdlib/Game/Arpg/DataBinding.zan`, `stdlib/Game/Arpg/Engine.zan`, `stdlib/Game/Arpg/Entity.zan`, `stdlib/Game/Arpg/Events.zan`, `stdlib/Game/Arpg/Fonts/PixelFont.zan`, `stdlib/Game/Arpg/Formula.zan`, `stdlib/Game/Arpg/Global.zan`, `stdlib/Game/Arpg/Map.zan`, `stdlib/Game/Arpg/Menu.zan`, `stdlib/Game/Arpg/Music.zan`, `stdlib/Game/Arpg/Net.zan`, `stdlib/Game/Arpg/NetRuntime.zan`, `stdlib/Game/Arpg/Presentation.zan`, `stdlib/Game/Arpg/Primitives.zan`, `stdlib/Game/Arpg/Project.zan`, `stdlib/Game/Arpg/RichText.zan`, `stdlib/Game/Arpg/Runtime.zan`, `stdlib/Game/Arpg/Server.zan`, `stdlib/Game/Arpg/ServerEvents.zan`, `stdlib/Game/Arpg/TextLayout.zan`, `stdlib/Game/Arpg/Tween.zan`, `stdlib/Game/Arpg/UiRenderer.zan`, `stdlib/Game/Arpg/UiRuntime.zan`


## ArpgActor (class)

- string id;

- string displayName;

- string definitionId;

- string faction;

- int level;

- int gridX;

- int gridY;

- int direction;

- bool alive;

- ArpgAttributes attributes;

- List<string> skills;

- ArpgInventory inventory;

- ArpgEquipment equipment;

- ArpgCooldowns cooldowns;

- List<ArpgBuffInstance> activeBuffs;

- ArpgActor(string id, string displayName)

- static ArpgActor FromDefinition(string instanceId, ArpgActorDefinition definition)

- string Id()

- string DisplayName()

- string DefinitionId()

- string Faction()

- int Level()

- int GridX()

- int GridY()

- int Direction()

- bool IsAlive()

- ArpgAttributes Attributes()

- ArpgInventory Inventory()

- ArpgEquipment Equipment()

- ArpgCooldowns Cooldowns()

- ArpgAttributes EffectiveAttributes()

- void SetLevel(int newLevel)

- void SetFaction(string newValue)

- void SetGridPosition(int x, int y)

- void SetDirection(int newValue)

- void SetAlive(bool newValue)

- void AddSkill(string skillId)

- bool RemoveSkill(string skillId)

- bool HasSkill(string skillId)

- ArpgBuffInstance ApplyBuff(ArpgBuffDefinition definition, ArpgActor source, bool unique)

- void RemoveBuff(string buffId)

- void RemoveAllBuffs()

- bool HasBuff(string buffId)

- double Heal(double hp, double mp)

- bool Spend(double hp, double mp)

- double TakeDamage(double amount)

- void ClampVitals()

- void Revive(double hpPercent)

- int SkillCount()

- string SkillAt(int index)

- int BuffCount()

- string BuffAt(int index)

- ArpgBuffInstance BuffInstanceAt(int index)

- void RemoveBuffAt(int index)


## ArpgActorDefinition (class)

可复用的角色组件定义，对应一个角色文件。

- string id;

- string displayName;

- bool defaultPlayer;

- string faction;

- string growthId;

- int level;

- int experienceReward;

- int attackInterval;

- int moveInterval;

- int direction;

- int visionRange;

- bool aggressive;

- bool autoCombat;

- int respawnMilliseconds;

- ArpgAttributes attributes;

- List<string> skills;

- List<string> startingBuffs;

- ArpgActorDefinition(string id, string displayName)

- string Id()

- string DisplayName()

- bool DefaultPlayer()

- string Faction()

- string GrowthId()

- int Level()

- int ExperienceReward()

- int AttackInterval()

- int MoveInterval()

- int Direction()

- int VisionRange()

- bool Aggressive()

- bool AutoCombat()

- int RespawnMilliseconds()

- ArpgAttributes Attributes()

- void SetDisplayName(string newValue)

- void SetDefaultPlayer(bool newValue)

- void SetFaction(string newValue)

- void SetGrowth(string newValue)

- void SetLevel(int newValue)

- void SetExperienceReward(int newValue)

- void SetAttackInterval(int newValue)

- void SetMoveInterval(int newValue)

- void SetDirection(int newValue)

- void SetVisionRange(int newValue)

- void SetAggressive(bool newValue)

- void SetAutoCombat(bool newValue)

- void SetRespawnMilliseconds(int newValue)

- void AddSkill(string skillId)

- void AddStartingBuff(string buffId)

- int SkillCount()

- string SkillAt(int index)

- int StartingBuffCount()

- string StartingBuffAt(int index)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgAttributes (class)

角色、物品、技能与增益共用的基础数值属性。
自定义数值属性，与 DM3 的扩展属性模型一致。

- double hp;

- double mp;

- double maxHp;

- double maxMp;

- double accuracy;

- double evasion;

- double criticalChance;

- double criticalDamage;

- double moveSpeedPercent;

- double attackSpeedPercent;

- List<ArpgCustomAttribute> custom;

- ArpgAttributes()

- double Hp()

- double Mp()

- double MaxHp()

- double MaxMp()

- double Accuracy()

- double Evasion()

- double CriticalChance()

- double CriticalDamage()

- double MoveSpeedPercent()

- double AttackSpeedPercent()

- void SetHp(double amount)

- void SetMp(double amount)

- void SetMaxHp(double amount)

- void SetMaxMp(double amount)

- void SetAccuracy(double amount)

- void SetEvasion(double amount)

- void SetCriticalChance(double amount)

- void SetCriticalDamage(double amount)

- void SetMoveSpeedPercent(double amount)

- void SetAttackSpeedPercent(double amount)

- void SetCustom(string name, double amount)

- double GetCustom(string name)

- double Get(string name)

- void AddFrom(ArpgAttributes other)

- void AddModifiersFrom(ArpgAttributes other)

- ArpgAttributes Clone()

- int CustomCount()

- ArpgCustomAttribute CustomAt(int index)


## ArpgBuffDefinition (class)

- string id;

- string displayName;

- string iconResource;

- string title;

- string tooltip;

- int durationMilliseconds;

- int triggerIntervalMilliseconds;

- bool removeOnDeath;

- bool removeOnMove;

- bool removeOnAttack;

- bool removeOnDamage;

- bool movementDisabled;

- bool attackDisabled;

- bool blinded;

- bool runningAllowed;

- ArpgAttributes attributes;

- ArpgBuffDefinition(string id, string displayName)

- string Id()

- string DisplayName()

- string IconResource()

- string Title()

- string Tooltip()

- int DurationMilliseconds()

- int TriggerIntervalMilliseconds()

- bool RemoveOnDeath()

- bool RemoveOnMove()

- bool RemoveOnAttack()

- bool RemoveOnDamage()

- bool MovementDisabled()

- bool AttackDisabled()

- bool Blinded()

- bool RunningAllowed()

- ArpgAttributes Attributes()

- void SetIconResource(string newValue)

- void SetTitle(string newValue)

- void SetTooltip(string newValue)

- void SetDurationMilliseconds(int newValue)

- void SetTriggerIntervalMilliseconds(int newValue)

- void SetRemoveConditions(bool death, bool move, bool attack, bool damage)

- void SetRestrictions(bool movementDisabled, bool attackDisabled, bool blinded, bool runningAllowed)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgBuffInstance (class)

- ArpgBuffDefinition definition;

- ArpgActor source;

- int remainingMilliseconds;

- int triggerElapsedMilliseconds;

- static ArpgBuffInstance CreateFrom(ArpgBuffDefinition definition, ArpgActor source)

- ArpgBuffDefinition Definition()

- ArpgActor Source()

- int RemainingMilliseconds()

- bool IsInfinite()

- bool IsExpired()

- int Tick(int deltaMilliseconds)


## ArpgColor (class)

Arpg 定义使用的 RGBA 颜色。

- int red;

- int green;

- int blue;

- int alpha;

- ArpgColor(int red, int green, int blue, int alpha)

- static ArpgColor Transparent()

- static ArpgColor Black()

- static ArpgColor White()

- int Red()

- int Green()

- int Blue()

- int Alpha()


## ArpgCombat (class)

- static int GridDistance(ArpgActor left, ArpgActor right)

- static bool IsValidTarget(ArpgActor caster, ArpgActor target, string targetFaction)

- static int HitChance(ArpgActor caster, ArpgActor target)

- static double Damage(ArpgActor caster, ArpgActor target, ArpgSkillDefinition skill, bool critical)


## ArpgComponentFile (class)

- string kind;

- string id;

- string file;

- ArpgComponentFile(string kind, string id, string file)

- string Kind()

- string Id()

- string File()


## ArpgComponentKind (class)

- static string Map()

- static string Actor()

- static string Item()

- static string Skill()

- static string Buff()

- static string Window()

- static string Growth()

- static string Prefab()

- static bool IsValid(string kind)


## ArpgConfig (class)

文档所述 App.lua 启动配置的类型化等价物。

- string title;

- int width;

- int height;

- int frameRate;

- bool verticalSync;

- int screenMode;

- bool repeatKeys;

- string defaultFont;

- ArpgColor background;

- ArpgConfig()

- string Title()

- int Width()

- int Height()

- int FrameRate()

- bool VerticalSync()

- int ScreenMode()

- bool RepeatKeys()

- string DefaultFont()

- ArpgColor Background()

- int BackgroundRed()

- int BackgroundGreen()

- int BackgroundBlue()

- int BackgroundAlpha()

- void SetTitle(string newTitle)

- void SetSize(int width, int height)

- void SetFrameRate(int framesPerSecond)

- void SetVerticalSync(bool enabled)

- void SetScreenMode(int mode)

- void SetRepeatKeys(bool enabled)

- void SetDefaultFont(string resourceId)

- void SetBackground(int red, int green, int blue, int alpha)

- ArpgDiagnostics Validate()


## ArpgControl (class)

- ArpgControlDefinition definition;

- string windowId;

- ArpgEvents systemEvents;

- ArpgDataSource dataSource;

- List<ArpgNode> nodes;

- string evaluatedTitle;

- string evaluatedContent;

- ArpgRichTextDocument document;

- ArpgControlEventHandlers prefabEvents;

- int x;

- int y;

- bool visible;

- bool enabled;

- bool focused;

- bool hovered;

- bool pressed;

- bool dragging;

- bool selected;

- double currentValue;

- string appendedContent;

- ArpgControl(ArpgControlDefinition definition, string windowId, ArpgDataSource defaultSource, ArpgProject project, ArpgEvents systemEvents)

- string Id()

- string Kind()

- int X()

- int Y()

- int Width()

- int Height()

- int Order()

- bool Visible()

- bool Enabled()

- bool MouseEvents()

- bool Draggable()

- bool Focused()

- bool Hovered()

- bool Pressed()

- bool Selected()

- double AnchorX()

- double AnchorY()

- double ScaleX()

- double ScaleY()

- int Angle()

- double Opacity()

- bool Clip()

- string Title()

- string Content()

- string Resource()

- ArpgColor BackgroundColor()

- ArpgColor BorderColor()

- ArpgColor TextColor()

- ArpgColor HoverColor()

- ArpgColor PressedColor()

- ArpgColor DisabledColor()

- int BorderWidth()

- int FontSize()

- int HorizontalAlign()

- int VerticalAlign()

- bool Wrap()

- int LineSpacing()

- double Minimum()

- double Maximum()

- double Value()

- double Progress()

- ArpgNineSlice NineSlice()

- ArpgRichTextDocument Document()

- ArpgDataSource DataSource()

- ArpgControlEventHandlers Events()

- int NodeCount()

- ArpgNode NodeAt(int index)

- ArpgNode FindNode(string id)

- void SetPosition(int x, int y)

- void SetVisible(bool newValue)

- void SetEnabled(bool newValue)

- void SetSelected(bool newValue)

- void SetValue(double newValue)

- void ResetValue()

- void AppendContent(string content)

- void ClearAppendedContent()

- void SetDataSource(ArpgDataSource newValue)

- void Evaluate()

- void Update(int deltaMilliseconds)

- bool HitTest(double localX, double localY)

- ArpgNode HitNode(double localX, double localY)

- void SetFocused(bool newValue, ArpgNode node)

- bool PointerDown(int button, int modifiers, double localX, double localY)

- bool PointerUp(int button, int modifiers, double localX, double localY)

- bool PointerMove(int button, int modifiers, double localX, double localY, double deltaX, double deltaY)

- bool ActivateLink(int runIndex)

- void FireCustom(string name, string payload)


## ArpgControlDefinition (class)

- string id;

- string kind;

- int x;

- int y;

- int width;

- int height;

- int order;

- bool visible;

- bool enabled;

- bool mouseEvents;

- bool draggable;

- bool selected;

- double anchorX;

- double anchorY;

- double scaleX;

- double scaleY;

- int angle;

- double opacity;

- bool clip;

- bool richText;

- string title;

- string content;

- string resource;

- string prefabId;

- ArpgColor backgroundColor;

- ArpgColor borderColor;

- ArpgColor textColor;

- ArpgColor hoverColor;

- ArpgColor pressedColor;

- ArpgColor disabledColor;

- int borderWidth;

- int fontSize;

- int horizontalAlign;

- int verticalAlign;

- bool wrap;

- int lineSpacing;

- double minimum;

- double maximum;

- double currentValue;

- ArpgNineSlice nineSlice;

- ArpgDataSource dataSource;

- List<ArpgNodeDefinition> nodes;

- ArpgControlEventHandlers events;

- ArpgControlDefinition(string id, string kind, int x, int y, int width, int height)

- string Id()

- string Kind()

- int X()

- int Y()

- int Width()

- int Height()

- int Order()

- bool Visible()

- bool Enabled()

- bool MouseEvents()

- bool Draggable()

- bool Selected()

- double AnchorX()

- double AnchorY()

- double ScaleX()

- double ScaleY()

- int Angle()

- double Opacity()

- bool Clip()

- bool RichText()

- string Title()

- string Content()

- string Resource()

- string PrefabId()

- ArpgColor BackgroundColor()

- ArpgColor BorderColor()

- ArpgColor TextColor()

- ArpgColor HoverColor()

- ArpgColor PressedColor()

- ArpgColor DisabledColor()

- int BorderWidth()

- int FontSize()

- int HorizontalAlign()

- int VerticalAlign()

- bool Wrap()

- int LineSpacing()

- double Minimum()

- double Maximum()

- double Value()

- ArpgNineSlice NineSlice()

- ArpgDataSource DataSource()

- int NodeCount()

- ArpgNodeDefinition NodeAt(int index)

- ArpgControlEventHandlers Events()

- void SetOrder(int newValue)

- void SetVisible(bool newValue)

- void SetEnabled(bool newValue)

- void SetMouseEvents(bool newValue)

- void SetDraggable(bool newValue)

- void SetSelected(bool newValue)

- void SetAnchor(double x, double y)

- void SetScale(double x, double y)

- void SetAngle(int newValue)

- void SetOpacity(double newValue)

- void SetClip(bool newValue)

- void SetTitle(string newValue)

- void SetContent(string newValue, bool richText)

- void SetResource(string newValue)

- void SetPrefabId(string newValue)

- void SetBackgroundColor(ArpgColor newValue)

- void SetBorder(int width, ArpgColor color)

- void SetTextColor(ArpgColor newValue)

- void SetStateColors(ArpgColor hover, ArpgColor pressed, ArpgColor disabled)

- void SetTextStyle(int fontSize, int horizontalAlign, int verticalAlign, bool wrap, int lineSpacing)

- void SetRange(double minimum, double maximum, double newValue)

- void SetValue(double newValue)

- void SetNineSlice(ArpgNineSlice newValue)

- void SetDataSource(ArpgDataSource newValue)

- void AddNode(ArpgNodeDefinition node)

- void Validate(ArpgDiagnostics diagnostics, string parentPath)


## ArpgControlEventHandlers (class)

- ArpgControlCreated created;

- ArpgControlUpdated updated;

- ArpgControlPointerChanged pointerDown;

- ArpgControlPointerChanged pointerUp;

- ArpgControlPointerChanged pointerMove;

- ArpgControlFocusChanged focusChanged;

- ArpgControlDragged dragged;

- ArpgControlLinkActivated linkActivated;

- ArpgControlActivated activated;

- ArpgControlValueChanged valueChanged;

- ArpgControlCustomEvent customEvent;

- void OnCreated(ArpgControlCreated handler)

- void OnUpdated(ArpgControlUpdated handler)

- void OnPointerDown(ArpgControlPointerChanged handler)

- void OnPointerUp(ArpgControlPointerChanged handler)

- void OnPointerMove(ArpgControlPointerChanged handler)

- void OnFocusChanged(ArpgControlFocusChanged handler)

- void OnDragged(ArpgControlDragged handler)

- void OnLinkActivated(ArpgControlLinkActivated handler)

- void OnActivated(ArpgControlActivated handler)

- void OnValueChanged(ArpgControlValueChanged handler)

- void OnCustomEvent(ArpgControlCustomEvent handler)

- void RaiseCreated(string controlId)

- void RaiseUpdated(string controlId, int deltaMilliseconds)

- bool RaisePointerDown(string controlId, string nodeId, int button, int modifiers, double x, double y)

- bool RaisePointerUp(string controlId, string nodeId, int button, int modifiers, double x, double y)

- bool RaisePointerMove(string controlId, string nodeId, int button, int modifiers, double x, double y)

- void RaiseFocusChanged(string controlId, bool focused)

- void RaiseDragged(string controlId, double deltaX, double deltaY)

- void RaiseLink(string controlId, string link)

- void RaiseActivated(string controlId)

- void RaiseValueChanged(string controlId, double newValue)

- void RaiseCustom(string controlId, string name, string payload)


## ArpgControlKind (class)

- static string Label()

- static string Button()

- static string ImageBox()

- static string TextBox()

- static string ProgressBar()

- static string RichTextBox()

- static string Image()

- static string Progress()

- static string Prefab()

- static string Custom()

- static bool IsValid(string kind)


## ArpgCooldownEntry (class)

- string name;

- int remaining;

- int duration;

- ArpgCooldownEntry(string name, int remaining, int duration)


## ArpgCooldowns (class)

- List<ArpgCooldownEntry> entries;

- ArpgCooldowns()

- int IndexOf(string name)

- void Start(string name, int milliseconds)

- bool IsReady(string name)

- int Remaining(string name)

- int Progress(string name)

- void Tick(int deltaMilliseconds)


## ArpgCustomAttribute (class)

- string name;

- double amount;

- ArpgCustomAttribute(string name, double amount)

- string Name()

- double Amount()

- void SetAmount(double amount)


## ArpgDataSource (class)

每帧由游戏 UI 模板求值的可变 key/newValue 数据源。

- ArpgValue root;

- int revision;

- ArpgDataSource()

- ArpgValue Root()

- int Revision()

- void Set(string path, ArpgValue newValue)

- void SetText(string path, string newValue)

- void SetNumber(string path, double newValue)

- void SetInteger(string path, int newValue)

- void SetBoolean(string path, bool newValue)

- ArpgValue Resolve(string path)


## ArpgDiagnostic (class)

- int severity;

- string code;

- string path;

- string message;

- ArpgDiagnostic(int severity, string code, string path, string message)

- int Severity()

- string Code()

- string Path()

- string Message()


## ArpgDiagnosticSeverity (class)

- static int Info()

- static int Warning()

- static int Error()


## ArpgDiagnostics (class)

收集校验错误，而不是在首个问题处抛异常。

- List<ArpgDiagnostic> items;

- int errorCount;

- int warningCount;

- ArpgDiagnostics()

- void AddError(string code, string path, string message)

- void AddWarning(string code, string path, string message)

- bool HasErrors()

- int ErrorCount()

- int WarningCount()

- int Count()

- ArpgDiagnostic At(int index)

- void Append(ArpgDiagnostics other)


## ArpgEngine (class)

类型化 Arpg 组件引擎的 SDL3 应用宿主。

- ArpgConfig config;

- ArpgProject project;

- ArpgRegistry registry;

- ArpgEvents events;

- ArpgGameplayEvents gameplayEvents;

- ArpgScheduler scheduler;

- ArpgWorld world;

- ArpgUiRuntime ui;

- ArpgUiRenderer uiRenderer;

- ArpgDiagnostics diagnostics;

- SdlWindow window;

- SdlRenderer renderer;

- bool running;

- int lastTick;

- ArpgEngine(ArpgConfig config)

- ArpgConfig Config()

- ArpgProject Project()

- ArpgRegistry Registry()

- ArpgEvents Events()

- ArpgGameplayEvents GameplayEvents()

- ArpgScheduler Scheduler()

- ArpgWorld World()

- ArpgUiRuntime Ui()

- ArpgUiRenderer UiRenderer()

- ArpgDiagnostics Diagnostics()

- SdlRenderer Renderer()

- bool IsRunning()

- bool LoadProject(ArpgProject project)

- ArpgDiagnostics Validate()

- bool Start(string arguments)

- void PumpEvents()

- int Update()

- void Render()

- bool Run(string arguments)

- void Stop()


## ArpgEquipSlot (class)

- string slot;

- ArpgItemDefinition item;

- ArpgEquipSlot(string slot, ArpgItemDefinition item)


## ArpgEquipment (class)

- List<ArpgEquipSlot> slots;

- ArpgEquipment()

- int IndexOf(string slot)

- ArpgItemDefinition Get(string slot)

- bool Equip(string slot, ArpgItemDefinition definition, ArpgInventory inventory)

- bool Unequip(string slot, ArpgInventory inventory)

- int Count()

- string SlotNameAt(int index)

- ArpgItemDefinition ItemAt(int index)

- ArpgAttributes Attributes()


## ArpgEvents (class)

对应 DM3 App.lua 事件的类型化系统事件接口。

- ArpgStarting starting;

- ArpgStarted started;

- ArpgClosing closing;

- ArpgFocusChanged focusChanged;

- ArpgWindowStateChanged windowStateChanged;

- ArpgSizeChanged sizeChanged;

- ArpgKeyChanged keyDown;

- ArpgKeyChanged keyUp;

- ArpgMenuChanged menuChanged;

- ArpgSystemPrompt systemPrompt;

- ArpgRichTextLinkChanged richTextLink;

- void OnStarting(ArpgStarting handler)

- void OnStarted(ArpgStarted handler)

- void OnClosing(ArpgClosing handler)

- void OnFocusChanged(ArpgFocusChanged handler)

- void OnWindowStateChanged(ArpgWindowStateChanged handler)

- void OnSizeChanged(ArpgSizeChanged handler)

- void OnKeyDown(ArpgKeyChanged handler)

- void OnKeyUp(ArpgKeyChanged handler)

- void OnMenuChanged(ArpgMenuChanged handler)

- void OnSystemPrompt(ArpgSystemPrompt handler)

- void OnRichTextLink(ArpgRichTextLinkChanged handler)

- bool RaiseStarting(string arguments)

- void RaiseStarted()

- bool RaiseClosing()

- void RaiseFocusChanged(bool focused)

- void RaiseWindowStateChanged(int state)

- void RaiseSizeChanged(int width, int height)

- void RaiseKeyDown(int keycode, bool alt, bool shift, bool control)

- void RaiseKeyUp(int keycode, bool alt, bool shift, bool control)

- void RaiseMenuChanged(string menuName, string itemName, string payload)

- bool RaiseSystemPrompt(int code, string message)

- void RaiseRichTextLink(string windowId, string controlId, string link)


## ArpgFaction (class)

- static string Friendly()

- static string Neutral()

- static string Hostile()

- static string Any()


## ArpgFormula (class)

- string text;

- int index;

- bool valid;

- ArpgAttributes leftAttrs;

- ArpgAttributes rightAttrs;

- ArpgFormula(string text, ArpgAttributes leftAttrs, ArpgAttributes rightAttrs)

- bool IsValid()

- int Current()

- void SkipSpace()

- bool Match(int expected)

- int ParseNumber()

- bool IsNameChar(int c)

- string ParseName()

- int Attribute(string name)

- int ParsePrimary()

- int ParseUnary()

- int Power(int baseValue, int exponent)

- int ParsePower()

- int ParseMulDiv()

- int ParseAddSub()

- int Evaluate()


## ArpgGameplayEvents (class)

类型化运行时发出的游戏玩法事件。

- ArpgItemChanged itemChanged;

- ArpgItemUsed itemUsed;

- ArpgSkillResolved skillResolved;

- ArpgBuffChanged buffChanged;

- ArpgActorDamaged actorDamaged;

- ArpgActorDied actorDied;

- ArpgMapChanged mapChanged;

- void OnItemChanged(ArpgItemChanged handler)

- void OnItemUsed(ArpgItemUsed handler)

- void OnSkillResolved(ArpgSkillResolved handler)

- void OnBuffChanged(ArpgBuffChanged handler)

- void OnActorDamaged(ArpgActorDamaged handler)

- void OnActorDied(ArpgActorDied handler)

- void OnMapChanged(ArpgMapChanged handler)

- void RaiseItemChanged(ArpgActor actor, ArpgItemDefinition item, int quantity)

- void RaiseItemUsed(ArpgActor actor, ArpgItemDefinition item)

- void RaiseSkillResolved(ArpgSkillCastResult result)

- void RaiseBuffChanged(ArpgActor actor, ArpgBuffInstance buff, bool added)

- void RaiseActorDamaged(ArpgActor source, ArpgActor target, double amount, bool critical)

- void RaiseActorDied(ArpgActor actor, ArpgActor killer)

- void RaiseMapChanged(string previousMapId, string currentMapId, int portalId)


## ArpgGlobals (class)

- List<GlobalEntry> entries;

- ArpgGlobals()

- int IndexOf(string key)

- ArpgGlobals Put(string key, string content, int kind)

- ArpgGlobals Set(string key, string content)

- ArpgGlobals SetInt(string key, int amount)

- ArpgGlobals SetBool(string key, bool enabled)

- bool Contains(string key)

- int Count()

- string KeyAt(int index)

- string ValueAt(int index)

- int KindAt(int index)

- string Get(string key, string fallback)

- int GetInt(string key, int fallback)

- bool GetBool(string key, bool fallback)

- bool Remove(string key)

- void Clear()


## ArpgGrowthDefinition (class)

- string id;

- List<ArpgGrowthLevel> levels;

- ArpgGrowthDefinition(string id)

- string Id()

- void AddLevel(int level, ArpgAttributes attributes)

- int LevelCount()

- ArpgGrowthLevel LevelAt(int index)

- ArpgAttributes AttributesFor(int level)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgGrowthLevel (class)

- int level;

- ArpgAttributes attributes;

- ArpgGrowthLevel(int level, ArpgAttributes attributes)

- int Level()

- ArpgAttributes Attributes()


## ArpgInventory (class)

角色与背包控件使用的可堆叠格子背包。

- int capacity;

- List<ArpgItemInstance> slots;

- ArpgInventory(int capacity)

- int Capacity()

- int SlotCount()

- ArpgItemInstance SlotAt(int index)

- bool IsFull()

- int QuantityOf(string itemId)

- ArpgItemInstance Find(string itemId)

- int Add(ArpgItemDefinition definition, int count)

- bool Remove(string itemId, int count)


## ArpgItemCategory (class)

- static string Item()

- static string Equipment()

- static string Special()


## ArpgItemDefinition (class)

- string id;

- string displayName;

- string iconResource;

- string tooltip;

- string category;

- string subcategory;

- string detailCategory;

- string cooldownGroup;

- int cooldownMilliseconds;

- int maxStack;

- bool discardDisabled;

- int disappearMilliseconds;

- int requiredLevel;

- int restoreHp;

- int restoreMp;

- double restoreHpPercent;

- double restoreMpPercent;

- ArpgAttributes attributes;

- ArpgItemDefinition(string id, string displayName)

- string Id()

- string DisplayName()

- string IconResource()

- string Tooltip()

- string Category()

- string Subcategory()

- string DetailCategory()

- string CooldownGroup()

- int CooldownMilliseconds()

- int MaxStack()

- bool DiscardDisabled()

- int DisappearMilliseconds()

- int RequiredLevel()

- int RestoreHp()

- int RestoreMp()

- double RestoreHpPercent()

- double RestoreMpPercent()

- ArpgAttributes Attributes()

- void SetIconResource(string newValue)

- void SetTooltip(string newValue)

- void SetCategory(string newValue)

- void SetSubcategory(string newValue)

- void SetDetailCategory(string newValue)

- void SetCooldown(string group, int milliseconds)

- void SetMaxStack(int newValue)

- void SetDiscardDisabled(bool newValue)

- void SetDisappearMilliseconds(int newValue)

- void SetRequiredLevel(int newValue)

- void SetRestore(int hp, int mp, double hpPercent, double mpPercent)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgItemInstance (class)

- ArpgItemDefinition definition;

- int quantity;

- ArpgItemInstance(ArpgItemDefinition definition, int quantity)

- ArpgItemDefinition Definition()

- int Quantity()

- bool IsEmpty()

- int Add(int count)

- int Remove(int count)


## ArpgMapCell (class)

- int x;

- int y;

- int cellType;

- ArpgMapCell(int x, int y, int cellType)

- int X()

- int Y()

- int CellType()

- bool IsBlocked()

- bool IsTransparent()


## ArpgMapDefinition (class)

含出生点、阻挡与传送门的网格地图定义。

- string id;

- int cellWidth;

- int cellHeight;

- int columns;

- int rows;

- bool defaultMap;

- int initialGridX;

- int initialGridY;

- int initialRange;

- string templateResource;

- string miniMapResource;

- ArpgColor ambientColor;

- bool allowDropsOnBlockedCells;

- List<ArpgMapCell> cells;

- List<ArpgMapSpawn> spawns;

- List<ArpgMapPortal> portals;

- ArpgMapDefinition(string id, int columns, int rows)

- string Id()

- int CellWidth()

- int CellHeight()

- int Columns()

- int Rows()

- bool DefaultMap()

- int InitialGridX()

- int InitialGridY()

- int InitialRange()

- string TemplateResource()

- string MiniMapResource()

- ArpgColor AmbientColor()

- void SetCellSize(int width, int height)

- void SetDefaultMap(bool newValue)

- void SetInitialPosition(int x, int y, int range)

- void SetTemplateResource(string newValue)

- void SetMiniMapResource(string newValue)

- void SetAmbientColor(ArpgColor newValue)

- void SetAllowDropsOnBlockedCells(bool newValue)

- void AddCell(ArpgMapCell newValue)

- void AddSpawn(ArpgMapSpawn newValue)

- void AddPortal(ArpgMapPortal newValue)

- int CellCount()

- ArpgMapCell CellAt(int index)

- int SpawnCount()

- ArpgMapSpawn SpawnAt(int index)

- int PortalCount()

- ArpgMapPortal PortalAt(int index)

- bool IsInside(int x, int y)

- bool IsBlocked(int x, int y)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgMapPortal (class)

- int gridX;

- int gridY;

- string targetMapId;

- int targetGridX;

- int targetGridY;

- int targetDirection;

- bool clearTargetMap;

- int id;

- ArpgMapPortal(int gridX, int gridY, string targetMapId, int targetGridX, int targetGridY)

- int GridX()

- int GridY()

- string TargetMapId()

- int TargetGridX()

- int TargetGridY()

- int TargetDirection()

- bool ClearTargetMap()

- int Id()

- void SetTargetDirection(int newValue)

- void SetClearTargetMap(bool newValue)

- void SetId(int newValue)


## ArpgMapSpawn (class)

- string actorId;

- string faction;

- int gridX;

- int gridY;

- int range;

- int count;

- int level;

- int direction;

- int respawnMilliseconds;

- ArpgMapSpawn(string actorId, string faction, int gridX, int gridY, int count)

- string ActorId()

- string Faction()

- int GridX()

- int GridY()

- int Range()

- int Count()

- int Level()

- int Direction()

- int RespawnMilliseconds()

- void SetRange(int newValue)

- void SetLevel(int newValue)

- void SetDirection(int newValue)

- void SetRespawnMilliseconds(int newValue)


## ArpgMath (class)

- static int Clamp(int newValue, int minimum, int maximum)

- static double ClampDouble(double newValue, double minimum, double maximum)

- static int Max(int left, int right)


## ArpgMenu (class)

- string name;

- List<ArpgMenuItem> items;

- ArpgMenu(string name)

- string Name()

- ArpgMenu AddItem(ArpgMenuItem item)

- int IndexOf(string itemName)

- ArpgMenuItem FindItem(string itemName)

- int ItemCount()

- ArpgMenuItem ItemAt(int index)

- void Dispose()


## ArpgMenuItem (class)

- string name;

- string title;

- string payload;

- bool visible;

- bool enabled;

- bool marked;

- ArpgMenuItem(string name, string title, string payload)

- string Name()

- string Title()

- string Payload()

- bool Visible()

- bool Enabled()

- bool Marked()

- ArpgMenuItem SetTitle(string title)

- ArpgMenuItem SetPayload(string payload)

- ArpgMenuItem SetVisible(bool visible)

- ArpgMenuItem SetEnabled(bool enabled)

- ArpgMenuItem SetMarked(bool marked)


## ArpgMenuRuntime (class)

- List<ArpgMenu> menus;

- ArpgEvents events;

- ArpgMenuRuntime(ArpgEvents events)

- int IndexOf(string name)

- ArpgMenuRuntime AddMenu(ArpgMenu menu)

- ArpgMenu FindMenu(string name)

- int MenuCount()

- ArpgMenu MenuAt(int index)

- bool Activate(string menuName, string itemName)

- void Dispose()


## ArpgMessage (class)

- int cid;

- string body;

- int bodyLen;

- static ArpgMessage Of(int cid, string body, int bodyLen)

- static ArpgMessage Text(int cid, string body)

- int Cid()

- string Body()

- int BodyLength()

- int FrameLength()

- byte[]Encode()

- static ArpgMessage Decode(string buf, int len)


## ArpgMusic (class)

- string resource;

- double volume;

- double pitch;

- int channel;

- int length;

- bool loop;

- MusicState state;

- ArpgMusic(string resource)

- ArpgMusic SetVolume(double volume)

- ArpgMusic SetPitch(double pitch)

- ArpgMusic SetChannel(int channel)

- ArpgMusic SetLoop(bool loop)

- void Play()

- void Pause()

- void Stop()

- string Resource()

- double Volume()

- double Pitch()

- int Channel()

- int Length()

- bool Loop()

- MusicState State()

- void Dispose()


## ArpgNetDefaults (class)

- static int MaxMessageLength()


## ArpgNineSlice (class)

- string topLeft;

- string top;

- string topRight;

- string left;

- string center;

- string right;

- string bottomLeft;

- string bottom;

- string bottomRight;

- int leftWidth;

- int topHeight;

- int rightWidth;

- int bottomHeight;

- ArpgNineSlice(string topLeft, string top, string topRight, string left, string center, string right, string bottomLeft, string bottom, string bottomRight)

- string TopLeft()

- string Top()

- string TopRight()

- string Left()

- string Center()

- string Right()

- string BottomLeft()

- string Bottom()

- string BottomRight()

- int LeftWidth()

- int TopHeight()

- int RightWidth()

- int BottomHeight()

- void SetInsets(int left, int top, int right, int bottom)


## ArpgNode (class)

- ArpgNodeDefinition definition;

- ArpgDataSource dataSource;

- string evaluatedContent;

- ArpgRichTextDocument document;

- bool focused;

- bool inheritedDataSource;

- bool playing;

- int frameIndex;

- int frameElapsed;

- ArpgNode(ArpgNodeDefinition definition, ArpgDataSource defaultSource)

- string Id()

- string Kind()

- int X()

- int Y()

- int Width()

- int Height()

- int Order()

- bool Visible()

- bool MouseEvents()

- double AnchorX()

- double AnchorY()

- double ScaleX()

- double ScaleY()

- int Angle()

- double Opacity()

- bool Clip()

- string Content()

- string Resource()

- ArpgColor Color()

- ArpgColor BackgroundColor()

- ArpgColor BorderColor()

- ArpgColor OutlineColor()

- int BorderWidth()

- int Radius()

- int FontSize()

- int HorizontalAlign()

- int VerticalAlign()

- bool Wrap()

- int LineSpacing()

- ArpgNineSlice NineSlice()

- bool Playing()

- int FrameIndex()

- ArpgRichTextDocument Document()

- ArpgNodeEventHandlers Events()

- ArpgDataSource DataSource()

- bool UsesInheritedDataSource()

- void SetDataSource(ArpgDataSource newValue)

- void SetInheritedDataSource(ArpgDataSource newValue)

- void Evaluate()

- void Update(int deltaMilliseconds)

- void Play()

- void Pause()

- void ResetAnimation()

- bool HitTest(double localX, double localY)

- void SetFocused(bool newValue)

- void FireCustom(string name, string payload)


## ArpgNodeDefinition (class)

- string id;

- string kind;

- int x;

- int y;

- int width;

- int height;

- int order;

- bool visible;

- bool mouseEvents;

- double anchorX;

- double anchorY;

- double scaleX;

- double scaleY;

- int angle;

- double opacity;

- bool clip;

- bool richText;

- string content;

- string resource;

- ArpgColor color;

- ArpgColor backgroundColor;

- ArpgColor borderColor;

- ArpgColor outlineColor;

- int borderWidth;

- int radius;

- int fontSize;

- int horizontalAlign;

- int verticalAlign;

- bool wrap;

- int lineSpacing;

- int frameRate;

- bool loopAnimation;

- bool autoPlay;

- List<string> animationFrames;

- ArpgNineSlice nineSlice;

- ArpgDataSource dataSource;

- ArpgNodeEventHandlers events;

- ArpgNodeDefinition(string id, string kind, int x, int y, int width, int height)

- string Id()

- string Kind()

- int X()

- int Y()

- int Width()

- int Height()

- int Order()

- bool Visible()

- bool MouseEvents()

- double AnchorX()

- double AnchorY()

- double ScaleX()

- double ScaleY()

- int Angle()

- double Opacity()

- bool Clip()

- bool RichText()

- string Content()

- string Resource()

- ArpgColor Color()

- ArpgColor BackgroundColor()

- ArpgColor BorderColor()

- ArpgColor OutlineColor()

- int BorderWidth()

- int Radius()

- int FontSize()

- int HorizontalAlign()

- int VerticalAlign()

- bool Wrap()

- int LineSpacing()

- int FrameRate()

- bool LoopAnimation()

- bool AutoPlay()

- int AnimationFrameCount()

- string AnimationFrameAt(int index)

- ArpgNineSlice NineSlice()

- ArpgDataSource DataSource()

- ArpgNodeEventHandlers Events()

- void SetOrder(int newValue)

- void SetVisible(bool newValue)

- void SetMouseEvents(bool newValue)

- void SetAnchor(double x, double y)

- void SetScale(double x, double y)

- void SetAngle(int newValue)

- void SetOpacity(double newValue)

- void SetClip(bool newValue)

- void SetContent(string newValue, bool richText)

- void SetResource(string newValue)

- void SetColor(ArpgColor newValue)

- void SetBackgroundColor(ArpgColor newValue)

- void SetBorder(int width, ArpgColor color)

- void SetOutlineColor(ArpgColor newValue)

- void SetRadius(int newValue)

- void SetTextStyle(int fontSize, int horizontalAlign, int verticalAlign, bool wrap, int lineSpacing)

- void SetAnimation(int frameRate, bool loop, bool play)

- void AddAnimationFrame(string resource)

- void SetNineSlice(ArpgNineSlice newValue)

- void SetDataSource(ArpgDataSource newValue)

- void Validate(ArpgDiagnostics diagnostics, string parentPath)


## ArpgNodeEventHandlers (class)

- ArpgNodeCreated created;

- ArpgNodeUpdated updated;

- ArpgNodePointerChanged pointerDown;

- ArpgNodePointerChanged pointerUp;

- ArpgNodePointerChanged pointerMove;

- ArpgNodeFocusChanged focusChanged;

- ArpgNodeCustomEvent customEvent;

- void OnCreated(ArpgNodeCreated handler)

- void OnUpdated(ArpgNodeUpdated handler)

- void OnPointerDown(ArpgNodePointerChanged handler)

- void OnPointerUp(ArpgNodePointerChanged handler)

- void OnPointerMove(ArpgNodePointerChanged handler)

- void OnFocusChanged(ArpgNodeFocusChanged handler)

- void OnCustomEvent(ArpgNodeCustomEvent handler)

- void RaiseCreated(string nodeId)

- void RaiseUpdated(string nodeId, int deltaMilliseconds)

- bool RaisePointerDown(string nodeId, int button, int modifiers, double x, double y)

- bool RaisePointerUp(string nodeId, int button, int modifiers, double x, double y)

- bool RaisePointerMove(string nodeId, int button, int modifiers, double x, double y)

- void RaiseFocusChanged(string nodeId, bool focused)

- void RaiseCustom(string nodeId, string name, string payload)


## ArpgNodeKind (class)

- static string Text()

- static string ArtText()

- static string Sprite()

- static string Rectangle()

- static string Circle()

- static string Animation()

- static bool IsValid(string kind)


## ArpgPixelFont (class)

- static int[]rows;

- static bool ready;

- static int CellW()

- static int CellH()

- static int Advance()

- static string Data()

- static int HexVal(int ch)

- static void Ensure()

- static int Row(int codepoint, int row)

- static int Measure(string text, int scale)


## ArpgPoint (class)

- double x;

- double y;

- ArpgPoint(double x, double y)

- double X()

- double Y()

- void Set(double x, double y)


## ArpgPointerButton (class)

- static int Left()

- static int Right()

- static int Middle()

- static int FromSdl(int button)


## ArpgPrefabDefinition (class)

- string id;

- int width;

- int height;

- double anchorX;

- double anchorY;

- ArpgColor backgroundColor;

- int borderWidth;

- ArpgColor borderColor;

- double scaleX;

- double scaleY;

- int order;

- int angle;

- double opacity;

- string backgroundResource;

- bool autoClip;

- bool mouseEvents;

- List<ArpgPrefabProperty> properties;

- List<ArpgNodeDefinition> nodes;

- ArpgControlEventHandlers events;

- ArpgPrefabDefinition(string id, int width, int height)

- string Id()

- int Width()

- int Height()

- double AnchorX()

- double AnchorY()

- ArpgColor BackgroundColor()

- int BorderWidth()

- ArpgColor BorderColor()

- double ScaleX()

- double ScaleY()

- int Order()

- int Angle()

- double Opacity()

- string BackgroundResource()

- bool AutoClip()

- bool MouseEvents()

- int NodeCount()

- ArpgNodeDefinition NodeAt(int index)

- ArpgControlEventHandlers Events()

- void SetAnchor(double x, double y)

- void SetBackgroundColor(ArpgColor newValue)

- void SetBorder(int width, ArpgColor color)

- void SetScale(double x, double y)

- void SetOrder(int newValue)

- void SetAngle(int newValue)

- void SetOpacity(double newValue)

- void SetBackgroundResource(string newValue)

- void SetAutoClip(bool newValue)

- void SetMouseEvents(bool newValue)

- void AddProperty(string name, string defaultValue)

- int PropertyCount()

- ArpgPrefabProperty PropertyAt(int index)

- void AddNode(ArpgNodeDefinition node)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgPrefabProperty (class)

- string name;

- string defaultValue;

- ArpgPrefabProperty(string name, string defaultValue)

- string Name()

- string DefaultValue()


## ArpgProject (class)

内存组件数据库。与文件格式无关，因此工具
可从 JSON、Zan 源码、编辑器或其他管线生成它。

- ArpgRegistry registry;

- List<ArpgMapDefinition> maps;

- List<ArpgActorDefinition> actors;

- List<ArpgItemDefinition> items;

- List<ArpgSkillDefinition> skills;

- List<ArpgBuffDefinition> buffs;

- List<ArpgWindowDefinition> windows;

- List<ArpgGrowthDefinition> growth;

- List<ArpgPrefabDefinition> prefabs;

- List<SqliteComponentConfig> databases;

- ArpgProject()

- ArpgRegistry Registry()

- void AddMap(ArpgMapDefinition newValue)

- void AddActor(ArpgActorDefinition newValue)

- void AddItem(ArpgItemDefinition newValue)

- void AddSkill(ArpgSkillDefinition newValue)

- void AddBuff(ArpgBuffDefinition newValue)

- void AddWindow(ArpgWindowDefinition newValue)

- void AddGrowth(ArpgGrowthDefinition newValue)

- void AddPrefab(ArpgPrefabDefinition newValue)

- void AddDatabase(SqliteComponentConfig newValue)

- int MapCount()

- int ActorCount()

- int ItemCount()

- int SkillCount()

- int BuffCount()

- int WindowCount()

- int GrowthCount()

- int PrefabCount()

- ArpgMapDefinition MapAt(int index)

- ArpgActorDefinition ActorAt(int index)

- ArpgItemDefinition ItemAt(int index)

- ArpgSkillDefinition SkillAt(int index)

- ArpgBuffDefinition BuffAt(int index)

- ArpgWindowDefinition WindowAt(int index)

- ArpgGrowthDefinition GrowthAt(int index)

- ArpgPrefabDefinition PrefabAt(int index)

- int DatabaseCount()

- SqliteComponentConfig DatabaseAt(int index)

- ArpgMapDefinition FindMap(string id)

- ArpgActorDefinition FindActor(string id)

- ArpgItemDefinition FindItem(string id)

- ArpgSkillDefinition FindSkill(string id)

- ArpgBuffDefinition FindBuff(string id)

- ArpgWindowDefinition FindWindow(string id)

- ArpgGrowthDefinition FindGrowth(string id)

- ArpgPrefabDefinition FindPrefab(string id)

- SqliteComponentConfig FindDatabase(string name)

- ArpgMapDefinition DefaultMap()

- ArpgActorDefinition DefaultPlayer()

- void ValidateResource(ArpgDiagnostics diagnostics, string resourceId, string path)

- ArpgDiagnostics Validate()

- void ValidateMaps(ArpgDiagnostics diagnostics)
  - 地图定义：逐图校验、资源、重复 id。

- void ValidateActors(ArpgDiagnostics diagnostics)
  - 角色定义：校验、重复 id、成长/技能/增益引用。

- void ValidateItems(ArpgDiagnostics diagnostics)
  - 物品定义：校验、图标资源、重复 id。

- void ValidateSkills(ArpgDiagnostics diagnostics)
  - 技能定义：校验、图标、增益效果引用、重复 id。

- void ValidateBuffs(ArpgDiagnostics diagnostics)
  - 增益定义：校验、图标、重复 id。

- void ValidateWindows(ArpgDiagnostics diagnostics)
  - 窗口定义：校验、背景、重复 id。

- void ValidateGrowth(ArpgDiagnostics diagnostics)
  - 成长定义：校验、重复 id。

- void ValidatePrefabs(ArpgDiagnostics diagnostics)
  - 预制体定义：校验、背景、重复 id。

- void ValidateMapPortals(ArpgDiagnostics diagnostics)
  - 地图传送门/出生点及单一默认地图规则。

- void ValidateDefaultPlayer(ArpgDiagnostics diagnostics)
  - 单一默认玩家规则。


## ArpgProperty (class)

- string key;

- ArpgValue propertyValue;

- ArpgProperty(string key, ArpgValue newValue)


## ArpgRandom (class)

适合回放与测试的小型确定性随机数生成器。

- int state;

- ArpgRandom(int seed)

- int Next()

- int NextPercent()


## ArpgRect (class)

- double x;

- double y;

- double width;

- double height;

- ArpgRect(double x, double y, double width, double height)

- double X()

- double Y()

- double Width()

- double Height()


## ArpgRegistry (class)

- List<ArpgResource> resources;

- List<ArpgComponentFile> components;

- List<string> scriptFiles;

- ArpgRegistry()

- void AddResource(string id, string file)

- void AddComponent(string path)

- void AddComponentFile(string kind, string id, string path)

- void AddScript(string path)

- int ResourceCount()

- int ComponentCount()

- int ScriptCount()

- ArpgResource ResourceAt(int index)

- string ComponentAt(int index)

- ArpgComponentFile ComponentFileAt(int index)

- string ScriptAt(int index)

- bool HasResource(string id)

- bool HasComponent(string kind, string id)

- ArpgDiagnostics Validate()


## ArpgResource (class)

- string id;

- string file;

- ArpgResource(string id, string file)

- string Id()

- string File()


## ArpgRichText (class)

- static ArpgRichTextDocument Parse(string content, ArpgDataSource source)


## ArpgRichTextDocument (class)

- string sourceText;

- List<ArpgRichTextRun> runs;

- ArpgRichTextDocument(string sourceText)

- string SourceText()

- int RunCount()

- ArpgRichTextRun RunAt(int index)

- void Add(ArpgRichTextRun run)


## ArpgRichTextLayout (class)

- List<ArpgRichTextLayoutItem> items;

- int width;

- int height;

- int lineCount;

- ArpgRichTextLayout()

- int ItemCount()

- ArpgRichTextLayoutItem ItemAt(int index)

- int Width()

- int Height()

- int LineCount()

- void Add(ArpgRichTextLayoutItem item)

- void SetSize(int width, int height, int lineCount)


## ArpgRichTextLayoutItem (class)

- int runIndex;

- int line;

- string text;

- int x;

- int y;

- int width;

- int height;

- ArpgRichTextLayoutItem(int runIndex, int line, string text, int x, int y, int width, int height)

- int RunIndex()

- int Line()

- string Text()

- int X()

- int Y()

- int Width()

- int Height()

- void OffsetX(int offset)


## ArpgRichTextLayouter (class)

- static ArpgRichTextLayout Layout(ArpgRichTextDocument document, int availableWidth, int fontSize, int lineSpacing, bool wrap, int horizontalAlign)


## ArpgRichTextLink (class)

- string raw;

- int style;

- ArpgColor normalColor;

- ArpgColor hoverColor;

- ArpgColor pressedColor;

- ArpgRichTextLink(string marker)

- string Raw()

- int Count()

- string At(int index)

- int Style()

- ArpgColor NormalColor()

- ArpgColor HoverColor()

- ArpgColor PressedColor()


## ArpgRichTextParser (class)

- string input;

- int position;

- ArpgRichTextDocument document;

- ArpgRichTextStyle style;

- ArpgRichTextLink link;

- ArpgRichTextParser(string input, ArpgRichTextStyle style, ArpgRichTextLink link)

- static int FindIn(string text, string token, int start)

- static int ArgCount(string text)

- static string ArgAt(string text, int index)

- static int HexDigit(string digit)

- static int ParseInt(string text)

- static ArpgColor ParseColor(string text)

- static ArpgColor ParseColorArgs(string args)

- bool Starts(string token)

- string Parenthesized(int prefixLength)

- void AddText(string text)

- void AddSimple(int kind)

- void AddNested(string text, ArpgRichTextLink nestedLink)

- bool ParseColorShortcut()

- bool ParseTag()

- ArpgRichTextDocument Parse()


## ArpgRichTextRun (class)

- int kind;

- string text;

- string resource;

- string action;

- int quantity;

- int offsetX;

- int offsetY;

- int width;

- int height;

- double scale;

- ArpgRichTextStyle style;

- ArpgRichTextLink link;

- ArpgRichTextRun(int kind, ArpgRichTextStyle style, ArpgRichTextLink link)

- static ArpgRichTextRun CreateText(string text)

- int Kind()

- string Text()

- string Resource()

- string Action()

- int Quantity()

- int OffsetX()

- int OffsetY()

- int Width()

- int Height()

- double Scale()

- ArpgRichTextStyle Style()

- ArpgRichTextLink Link()

- bool IsLink()

- void SetText(string newValue)

- void SetResource(string newValue)

- void SetAction(string newValue)

- void SetQuantity(int newValue)

- void SetOffset(int x, int y)

- void SetSize(int width, int height)

- void SetScale(double newValue)


## ArpgRichTextRunKind (class)

- static int Text()

- static int Image()

- static int Animation()

- static int Spacer()

- static int Item()

- static int LineBreak()

- static int WrapWidth()


## ArpgRichTextStyle (class)

- ArpgColor color;

- ArpgColor background;

- string font;

- int alignment;

- ArpgRichTextStyle(ArpgColor color, ArpgColor background, string font, int alignment)

- static ArpgRichTextStyle Default()

- ArpgRichTextStyle Clone()

- ArpgColor Color()

- ArpgColor Background()

- string Font()

- int Alignment()

- void SetColor(ArpgColor newValue)

- void SetBackground(ArpgColor newValue)

- void SetFont(string newValue)

- void SetAlignment(int newValue)


## ArpgScheduler (class)

与文档所述 "join event" 行为一致的具名一次性/循环事件
。载荷特意序列化，以保持调度器的类型安全。

- List<ArpgTimerTask> tasks;

- ArpgScheduler()

- void Add(string name, string payload, int delayMilliseconds, bool autoRemove, ArpgTimedEvent handler)

- bool Remove(string name)

- bool Contains(string name)

- int Count()

- int IndexOf(string name)

- int Tick(int deltaMilliseconds)


## ArpgScreenMode (class)

- static int Fixed()

- static int Scale()

- static int ResizeWorld()

- static int TransparentBorderless()

- static bool IsValid(int mode)


## ArpgServerClient (class)

- ServerConfig config;

- int id;

- nint sock;

- bool connected;

- byte[]recvBuf;

- ArpgServerEvents events;

- static ArpgServerClient FromSocket(ServerConfig config, int id, nint sock, ArpgServerEvents events)

- int Id()

- nint Handle()

- bool IsConnected()

- async int SendMessageAsync(ArpgMessage message)

- async ArpgMessage ReceiveMessageAsync()

- void Close()


## ArpgServerEvents (class)

- ArpgServerStarting starting;

- ArpgServerStarted started;

- ArpgServerStopping stopping;

- ArpgServerStopped stopped;

- ArpgServerClientChanged clientConnected;

- ArpgServerClientChanged clientDisconnected;

- ArpgServerMessageReceived messageReceived;

- void OnStarting(ArpgServerStarting handler)

- void OnStarted(ArpgServerStarted handler)

- void OnStopping(ArpgServerStopping handler)

- void OnStopped(ArpgServerStopped handler)

- void OnClientConnected(ArpgServerClientChanged handler)

- void OnClientDisconnected(ArpgServerClientChanged handler)

- void OnMessageReceived(ArpgServerMessageReceived handler)

- bool RaiseStarting(string bindHost)

- void RaiseStarted(ServerConfig config, string bindHost)

- bool RaiseStopping()

- void RaiseStopped(ServerConfig config)

- void RaiseClientConnected(ArpgServerClient client)

- void RaiseClientDisconnected(ArpgServerClient client)

- void RaiseMessageReceived(ArpgServerClient client, ArpgMessage message)


## ArpgSkillCastResult (class)

- int status;

- ArpgActor caster;

- ArpgActor target;

- ArpgSkillDefinition skill;

- bool hit;

- bool critical;

- double damage;

- ArpgSkillCastResult(int status, ArpgActor caster, ArpgActor target, ArpgSkillDefinition skill)

- int Status()

- bool Success()

- ArpgActor Caster()

- ArpgActor Target()

- ArpgSkillDefinition Skill()

- bool Hit()

- bool Critical()

- double Damage()

- void SetResolved(bool hit, bool critical, double damage)


## ArpgSkillCastStatus (class)

- static int Success()

- static int UnknownSkill()

- static int NotLearned()

- static int CasterDead()

- static int Cooldown()

- static int InsufficientResources()

- static int OutOfRange()

- static int InvalidTarget()


## ArpgSkillDefinition (class)

- string id;

- string displayName;

- string iconResource;

- string tooltip;

- int level;

- double damageFactor;

- string targetFaction;

- int attackRange;

- int minimumRange;

- string action;

- int hpCost;

- int mpCost;

- int rageCost;

- int threat;

- int cooldownMilliseconds;

- int targetMode;

- double priority;

- int damageDelayMilliseconds;

- bool autoEndPrevious;

- bool alwaysHit;

- bool alwaysCritical;

- int startingDirectionMode;

- string damageFormula;

- List<string> buffEffects;

- ArpgSkillDefinition(string id, string displayName)

- string Id()

- string DisplayName()

- string IconResource()

- string Tooltip()

- int Level()

- double DamageFactor()

- string TargetFaction()

- int AttackRange()

- int MinimumRange()

- string Action()

- int HpCost()

- int MpCost()

- int RageCost()

- int Threat()

- int CooldownMilliseconds()

- int TargetMode()

- double Priority()

- int DamageDelayMilliseconds()

- bool AutoEndPrevious()

- bool AlwaysHit()

- bool AlwaysCritical()

- int StartingDirectionMode()

- string DamageFormula()

- int BuffEffectCount()

- string BuffEffectAt(int index)

- void SetIconResource(string newValue)

- void SetTooltip(string newValue)

- void SetLevel(int newValue)

- void SetDamageFactor(double newValue)

- void SetTargetFaction(string newValue)

- void SetRange(int minimum, int maximum)

- void SetAction(string newValue)

- void SetCosts(int hp, int mp, int rage)

- void SetThreat(int newValue)

- void SetCooldownMilliseconds(int newValue)

- void SetTargetMode(int newValue)

- void SetPriority(double newValue)

- void SetDamageDelayMilliseconds(int newValue)

- void SetAutoEndPrevious(bool newValue)

- void SetAlwaysHit(bool newValue)

- void SetAlwaysCritical(bool newValue)

- void SetStartingDirectionMode(int newValue)

- void SetDamageFormula(string newValue)

- void AddBuffEffect(string buffId)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgSkillTarget (class)

- static int Actor()

- static int MouseCell()

- static int ActorThenMouseCell()

- static int Player()


## ArpgTcpClientRuntime (class)

- TcpClientConfig config;

- nint sock;

- bool connected;

- byte[]recvBuf;

- static ArpgTcpClientRuntime FromConfig(TcpClientConfig config)

- static ArpgTcpClientRuntime FromSocket(TcpClientConfig config, nint sock)

- bool IsConnected()

- nint Handle()

- async void ConnectAsync()

- async int ConnectAndWaitAsync()

- async int SendMessageAsync(ArpgMessage message)

- async ArpgMessage ReceiveMessageAsync()

- void Close()


## ArpgTcpServerRuntime (class)

- ServerConfig config;

- nint listener;

- bool running;

- int nextClientId;

- List<ArpgServerClient> clients;

- ArpgServerEvents events;

- static ArpgTcpServerRuntime Start(ServerConfig config, string bindHost)

- static ArpgTcpServerRuntime Start(ServerConfig config, string bindHost, ArpgServerEvents events)

- static async ArpgTcpServerRuntime StartAsync(ServerConfig config, string bindHost)

- async nint AcceptAsync()

- async ArpgServerClient AcceptClientAsync()

- bool IsRunning()

- int Port()

- int ClientCount()

- ArpgServerClient ClientAt(int index)

- bool RemoveClient(int id)

- void Stop()


## ArpgTemplate (class)

- static string Evaluate(string template, ArpgDataSource source)

- static string EvaluateValue(string template, ArpgValue source)


## ArpgTemplateParser (class)

- string input;

- ArpgValue source;

- int position;

- ArpgTemplateParser(string input, ArpgValue source)

- static bool StartsAt(string text, int position, string prefix)

- static string Trim(string text)

- int Find(string token, int start)

- static int FindOperator(string expression, string token)

- static bool LooksNumeric(string text)

- ArpgValue Operand(string token)

- bool Compare(string leftToken, string rightToken, string operation)

- bool EvaluateCondition(string expression)

- string ParseConditional(string condition)

- ArpgTemplateSection ParseSection()


## ArpgTemplateSection (class)

- string text;

- int terminator;

- string condition;

- ArpgTemplateSection(string text, int terminator, string condition)

- string Text()

- int Terminator()

- string Condition()


## ArpgTemplateTerminator (class)

- static int None()

- static int ElseIf()

- static int Else()

- static int End()


## ArpgTextAlign (class)

- static int Start()

- static int Center()

- static int End()


## ArpgTextMetrics (class)

- static int NextUtf8(string text, int index)

- static int GlyphWidth(string glyph, int fontSize)

- static int Measure(string text, int fontSize)


## ArpgTimerTask (class)

- string name;

- string payload;

- int interval;

- int elapsed;

- bool oneShot;

- ArpgTimedEvent handler;

- ArpgTimerTask(string name, string payload, int interval, bool oneShot, ArpgTimedEvent handler)


## ArpgTween (class)

- List<ArpgTweenStep> steps;

- ArpgTween()

- ArpgTween Add(ArpgTweenStep step)

- int StepCount()

- ArpgTweenStep StepAt(int index)

- int TotalDuration()

- void Dispose()


## ArpgTweenStep (class)

- string op;

- string target;

- List<TweenTarget> props;

- int duration;

- ArpgEasing easing;

- string callback;

- static ArpgTweenStep To(string target, int duration, ArpgEasing easing)

- static ArpgTweenStep By(string target, int duration, ArpgEasing easing)

- static ArpgTweenStep Wait(int duration)

- static ArpgTweenStep Call(string callback)

- ArpgTweenStep Set(string prop, double amount)

- string Op()

- string Target()

- int Duration()

- ArpgEasing Curve()

- string Callback()

- int PropCount()

- TweenTarget PropAt(int index)

- void Dispose()


## ArpgUiRenderer (class)

- ArpgRegistry registry;

- List<string> textureIds;

- List<SdlTexture> textures;

- ArpgTextMeasured textMeasured;

- ArpgFontHeightMeasured fontHeightMeasured;

- ArpgTextDrawn textDrawn;

- ArpgUiRenderer(ArpgRegistry registry)

- void SetTextBackend(ArpgTextMeasured measure, ArpgFontHeightMeasured fontHeight, ArpgTextDrawn draw)

- string ResourceFile(string id)

- SdlTexture Texture(SdlRenderer renderer, string id)

- ArpgColor ApplyOpacity(ArpgColor color, double opacity)

- void Fill(SdlRenderer renderer, ArpgColor color, int x, int y, int width, int height)

- void Border(SdlRenderer renderer, ArpgColor color, int borderWidth, int x, int y, int width, int height)

- void Resource(SdlRenderer renderer, string resourceId, int x, int y, int width, int height)

- void TransformedResource(SdlRenderer renderer, string resourceId, int x, int y, int width, int height, double opacity, int angle)

- void Circle(SdlRenderer renderer, ArpgColor fillColor, ArpgColor borderColor, int borderWidth, int centerX, int centerY, int radius)

- void NineSlice(SdlRenderer renderer, ArpgNineSlice slice, int x, int y, int width, int height)

- int MeasureText(string text, int fontSize)

- int FontHeight(int fontSize)

- void DrawFallbackText(SdlRenderer renderer, string text, ArpgColor color, int fontSize, int x, int y)

- void DrawText(SdlRenderer renderer, string text, ArpgColor color, int fontSize, int x, int y, int width, int height, int angle)

- void RenderDocument(SdlRenderer renderer, ArpgRichTextDocument document, ArpgColor defaultColor, int fontSize, int horizontalAlign, int verticalAlign, bool wrap, int lineSpacing, double opacity, int x, int y, int width, int height, int angle)

- void RenderNode(SdlRenderer renderer, ArpgNode node, int x, int y)

- void RenderControl(SdlRenderer renderer, ArpgControl control, int x, int y)

- void RenderWindow(SdlRenderer renderer, ArpgWindow window)

- void Render(SdlRenderer renderer, ArpgUiRuntime runtime)

- void Close()


## ArpgUiRuntime (class)

- ArpgProject project;

- ArpgWorld world;

- ArpgEvents systemEvents;

- ArpgDataSource defaultDataSource;

- List<ArpgWindow> windows;

- ArpgControl focusedControl;

- ArpgControl capturedControl;

- ArpgWindow capturedWindow;

- int capturedButton;

- ArpgUiRuntime(ArpgProject project, ArpgWorld world, ArpgEvents systemEvents)

- ArpgProject Project()

- ArpgWorld World()

- ArpgEvents SystemEvents()

- ArpgDataSource DefaultDataSource()

- int WindowCount()

- ArpgWindow WindowAt(int index)

- ArpgControl FocusedControl()

- ArpgControl CapturedControl()

- int CapturedButton()

- ArpgWindow FindWindow(string id)

- ArpgControl FindControl(string windowId, string controlId)

- void SyncDefaultDataSource()

- void Update(int deltaMilliseconds)

- ArpgWindow HitWindow(double x, double y)

- void ClearFocus(ArpgControl next)

- bool PointerDown(int button, int modifiers, double x, double y)

- bool PointerUp(int button, int modifiers, double x, double y)

- bool PointerMove(int button, int modifiers, double x, double y, double deltaX, double deltaY)


## ArpgValidation (class)

- static bool Required(ArpgDiagnostics diagnostics, string newValue, string path, string code)

- static bool Positive(ArpgDiagnostics diagnostics, int newValue, string path, string code)


## ArpgValue (class)

游戏控件与模板表达式使用的动态值树。

- int kind;

- string textValue;

- double numberValue;

- bool boolValue;

- List<ArpgProperty> properties;

- static ArpgValue Null()

- static ArpgValue Text(string text)

- static ArpgValue Number(double number)

- static ArpgValue Integer(int number)

- static ArpgValue Boolean(bool enabled)

- static ArpgValue Object()

- int Kind()

- bool IsNull()

- bool IsObject()

- string AsText()

- double AsNumber()

- bool AsBoolean()

- int IndexOf(string key)

- void Set(string key, ArpgValue newValue)

- void SetText(string key, string newValue)

- void SetNumber(string key, double newValue)

- void SetInteger(string key, int newValue)

- void SetBoolean(string key, bool newValue)

- ArpgValue Get(string key)

- int Count()

- string KeyAt(int index)

- ArpgValue ValueAt(int index)

- static int DotAt(string path)

- ArpgValue Resolve(string path)

- void SetPath(string path, ArpgValue newValue)


## ArpgValueKind (class)

- static int Null()

- static int Text()

- static int Number()

- static int Boolean()

- static int Object()


## ArpgWebSocketRuntime (class)

- WebSocketClientConfig config;

- WsUrl url;

- WebSocketClient ws;

- bool connected;

- static ArpgWebSocketRuntime FromConfig(WebSocketClientConfig config)

- WsUrl Url()

- bool IsConnected()

- async void ConnectAsync()

- async void SendText(string text)

- async string RecvText()

- async void Close()


## ArpgWindow (class)

- ArpgWindowDefinition definition;

- ArpgDataSource defaultDataSource;

- List<ArpgControl> controls;

- string evaluatedTitle;

- int x;

- int y;

- bool visible;

- bool dragging;

- ArpgWindow(ArpgWindowDefinition definition, ArpgProject project, ArpgDataSource defaultDataSource, ArpgEvents systemEvents)

- string Id()

- string Title()

- int X()

- int Y()

- int Width()

- int Height()

- int BorderWidth()

- ArpgColor BorderColor()

- ArpgColor BackgroundColor()

- string BackgroundResource()

- bool AlwaysOnTop()

- bool Movable()

- bool ClickThrough()

- bool Visible()

- ArpgWindowEventHandlers Events()

- int ControlCount()

- ArpgControl ControlAt(int index)

- ArpgControl FindControl(string id)

- void SetPosition(int x, int y)

- void Open()

- void Close()

- void Evaluate()

- void Update(int deltaMilliseconds)

- bool HitTest(double x, double y)

- ArpgControl HitControl(double x, double y)

- bool PointerDown(int button, int modifiers, double x, double y)

- bool PointerUp(ArpgControl capturedControl, int button, int modifiers, double x, double y)

- bool PointerMove(ArpgControl capturedControl, int button, int modifiers, double x, double y, double deltaX, double deltaY)

- void FireCustom(string name, string payload)


## ArpgWindowDefinition (class)

- string id;

- string title;

- int x;

- int y;

- int width;

- int height;

- int borderWidth;

- ArpgColor borderColor;

- ArpgColor backgroundColor;

- bool alwaysOnTop;

- bool movable;

- bool clickThrough;

- bool visibleByDefault;

- int maskOpacity;

- string backgroundResource;

- string skinId;

- List<ArpgControlDefinition> controls;

- ArpgWindowEventHandlers events;

- ArpgWindowDefinition(string id, int width, int height)

- string Id()

- string Title()

- int X()

- int Y()

- int Width()

- int Height()

- int BorderWidth()

- ArpgColor BorderColor()

- ArpgColor BackgroundColor()

- bool AlwaysOnTop()

- bool Movable()

- bool ClickThrough()

- bool VisibleByDefault()

- int MaskOpacity()

- string BackgroundResource()

- string SkinId()

- int ControlCount()

- ArpgControlDefinition ControlAt(int index)

- ArpgWindowEventHandlers Events()

- void SetTitle(string newValue)

- void SetPosition(int x, int y)

- void SetBorder(int width, ArpgColor color)

- void SetBackgroundColor(ArpgColor newValue)

- void SetAlwaysOnTop(bool newValue)

- void SetMovable(bool newValue)

- void SetClickThrough(bool newValue)

- void SetVisibleByDefault(bool newValue)

- void SetMaskOpacity(int newValue)

- void SetBackgroundResource(string newValue)

- void SetSkinId(string newValue)

- void AddControl(ArpgControlDefinition control)

- void Validate(ArpgDiagnostics diagnostics)


## ArpgWindowEventHandlers (class)

- ArpgWindowCreated created;

- ArpgWindowVisibilityChanged opened;

- ArpgWindowVisibilityChanged closed;

- ArpgWindowUpdated updated;

- ArpgWindowPointerChanged pointerDown;

- ArpgWindowPointerChanged pointerUp;

- ArpgWindowPointerChanged pointerMove;

- ArpgWindowCustomEvent customEvent;

- void OnCreated(ArpgWindowCreated handler)

- void OnOpened(ArpgWindowVisibilityChanged handler)

- void OnClosed(ArpgWindowVisibilityChanged handler)

- void OnUpdated(ArpgWindowUpdated handler)

- void OnPointerDown(ArpgWindowPointerChanged handler)

- void OnPointerUp(ArpgWindowPointerChanged handler)

- void OnPointerMove(ArpgWindowPointerChanged handler)

- void OnCustomEvent(ArpgWindowCustomEvent handler)

- void RaiseCreated(string windowId)

- void RaiseOpened(string windowId)

- void RaiseClosed(string windowId)

- void RaiseUpdated(string windowId, int deltaMilliseconds)

- bool RaisePointerDown(string windowId, string controlId, int button, int modifiers, double x, double y)

- bool RaisePointerUp(string windowId, string controlId, int button, int modifiers, double x, double y)

- bool RaisePointerMove(string windowId, string controlId, int button, int modifiers, double x, double y)

- void RaiseCustom(string windowId, string name, string payload)


## ArpgWorld (class)

独立于不可变组件定义的实时地图状态。

- ArpgProject project;

- ArpgMapDefinition currentMap;

- List<ArpgActor> actors;

- ArpgActor player;

- int nextInstanceId;

- ArpgEvents systemEvents;

- ArpgGameplayEvents gameplayEvents;

- ArpgRandom random;

- static ArpgWorld CreateWithEvents(ArpgProject project, ArpgEvents systemEvents, ArpgGameplayEvents gameplayEvents)

- ArpgProject Project()

- ArpgMapDefinition CurrentMap()

- ArpgActor Player()

- ArpgGameplayEvents GameplayEvents()

- int ActorCount()

- ArpgActor ActorAt(int index)

- ArpgActor CreateActor(string actorId, string faction, int gridX, int gridY, int level)

- bool EnterDefaultMap()

- bool EnterMap(string mapId)

- bool LoadMap(string mapId, ArpgActor preservedPlayer, int portalId)

- void ApplyStartingBuffs(ArpgActor actor, ArpgActorDefinition definition)

- bool MoveActor(ArpgActor actor, int x, int y)

- bool HasMovementRestriction(ArpgActor actor)

- bool HasAttackRestriction(ArpgActor actor)

- int AddItem(ArpgActor actor, string itemId, int count)

- bool UseItem(ArpgActor actor, string itemId)

- bool EquipItem(ArpgActor actor, string itemId, string slot)

- bool UnequipItem(ArpgActor actor, string slot)

- ArpgBuffInstance ApplyBuff(ArpgActor actor, string buffId, ArpgActor source, bool unique)

- bool RemoveBuff(ArpgActor actor, string buffId)

- ArpgSkillCastResult CastSkill(ArpgActor caster, string skillId, ArpgActor target)

- ArpgSkillCastResult ResolveCast(ArpgSkillCastResult result)

- double DamageActor(ArpgActor source, ArpgActor target, double amount, bool critical)

- void RemoveConditionalBuffs(ArpgActor actor, int condition)

- void Update(int deltaMilliseconds)

- bool TryUsePortal(ArpgActor actor)


## GlobalEntry (class)

- string key;

- string val;

- int kind;

- GlobalEntry(string key, string val, int kind)


## ServerConfig (class)

- string name;

- int port;

- int messageLength;

- List<string> extComponents;

- List<string> extScripts;

- ServerConfig(string name, int port)

- ServerConfig SetMessageLength(int amount)

- ServerConfig AddComponent(string script)

- ServerConfig AddScript(string script)

- string Name()

- int Port()

- int MessageLength()

- int ComponentCount()

- int ScriptCount()

- string ComponentAt(int index)

- string ScriptAt(int index)


## SqliteComponentConfig (class)

- string kind;

- string name;

- SqliteComponentConfig(string name)

- string Kind()

- string Name()


## TcpClientConfig (class)

- string kind;

- string name;

- string ip;

- int port;

- int messageLength;

- int token;

- TcpClientConfig(string name, string ip, int port)

- TcpClientConfig SetMessageLength(int amount)

- TcpClientConfig SetToken(int t)

- string Kind()

- string Name()

- string Ip()

- int Port()

- int MessageLength()

- int Token()


## TweenTarget (class)

- string prop;

- double amount;

- static TweenTarget Of(string prop, double amount)

- string Prop()

- double Amount()


## WebSocketClientConfig (class)

- string kind;

- string name;

- string url;

- WebSocketClientConfig(string name)

- WebSocketClientConfig SetUrl(string url)

- string Kind()

- string Name()

- string Url()


## WsUrl (class)

- string scheme;

- string host;

- int port;

- string path;

- bool secure;

- static int DigitsToInt(string s, int start, int end)

- static WsUrl Parse(string url)

- string Scheme()

- string Host()

- int Port()

- string Path()

- bool Secure()


## bool (delegate)

`delegate bool ArpgStarting(string arguments);`


## bool (delegate)

`delegate bool ArpgClosing();`


## bool (delegate)

`delegate bool ArpgSystemPrompt(int code, string message);`


## bool (delegate)

`delegate bool ArpgServerStarting(string bindHost);`


## bool (delegate)

`delegate bool ArpgServerStopping();`


## bool (delegate)

`delegate bool ArpgNodePointerChanged(string nodeId, int button, int modifiers, double x, double y);`


## bool (delegate)

`delegate bool ArpgControlPointerChanged(string controlId, string nodeId, int button, int modifiers, double x, double y);`


## bool (delegate)

`delegate bool ArpgWindowPointerChanged(string windowId, string controlId, int button, int modifiers, double x, double y);`


## int (delegate)

`delegate int ArpgTextMeasured(string text, int fontSize);`


## int (delegate)

`delegate int ArpgFontHeightMeasured(int fontSize);`


## void (delegate)

`delegate void ArpgStarted();`


## void (delegate)

`delegate void ArpgFocusChanged(bool focused);`


## void (delegate)

`delegate void ArpgWindowStateChanged(int state);`


## void (delegate)

`delegate void ArpgSizeChanged(int width, int height);`


## void (delegate)

`delegate void ArpgKeyChanged(int keycode, bool alt, bool shift, bool control);`


## void (delegate)

`delegate void ArpgMenuChanged(string menuName, string itemName, string payload);`


## void (delegate)

`delegate void ArpgRichTextLinkChanged(string windowId, string controlId, string link);`


## void (delegate)

`delegate void ArpgTimedEvent(string name, string payload, int deltaMilliseconds, int elapsedMilliseconds);`


## void (delegate)

`delegate void ArpgItemChanged(ArpgActor actor, ArpgItemDefinition item, int quantity);`


## void (delegate)

`delegate void ArpgItemUsed(ArpgActor actor, ArpgItemDefinition item);`


## void (delegate)

`delegate void ArpgSkillResolved(ArpgSkillCastResult result);`


## void (delegate)

`delegate void ArpgBuffChanged(ArpgActor actor, ArpgBuffInstance buff, bool added);`


## void (delegate)

`delegate void ArpgActorDamaged(ArpgActor source, ArpgActor target, double amount, bool critical);`


## void (delegate)

`delegate void ArpgActorDied(ArpgActor actor, ArpgActor killer);`


## void (delegate)

`delegate void ArpgMapChanged(string previousMapId, string currentMapId, int portalId);`


## void (delegate)

`delegate void ArpgServerStarted(ServerConfig config, string bindHost);`


## void (delegate)

`delegate void ArpgServerStopped(ServerConfig config);`


## void (delegate)

`delegate void ArpgServerClientChanged(ArpgServerClient client);`


## void (delegate)

`delegate void ArpgServerMessageReceived(ArpgServerClient client, ArpgMessage message);`


## void (delegate)

`delegate void ArpgTextDrawn(SdlRenderer renderer, string text, ArpgColor color, int fontSize, int x, int y, int width, int height, int angle);`


## void (delegate)

`delegate void ArpgNodeCreated(string nodeId);`


## void (delegate)

`delegate void ArpgNodeUpdated(string nodeId, int deltaMilliseconds);`


## void (delegate)

`delegate void ArpgNodeFocusChanged(string nodeId, bool focused);`


## void (delegate)

`delegate void ArpgNodeCustomEvent(string nodeId, string name, string payload);`


## void (delegate)

`delegate void ArpgControlCreated(string controlId);`


## void (delegate)

`delegate void ArpgControlUpdated(string controlId, int deltaMilliseconds);`


## void (delegate)

`delegate void ArpgControlFocusChanged(string controlId, bool focused);`


## void (delegate)

`delegate void ArpgControlDragged(string controlId, double deltaX, double deltaY);`


## void (delegate)

`delegate void ArpgControlLinkActivated(string controlId, string link);`


## void (delegate)

`delegate void ArpgControlActivated(string controlId);`


## void (delegate)

`delegate void ArpgControlValueChanged(string controlId, double newValue);`


## void (delegate)

`delegate void ArpgControlCustomEvent(string controlId, string name, string payload);`


## void (delegate)

`delegate void ArpgWindowCreated(string windowId);`


## void (delegate)

`delegate void ArpgWindowVisibilityChanged(string windowId);`


## void (delegate)

`delegate void ArpgWindowUpdated(string windowId, int deltaMilliseconds);`


## void (delegate)

`delegate void ArpgWindowCustomEvent(string windowId, string name, string payload);`


## ArpgEasing (enum)

- Linear

- QuadIn

- QuadOut

- QuadInOut

- CubicIn

- CubicOut

- CubicInOut

- QuartIn

- QuartOut

- QuartInOut

- QuintIn

- QuintOut

- QuintInOut

- SineIn

- SineOut

- SineInOut

- ExpoIn

- ExpoOut

- ExpoInOut

- CircIn

- CircOut

- CircInOut

- ElasticIn

- ElasticOut

- ElasticInOut

- BackIn

- BackOut

- BackInOut

- BounceIn

- BounceOut

- BounceInOut = 反弹衰减


## MusicState (enum)

- Stopped

- Playing

- Paused
