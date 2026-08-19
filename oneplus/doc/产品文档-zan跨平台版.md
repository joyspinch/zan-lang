# 群爆款优化神器 · zan 跨平台版 产品文档

> 版本：v1.0（2026-08-06）
> 基线：`D:/群爆款/群爆款优化神器2`（主程序）+ `D:/群爆款/BizanSoft`（API 库）+ `D:/project/zan-lang`（zan 0.2.1）
> 用途：zan 跨平台重写版的产品与开发基线文档。功能详单可配合本目录《业务功能结构导图-XMind.md》使用（本文档不重复其全部右键菜单明细，只归纳模式与差异）。

---

## 1. 产品定位

**一句话**：面向电商商家/代运营投手的多平台广告投放管理工作台 —— 用「多标签内嵌浏览器 + 多级数据列表 + 右键批量操作」的形态，把阿里妈妈/京东后台需要逐个点击完成的投放管理动作，变成按「计划组 → 计划 → 主体 → 关键词/词包/人群/资源位/创意」逐级下钻的批量操作。

**核心价值**：
- **批量**：官方后台一次改一个计划，本工具一次改几百个（改价、改预算、启停、增删、复制、克隆）。
- **本地缓存 + 报表**：把平台数据拉到本地 SQLite，列表秒开，并挂载花费/点击/转化等报表字段用于筛选决策。
- **浏览器与工具一体**：登录、滑块验证、后台页面浏览都在应用内多标签页完成，登录态自动桥接给 API 调用。

**覆盖平台**：

| 平台 | 广告产品 | 现状 |
|---|---|---|
| 淘宝/天猫（阿里妈妈） | 万相台无界：关键词推广(Search)、Display 推广体系（精准人群/货品运营/消费者运营/全站推广/多目标直投/超级短视频/超级直播） | 主力产品，成熟 |
| 淘宝辅助系统 | 达摩盘(人群)、生意参谋(取词)、素材中心/旺铺、创意中心 | 已集成 |
| 京东 | 京东快车（计划/单元/关键词/人群/商品/报表，与淘宝同构） | 半成品（API 已封装，客户端签名缺失） |

**目标形态（zan 版）**：Windows 首发，macOS/Linux 跟进；单一代码库覆盖淘宝+京东双平台，列表/批量操作引擎平台无关。

---

## 2. 产品总体形态

```
┌─────────────────────────────────────────────────────────────┐
│  主窗口 = 多标签容器（Tab 壳）                                  │
│ ┌────────────┬────────────┬───────────┬───────────────────┐  │
│ │关键词推广   │Display推广  │ 生意参谋   │ 万相台无界(网页)   │  │
│ │(业务模块页) │(业务模块页)  │ (网页)    │ …每个店铺/站点一Tab│  │
│ └────────────┴────────────┴───────────┴───────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│  业务模块页内部：左侧导航(列表切换) + 右侧列表工作台              │
│  ┌──────────────────────────────────────────────┐            │
│  │ 数据表格（虚拟化、万行级、报表列、筛选、排序、勾选） │            │
│  │   └─ 右键菜单 → 批量操作窗体(参数输入) → 进度窗      │            │
│  └──────────────────────────────────────────────┘            │
└─────────────────────────────────────────────────────────────┘
```

**核心交互范式（zan 版必须原样保留）**：
1. 列表数据**全部来自本地 SQLite**（先「同步」拉取），不是实时 API —— 保证离线秒开与快速筛选；
2. 每个列表挂**右键菜单**，菜单项 = 批量操作动作，按 BizCode（流量类型）与选中行动态显隐；
3. 批量操作统一流程：`勾选行 → 收集实体 → 参数窗体(校验/预览) → 进度窗(并发执行、逐条成败标记) → API 调用 → 回写本地 SQLite → 刷新表格`；
4. 「同步」= 分页并发拉取平台数据 → 清空重建本地表 → 清理孤儿数据，带 600s TTL 防重复拉取；
5. 多店铺：每店铺一个浏览器 profile + 一个独立数据库目录，切换店铺 = 登出清 Cookie + 重新登录。

