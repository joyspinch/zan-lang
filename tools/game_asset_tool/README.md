# GameAssetTool · 游戏素材管理工作台

`GameAssetTool` 是专为 2D、2.5D ARPG、等轴测（Isometric）与网格切片类游戏设计的可视化素材管理与管线配置工作台。作为 Zan 原生标准库 GUI 应用开发，可直接在 **ZanIDE Tools 面板** 中一键双击运行。

---

## 核心设计与解决的业务痛点

在传统的 2.5D / 等轴测 / 传奇类 / ARPG 游戏开发中，素材管理往往面临以下严峻问题：
1. **美术工期与贴图显存浪费**：8 个朝向若全部由美术手绘并打包切片，会浪费 37.5% 的人力与显存空间；
2. **纸娃娃系统（Paperdoll）严重穿模**：多层装备（身体、衣服、武器、翅膀、发型）在不同朝向视角下存在遮挡层级倒置问题（例如朝南正面时武器在外层，朝北背面时武器应收在身后被身体遮挡；正面时翅膀在最底层，背面时翅膀在外层覆盖背部）；
3. **纹理图集碎片化与非 POT（Power-of-Two）尺寸**：散碎切片导致 GPU Draw Call 暴增，且不规则尺寸导致纹理压缩率与缓存命中率低下；
4. **等轴测地砖卡角与碰撞遍历过载**：2:1 菱形地砖如果每一个小格子独立作为物理碰撞体，会导致碰撞体数量成千上万，且多边形顶点接缝容易导致角色移动卡角。

`GameAssetTool` 针对上述四大痛点提供了开箱即用的模块化解决方案。

---

## 四大核心工作台模块

### 1. 5向/8向动作序列 Studio（5DirSymmetricMirror）
- **东侧 3 向对称镜像到西侧**：美术仅需绘制正南（S）、东南（SE）、正东（E）、东北（NE）、正北（N）5 个基础切片行。
- **西南（SW）、正西（W）、西北（NW）自动镜像复用**：在渲染批处理提交阶段直接交换水平纹理坐标 $u_0 \leftrightarrow u_1$（`FlipX = true`），零 CPU 像素拷贝开销，贴图显存与美术工期立省 **37.5%**。
- **实时朝向播演器**：内置 8 方向罗盘切换、动作类型循环播放与单步步进验证。

### 2. 纸娃娃动态层深矩阵（Paperdoll Depth Matrix）
- **挂件解耦**：将角色拆分为 Body（身体）、Cloth（衣服/重铠）、Weapon（武器/巨剑）、Wing（翅膀）、Hair（发型/头盔）等独立部件，由同一动作时钟推进帧。
- **8 方向层深矩阵（`depthPerDir[8]`）**：为每个部件在 8 个朝向独立配置 Z 轴深度偏移值（-50 ~ +50）。
- **动态轻量插入排序**：角色朝向改变时，依据当前朝向数组重新执行极低开销的插入排序，生成正确的自底向上渲染队列，彻底杜绝装备穿模。

### 3. 纹理图集紧凑打包计算器（POT Atlas Packer）
- **最优 POT 尺寸推导**：输入单帧像素宽高与总帧数，自动依据最小包络面积推导 256/512/1024/2048/4096 最优 2 的幂次方规格。
- **网格行列与 UV 归一化**：自动排布网格行列，实时解算各切片的像素坐标（$px, py$）与归一化浮点 UV 坐标（$u_0, v_0, u_1, v_1$）。
- **显存与利用率实时估算**：即时反馈当前排布的显存空间利用率（%）以及 RGBA32 显存占用量（KB）。

### 4. 2:1 等轴测地砖切片与碰撞合并（2:1 Isometric Slicer）
- **2:1 菱形几何双向投影**：提供 64x32、128x64 经典 2:1 等轴测菱形地砖涂鸦与交互拾取。
- **地砖属性画笔**：支持平地（Ground）、阻挡（Solid）、透明遮挡（Alpha）、刷怪点（Spawn）的多层属性涂刷。
- **连续水平线段碰撞体合并算法**：自动扫描水平方向连续的阻挡地砖并合并为单条线段碰撞流形，大幅削减物理引擎遍历开销并消除接缝卡角。

---

## 运行与编译指南

### 1. 从 ZanIDE 直接运行（推荐）
在 ZanIDE 中打开本项目，展开左侧导航的 **Tools** 面板：
- 工具树中会自动扫描并显示 `game_asset_tool/GameAssetTool.zan`；
- 双击该节点，IDE 即可自动完成编译并调起原生窗口。

### 2. 命令行直接编译运行
```bash
# 编译生成原生可执行程序
build/zanc.exe tools/game_asset_tool/GameAssetTool.zan --auto-stdlib -o _scratch/game_asset_tool.exe

# 运行工具
./_scratch/game_asset_tool.exe
```

---

## 导出配置与引擎对接规范

点击工具右下角的 **“复制 JSON 配置”** 或 **“保存配置文件”** 即可导出对应的规范文件：

### 1. 5DirSymmetricMirror 协议范例
```json
{
  "model": "Warrior_Male",
  "action": "Walk",
  "mode": "5DirSymmetricMirror",
  "frameCount": 6,
  "fps": 10,
  "directions": [
    { "directionIndex": 0, "directionName": "East", "sourceSliceRow": 2, "flipX": false },
    { "directionIndex": 1, "directionName": "SouthEast", "sourceSliceRow": 1, "flipX": false },
    { "directionIndex": 2, "directionName": "South", "sourceSliceRow": 0, "flipX": false },
    { "directionIndex": 3, "directionName": "SouthWest", "sourceSliceRow": 1, "flipX": true },
    { "directionIndex": 4, "directionName": "West", "sourceSliceRow": 2, "flipX": true },
    { "directionIndex": 5, "directionName": "NorthWest", "sourceSliceRow": 3, "flipX": true },
    { "directionIndex": 6, "directionName": "North", "sourceSliceRow": 4, "flipX": false },
    { "directionIndex": 7, "directionName": "NorthEast", "sourceSliceRow": 3, "flipX": false }
  ]
}
```

### 2. Paperdoll 动态层深协议范例
```json
{
  "kind": "PaperdollConfig",
  "partCount": 5,
  "parts": [
    { "id": "wing", "name": "翅膀 (Wing)", "depthPerDir": [-10, -15, -20, -15, -10, 20, 30, 20] },
    { "id": "body", "name": "身体 (Body)", "depthPerDir": [0, 0, 0, 0, 0, 0, 0, 0] },
    { "id": "cloth", "name": "重铠 (Cloth)", "depthPerDir": [5, 5, 5, 5, 5, -5, -5, -5] },
    { "id": "hair", "name": "头盔 (Hair)", "depthPerDir": [8, 8, 8, 8, 8, 10, 10, 10] },
    { "id": "weapon", "name": "武器 (Weapon)", "depthPerDir": [8, 10, 10, 10, 8, -15, -30, -15] }
  ]
}
```
该数据可直接供 `packages/Zan.Game/src/Game/Tools/AssetPipeline.zan` 与 `Game.Arpg` 模块反序列化后使用。
