# 迷你传奇资源包（data/*.dll）加密破解与提取

`D:\game\迷你传奇` 的 10 个资源包（tupu/boss/show/item/ui/car/map/sound/city/skill
.dll）是自定义加密容器。本文档是对 `game.exe` 的静态逆向结论，提取器为同目录
`extract_minimir.py`（自包含，无需外部状态）。全量提取结果：**6843 条目
→ 6058 PNG + 87 GIF + 671 JPG + 19 WAV，0 失败**，是客户端
`monster_art.csv`/物品图标管线（M2 `picture` 编号）的真实素材来源。

## 包对象模型（game.exe，VA = 文件偏移 + 0x400000，各节 RAW==RVA）

每个打开的资源包是一个 604 字节（0x25C）C++ 对象：

| 位置   | 内容                                                                 |
|--------|----------------------------------------------------------------------|
| +0x000 | vtable = 0x6fb7b4                                                    |
| +0x010 | encStart（该偏移之前视为明文；实际运行值为 0）                        |
| +0x014 | 状态 A 密钥 keyA[32]                                                  |
| +0x035 | 状态 A 的 RC4 状态表 tableA[258]（S[256] + i + j）                    |
| +0x137 | 状态 B 密钥 keyB[32]（+0x158 起状态 B 表）                            |

- 工厂 0x513f00：校验模式（ecx 1–6 / eax 1–4）拼 Open 标志 → 分配 0x25C → 构造器 0x53db90 → `call [edx+0x20]` Open → 注册类型 0x7dc（0x534bb0）。
- 构造器成员初始化 0x53dbf0：`tableA = RC4-KSA(路径串)`；`keyA = 变换(MD5(路径)+16 个 0)`（变换 = 偶位相邻交换 + 32 字节反转，0x53e140）。
- **运行时 E 层用 SetKey（0x534c86 → 0x53e140）把 10 个包统一覆盖为同一密钥**：
  keyA = keyB = ASCII `"23ea0c7fdc0c73ca7803f78896383e53"`，tableA = tableB =
  同一 KSA 结果（与路径无关）。因此磁盘密钥与包路径无关，十包一把钥匙——
  按路径推导密钥的所有尝试都不会命中，这是本次逆向的关键弯路。
- Read 虚方法 +0x34 = 0x53dcb0：若 keyA[0]≠0（恒真）走加密分支 0x53dccf，
  明读后调 0x53df40 原地解密；+0x38 是使用状态 B 的同型变体。

## 解密算法（0x53df40，任意 offset/length）

```
S    = tableA 副本（含 i、j）
block = off >> 12
drop(S, block*4)                       # 0x5f2990：空转 PRGA n 步
ksbuf = PRGA(全零 (len>>12)*4+8 字节)  # 密钥流前缀，每 4 字节播种一个块
按 4096 字节块循环：
    dw    = LE32(ksbuf[第 idx 个双字])
    key40 = dw(4B) || keyA(32B) || (dw ^ blockCtr)(4B)   # 40 字节
    S2    = RC4-KSA(key40)               # 0x53de00：恒从恒等置换重启
    drop(S2, 36 + off%4096)              # 仅第一块加块内偏移；其后 36
    明文  = PRGA 异或密文（本块 min(4096-块内偏移, 剩余) 字节）
    blockCtr += 1; idx += 1
```

- PRGA（0x53dec0）：`i++; j += S[i]; swap(S[i],S[j]); out ^= S[(S[i]+S[j])&0xff]`。
- KSA（0x53de00）：标准 RC4 KSA，密钥按 `m = (m+1) mod n` 循环取用（n=40），
  每次先从全局恒等表（0x81d110，惰性初始化）重建 S。
- MD5（0x559bc0 包装，0x55a670 init / 0x55a7f0 update）为标准实现，仅用于
  被覆盖前的初始 keyA 推导。

## 包格式

```
"ZMS\0" + u32 条目数
条目 × N：[名字\0][u32 zlib大小][u32 0x033E0F0D 常量][u32 原始大小][zlib 流]
```