---

## 3. 功能模块详单

### 3.1 顶部菜单结构（Form1 主壳）

| 菜单 | 子项 | 形态 |
|---|---|---|
| 推广软件 | 关键词推广、精准人群推广、货品运营、消费者运营、人群击穿等（Display 各 BizCode） | 业务模块页 |
| 快捷访问 | 淘宝首页、万相台无界、生意参谋、达摩盘、商品素材、商品素材新、导购素材管理、猜你喜欢 | 内嵌浏览器 Tab |
| 站点 | 千牛、天猫超市、天猫国际、阿里健康、淘工厂 | 切换 IndexType（决定登录页/注入逻辑/首页 URL，持久化于 IndexFile.txt） |
| 店铺工具 | 素材填充（商品素材/基础图片/旺铺素材填充） | 业务模块页 |

### 3.2 关键词推广（Search，BizCode=onebpSearch）

模块宿主 `One/Search/FormMain`，登录门控（IsLogin/IsDbCreated/IsAuth）后进入。

| 列表页 | 数据表 | 核心右键动作（类别） |
|---|---|---|
| 计划组列表 | OneCampaignGroupDao + 各级计数 | 新建/改名/删除计划组；同步；批量推广主体；报表(分时/分省/城市)；人群×8、关键词×5、词包×6、创意×7 类批量动作；计划设置×6；启停删 |
| 计划列表 | OneCampaignDao | 克隆/新建/改名/启停删计划；人群×14、关键词×5、词包×6、创意×7、设置×10；条件筛选；移动计划组 |
| 主体列表 | OneAdGroupDao + OneTaobaoItemDao | 人群/关键词/词包/创意批量动作（同上复用）；同步主体销量；营销场景；启停删 |
| 关键词列表 | OneKeyWordDao + OneMarketTrendsDao | 查询均价/质量分；改出价/匹配方式；导出；去重；排名卡位；删除 |
| 人群列表 | OneCrowdDao | 改溢价；启停删；导出 |
| 词包列表 | OneWordPackageDao | 改出价；启停删 |
| 创意列表 | OneCreativeSumDao + CommCacheImageDao | 创意组详情；复制素材；视频创意；替换素材；智能创意开关；改标题；一键修补素材 |
| 资源位列表 | OneAdzoneDao | 开启暂停 |
| 一键优化 | FormOptimize | 优化配置 + 优化计划列表 + 执行优化任务 |

### 3.3 Display 推广体系（多 BizCode 复用同一套代码）

覆盖业务：精准人群推广、货品运营、消费者运营、全站推广、多目标直投、超级短视频、超级直播。**按 BizCode 控制功能显隐**（如超级短视频隐藏一键优化/创意/资源位入口；全站推广主体入口改为全店）。

列表与 Search 同构，差异点：
- 人群体系不同：**定向人群**（含屏蔽人群、精细化调控人群 `FormCrowdTrimList`/OneCrowdDistinctDao）、关键词定向、系统人群、小二推荐人群、DMP 人群；
- 新增列表：**全站主体列表**（OneAdGroupSiteDao）、**精细化调控列表**；
- 计划设置多出：触达范围、冷启动、一键起量、多目标（FormCampMultiTarget）。

### 3.4 京东快车（同构，zan 版统一架构）

对象模型与淘宝一一对应：计划组/计划(campaign) → 单元(adgroup) → 关键词(keyword)/人群(crowd) → 商品(wares) → 报表(report)。BizanSoft `Api/Jdkc` 已有 13 个请求（CampaignList/Copy/DateUpdate/NameUpdate/StatusUpdate/UpdateBudget/ExistsName/KcCampaignAdd、AdgroupList、KeyWordList、CrowdList、商品列表×2）。

