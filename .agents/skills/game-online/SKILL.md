---
name: game-online
description: Zan 上做"客户端展示 + 服务端权威"网游模板（templates/game/* 客户端 + templates/server/server-* 服务端）的架构定式与全链路验证方法——发布内置单服务器入口、一个账号通进所有区、登录流页序（请登录/建号/修改密码/分区/选角/建角）、服务端表结构自动迁移、ORM 字面量查询铁律、原版素材恢复的盘点顺序、表单页 state-only 回调 + 轮询重建定式、UiDriver 端到端驱动（坐标点击/DPI 物理像素/探针断言/空闲停摆光标保活）。做或改网游类模板（legend、server-game 及后续同类）、补登录/分区/建角/好友等服务端功能、或提到 服务器地址配置/区服列表/建删角色/UiDriver 驱动卡顿时使用；纯单机帧循环游戏走 game-dev，迁移方法论走 app-migration。
---

# Zan 网游模板：客户端展示 + 服务端权威

> 提炼自迷你传奇复刻（templates/game/legend + templates/server/server-game）
> 登录流重构到建角页定稿的完整迭代。每条都是验证过的事实；被推翻的规则
> 当场删除（见文末迭代纪律）。参考 testing-zanide-uidriver（UiDriver 通用
> 陷阱）、game-dev（帧循环类）、app-migration（复刻不创造不阉割）。

## 1. 架构定式：服务端权威，客户端展示

- **一切数据与计算在服务端**：账号、角色、背包、战斗结算、挂机收益全在
  server-game；客户端只发意图（op 请求）+ 渲染推送。客户端不存任何可
  授予资源的本地状态。
- **发布内置单一服务器入口**：`data/server.txt` 首行 `host:port|gatepass`，
  客户端启动即自动连接（`ConnectPublished`）；`LEGEND_SERVER` 环境变量可
  覆盖（测试用）。登录窗不再有"自定义服务器"输入框——照原版，玩家只见
  账号密码。服务端网关校验 gatePass（config app.json 里配），不匹配直接
  断开（客户端报"服务器密码不正确"）。
- **一个账号通进所有区**：账号表全局唯一；角色按 realmId 隔离（模板每区
  一个角色，列表按多角色返回，放开多角色客户端不用改）。登录成功即拿全
  部区服列表（应答自带 realms 数组，含 grp 分组/expRate/dropRate/online/
  state），照原版停在「分区」选择页不自动进区。
- **协议**：一问一答严格按序（op JSON 进 FIFO，回复按序对号入座
  replySinks）；推送类走 chat/state。加 op = 服务端 dispatch 加一行 +
  客户端 Net 加一个带回调的方法。

## 2. 服务端 ORM / 迁移铁律（踩过坑的）

- **表达式树查询里只写字面量，不写局部串变量**：`Where(a => a.name ==
  "布衣(男)")` 稳；`Where(a => a.name == someLocalString)` 求值不稳。
  必须按变量查时拆成两个字面量分支（如按性别查起点装备）。
- **表结构演进用自动迁移**：启动时 `SyncStructureAllAsync()` 会给既有表
  补新列（Account.email、Realm.grp、Player.gender 都这么加的），种子数据
  用计数守卫（存在即跳过），别写手工 ALTER。
- **删号等危险操作先查在线**：`World.HoldsPlayer(playerId)` 命中即拒；
  删角色连背包一起删（同 accountId+realmId）。
- **自增主键不自动回填内存行**：`ExecuteIdentityAsync()` 返回 id 后手动
  `row.id = id`，会话与 GM 页都按 row.id 定位角色。

## 3. 原版素材恢复：先盘点，后裁图

- **先全量盘点资源包的序号资源**（data/*.pak 解包出的 ui/ 277 张、tupu/
  skill/show…），按序号猜语义前先问用户或对照原版窗口——本项目建角页
  头像就是 ui 包里的 00111-00116（顺序=战士男女、道士男女、法师男女，
  不按职业索引排），一开始却去 exe 内嵌表/Skin.dll 里大海捞针，最后从
  截图裁图，绕了一大圈被用户一句"不是有这组素材吗"点破。
- 用户截图裁图是**最后手段**：裁出的图带原窗底色、尺寸非标，直接摆会
  丑（缩放进框=变形+模糊+套底框）。要用就 1:1 原尺寸、无底框、选中才
  描边。
- exe 逆向找图多为死路：E 语言程序的嵌的资源常只有 JPEG/GIF 块与运行时
  字符串表，Skin.dll/dll 后缀可能是音频或加壳数据——别在这上面烧时间。

## 4. 登录窗表单页定式（stdlib/Gui）

- **按钮回调只置状态，绝不同步重建**：在 click 处理器里 RemoveAll 自己
  所在容器会弄坏派发链（重建出的输入框收不到键）。页面切换统一为
  `formPage = "xxx"`，由 120ms 轮询泵（PumpNetStage）比较 `formPage !=
  renderedFormPage` 后重建。
- **异步数据到达靠重建 key**：每页一个 key（修改密码页=问题+结果串，
  选角页=就绪位+数量+armDel，建角页=job+gender），key 变了才重建。
  key 要把"需要立刻反视觉反馈的状态"（如两击删除的 armDel）计入。
- **输入草稿跨重建保留**：重建会丢输入框内容；切页/点块前把
  `input.GetText()` 存字段，Build 时 SetText 回填（建角页名字草稿、修改
  密码页账号都是这么保的）。
- **换页必须清空，且清空只能放在 `RemoveAll()` 之后**：参考图的注册/改密/
  建角页输入框**全是空的**（框内墨像素实测 0），上一页的值带过去就是脏
  界面（用户当场点出「输入框都没清理肯定不对」）。坑在时序：`Input` 析构
  时会把编辑缓冲经 `data` 绑定**回写字段**（`Widget/Input.zan` 的
  `PushBinding`），先清字段再 `RemoveAll` 等于白清——必须在 `RemoveAll()`
  之后清。也别把清空挂在换页按钮的 `LgGo` 上：页序有**两条来源**（用户点钮
  走 `LgGo`，服务端状态推进由轮询泵直接改 `page` 字段），挂在
  「页真的变了」那一次（`page != renderedPage`）才两条都覆盖。
- **连接状态只在登录页显示**：泵把 net 状态写进提示行时，非登录页一律
  返回空——否则「服务器可达，请登录。」会从登录页漏进注册/改密的空区，
  看起来就是上一页没清干净。
- **提示行分页治理**：只有"动态页"（登录页=连接/服务器消息，建号页=
  注册结果）让泵改 netHint；其余页的提示是页面固定说明，泵一概不碰，
  否则本地校验消息下一拍就被聊天流冲掉。
- **图标块（职业/性别/头像）**：块=Panel.Column（Class 控制选中态边框，
  默认透明底无框）+ Image（原生尺寸或整数倍，"contain"）+ Label；点块
  一次选定多个维度（如职业+性别）比两层联动简单得多——用户原话：
  "6 张图，5 个按钮你怎么玩的"。
  **栅格要一行一个维度**：建角页按「2 行 × 3 列六格立绘」实现——**上行
  男、下行女，列序 战/法/道**，每格自带 `(职业, 性别)` 一对，点一格同时
  定下两者（用户拍板原话：「男女这样是不对的，还不如 6 个头像 一排男
  一排女 战法道」）。别做「3 职签 + 2 性别签」的两层联动：5 块既不是六宫格
  也不符合「选谁像谁」，且参考图里那 5 块本身就是原版的旧设计。
  选中态记 `(job, gender)` 两个 int + 一个 picked 位，重建 key 带上三者。
- **小窗口**：登录窗 440x520，进世界才 SetClientSize 长成主窗。控件
  Prefer 高度给足（输入 34-42、主按钮 44-46）。
- **窗口创建尺寸 = OS 最小拖拽尺寸**：`Win32Shell.CreateWindowSized` 把
  `min(800×dpi/96, 本次创建尺寸)` 记成 `minTrackW/H`，此后 `SetClientSize`
  **缩不到它以下**。按主窗尺寸开窗、登录期再缩 → 被钳在 1200×852，
  540×530 的登录页铺进 1200×852 画布、整页缩到 45%、**字全变小**（用户
  实测「怎么文本很小」就是这么来的）。定式：登录流**一开窗就是登录窗
  尺寸**，进世界只**长大**（放大不受下限约束）。
- **`AdjustWindowRect` 补偿会多出死边**：本壳 `WM_NCCALCSIZE`
  (0x0083, wp≠0) 已让客户区 == 窗口矩形，`ResizeClient` 再补一次就多出
  150% 下的 562×633（应 540×578）。要精确到设备像素走
  `SetClientSizeDev` → `ResizeClientExact`（设备像素直给、不做补偿）；
  逻辑像素入参在 150% 下还会先乘 1.5 再截断（578/1.5 = 385.33 落不到
  整数），故开窗逻辑值取向下取整、随后精确补那 1 设备px。
- **登录窗不做通用工具窗**：标题栏去掉**置顶**与**皮肤选择**
  （`SetChromeButtons(false, false, true, true)`）——原版登录器没这两样，
  皮肤抽屉还会盖住小窗；最小化/最大化/关闭保留，关窗是唯一退出通道。

## 5. UiDriver 全链路验证仪式（Windows 实机）

1. **launcher 保活**（必须）：Gui 事件循环空闲会停摆，注入事件以 ~3s/个
   爬行、`wait` 冻结。launcher 每隔 ~1.2s 在窗口内 `SetCursorPos` 轻移
   真实光标（不点击）保持出帧，事件即刻恢复即时处理。
   `_scratch/legend_launch3.ps1` 是可抄的模板——**纯 ASCII**，PowerShell
   5.1 把 BOM-less ps1 当 GBK 读，中文注释都会炸语法。
2. **坐标点击，别用数字 id**：控件 id 每次重建重新编号，几何在固定窗
   尺寸下稳定。先 `dump hitregions` 拿区域再换算中心点。
3. **坐标是物理像素**：150% DPI 下 = 逻辑值×1.5（ Prefer(108) 的块实测
   162 宽）；`dump pixels` 的 ZPX 尺寸也是物理像素（含 48px 标题栏偏移
   y0）。UI 布局代码里写的都是逻辑值。
4. **首击激活**：对未聚焦窗口的第一个注入点击只激活窗口，脚本开头先来
   一次中性 click。
5. **事件可能被吞**：页面重建瞬间注入的点击会丢（按序处理跨 RemoveAll）。
   关键导航点击"两击保险"（第二击落在下一页的等位控件时应无害——比如
   落在"创建并进入"上，空名会被服务端拒绝）。
6. **断言用探针**：应用每拍 `UiDriver.SetProbe("form", ...)` 发布表单状
   态（页名/职业/草稿/输入框现文），脚本 `dump probe form x.txt` 直接断
   言，比截图快且准。像素截图（ZPX 解码：24 字节头，BGRA，先转 RGBA 再
   存 PNG）用于最终视觉验收。
   **但探针是临时脚手架，验完必须摘**：`dump tree` 的 root `w`/`h` 就能
   判窗口形态（登录期 540×530、进世界 1888×945），不需要常驻探针；提交
   前把 `SetProbe` 删干净（本仓库只留出错上报那几条 `*-err`）。
7. **点击坐标要落控件真中心，不能落矩形下边**：矩形下边是**开区间**，
   点在边上命中不到任何控件（实测四行输入框全点空、输入一个字没进去，
   dump 差 0 像素）。逐框真中心 = `框顶 + 框高/2`。
8. **dump 是 BGRA、参考 PNG 是 RGB**：逐像素对比要换通道，否则整页差
   9%（假阳性）；换对之后结构差才落到 4–7% 的真实量级。
9. **UiDriver 的 click 坐标是画布坐标**（含 48px 自绘标题栏），内容区相对
   坐标要 **+48**；同一脚本里两套坐标系混用是最隐蔽的错（本仓库踩过：
   checkbox 用内容区 y、其余用画布 y）。
7. **`type` 进不了 CJK**：中文名验证用拼音/ASCII 代之，不阻塞流程。
8. 每轮验证 = launcher 起进程 + 脚本（wait→click→type→dump probe→
   redraw full→dump pixels）+ Python 解码截图 + 对照原版截图逐项过。

## 6. server-game e2e 回灌仪式（tools/e2e.py 会"过时"）

e2e.py 只在"区服上线/战斗闭环/排查轮"这类大提交里更新过；之后任何
改协议/改数值/改 op 回复形状的提交如果不回灌 e2e，它就会停在旧语义
上挂掉（这次连挂四处：proto-2 握手、say/walk 免回执、move 门槛
minLevel→BOSS 链、M2.DB 快照数值）。改了下面这些就要同步改 e2e 并
实跑一遍：

1. **改握手/加密**：hello 断言要跟着 Secure.zan 的握手走（现在连接
   建立即收 `{"ok":1,"proto":2,...}`，明文 op 在 requireEnc=0 时照常
   可用）。
2. **改 op 回复形状**：a68edeea 起 `state` 回 `{"ok":1,"self":...}`，
   `ev:"state"` 只在 GM 发奖推送时出现；walk/say 免回执，探闸门用
   有回执的 op。
3. **改数值来源**：M2.DB 快照（945d9a908）后怪物 hp/atk/exp、物品名
   （金创药→无、兽皮→无）、minLevel 全按快照走；e2e 里别再写死旧
   合成数值（maxhp 100 → 战士 140+20L）。
4. **改门槛逻辑**：换图门槛是 BOSS 链解锁（BossFight 胜利
   nextMap），e2e 要走正路挑战 BOSS——GM 直改库 mapUnlocked 会被
   会话内存权威的 tick flush（WriteRow 全列回写）覆盖，**测试绕过
   内存权威直改库必被回写吃掉**。
5. **跑前清环境**：fresh `rm data/app.db*`（进程占着就先停进程）；
   `_scratch` 跑服时确认端口没被别人占（wslrelay 会把 Windows 端口
   转发进 WSL，里面一个残留 server 就会随机接管连接——`Get-NetTCPConnection
   -LocalPort 7100 -State Listen` 看 OwningProcess，wslrelay 即有劫；
   并行会话在用这些端口就整体换 17100/18099）。
6. **GM 表单连发多条推送**：玩家编辑会触发多条 state/drop ev，
   断言前 `pending.clear()`，并用带终值/金币差值的谓词，别裸
   `ok_for(m: "self" in m)`——会匹配到积压旧消息。

## 7. server-game 多 worker 定式（2026-09-11，A268 实测入账）

架构=**HTTP 接入层水平扩 + 游戏世界单写者**（GameShared.zan 四张匿名
表，PermTable 模式：tokens/auth/sys/online）。改这一层的铁律：

1. **游戏 TCP 监听只在世界角色（1 号 worker）**：推送连接因此全部
   本地，World/Fight/Play 的几十处 `Gateway.Send` 零改动。
2. **跨 worker 中继走环回 TCP 控制通道，不走共享表轮询**（A265 的
   ops 表 2ms 轮询已被推翻：写表+轮询读+序号协议三段开销 2-6ms，
   换环回短连接后 RTT 0.1-0.3ms，8 并发登录 178→299-330/s）。世界
   进程从 `Cfg.Server.port+1` 起扫 64 个端口取第一个空闲（facade 无
   getsockname），实际端口写 sys 表 "rpc" 行作服务发现，调用方读表
   缓存、连接失败失效缓存重发现；每请求一条环回短连接、换行分帧
   紧凑 JSON，同一连接严格 FIFO 保序，两轮重试覆盖世界重启换端口。
   坑：`SendAsync` 返回值是**已发字节数**（可短写），`>= 0` 判成功
   = 帧被截断、对端永远等不到换行——必须全等比较，短写换新连接。
3. **RPC 协议字段单一来源**：A265 初版世界侧读行内 acc 列、调用方
   只写进了 req JSON——全部中继以 accountId=0 执行、中继登录把会话
   登记到账号 0（跨账号互顶），而世界 worker 本机直调的一半正常，
   极易误判偶发。
4. **共享表容量按「峰值日写入量 × TTL 天数」给足，写入返回值不可
   丢**：SharedTable 固定容量无扩容，表满后 SetInt 静默 false——
   tokens 表 8192 容量 × 10 天 TTL，36k 次登录压测打穿后新签 token
   全部解析不到而登录仍 ok=1（"登录已过期"假故障）。修法=容量给足
   （131072 ≈ 1.3 万日登录 × 10 天）+ 写失败让登录响亮失败。
5. **Windows 钳制 count=1**：master 按自己的 worker 表接受/分发连接，
   worker 多注册的游戏 worker 索引对不上；多 worker 仅 Linux。
6. **压测 ritual**：单 IP 每 op 5 次/5s 的匿名限流是登录吞吐的假顶——
   loopback 绑 `127.0.0.2..N` 源地址给每账号独立桶，sustained soak
   也要每请求轮转源 IP（固定 8 个 IP 各 18 rps 同样触发限流）；并行
   客户端数不要超过单进程 sqlite 池（poolSize=8，超了报"数据库不可
   用"= 池 fail-fast 非缺陷）；rm 掉服务端还开着的 sqlite 文件 =
   disk I/O error，先停进程再清库。基线（count=4）：8 并发突发
   299-330/s p50 23ms，60s 持续 ~200/s。**已知预存缺陷**：高并发突发
   有 bistable 停摆（13-24s 自愈，count=1 复现，根因=RecvAsync 轮询
   帧淹没就绪队列，见 TASKS A268）——吞吐数字要在"飞起"轮次取。
   soak harness 三坑：①threading.Lock 不可重入——推送机器人的 kick
   分支在锁内调带锁的 drop()，第一次重登就全 harness 死锁（所有线程
   停 futex，极易误判成 subprocess fork 死锁）；②多线程 python 里
   subprocess 采样小概率 fork 死锁，RSS/fd 采样用纯 /proc 读（status
   的 VmRSS + listdir fd 目录）；③推送机器人 attach 被拒要立刻断开
   重试并把 attach 回复逐条记日志——gm/聊天送达数一掉，先看 attach
   日志再看服务端。
7. **顶号/换会话必须「先登记后拆通道」**：EnsureHttpSession 顶号若先
   kick+Leave 再登记新会话，Leave 落库是一次几十 ms 的 DB 写——客户端
   收到 kick 当场重连 attach（零延迟），掉进「旧会话已摘、新会话未
   登记」的缝，attach 答「登录已过期」，这条推送连接从此静默聋（不
   报错、收不到任何推送，直到客户端自己的超时）。负载越高 DB 写越慢
   缝隙越宽（实测 12/12 必现）；无负载探针里前一步 HTTP RTT 恰好盖过
   缝隙，复现不出——缝隙类缺陷以时序为变量，压测是唯一的复现手段。
   修法=登记先行：建会话+byCid/byAccount 换绑+kicked 标记全部完成后
   才 kick+close+Leave，两步之间不放 await（单线程调度下外部看不到
   中间态），Leave 的 byAccount cid 守卫保证摘旧不误摘新。回归：e2e
   的「kick 后零延迟 attach」断言。

## 8. 客户端首帧时序与富文本聊天（2026-09-11 legend 实测）

- **首帧要有的内容，建壳前就绪，别挂第一个 Every tick**：`form.Every`
  的秒刻度在忙机器/冷启动上会被拖到数秒外（实测 7 秒只走 2 拍），挂在
  "第一拍再灌数据"上的内容会让首帧**时有时无**，回归验证随机翻车。
  legend 离线壳的种子聊天 `net.SeedDemoChat()` 原本排在
  `BuildShell()/Refresh()` 之后，已改为在分流（离线/连接）阶段、建壳前
  播好——启动第一帧（同步那次 Refresh）聊天区就有内容。凡是"打开就
  该看到"的数据都照此办理。
- **多色混排行（聊天/战况）用 `Gui.Widget.RichText`，别用 Label 拼**：
  `#H[聊天] #L[s31.名字]#W 正文` 这类标记一段流式排版逐字折行，颜色
  /链接跨行不断色（Label 拼不出）。正文里的 `#` 要先转义成 `##`
  （`EscRich`）；玩家名配色按名字散列从固定色板挑（原版聊天截图的
  配色）。控件排版惰性（下一帧才重建），断言行数要在渲染帧后读
  `LineCount()`；`LineCount()==-1` 是"尚未排版"哨兵。滚动模型仿
  StyledText：`bottomAnchor=true` 底端锚定，`scroll`/`maxScroll` 由
  调用方持有。

## 9. 经验迭代纪律（本项目约束，写进 AGENTS.md）

- 会话里**验证过**的新经验：当天并入本 skill（或对应 skill），随功能
  同一提交。
- 被**推翻**的旧经验：同一提交里删除，不留"可能也对"的僵尸规则。
- 每条规则必须带"为什么"（踩过的坑），没有坑的规则不配存在。