zlib 解压即最终文件。item/show/ui/boss/skill 条目有名字（如 item 的
`00001`…`02819`，与 M2.DB 物品 `picture` 编号一一对应）；map/sound/car/city
条目名字为空串，按出现顺序编号。内容无二次加密。

## 复现

```powershell
python templates/game/legend/tools/minimir/extract_minimir.py
# 默认输出 _scratch/minimir/extract_full/<pack>/；--game/--out 可覆盖
```

pack_key/table 的出处：对运行中进程做 ReadProcessMemory 转储（203MB，无害，
游戏进程不受影响），按 vtable 0x6fb7b4 特征扫描堆得到 10 个包对象。两个状态
完全相同；`extract_minimir.py` 顶部 `PACK_KEY`/`PACK_TABLE` 即其内嵌副本。
不附加调试器（游戏会崩），这是唯一动过的进程内存读取。

## UI 布局与素材→玩法模块映射（game.exe 静态分析结论）

结论先行：**ui 包的 275 张图没有任何资源内布局元数据**。逐个证据：

- `data/data.dll` 不是加密包，是明文 JSON（`{"item":...` 开头），只有数据表
  （item/map/monster/skill/boss/sc/title/city/achieve/fw/jue/shuxing），无 UI 表。
- `data/login.dll` 是 MP3 音乐（文件头 `fffb90`）；`Skin.dll` 是 SkinH 换肤引擎
  （`SkinH_AttachRes`），只管系统控件配色；`tupu` 包 460 张全是 120×170 图鉴卡。
- `game.exe` 是易语言编译的 MFC 程序，UI 布局 = 硬编码坐标 + 包图索引，编译进代码。

### 静态可提取的东西（已做）

1. **包加载序**（0x436f00 起）：item=1, map=2, show=3, boss=4, skill=5, ui=6,
   sound=7, car=8, city=9, tupu=10。取图函数 `0x41ee8a(index, pack=6, flag)`，
   pack 默认 6 即 ui 包 → 全 exe 只有 155 处调用、落在 77 个功能函数。
2. **缩放机制**：`0x40ee97` 是坐标缩放器 `x * [0x81088c] / 100`（全局缩放
   0x81088c，100=原大）。ui 包里同名按钮有 1x/2x 两套（如 00045-48 是 62×60、
   00113-116 是 32×31，主屏 x=747 按钮列两套都命中）——原版自带 UI 缩放档位。
3. **模块归属**：逐函数收集 push 的 GBK 字符串 + 索引立即数
   （全表见 `ui_index_map.csv`，函数级明细见仓库 CI 外的
   `_scratch/ui_func_strings.json`）。已确认：
   - 00009/00010 主角状态面板（等级/生命/法力/攻击/防御…）
   - 00122/176/177/235/240/242/247/252/254/260 洗炼·勋章·传承·装备升级·黑铁升级·符文吞噬·天赋转换
   - 00211 物品格子（通用）、00255 格子锁标志、00002/00004 通用底图
   - 00132/00134 焦点/选中标志（所有列表）、00193 已读标识
   - 00063/00183 列表行背景（排行榜等）、0061-64 排行榜行
   - 00006 通用窗口框/关闭钮（15+ 窗口共用）
   - 00191 转生/角色信息、00005 突破/重置/升级、004eaaab 段函数=强化/查看/激活
   - 00237/256 城池图（攻城战）、004fc956 段函数=摆摊（灵符/改价/登记装备）
4. **精确 x,y**：布局坐标经易语言方法描述符间接调用（`mov ebx,desc; call thunk`），
   完整静态还原需要描述符解析；经验替代 = 截图模板匹配（缩放 1.0 时 score=1.0
   精确命中，`_scratch/ui_tmatch_main.txt`）。逐页点开原版窗口截图匹配即可建
   完整位置表。

### 复现分析

```bash
python - <<'PY'   # 索引→函数→字符串扫描（依赖 capstone）
# 见 _scratch/ui_func_strings.json 的生成脚本；getter=0x41ee8a，字符串按
# push 0x64xxxx-0x7exxxxxx 引用 .rdata GBK 串
PY
```