**现状缺口**：京东版客户端 `JztApiClient`（含签名）缺失，zan 版立项时需补齐 jzt-api.jd.com 的登录态与签名机制调研。

### 3.5 标题助手（CaptionPro）

流程：`取词（淘词 FormTaoCi / 生意参谋 FormCanMou，走 SycmApi）→ 过滤（FormFilter 规则）→ 组合生成标题（FormTitle）→ 覆盖查询（FormViewCover）`。本地表：SplitedWordDao（拆词）、SaleAnalysisResultDao（参谋数据）。含分词、去重、词根分析等纯计算逻辑。

### 3.6 店铺工具 · 素材填充（Shop）

`Shop/FormMain`：商品素材填充主流程、基础图片素材填充、旺铺素材填充；配套 FormStarb*（素材保存）、FormSucai、FormXiangqing。走 ShopApi（素材中心 sucai、旺铺详情 xiangqing.wangpu、Mtop H5 接口需 JS 签名）。

### 3.7 通用功能

- **主体黑名单**（FormBlackItem + BlackItemDownloader，与云端同步）；
- **账号保险箱**（FormLogin：AES 存储账号密码，双击注入登录页；非登录入口本身）；
- **表格配置持久化**：列布局/过滤规则存 CommGridFilterRuleDao，右键菜单挂「保存/应用/自定义过滤策略」；
- **风控滑块**（MouseSlider：命中 x5sec 验证时弹出 WebView 滑块页，通过后回调继续）；
- **错误上报**（异常 POST 到厂商后端）；**云端配置**（一键优化配置、表格配置、黑名单、类目均从 auth.lihuibox.com 拉取）。

---

## 4. 现状技术架构（C# 版，作为迁移对照基线）

```
WinForms + DevExpress 24.2 (XtraGrid 虚拟表格 / Ribbon / TabFormControl 页签壳 / 皮肤)
  │
  ├─ WebView2 内嵌浏览器（不是 CefSharp！Browser/ 目录为死代码）
  │    ├─ 每店铺一个 UserDataFolder（WV2Profile_N）
  │    ├─ 登录嗅探：WebResourceResponseReceived + DevTools 提取 csrfId/店铺信息
  │    └─ Cookie 桥：CoreWebView2Cookie → System.Net.Cookie → 静态 CookieManager
  │
  ├─ HTTP 层
  │    ├─ 门面 Api/OneApi(≈129方法) / DmpApi / ShopApi / SycmApi
  │    ├─ 客户端 Client/OneApiClient：附加头(Bx-V/Sec-Ch-Ua)、JSONP剥离、
  │    │   响应修补、风控状态机(滑块/403重登/703·704重试/FAIL_SYS_SNOW冷却)
  │    └─ BizanSoft：Request(URL/参数/JSON体) → Parser(JSON) → Entity(POCO)
  │         签名：Jint 执行混淆 JS（hash/dynamic token/mtop sign）★高风险
  │
  ├─ 数据层：FreeSql + SQLite
  │    ├─ 布局：cacheWj/data/{店铺NickName}v3/{表名}.db（每店铺每表一文件）
  │    │        + 全局 KvData.db（KvData/LoginAccount）
  │    ├─ PRAGMA：WAL / synchronous=NORMAL / 大缓存；自动建表
  │    └─ DbDamageDetector：按异常分级自愈（重建表/重建库）
  │
  ├─ 同步任务：≈20 个 FormNotify* 进度窗 + DynamicConcurrencyThrottler
  │    （并发度=风控动态调节的 processorOneCount；600s TTL 防重复拉取）
  │
  └─ 授权：WMI 机器码 + 卡密 + auth.lihuibox.com HTTP 校验
       + TouchSocket WebSocket 长连接（签名帧、心跳、close=杀进程）
       + 内存防篡改（随机数对校验）
```

