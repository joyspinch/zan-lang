# MGE — 传奇类游戏数据编辑器

`tools/mge/Mge.zan` 是一个用 Zan 写的 GUI 工具，用来编辑传奇（Mir）类游戏的
基础数据：**角色 / 道具装备 / 技能 / 地图**。四个编辑器合并在一个窗口里，
共用一套数据目录和导出流程。

## 构建与运行

```powershell
.\scripts\build_mge.ps1          # -> build\mge.exe（GUI 运行时静态链接）
build\mge.exe                    # 数据目录默认 mge-data
build\mge.exe d:\mygame\data     # 或指定数据目录
```

## 数据格式

编辑结果落在数据目录下的普通 JSON 里，方便 diff、脚本处理和版本控制：

| 文件         | 内容                                       |
|--------------|--------------------------------------------|
| `npc.json`   | 角色：等级、HP/MP、命中/躲避/暴击、AI、掉落几率、技能表 |
| `item.json`  | 道具：分类/子类、等级需求、叠加、攻击/魔法/道术/防御/魔御 |
| `skill.json` | 技能：阵营、目标位置、距离、消耗、冷却、范围、伤害系数 |
| `map.json`   | 地图：格子尺寸、行列、起点、逐格标记、刷怪点 |

地图的每个格子存成一位数字：`0` 空地、`1` 障碍、`2` 透明、`3` 障碍+透明，
与 `MapCell.Of(x, y, flags)` 的取值一致。

## 导出到 Game.Zgm

「导出 Zan」会先保存 JSON，再生成 `<数据目录>/ZgmData.zan`——一个针对
`Game.Zgm` 的 `ZgmProject` 构建器（`NpcConfig` / `ItemConfig` / `SkillConfig` /
`MapConfig`）。游戏侧直接把它编进去即可：

```zan
using Game.Zgm;

ZgmEngine engine = ZgmEngine.FromProject(MgeData.Build());
engine.Start("");
```

## 界面

- 左侧栏切换四个编辑器，底部是「保存全部」和「导出 Zan」。
- 角色/道具/技能页：左边是带搜索的列表（新建 / 复制 / 删除），右边是按
  schema 自动生成的属性表单。加字段只需在 `Mge.BuildSheets` 里加一行
  `FieldSpec`，不用写界面代码。
- 地图页：中间是格子画布，顶部工具条选择「画格子 / 放刷怪点 / 设起点 /
  删刷怪点」和笔刷；放刷怪点时使用「角色」页当前选中的角色，数量和范围
  在右侧参数里改。
