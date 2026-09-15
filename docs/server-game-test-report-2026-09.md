# server-game 全协议测试报告（2026-09）

对 `templates/server/server-game` 模板的**全部已支持协议**，从服务端功能、
客户端、安全性、稳定性、性能五个维度做的一轮全量检测。结论先行：
**功能全绿（e2e 125/125），安全 21 项通过 0 项失败，性能达到主流量级，
10 分钟敌意混沌 + 30 分钟混合协议 soak 零错误零停摆**。三个非阻塞观察项
与两个运行时/跨进程挂账（均已登记 TASKS）随附。

## 1. 测试对象与环境

| 项 | 值 |
|---|---|
| 被测 | templates/server/server-game，多 worker（count=4：HTTP 水平扩 + 世界单写者） |
| 构建 | zanc --target linux-x64，模板源 83 文件（与本报告同日提交 7b81696a 同源） |
| 宿主 | WSL2 Ubuntu，sqlite（poolSize=8），数据/日志内嵌 |
| 客户端 | Python 3.12（urllib/socket 原生帧）、Zan stdlib 客户端（HttpClient/TcpClient/GetHttpsAsync）、管理页 cookie 会话 |
| 复现 | 工具见 `templates/server/server-game/tools/`：`e2e.py`（功能门）、`sec_probe.py`（安全）、`perf_probe.py`（性能）、`chaos_probe.py`（稳定性混沌） |

## 2. 协议清单（代码盘点为准）

**模板已支持且本轮全测：**

| 协议 | 端口/路径 | 形态 |
|---|---|---|
| HTTP/1.1 页面 | :8099 `/` `/login` `/register` `/forgot` `/admin/*` `/static` | MVC + 静态文件 + cookie 会话 |
| HTTP JSON 游戏 API | :8099 `POST /api/game/<op>` | ~55 个 op，与 TCP 共用 `Gateway.Op` 分发；应答 `{"ok":1..}`/`{"ok":0,"err"}`（限流 429，会话失效 401） |
| TCP 游戏协议 | :7100 | 换行 JSON 帧；连接通告 banner（含 `proto`/`enc":"aes-gcm"`/`snonce`，hello 加密握手可选，默认 requireEnc=0）；`attach` 推送通道；`ev` 推送族（chat/walk/move/announce/kick/fight/levelup/drop/die/idlesum） |
| 环回 RPC 控制通道 | :8100 起扫描 | HTTP worker → 世界进程中继，换行 JSON 短连接，sys 表服务发现，两轮重试 |
| TLS 客户端 | 出网 | stdlib TlsStream（OpenSSL 原生），`HttpClient.GetHttpsAsync` |

**平台支持、本模板未路由（不适用本轮）：** WebSocket 升级（HttpContext 实现
RFC 6455，可同端口升级）、SSE、stdlib `HttpsServer`（服务端 TLS）、MySQL
驱动（配置了 sqlite）、MQTT/CoAP/Modbus/NTP/SIP/WebDav 客户端栈。

## 3. 服务端功能

功能门 = `tools/e2e.py` **125/125 断言 PASS**（fresh-db 起服）。覆盖：网页
注册/找回（三步+错答锁定）、TCP realms/register/login/characters/create/
enter、区服内 chat/walk/who、换区保状态、GM 区服 CRUD 与维护门、封号+踢线、
离线补偿推送、公告推送、打猎循环（经验/升级/金币/掉落）、商店买卖、药水、
武器穿脱、挂机结算、死亡回城、GM 送礼（在线推送+离线入账）、GM 改级夹血量、
满血拒药、DB 落库复核，以及 HTTP 会话推送通道四连（attach→relogin kick→
零延迟重挂→GM 广播送达）。

多 worker 行为在 soak/e2e 中验证：跨 worker 登录中继、token 跨 worker 解析、
世界单写者、顶号（kick 送达 + 通道拆线 + 立即重挂）。

## 4. 客户端

**Zan 客户端栈探针 6/6 PASS**（stdlib 客户端打本服务端，Zan↔Zan 全链路）：

| 断言 | 结果 |
|---|---|
| HttpClient GET `/` 页面（7.8KB） | PASS |
| HttpClient POST `/api/game/realms` JSON | PASS |
| HttpClient POST `/api/game/login` 拿 token（32 hex） | PASS |
| TcpClient 连接通告 banner | PASS |
| TcpClient attach → `{"ok":1,"push":1}` | PASS |
| GetHttpsAsync TLS 出网冒烟（example.org） | PASS |

