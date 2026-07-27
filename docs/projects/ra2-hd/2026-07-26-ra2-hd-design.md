# RA2 高清版演示（examples/game/ra2）设计文档

日期：2026-07-26
状态：已与用户确认

## 目标

用 Zan 在 `examples/game/ra2/` 下实现一个"红警2高清版"演示：读取用户自备的
原版 RA2 安装（`examples/game/ra2/data/`，含 `ra2.mix` 等），以现代分辨率
（1920×1080 逻辑分辨率、平滑缩放）渲染原版资源，并提供小规模遭遇战玩法。

**用户决策记录：**
- 资源来源：直接读取原版 MIX 档案（OpenRA 模式，用户自备正版资源）。
- 代码位置：全部自包含在 `examples/game/ra2/`；把 stdlib 的 SHP/PAL/TMP/INI/CSF
  解析器**移植副本**到示例目录（不是引用 stdlib/Game/Rts）。
- MIX 加密头：直接在 Zan 中清洁室实现解密（Westwood RSA + Blowfish，公开算法）。
- 第一里程碑范围：小规模遭遇战演示（渲染 + 选取 + 移动 + 交战 + 简单 AI），
  不含建造/经济系统。

## 第一里程碑验收标准

1. 启动后加载 `data/` 中的 MIX 资源，解出 rules.ini、CSF、调色板、SHP、TMP。
2. 渲染一张官方遭遇战小地图的等距地形，相机可平移、滚轮 0.5x–2x 平滑缩放。
3. 双方各一队单位（动员兵、美国大兵、灰熊坦克、犀牛坦克，可含防御塔），
   左键框选/点选、右键移动/攻击，A* 寻路，进入射程自动开火，有死亡动画。
4. 敌方 AI 周期性向玩家进攻。
5. HUD：单位中文名（CSF）、血条、选择框、底部小地图。
6. `data/` 缺失时显示友好错误屏，不崩溃。

## 目录结构与分层

```
examples/game/ra2/
├── main.zan              # 入口：窗口、主循环、场景切换（加载/错误/对局）
├── formats/              # 文件格式层（无 SDL 依赖，纯解析）
│   ├── Binary.zan        # 小端读取器（自 stdlib/Game/Rts/Formats 移植）
│   ├── Mix.zan           # 新写：MIX 档案（嵌套、CRC 索引、加密头解密）
│   ├── Ini.zan Csf.zan Pal.zan Shp.zan Tmp.zan  # 自 stdlib 移植
│   └── MapFile.zan       # 新写：地图（INI 结构 + IsoMapPack5 解码）
├── assets/               # VFS（多 MIX 按优先级叠加）+ AssetCache（解码→纹理图集）
├── sim/                  # 确定性模拟：格网、单位、A* 寻路、战斗、简单 AI
├── render/               # 等距渲染：地形层、y 排序单位层、血条/选框、相机
├── game/                 # 输入与玩法胶水：框选、右键指令、编队、HUD
└── data/                 # 用户自备原版安装（git 不提交）
```

依赖单向：`formats ← assets ← render/sim ← game ← main`。formats 层不 import SDL3。

命名空间约定：`Ra2.Formats` / `Ra2.Assets` / `Ra2.Sim` / `Ra2.Render` / `Ra2.Game`。

## 资源管线

### MIX 档案（Mix.zan，新写）

- 头部：flags(u32)。若含加密位（0x00020000）：80 字节 Westwood RSA 块解出
  56 字节 → 取前 8 字节为 Blowfish 密钥，用 Blowfish-ECB 解密头部（条目数、
  数据大小、条目表），8 字节块对齐。无加密位则直接读明文头。
- 条目表：每项 { id(i32), offset(u32), size(u32) }，id 为文件名 CRC 哈希。
- 哈希：TS/RA2 变体 CRC（文件名大写、按 4 字节分组的特殊填充规则 + CRC32）。
- 嵌套：`ra2.mix` 内含 `cache.mix / local.mix / conquer.mix / isotemp.mix /
  isosnow.mix / temperat.mix` 等子 MIX，按需递归打开。
