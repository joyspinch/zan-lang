# Game.Arpg.Data

> 源码: `packages/Zan.Game/src/Game/Arpg/Data/Db.zan`, `packages/Zan.Game/src/Game/Arpg/Data/Save.zan`, `packages/Zan.Game/src/Game/Arpg/Data/SaveState.zan`


## ArpgDatabase (class)

ZGM Sqlite 组件的实时 SQLite 数据库运行时。绑定
SqliteComponentConfig (类型="Sqlite", 名称) to a real System.Data.Sqlite
连接，暴露 DM 引擎在构建/运行期使用的查询/执行/事务接口
打开/执行/查询 methods act on. Requires the sqlite3 native driver at
（见 stdlib/System/Data/Sqlite/drivers）。

- string name;

- SqliteConnection conn;

- bool open;

- static ArpgDatabase Open(SqliteComponentConfig config, string path)
  - 打开组件的数据库文件（或 ":memory:"）并绑定实时
    连接 to the config's 名称.

- static ArpgDatabase OpenMemory(string name)
  - 便捷方法：为指定组件打开私有内存数据库。

- static ArpgDatabase OpenProject(ArpgProject project, string componentName, string path)
  - 从项目解析名为 componentName 的 Sqlite 组件并打开其数据库
    文件 path；project 为 null 或找不到该组件时返回 null。

- string Name()
  - 绑定的组件名。

- bool IsOpen()
  - 连接是否处于打开状态。

- SqliteConnection Conn()
  - 底层数据库连接（供 ORM 实体操作与高级查询使用）。

- int Execute(string sql)
  - 执行: runs a non-query statement, 返回 affected row 数量 (-1 on 错误).

- int Execute(string sql, DbParams prms)
  - 执行带参数的非查询语句。

- DbResult Query(string sql)
  - 查询: runs a query and 返回 the materialized result 设置.

- DbResult Query(string sql, DbParams prms)
  - 执行带参数的查询并返回物化的结果集。

- string Scalar(string sql)
  - 执行聚合查询并返回第一行第一列（没有则为空字符串）。

- string Scalar(string sql, DbParams prms)
  - 执行带参数的标量查询。

- int LastInsertId()
  - 最近一次 INSERT 语句生成的 rowid。

- string GetError()
  - 底层连接记录的最近一次错误信息。

- void Begin()
  - 开启事务。

- void Commit()
  - 提交当前事务。

- void Rollback()
  - 回滚当前事务。

- void Close()
  - 关闭连接；未打开时为无操作，重复调用安全。


## ArpgSaveBuff (class)

存档中的一个生效中增益：名称与剩余毫秒数。

- string name;

- int remaining;

- ArpgSaveBuff(string name, int remaining)

- string Name()
  - 增益名。

- int Remaining()
  - 剩余时长（毫秒）。


## ArpgSaveEquipment (class)

存档中的一件已装备物品：装备槽名与物品名。

- string slot;

- string itemName;

- ArpgSaveEquipment(string slot, string itemName)

- string Slot()
  - 装备槽名。

- string ItemName()
  - 装备的物品名。


## ArpgSaveItem (class)

存档中的一个背包条目：物品名与数量。

- string name;

- int quantity;

- ArpgSaveItem(string name, int quantity)

- string Name()
  - 物品名。

- int Quantity()
  - 数量（恒 ≥ 1，AddItem 已过滤非正值）。


## ArpgSaveRepository (class)

基于通用 SQLite 连接的 ZGM 原生存档仓库。
其结构由标准库 ORM 实体（ArpgSaveSlotEntity 等）与强类型表达式查询驱动，
涵盖主槽位及规范化的子表（背包、装备、已学技能与生效增益）。
完全杜绝手拼 SQL 字符串与裸 SQL 注入风险。

- ArpgDatabase database;

- bool ready;

- static ArpgSaveRepository Open(ArpgDatabase database)
  - 在已打开的数据库上打开存档仓库：database 为 null 或未打开时
    返回 null，否则同步实体结构建表并返回仓库；IsReady() 反映就绪状态，
    未就绪时所有读写操作均失败。

- static ArpgSaveRepository OpenProject(ArpgProject project, string componentName, string path)
  - 便捷入口：从项目解析名为 componentName 的 Sqlite 组件并在其
    数据库文件 path 上打开存档仓库（组件缺失时返回 null）。