**关键数据流**：
1. 登录：内嵌浏览器打开平台登录页 → 扫码/账号密码（可从保险箱注入）→ 嗅探响应确认登录 → 抽取 csrfId/token → 建店铺数据库 → WebSocket 上报登录；
2. 同步：点「同步」→ 进度窗 → 分页并发拉取（信号量限流+随机延迟）→ DeleteMany 清空 → InsertBulk → 清孤儿 → 写 KV 时间戳；
3. 批量操作：右键 → 参数窗 → 按批（如 20 个/批）调 API → 逐条标记成败 → 回写本地 → 刷新表格；
4. 风控：FAIL_SYS_USER_VALIDATE/x5secdata → 降并发 + 弹滑块窗 → 通过后继续；403 → 登出重登。

---

## 5. 数据架构（SQLite 表清单）

**布局规则**：每店铺独立目录 `{数据根}/data/{NickName}v3/`，每张表一个 .db 文件；zan 版建议保留该布局（便于店铺级备份/清理/损坏隔离）。

| 分组 | 表（DAO） |
|---|---|
| 全局 | KvData（KV+TTL）、LoginAccount（AES 密文账号） |
| One 广告对象(17) | CampaignGroup、Campaign、AdGroup、AdGroupSite、KeyWord、WordPackage、Crowd、CrowdDistinct、Adzone、Creative、CreativeSum、TaobaoItem、MarketTrends、Report(Hour/Province/City)、BlackWord |
| 公共(7) | CacheImage（商品图缓存）、GridFilterRule（表格过滤规则）、CampTask、CrowdCoverage、TaobaoItemImgUrl、Kv |
| Shop(4) | SucaiItem 等素材表 |
| CaptionPro(2) | SplitedWord、SaleAnalysisResult |

**ORM 现状**：FreeSql（自动建表、JSON 列映射、连接池、WAL）。zan 版对应：stdlib 自带 SQLite FFI + 轻量 ORM，DAO 接口面很小（Count/Find/Insert/InsertBulk/Update/Delete/Exists 十余个方法），可手写薄封装替代。

---

## 6. 外部接口清单（BizanSoft SDK）

| 平台 | 域 | Request 数 | 代表接口 |
|---|---|---|---|
| One 万相台 | one.alimama.com / bpcommon.alimama.com | ~115 | Campaign/AdGroup/KeyWord/Wordpackage/Crowd/Adzone/Creative/Solution 全套 CRUD + ReportGet + MemberCheckAccess |
| Dmp 达摩盘 | dmp.advgateway.taobao.com 等 | 21 | 人群列表/绑定/标签组/主题模板/画像分析/模板推荐 |
| Shop 卖家侧 | sucai / xiangqing.wangpu / h5api.m.taobao.com | ~21 | 素材中心、旺铺详情、Mtop H5（需 JS 签名）、AI 文案 |
| Soft 厂商后端 | auth.lihuibox.com | 28 | 授权/埋点/云端配置/滑块上报/黑名单 |
| Jdkc 京东快车 | jzt-api.jd.com | 13 | 计划/单元/关键词/人群/商品（同构） |
| ~~Subway/Sycm~~ | — | ~~79~~ | 已排除编译，死代码，zan 版不迁移（Sycm 取词若需要另行评估） |

**登录态携带方式**：4 个静态 CookieContainer（主站/One/京东/Soft），浏览器 Cookie 桥接注入；关键 token（`_tb_token_`、XSRF-TOKEN、`_m_h5_tk`、csrfId）从 Cookie/嗅探抽取后由请求构造器带入 URL 参数或头。

**签名机制（迁移最高风险项）**：
- 阿里妈妈 hash/dynamic token：Jint 执行内嵌混淆 JS（jsvm/gethash20201031）；
- Mtop sign：`token&t&appKey&data` 的 JS 算法；
- 备选方案：a) 逆向 JS 用 zan 原生实现（推荐，彻底摆脱 JS 引擎依赖）；b) zan 嵌入轻量 JS 解释器（FFI 接 QuickJS 等）。立项后第一周必须验证。