Python 客户端侧即 e2e 125 断言（同版协议语义）。协议语义备忘：token 为
32 hex；`attach` 只把连接挂为推送通道，op 会话需本连接 TCP login/enter；
HTTP 与 TCP 的 op 分发同构，客户端可任选通道混用。

## 5. 安全性

`tools/sec_probe.py`：**22 PASS / 0 FAIL / 4 观察项**（提交版实跑）。要点：

| 类别 | 结果 |
|---|---|
| SQL 注入 | 5 种注入形态（UNION/DROP/注释/永真）login/forgot 全部按业务失败处理，无 500、无 SQL 回显（ORM 字面量绑定） |
| XSS 回显 | forgot/register 对 `<script>` 载荷不原样回显（模板转义） |
| 路径穿越 | `/static` 与根路径 6 形态（`../`、`..%2f`、`%2e%2e`、`....//`）均未泄漏 /etc/passwd 或 app 配置 |
| 管理面授权 | 无 cookie/伪 cookie 访问 4 个管理页全部被拦；未授权 POST `/admin/game/realms/save` 答 401 JSON |
| 会话 cookie | `HttpOnly; SameSite=Lax; Max-Age=864000; Path=/` |
| token 生命周期 | 伪造 64hex、空 token、单字符翻转 token 全拒；relogin 必换发新 token |
| 输入校验 | 3MB body（maxBodyMB=2）干净拒收；畸形 JSON 值不致 500；GET 打 POST-only API 得 405；注册密保问题长度校验（2-50 字）拒绝 1 字符载荷 |
| 反爆破 | 匿名端点限流（login/register/forgot/reset/realms/hb 按 IP 5 次/5s/worker）实测触发 429（25 连发 → 6×429）；authed op 按账号+op 3-30/s；密保答错 5 次锁定 1 小时（正确答案在锁定期内同样被拒，实测复核） |
| TCP 敌意输入 | 二进制无换行帧、2MB 单行（答"消息过长"）、非 JSON 行（答"请先 login"）、未知 op、未登录 op、20 连接洪水——全部干净处置，进程零崩溃 |

**观察项（非失败，生产部署须知）：**

1. 管理后台默认口令 `admin/admin1234` 可登录——模板演示预期，**生产首启必须改**（建议后续版本首启强制改密）。
2. **玩家 token 在 relogin 后不吊销**，旧 token 有效至 10 天 TTL（TokenBox 864000s）；管理端有 30s 版本缓存吊销机制，玩家 token 无按账号吊销。建议：登录 Issue 时吊销该账号旧 token，或给玩家 token 加账号版本号。
3. 口令爆破无账号级锁定（密保有锁定、登录没有），当前靠 IP 限流兜底；分布式爆破是残余风险。
4. 限流器为进程内 `RateLimiter.Owned`：多 worker 下单 IP 名义上限 = count×5/5s，且 keep-alive 连接钉住单 worker 后实际 ≈1×名义值——限流阈值按 worker 数取整的含义要有预期。
5. **偶发 admin 登录 302 无 Set-Cookie**（登录风暴后窗口内较易出现，间歇性、自愈，服务端审计日志每次都 ok=1，裸 socket 复核响应头 Set-Cookie 在场——排除了服务端缺陷，疑点在客户端栈或中间层，未定位）。管理功能本身在 e2e 与多数复测中稳定通过。

## 6. 稳定性

| 轮次 | 内容 | 结果 |
|---|---|---|
| 30 分钟混合协议 soak（同日早前） | 12 bot HTTP 全 op + TCP attach 推送 + admin 页 + GM 广播 + 聊天 fanout | ~2 万请求**零错误零停摆**；gm 57/57；kick=drops=180 配平；attached 12/12；RSS 122MB 平、fds ~239 平、p95 个位数 ms |
| 10 分钟敌意混沌（本轮，`tools/chaos_probe.py`） | 3 畸形帧线程（随机字节/300KB 大帧/拆帧）+ 3 RST 断连风暴线程（专用账号高频 relogin 顶号）+ 2 正常 bot 全程 + /proc 资源采样（116 样本） | 602s：正常 bot **1150 次操作 0 错误**、延迟 p50 2ms；四 worker RSS 13-24MB 平稳无斜率、fds 33-52 恒定；服务存活，混沌结束后新账号 register/login/create/enter 全流程正常 |
| 混沌后功能自检 | realms 存活 + 新账号全流程 | 双 PASS（见上） |