- bool IsReady()
  - 建表与实体同步是否成功；为 false 时 Save/Load 等操作一律拒绝。

- ArpgDatabase Database()
  - 底层数据库连接。

- bool HasColumn(string table, string column)
  - 表 table 是否已存在列 column。

- bool Initialize()
  - 基于 ORM CodeFirst 自动同步并迁移全部存档表结构（含主槽表与四张子表），
    自动补齐缺失列，任一步失败返回 false。

- bool Save(string slot, ArpgSaveState state)
  - 将 state 覆写保存到槽位 slot：主行与四张子表在事务内先删后插，
    全部采用 ORM 强类型表达式与实体写入。未就绪、slot 为空串或 state 为 null 时
    直接返回 false。

- ArpgSaveState Load(string slot)
  - 读取槽位 slot 为 ArpgSaveState：使用 ORM 强类型表达式查询。
    主行不存在、未就绪或 slot 为空串时返回 null。
    物品/装备/技能/增益子行分别按实体表达式排序读回。

- bool Exists(string slot)
  - 槽位 slot 是否已有存档；未就绪或 slot 为空串时返回 false。

- bool Delete(string slot)
  - 删除槽位 slot：在一个事务内通过 ORM 表达式删除主行与全部子行。
    返回 true 表示槽位确实存在且已删除，槽位本不存在返回 false。

- int SlotCount()
  - 存档槽位总数；未就绪时返回 0。

- string SlotAt(int index)
  - 按槽位名排序后的第 index 个槽位名；未就绪、index 为负或越界时返回空串。

- void Close()
  - 关闭底层数据库并将仓库置为未就绪；之后所有操作均失败。


## ArpgSaveSkill (class)

存档中的一个已学技能名。

- string name;

- ArpgSaveSkill(string name)

- string Name()
  - 技能名。


## ArpgSaveState (class)

渲染器无关的存档快照。应用可用自己偏好的 JSON、数据库
或二进制层序列化此对象。

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
  - 以下修改器均返回 this，便于链式构建一个快照：
    new ArpgSaveState(...).SetProgress(...).AddItem(...).AddSkill(...)
    
    一次性写入经验与怒气，返回 this。

- ArpgSaveState AddItem(string name, int quantity)
  - 追加一个背包条目；name 为空串或 quantity ≤ 0 时忽略，返回 this。

- ArpgSaveState AddEquipment(string slot, string itemName)
  - 追加一件已装备物品；slot 或 itemName 为空串时忽略，返回 this。

- ArpgSaveState AddSkill(string name)
  - 追加一个已学技能；name 为空串时忽略，返回 this。

- ArpgSaveState AddBuff(string name, int remaining)
  - 追加一个生效中的增益；name 为空串时忽略，remaining 为剩余
    毫秒数，返回 this。

- string MapName()
  - 存档所在地图名。

- int HeroX()
  - 主角所在格 X。

- int HeroY()
  - 主角所在格 Y。

- int HeroDirection()
  - 主角朝向（-1 或 0~7）。

- int HeroLevel()
  - 主角等级。

- int HeroHp()
  - 主角当前 HP。

- int HeroMp()
  - 主角当前 MP。

- int HeroExperience()
  - 主角累计经验（构造时为 0，经 SetProgress 写入）。

- int HeroRage()
  - 主角怒气（构造时为 0，经 SetProgress 写入）。

- int ItemCount()
  - 背包条目数。

- ArpgSaveItem ItemAt(int index)
  - 第 index 个背包条目；越界行为由底层 List 决定（抛错）。

- int EquipmentCount()
  - 已装备条目数。

- ArpgSaveEquipment EquipmentAt(int index)
  - 第 index 个已装备条目；越界行为由底层 List 决定（抛错）。

- int SkillCount()
  - 已学技能数。

- ArpgSaveSkill SkillAt(int index)
  - 第 index 个已学技能；越界行为由底层 List 决定（抛错）。

- int BuffCount()
  - 生效中增益数。

- ArpgSaveBuff BuffAt(int index)
  - 第 index 个生效中增益；越界行为由底层 List 决定（抛错）。

- void Dispose()
  - 释放四个条目列表（置 null）；此后不应再读取或序列化该快照。