---

## 7. 授权与安全（zan 版需重做的部分）

| 机制 | C# 实现 | zan 版方案 |
|---|---|---|
| 机器码 | WMI（CPU/磁盘/MAC）SHA256 | 分平台硬件指纹：Win 可继续 WMI(FFI/COM)，macOS/Linux 用各自系统接口；或改为「安装 GUID + 可选硬件绑定」 |
| 卡密校验 | HTTP GetAuth + 防篡改随机数对 | 纯逻辑，直接平移 |
| 长连接控制 | TouchSocket WebSocket（签名帧+心跳+close 指令） | zan stdlib 有 WebSocket(WSS) 客户端，协议简单可重写 |
| 证书白名单/反代检测 | ServicePointManager 回调 + LOCALAPPDATA 标记 | 需在 zan HTTP 层补证书校验回调；路径换跨平台数据目录 |
| 账号保险箱 | AES 对称加密 | stdlib 加密全家桶齐备，平移 |

---

## 8. 非功能需求

- **并发**：同步任务并发度动态可调（风控降级时自动降低）；批量操作分批串行/小并发；图片下载 Semaphore(10) 后台轮询；
- **风控友好**：随机 UA、随机延迟、滑块验证通道、403 自动重登、"704 忙"退避重试 —— zan 版必须完整保留这套状态机（这是产品可用性的生命线）；
- **性能**：万行级表格虚拟化流畅（zan DataTable 已具备）；SQLite WAL + 批量 InsertBulk；图片缓存异步下载不阻塞 UI；
- **稳定性**：数据库损坏自愈（分级重建表/库）；全局异常捕获+上报；
- **多店铺隔离**：Cookie/数据库/浏览器 profile 三维隔离；证书异常全局停止请求。

---

## 9. zan 版技术选型对照（能力盘点，2026-08-06 实测）

### 9.1 已具备（可直接用）

