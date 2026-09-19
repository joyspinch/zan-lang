# 无为修仙传 · Zan 重制版

原版《无为修仙传》（Godot 版）的 Zan 原生重制：界面结构由 `src/App.html`
设计稿声明（GenForm 编译期展开成 partial 类的类型化字段），`main.zan`
只写 code-behind（生命周期、事件、模型刷新、存档）；外观由 CSS 皮肤
（`skins/wuwei`）完成，无自绘、无 WebView；数值全部来自 `data/` 下的
表格；发布版表格 AES-GCM 加密、美术音频内嵌进 exe，单文件即可运行。

## 快速开始

```powershell
# 发布版（推荐）：表格加密 + 资源内嵌，输出 build/wuwei/release/
tools/build.ps1

# 开发版：data/ 以明文散装随行，便于改表调试
tools/build.ps1 -Development

# 全套测试：数据完整性 / 加密 / 素材导入 / 无头模型测试 / GUI 全流程
tools/test.ps1
tools/test.ps1 -Gui      # 追加 UiDriver 驱动的真实窗口回归
```

需要 Python 3 + `cryptography`（表格加密）。编译器默认取 `build/zanc.exe`，
可用 `-Compiler`/`-Output` 覆盖。

## 玩法

- **修行**：吐纳/炼气自动运转，灵气、境界（`realms.csv` 10 境）、突破；
- **历练**：29 张地图、69 种敌人，回合制遭遇战，掉落与任务（`quests.csv`）；
- **杂学（百艺）**：108 个配方（钓鱼/采药/炼丹…），按分类筛选、分页制作；
- **万宝**：行囊 236 种物品，装备/使用/售出/分解；坊市（`market.csv`）；
- **宗门**：入门、宗门任务、宝库（`sects.csv` / `sect_tasks.csv` / `sect_vault.csv`）；
- **轮回**：转生继承（因果/世数），天赋（`talents.csv`）与熟练度星级；
- **成就**：33 项成就；设置页音量调节，背景音乐循环播放。

主菜单按原作版式复刻：整幅背景、题字、四个入口，加载资源后进入。
游戏主壳的中部 16 个页面均为 flex 纵列（`zan-flex class="column"`）：
定高行声明尺寸、卡片与行带 `grow` 撑满剩余空间，任何窗口尺寸下页面
正好填满视口——不出现滚动条，也不留页底死空间。

## 存档

`.wsv` 二进制存档，AES-256-GCM 加密（见 `SECURITY.md`）。默认写在用户
目录；设 `WUWEI_SAVE_DIR` 可整体重定向（测试与便携使用）。

## 目录

| 路径 | 内容 |
|------|------|
| `src/` | 游戏源码（`App.html` 界面设计稿，`main.zan` code-behind 生命周期与流程，`Game.zan` 无头模型，`Tables.zan` 表格装载，`Save.zan` 加密存档，`Profiles.zan` 槽位，`AudioService.zan` 音乐） |
| `data/` | 21 张权威数值表（CSV，UTF-8）。`source_tables.csv` 是原作表格存档，仅参考、不参与发布 |
| `assets/` | 美术与音频（WebP/OGG），发布时内嵌 |
| `skins/wuwei/` | 皮肤包（按路径打包：IDE 与 `--embed skins=skins` 都认这个目录） |
| `tools/` | `build.ps1` 构建、`test.ps1` 测试、`pack_data.py` 表格加密、`prepare_assets.py` 素材导入 |
| `tests/`（仓库根） | `tests/templates/wuwei/` 数据/加密/素材回归与无头模型测试 |

发布产物不含任何散装 `data/`、`assets/`、`skins/` 目录——`build.ps1`
遇到会直接报错，保证“内嵌生效”这件事不会被静默绕过。