**混沌口径说明：** 断连风暴若与正常 bot 撞同一账号，bot 的报错只是「被顶号」
这一正确业务语义（首版探针实测：churn 3 次/s relogin 顶掉 bot 会话 →
466 次"错误"），修正版给 churn 独立账号后归零——这正是顶号语义正确性的
副作用验证。

**已知挂账（非本轮引入，均已登记 TASKS）：**
- **A268(b)** 高并发突发下协程就绪队列被 RecvAsync 1ms select-poll 轮询帧
  淹没，bistable 停摆 13-24s 自愈（count=1 复现，运行时层）；修向已给：
  RecvAsync 事件驱动化 + 池等待者预留交接。
- **A302** 跨 worker 读你写缝隙：TCP 注册 ack（INSERT 已同步）后另一
  worker 的 SELECT 短窗内（实测 22ms 起）查无此号；写侧已排除，读侧机制
  未定位，e2e 以「账号不存在」重试容忍。

## 7. 性能

count=4、无其他负载、单机回环（数值为该形态下的量级基准）：

| 项 | 结果 | 说明 |
|---|---|---|
| HTTP 页面 GET 吞吐 | **1400/s**，p50 5.3ms p95 10ms max 20ms | 8 并发 8s，~11,100 请求 0 错 |
| HTTP authed op 延迟 | p50 2.6ms p95 5.4ms max 7ms | `state`，8 账号轮转 |
| HTTP authed op 持续吞吐 | 188/s（0 错） | 8 客户端持续 10s，与 soak 的 ~200/s 一致 |
| 登录 E2E 吞吐 | ~8-12/s（8 源 IP，keep-alive） | 被反爆破限流钳制：5/5s/IP/worker，keep-alive 钉单 worker → ~1/s/IP；历史压测源 IP 大规模轮转时 299-330/s 突发 |
| TCP op RTT（世界进程直调） | **p50 0.1ms** p95 0.2ms max 0.3ms | 7100 连接 login/enter 后 `state`×200 |
| 同区聊天推送 fanout | **8/8 送达，mean 3ms max 3ms** | say → 8 attach 通道（`tools/perf_probe.py` P6；GM 公告推送同管线，e2e 断言覆盖） |
| 环回 RPC 中继 | 0.1-0.3ms | A268 实测（跨 worker 登录中继） |
| 60s 持续负载 | ~200/s | soak 口径 |

性能结论：HTTP 管线千级 rps、authed op 个位数毫秒、TCP 世界内操作亚毫秒、
推送 fanout 毫秒级——对"客户端展示 + 服务端权威"的网游模板是主流量级
余量充足；登录吞吐天花板由反爆破限流**有意**决定而非管线瓶颈。

## 8. 结论与建议

**结论：** 五个维度全部达到可发布水准。功能 125/125；安全 22/0（无注入/
穿越/越权/崩溃类漏洞，输入处理全部干净）；稳定（敌意输入与断连风暴下
零崩溃零错误，长时 soak 资源平稳）；性能（页面 ~1400/s、op p50 2-3ms、
TCP RTT 0.1ms、推送 fanout 3ms）。

**建议（按优先级）：**
1. 玩家 token 增加按账号吊销（relogin 时吊销旧 token，或账号版本号机制）——收掉 10 天旧 token 窗口（观察项 2）。
2. 管理后台首启强制改默认口令（观察项 1）。
3. 跟进 A268(b)（RecvAsync 事件驱动）与 A302（读你写缝隙）两个挂账。
4. 登录接口考虑账号级失败锁定（当前只有 IP 限流 + 密保锁定）。
5. 文档更正：README 提及的 `ev online` 推送在代码中不存在（只有 op 响应字段）。
6. 可选：限流器从 `Owned` 改跨 worker 共享语义，消除「count×名义值」的取整歧义。

## 附录：复现步骤

```bash
# 0) 构建部署（WSL）
build/zanc.exe @server_srcs --stdlib-path <repo>/stdlib --auto-stdlib \
  --target linux-x64 -o _scratch/wsl/server-game   # 83 个模板源文件
# 部署到部署根下的 app/server-game，pkill 旧进程 → 清 data/app.db* → 起服等 8099

# 1) 功能门（fresh db 必须）
python3 tools/e2e.py                     # 125 断言

# 2) 安全（e2e 之后跑，建自己的 sec_* 账号）
python3 tools/sec_probe.py               # SEC_SUMMARY fail=0

# 3) 性能
python3 tools/perf_probe.py              # PERF P1..P6

# 4) 稳定性混沌（600s，可调）
python3 tools/chaos_probe.py 600         # CHAOS bot_err=0 + 后检
```