- 清洁室来源：算法参照公开文档与 OpenRA/XCC 的公开实现说明重写，不复制代码。

### VFS 与加载链（assets/）

- VFS：注册多个 MIX（含嵌套）+ data/ 散文件目录，按优先级查找文件名。
- 启动加载链：`rules.ini`（单位数值）→ `ra2.csf`（中文文本）→
  `isotem.pal / unittem.pal / palette.pal` → 所需单位/建筑/动画 SHP → 地形 TMP。
- 地图：从 `multi.mix` 内置官方遭遇战小图一张；`IsoMapPack5` 段
  Base64 解码 + Format80/LZO 解压得到每格地形与高度。
- 高清化：SHP 帧解码为 RGBA 后打入大图集纹理（每调色板+SHP 组合一次解码）；
  渲染线性过滤 + 任意倍率缩放；房屋色用调色板 remap 段（16–31）着色。

## 模拟核心（sim/）

- 固定步长逻辑帧 15 fps（原版节奏），渲染层对位置插值（60fps+ vsync）。
- 菱形等距格网；A* 寻路，8 向，通行代价来自 TMP 地形类型；单位占格与避让
  从简（同格互斥 + 重寻路）。
- 单位数值全部读自 rules.ini：Speed、Strength、武器（射程/伤害/ROF）。
- 战斗：射程内自动索敌开火，即时命中判定，开火/待机/移动/死亡动画序列
  按 SHP 帧组播放（朝向 8/32 向取帧）。
- AI：敌方持有一队单位，按周期编组向玩家单位/出生点进攻，无建造。
- 确定性：模拟只用整数/定点数，与渲染帧率解耦。

## 呈现与操作（render/、game/）

- 等距投影：cell 60×30；渲染顺序 = 地形 → (y+高度)排序的单位/建筑 →
  选框/血条 → HUD。
- 相机：方向键/边缘滚屏/中键拖拽平移；滚轮缩放 0.5x–2x（以鼠标为中心）。
- 操作：左键点选/框选，右键移动（指向敌人=攻击），Ctrl+1..9 编队，双击选同类。
- HUD：底部小地图（离屏渲染地形缩略 + 单位点，点击跳转）；选中单位显示
  CSF 中文名与血量；GameKit 的 CJK 图集字体渲染文本。
- 主循环沿用 examples/game 现有模式（参照 gomoku：`Host`/`SdlEvent.Poll`），
  但窗口/渲染直接用 SDL3 stdlib，逻辑分辨率 1920×1080 + letterbox。

## 错误处理

- `data/` 缺失、MIX 打不开、关键文件缺失：进入错误场景，明确提示
  "请将红警2安装文件放入 examples/game/ra2/data/"及缺失项，不崩溃。
- SHP/地图解析对越界、截断数据防御性校验（沿用 stdlib 解析器的风格）。

## 测试

- `tests/conformance/ra2_mix_parse.zan`：合成明文与加密 MIX（自造密钥流程
  用已知向量验证 Blowfish/CRC），清洁室、不嵌入任何原版数据。
- `tests/conformance/ra2_map_unpack.zan`：合成 IsoMapPack5（Format80）数据。
- Blowfish/RSA 单元向量测试（标准测试向量）。
- 真实资源验证使用 `_scratch/` 探针（列 ra2.mix 内容、导出 SHP 帧目检），
  不入库。

## 明确不做（本里程碑）

- 建造/采矿/电力经济系统
- 音频（SDL3 绑定尚无音频接口）
- VXL/HVA 体素单位（坦克先整体 SHP 渲染，无独立炮塔）
- 联机、存档、战役、触发脚本
- 修改 stdlib（一切代码在 examples/game/ra2/ 与 tests/ 内）

## 里程碑之后的方向（不在本次范围）

建造系统与经济、音频原生绑定、VXL 渲染、更多阵营单位、遭遇战胜负判定。
