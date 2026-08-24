# Game.Arpg.Data

> 源码: `stdlib/Game/Arpg/Data/Db.zan`, `stdlib/Game/Arpg/Data/Save.zan`, `stdlib/Game/Arpg/Data/SaveState.zan`


## ArpgDatabase (class)

- string name;

- SqliteConnection conn;

- bool open;

- static ArpgDatabase Open(SqliteComponentConfig config, string path)

- static ArpgDatabase OpenMemory(string name)

- static ArpgDatabase OpenProject(ArpgProject project, string componentName, string path)

- string Name()

- bool IsOpen()

- int Execute(string sql)

- DbResult Query(string sql)

- string Scalar(string sql)

- int LastInsertId()

- string GetError()

- void Begin()

- void Commit()

- void Rollback()

- void Close()


## ArpgSaveBuff (class)

- string name;

- int remaining;

- ArpgSaveBuff(string name, int remaining)

- string Name()

- int Remaining()


## ArpgSaveEquipment (class)

- string slot;

- string itemName;

- ArpgSaveEquipment(string slot, string itemName)

- string Slot()

- string ItemName()


## ArpgSaveItem (class)

- string name;

- int quantity;

- ArpgSaveItem(string name, int quantity)

- string Name()

- int Quantity()


## ArpgSaveRepository (class)

- ArpgDatabase database;

- bool ready;

- static ArpgSaveRepository Open(ArpgDatabase database)

- static ArpgSaveRepository OpenProject(ArpgProject project, string componentName, string path)

- bool IsReady()

- ArpgDatabase Database()

- bool HasColumn(string table, string column)

- bool EnsureColumn(string table, string column, string definition)

- bool Initialize()

- static string Quote(string text)

- bool Save(string slot, ArpgSaveState state)

- ArpgSaveState Load(string slot)

- bool Exists(string slot)

- bool Delete(string slot)

- int SlotCount()

- string SlotAt(int index)

- void Close()


## ArpgSaveSkill (class)

- string name;

- ArpgSaveSkill(string name)

- string Name()


## ArpgSaveState (class)

- string mapName;

- int heroX;

- int heroY;

- int heroDirection;

- int heroLevel;

- int heroHp;

- int heroMp;

- int heroExperience;

- int heroRage;

- List<ArpgSaveItem> items;

- List<ArpgSaveEquipment> equipment;

- List<ArpgSaveSkill> skills;

- List<ArpgSaveBuff> buffs;

- ArpgSaveState(string mapName, int heroX, int heroY, int heroDirection, int heroLevel, int heroHp, int heroMp)

- ArpgSaveState SetProgress(int experience, int rage)

- ArpgSaveState AddItem(string name, int quantity)

- ArpgSaveState AddEquipment(string slot, string itemName)

- ArpgSaveState AddSkill(string name)

- ArpgSaveState AddBuff(string name, int remaining)

- string MapName()

- int HeroX()

- int HeroY()

- int HeroDirection()

- int HeroLevel()

- int HeroHp()

- int HeroMp()

- int HeroExperience()

- int HeroRage()

- int ItemCount()

- ArpgSaveItem ItemAt(int index)

- int EquipmentCount()

- ArpgSaveEquipment EquipmentAt(int index)

- int SkillCount()

- ArpgSaveSkill SkillAt(int index)

- int BuffCount()

- ArpgSaveBuff BuffAt(int index)

- void Dispose()