注意：探针对 anon 限流敏感——串行跑、勿与其他压测并行；`e2e.py` 必须
fresh-db（断言含注册成功）。

## 9. 其余服务端模板抽测（2026-09-11 增补，A304）

延续本报告五维口径，对其余 7 个服务端模板做同轮抽测（构建 → 起服 → 功能门
→ 安全抽测 → 性能抽样 → 混沌），全部 linux-x64 侧起服。**结论：八模板全过，
无 FAIL 级缺陷。** 抽测前先修了 HEAD 编译器暴露的 6 处模板 type-check 错
（详见 TASKS A304）：四份 AppController 的 `__TxBegin/__TxCommit/__TxRollback`
override 摘掉 `protected`（对齐基类公开契约，`__Bind/__SetView` 同），mvc/collab
Users.zan 对可空的 `Cache()` 结果加 null 守卫。

| 模板 | 协议面 | 功能门 | 安全抽测 | 性能/稳定 |
|---|---|---|---|---|
| server-mvc :8099 (count=4) | ZanWeb MVC 页面 + JSON API + admin | 首页/admin 登录 302 发 cookie/7 admin 页 200/blog、users 页 200 | 未登录访问 302→login；405；路径穿越 404；3MB body 413；失败登录入 sys_login_log（30 失败+1 成功复核）；cookie `HttpOnly; SameSite=Lax` | 4 并发页面 ~1300/s；RSS 11-16MB |
| server-collab :8090 (count=4) | ZanWeb 多租户 admin（OA/CRM/ECBI/Flow/Wms） | 7 模块 admin 页全 200 | 同 mvc 栈 | **[Tx] 入库闭环**：submit→wms_stock_in 落行→inventory qty=5；RSS 13-15MB |
| server-licensing :8096 (count=4) | ZanWeb 授权后台 + 激活 API | 4 admin 页 200；`/api/index/ping` 0000 | activate 错码语义正确（0003 参数缺、2001 产品不存在） | RSS 10-12MB |
| server-iot MQTT :1883 + console :8080 | MQTT 3.1.1 broker（stdlib MqttBroker）+ HTTP console | CONNACK/SUBACK/PUBLISH 回显/PINGRESP 全过；console 登录发 64hex Bearer token；stats 计数正确（messages_in/out） | 敌意字节 RST 拒收、畸形 CONNECT 应答、badtoken 401 | PINGRESP RTT 0.039ms；双客户端 fanout 送达 |
| server-ws :8097 | RFC 6455 WebSocket 网关聊天 | 握手 101 + Sec-WebSocket-Accept 正确；双客户端广播 `[#N] text` 双端送达 | plain GET 服务索引页 200 | 广播 RTT 0.058ms/1000 ops |
| server-http :8083 | Worker 最小 HTTP 宿主 | `/ping` pong、`/` 模板页 | — | 8 并发 800 req 0.43s（~1900/s） |
| server-tcp :9000 | Worker 最小 TCP 回显 | welcome banner/echo | — | 64KB 单帧完整回显；RTT 0.035ms |

**稳定性抽样**：对 mvc 打 15s 混沌（二进制垃圾/30KB URI/RST 半包，22 连接）
后八服务全部存活、功能复测通过、mvc RSS 平稳（11.2MB）、日志无异常增长。

**与 A303 同源的观察项**：管理后台默认口令（mvc/collab/licensing 的
admin/admin1234、iot console 的 admin/admin）生产首启必须改；限流/会话语义
与 server-game 同一 ZanWeb/Worker 栈，A303 的建议同样适用。

**模板修正**：server-http/src/main.zan 示例端口 8080→8083（与 iot console
撞端口；两模板 README/示例本就各自选端口，8083 消除冲突）。

**复现口径**：探针为一次性 Python 脚本（MQTT 最小 3.1.1 客户端、RFC 6455
握手+掩码帧、Worker HTTP/TCP 直连），未沉淀进模板 tools/——七模板探针逻辑
各不相同且体量小，口径已录 TASKS A304，需要时按本表逐项重打即可。
