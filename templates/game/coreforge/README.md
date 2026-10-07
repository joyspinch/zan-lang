# 炉心边境 · COREFORGE

Zan 原生 2D 工厂自动化 + 塔防游戏：在外星荒原上开采铜铅石墨钛，
用传送带把矿石送进熔炉与装配机，维持炉心的电力与库存，抵御 20 波
进攻并完成科技树。灵感源自工厂自动化 + 塔防品类，全部代码、美术
（程序化绘制）、地图与数值均为原创，不使用任何第三方游戏资产。

## 运行

```bash
# App.html 是 HUD 设计稿，必须与 .zan 一起显式列进编译输入
build/zanc.exe templates/game/coreforge/src/main.zan templates/game/coreforge/src/App.html templates/game/coreforge/src/Interface.zan templates/game/coreforge/src/AutoTest.zan templates/game/coreforge/src/Model.zan templates/game/coreforge/src/Factory.zan templates/game/coreforge/src/Combat.zan templates/game/coreforge/src/Simulation.zan templates/game/coreforge/src/Persistence.zan templates/game/coreforge/src/Renderer.zan --auto-stdlib -o coreforge.exe
cd templates/game/coreforge && ../../coreforge.exe   # 从模板目录运行，./skins 皮肤才解析得到
```

窗口 1280x860（逻辑尺寸，随 DPI 缩放），世界即时渲染、失焦不暂停。
发布版用 `--publish --embed skins=skins` 把皮肤嵌进 exe，即可任意目录运行。

## 玩法循环

采集（矿脉钻头/取水泵/油井）→ 传送（传送带/分流器/管道）→ 加工
（电弧熔炉/晶格装配机）→ 电力（热差发电机/蓄电池/电力中继）→ 防御
（脉冲炮塔/苍穹散射/穿云狙炮/崩地臼炮/棱镜聚焦塔/合金墙）→ 波次进攻
→ 反击（铁蜂机巢组装无人机）→ 科技研究（热工学/晶格工程/弹道校准/
物流增压/炉心扩张/双管齐射/远程压制/攻城火力/纳米修复/铁蜂编队/
棱镜聚焦）→ 拔除敌巢、扩张新的远征据点。

防御纵深是核心课题：炮塔各有分工——脉冲均衡、散射双管速刷蜂群、
狙炮远程点名重甲、臼炮曲射溅射（无法对空）、棱镜持续光束灼蚀单体
（耗电极大）；敌军同样分层——3 波起混入重甲兽、5 波起混入自爆奔兽、
7 波起混入**飞行掠翼虫**（无视地形与建筑直线扑向炉心），没有对空火力
的防线会被撕开。研究纳米修复后全部建筑随时间缓慢自修。

### 反击战：拔除敌巢

地图三侧各有一座**敌巢**——它是该通道的出兵口。研究铁蜂编队后建造
**铁蜂机巢**（消耗铜铅硅自动组装无人机，战损自动补充，上限 24 架）：
守卫模式下铁蜂拦截逼近炉心的来犯敌人；切到**进攻**模式（工具栏
按钮），铁蜂群会飞向最近的敌巢啃咬——巢穴会拍死靠近的无人机，拔巢
有战损代价。摧毁一座巢即拔除一条出兵口：该通道从此不再出兵，并获得
铜铅钛重赏；三巢全拔，敌军永绝，只剩清完剩余波次即可凯旋。研究棱镜
聚焦还可建造**棱镜聚焦塔**，用巨额电力换取单点持续灼蚀的终极火力。

## 操作

- **鼠标左键**：放置/点选；**右键拖拽**：平移相机；**滚轮**：缩放；
- **WASD / 方向键**：移动相机；**R / Q / X**：旋转 / 取消 / 拆除；
- **空格**：暂停；**T**：科技面板；**1 / 2 / 3**：游戏速度 / 迎敌 / 回炉心；
- 顶栏/工具栏/目标/详情/科技/建造栏/状态栏/结算幕全部是 HTML+CSS
  声明式控件（`src/App.html` 设计稿 + `skins/coreforge/skin.css` 熔核
  皮肤），点击即用；小地图与边缘预警是画布上的游戏坐标热 HUD。

## 存档

自动存放在 `%LOCALAPPDATA%/Coreforge/expedition.json`
（Linux/macOS 为 `~/.local/share` / `~/Library/Application Support`），
可用环境变量 `COREFORGE_SAVE` 覆盖路径；HUD 的 保存 / 读档 按钮直接读写。

## 架构（src/）

| 文件 | 职责 |
|------|------|
| `App.html` | HUD 设计稿（GenForm 编译成 partial class CoreforgeHud 的类型化字段） |
| `Model.zan` | 目录数据（方块/配方/科技/波次）+ 世界状态 + 建造规则 + 特效池 |
| `Factory.zan` | 生产模拟：钻头/传送带/熔炉/装配机/电力/液体/无人机组装 |
| `Combat.zan` | 寻路、波次生成、敌人、炮塔/棱镜射击、无人机 AI、敌巢、击杀特效与屏震 |
| `Simulation.zan` | 固定步长模拟入口（Factory/Combat 编排 + 事件队列） |
| `Persistence.zan` | JSON 存档/读档 |
| `Renderer.zan` | 相机 + 世界渲染（地形/建筑/物品/敌人/粒子，全部程序化绘制） |
| `Interface.zan` | HUD code-behind：事件接线 + 每帧回写 + 世界输入遮蔽 + 小地图/边缘预警 |
| `main.zan` | 自有主循环：事件泵 → 固定步长模拟 → 世界直绘 → 控件树叠加 |
| `AutoTest.zan` | `COREFORGE_AUTOTEST=1` 交互自测（走真实 Input 与 form.Call 路径） |

## 验证

```bash
# 无头冒烟：跑 125 帧自建演示局并退出，打印 COREFORGE_GUI_PASS
COREFORGE_SMOKE=1 ./coreforge.exe

# 截图：第 120 帧写出 ZPX 帧缓冲（用 scripts/zpx2png.py 转 PNG）
COREFORGE_SMOKE=1 COREFORGE_CAPTURE=frame.zpx ./coreforge.exe

# 交互自测：建造钻头/传送带、点选炉心、暂停（含 HUD 按钮）、
# 缩放、平移、存读档、迎敌开战、HUD 按钮研究科技、铁蜂模式切换（17 步）
COREFORGE_AUTOTEST=1 COREFORGE_SAVE=/tmp/expedition.json ./coreforge.exe

# 逻辑无头测试（不开窗口）：地图生成 + 20 波模拟
python tests/templates/coreforge/verify.py
```

smoke/autotest 模式下桌面输入不转发进窗口，保证截图与断言确定性。