| 需求 | zan 能力 | 依据 |
|---|---|---|
| C# 风格业务代码 | 类/接口/泛型/委托/事件/属性/async-await(CPS)/LINQ(急切)/try-catch/lock | docs/SPEC.md、TASKS.md A43 |
| 数据表格（核心） | `Gui/Component/DataTable`（9600 行）：虚拟化、排序/冻结列、Excel 式筛选表头、勾选列+全选、右键行菜单、就地编辑、CSV 导出、自定义渲染器、泛型绑定 `DataGrid<T>` | stdlib/Gui/Component/DataTable/ |
| 多标签/右键/导航 | Tabs(可关闭+右键)、ContextMenu、TreeView、Ribbon、StatusBar、分栏、向导 | stdlib/Gui/Widget/ |
| 内嵌浏览器 | WebView 组件：Win=WebView2(COM 直驱)、macOS=WKWebView；导航/Cookie 读写/JS Eval/**多 profile 隔离** | stdlib/Gui/Component/WebView/、examples/gui_browser |
| SQLite | FFI 绑定 + 7 平台预编译库 + 协程感知连接池 + ORM | stdlib/System/Data/ |
| HTTP+TLS | async HttpClient、自定义头、超时、断点续传；OpenSSL TLS（证书校验可开关） | stdlib/System/Net/ |
| JSON / 正则 / 压缩 / 加密 | 纯 Zan JSON、Regex、Zip/GZip、AES/RSA/SHA/HMAC | stdlib/System/ |
| WebSocket | WSS 客户端/服务端 | stdlib/System/Net/ |
| 京东广告 SDK | `stdlib/Sdk/Jd/`（含 Ads/Adwords/Dmp 广告 API，11 万行级）——若京东侧改走开放平台可直接复用 | stdlib/Sdk/Jd/ |
| 发布 | `zanc --publish` 一条命令自包含发布（自动收集各平台原生驱动） | docs/RELEASE.md |

### 9.2 部分具备（需包装或小改）

- **并发**：async/await 扎实，但无 `Task<T>`/`WhenAll`，Channel 是简易实现 → 同步任务的「分页并发拉取」需基于 async + 信号量手写（C# 的 DynamicConcurrencyThrottler 逻辑需重实现，约 100 行）；取消必须用协作式（C# 版 Thread.Abort 不可平移）；
- **库编译**：zanc 可产 DLL 但 IDE 无库项目消费路径 → 首期按单 exe 全源码编译组织；
- **事件绑定**：捕获 lambda/方法组绑定 2026-08 初刚修复 → 列表引擎绑定右键菜单时可能踩坑，垂直切片要覆盖；
- **Linux GUI**：X11 可用但粗糙、Wayland 无 → Linux 版延后；macOS 未真机验证 → 需真机测试。

### 9.3 缺失（必须自建，列入开发计划）

| 缺口 | 影响 | 建议方案 |
|---|---|---|
| **HTTP Cookie 容器** | 整个登录态体系的基石 | 自建 `CookieJar`：域/路径/过期匹配、多容器隔离（主站/One/京东/Soft 四个）、Set-Cookie 解析、与 HttpClient 管道集成。≈300-500 行 |
| **WebView 登录嗅探** | C# 版靠 WebResourceResponseReceived+DevTools | 确认 zan WebView 是否暴露响应拦截；若无，退化为 JS 轮询/注入脚本回传 csrfId 与店铺信息 |
| **C# 语法缺口** | 索引器/元组/模式匹配/Func<>Action<>/using语句/命名实参/多维数组均不支持；无反射 | 制定「迁移改写规范」：索引器→Get/Set 方法、Func<>→自定义 delegate、using→显式 Close、JSON→实体用代码生成而非反射。26 万行全部人工改写，无自动转换 |
| **原生系统对话框** | 文件打开/保存 | 自绘 FilePicker 已有，或 FFI 接各平台原生对话框 |
| **字符串语义差异** | zan string 是 UTF-8（C# 是 UTF-16），char 4 字节 | 涉及下标运算/互操作的迁移代码逐个重审（标题助手分词逻辑重点核查） |
| **Linux 浏览器引擎** | Linux 版无内嵌浏览器 | 首期 Windows 发布；Linux 版评估系统 WebView 或推迟 |

---

## 10. zan 版目标架构

```
qunbaokuan-zan/
├── app/                    # 入口、主壳（Tab 容器）、全局状态
├── shell/                  # 多标签浏览器、登录嗅探、滑块窗、站点切换
├── core/
│   ├── http/               # HttpClient 封装 + CookieJar（多容器）+ 风控状态机
│   ├── sign/               # 阿里 hash / mtop sign（原生实现）
│   ├── db/                 # SQLite 封装 + DAO 基类 + 损坏自愈
│   └── auth/               # 机器码、卡密、WebSocket 长连接
├── sdk/
│   ├── alimama/            # One(万相台) / Dmp / Shop 请求-解析-实体（声明式定义）
│   ├── jd/                 # 京东快车（优先评估复用 stdlib/Sdk/Jd）
│   └── soft/               # 厂商后端（授权/配置/埋点）
├── biz/
│   ├── grid/               # 列表引擎：DataTable 绑定 + 列配置 + 过滤规则持久化 + 右键菜单注册表
│   ├── actions/            # 批量动作框架：参数窗 → 进度执行器(限流/重试/逐条标记) → 回写
│   ├── sync/               # 同步任务框架（替代 20 个 FormNotify* 的复制粘贴代码）
│   └── models/             # 实体 + DAO（One 17 + Comm 7 + Shop 4 + CaptionPro 2）
├── modules/
│   ├── search/             # 关键词推广（8 列表 + 一键优化）
│   ├── display/            # Display 体系（BizCode 驱动显隐）
│   ├── jdkc/               # 京东快车（同构复用 grid/actions）
│   ├── captionpro/         # 标题助手
│   └── shop/               # 素材填充
└── resources/              # 皮肤、图片、JS 注入脚本
```

**架构决策要点**：
1. **请求定义声明化**：C# 版每个 Request 是 9 个方法的样板接口实现（159 个 Request ≈ 大量重复），zan 版改为声明式（URL/方法/参数构造器/解析器注册），预计 API 层代码量压缩 50%+；
2. **列表引擎与动作系统平台无关化**：右键菜单 = 配置表（对象类型 × BizCode × 动作），淘宝/京东共用一套渲染与执行框架——这是用户确认的两平台同构性的直接兑现；
3. **同步任务框架化**：C# 版 20 个进度窗是复制粘贴，zan 版收敛为一个参数化同步执行器（实体类型 + 拉取器 + 入库器 + 孤儿清理器）；
4. **风控状态机下沉到 HTTP 层**：滑块/403/703/704/降并发在客户端层统一处理，业务代码不感知。

---

## 11. 开发路线与里程碑

### 阶段 0 · 垂直切片验证（建议 2 周，立项门槛）
用 zan 重写一条最重的端到端链路，验证所有高风险点：
- 一个 WebView2 标签页打开万相台并完成登录嗅探（验证 zan WebView 拦截/JS 注入能力）；
- 自建 CookieJar 桥接 Cookie 到 HttpClient；
- 原生实现一个签名（先挑 mtop sign 或阿里 hash 之一做 PoC）；
- 同步「计划」到 SQLite（分页并发 + 限流）；
- 计划列表 DataTable（万行虚拟、报表列、勾选）+ 右键「修改日限额」批量动作（参数窗 → 分批 API → 进度 → 回写 → 刷新）；
- 产出：风险清单闭环结论（尤其签名与浏览器嗅探），通过则立项继续。

### 阶段 1 · 基建（~3 周）
HTTP+CookieJar+风控状态机、SQLite DAO 层+自愈、授权模块（机器码/卡密/Ws）、主壳 Tab 容器、账号保险箱。

### 阶段 2 · 浏览器与登录（~2 周）
多标签浏览器、多店铺 profile、登录嗅探与 Cookie 桥、滑块验证窗、站点切换。

### 阶段 3 · 列表引擎 + Search 业务链（~4-6 周）
DataTable 绑定层、列配置/过滤规则持久化、右键菜单注册表、批量动作框架、同步任务框架；完成关键词推广 8 个列表与全部批量动作。

### 阶段 4 · Display + 达摩盘（~3-4 周）
BizCode 显隐机制、定向/精细化人群体系、全站主体、DMP 人群添加链路。

### 阶段 5 · 京东快车（~2-3 周）
jzt-api.jd.com 登录态/签名调研补齐；复用列表引擎落 13+ 接口（或切换 stdlib/Sdk/Jd 开放平台路线）。

### 阶段 6 · 标题助手 + 素材填充 + 收尾（~3 周）
CaptionPro 分词/组合逻辑（注意 UTF-8 字符串语义）、Shop 素材接口、错误上报、云端配置、打包发布（Win 先行，macOS 真机验证后跟进）。

**总量预估**：单人全量约 4-5 个月；其中 UI 重建占比最大（C# 版 DevExpress 相关代码无设计器加速，全部手写重实现）。

---

## 12. 风险清单（按级别）

| 级别 | 风险 | 缓解 |
|---|---|---|
| 🔴 高 | JS 签名移植（混淆 JS 逆向） | 阶段 0 PoC；失败则 FFI 嵌 QuickJS |
| 🔴 高 | zan WebView 登录嗅探能力不足（无响应拦截） | 阶段 0 PoC；退化方案 JS 注入回传 |
| 🔴 高 | 平台风控策略变化（滑块升级等） | 保留浏览器兜底通道：风控接口退化时引导用户在网页 Tab 手动操作 |
| 🟠 中 | zan 语言 pre-1.0（一个月历史，bug 以天计修） | 锁定编译器版本 + 与 zan 开发同仓协同（本项目即 zan 的最大 dogfood） |
| 🟠 中 | C# 语法缺口导致改写量超预期 | 迁移改写规范 + 逐模块估算；索引器/模式匹配高频处优先 |
| 🟠 中 | 26 万行单一编译单元构建时长 | 关注 zanc 增量编译；必要时推动 A34-2/3 库消费路径 |
| 🟡 低 | macOS/Linux 平台成熟度 | Win 首发策略，Linux 浏览器引擎后置 |
| 🟡 低 | 源码与在产 DLL 漂移（Subway/Sycm 残留引用） | 迁移以实际调用面为准，不搬 csproj 排除的死代码 |

---

## 附录 A · 迁移时直接丢弃的死代码

| 位置 | 说明 |
|---|---|
| 主项目 `Browser/` 全目录 | CefSharp 遗留，未参与编译 |
| 主项目 `Db/CommDao.cs`（SqlSugar 版）、`Db/CommSqliteDaoAsync.cs` | 死代码/零子类 |
| 主项目 `TaskManager/CampTaskManage.cs` 任务体 | 已注释 |
| 主项目 `Api/ChuangYoApi.cs` | 空壳 |
| BizanSoft `Api/Subway/`(67 文件)、`Api/Sycm/` | csproj 已排除编译 |
| BizanSoft `Api/Hemaos/`、`Api/Service/` | 无调用方 |
| BizanSoft Common/Util 约 50 个零使用类 | WinInet/RSA/RegDomain/SnowflakeId 等 |

## 附录 B · 关键源码索引

- 主壳与入口：`Program.cs`、`Form1.cs`、`Controls/CoreWebView.cs`
- 登录/账号：`Forms/FormLogin.cs`、`Db/LoginAccountDao.cs`、`GlobalVariables.cs`
- 授权：`AppDefine.cs`（机器码/域名）、`Ws.cs`（长连接）
- API 门面：`Api/OneApi.cs`（≈129 方法）、`Client/OneApiClient.cs`（风控状态机）
- 列表样板：`One/Search/Camp/FormCampList.cs`（~3000 行，解剖基准）
- 批量动作样板：`One/Common/Camp/FormBudget.cs`（改日限额完整链路）
- 同步样板：`One/Download/FormNotifyCamp.cs` + `Forms/Notify.cs`（进度引擎）+ `Helper/DynamicConcurrencyThrottler.cs`
- 数据库基类：`Db/CommDaoAsync.cs`、`Db/DbDamageDetector.cs`
- SDK 基建：BizanSoft `Client/ApiClient.cs`、`CookieBoundHttpClientProvider.cs`、`CookieManager.cs`、`Client/HashGenerator.cs`、`Util/TmallH5ApiSignGenerator.cs`
- 功能详单：本目录《业务功能结构导图-XMind.md》

## 附录 C · 迁移改写规范（C# → zan 常见替换）

| C# 写法 | zan 写法 |
|---|---|
| 索引器 `this[int i]` | `GetAt(i)` / `SetAt(i, v)` 方法 |
| `Func<T,R>` / `Action<T>` | 自定义 `delegate R FuncTR(T t)` |
| `using (var x = ...) {}` | 显式 `x.Close()`（配 try-catch-finally） |
| 模式匹配 `is T x` | 类型判断 + 显式转换 |
| `(a, b)` 元组 | 小 struct 或 out 参数 |
| LINQ 查询语法 | 方法链（Where/Select/OrderBy，已支持，注意全部急切求值） |
| `yield return` | 支持但急切物化为 List，大集合注意内存 |
| `Thread.Abort()` | 协作式取消标志（CancellationToken 式） |
| 反射/attribute 序列化 | 代码生成或手写字段映射 |
| UTF-16 字符串下标运算 | 重审为字节/码点安全操作（string=UTF-8） |
