# Sdk.Jd —— 京东开放平台（JOS）客户端

`stdlib/Sdk/Jd` 是京东官方 `jos-net-open-api-sdk-2.0`（C#）快照的 Zan 版本。
当前覆盖原版全部 212 个 Domain 实体、211 个 Request、211 个 Response 和 44 个 API 模块；
普通调用直接返回强类型 Response，不需要调用方再次处理 `JdResponse.Data`。

## 快速开始

用生成的强类型接口（推荐）：

```zan
using System;
using Sdk.Jd;
using Sdk.Jd.Api.Ware;

class Program {
    static async void Main() {
        JdClient client = new JdClient("your-app-key", "your-app-secret");
        client.SetAccessToken("oauth-access-token");
        JdWareApi wareApi = new JdWareApi(client);

        WareReadSearchWare4ValidRequest request =
            new WareReadSearchWare4ValidRequest();
        request.SearchKey("钢笔").PageNo(1).PageSize(50);

        WareReadSearchWare4ValidResponse result =
            await wareApi.ReadSearchWare4ValidAsync(request);
        Console.WriteLine(Convert.ToString(result.page.totalItem));
    }
}
```

只 `using` 你要用的模块（`Sdk.Jd.Api.Ware`），编译器不会把另外 43 个模块拉进来。

失败一律抛 `JdException`，不需要检查返回值：

```zan
try {
    WareReadSearchWare4ValidResponse result =
        await wareApi.ReadSearchWare4ValidAsync(request);
} catch (JdException e) {
    Console.WriteLine(e.Code + " / " + e.Message);
    Console.WriteLine(e.Body);   // 原始报文，便于排查
}
```

## 接口层

由 `scripts/generate_jd_sdk.py` 从原版 C# 源码生成，改动接口请改生成器后重跑，
不要直接编辑生成的文件：

```text
stdlib/Sdk/Jd/Domain/        212 个共享业务实体（Sdk.Jd.Domain，每个 C# Domain 只生成一次）
stdlib/Sdk/Jd/Api/<模块>/    211 组方法 Request/Response 契约 + 44 个 Jd*Api 模块客户端
```

按模块分目录是必需的：auto-stdlib 以命名空间目录为单位整体拉取，所有文件挤在一个目录
会让任何用到它的程序全量编译。

`Domain/` 是共享实体层；`Api/<模块>/` 中的 Request/Response 只是方法传输契约。
多个接口引用同一个 C# Domain 时都指向同一个 `Sdk.Jd.Domain` 类型，不重复生成实体。
模块客户端负责执行、解包和反序列化，并把完整网关信封保存在 Response 的 `Raw` 字段。

每个 Request 类携带自己的 API 名，参数是强类型链式 setter：

```zan
WareReadSearchWare4ValidRequest request = new WareReadSearchWare4ValidRequest();
request.SearchKey("钢笔")
       .PageNo(1)
       .CategoryId(4294967296); // long，不截断大数

WareReadSearchWare4ValidResponse result =
    await wareApi.ReadSearchWare4ValidAsync(request);
```

类型映射：`Nullable<T>` 去壳；`DateTime` → `string`（网关本就发字符串）；
`Dictionary<K,V>` 和 `object` → `JsonValue`（仅保留原 C# 本来就是动态结构的字段）；
数组及 `List/IList/ICollection/IEnumerable<T>` 统一成 `List<T>`。字段按 `JsonProperty`
键命名。生成器对未知实体类型直接失败，当前 1887 个实体/响应字段全部完成类型映射。

## 不用生成类：直接发裸请求

要调用当前快照尚未收录或京东新上线的接口，可以显式使用底层 `JdRequest`：

```zan
JdRequest req = new JdRequest("jingdong.area.province.get");
req.Set("keyword", "北京").SetInt("pageNo", 1);
JdResponse resp = await client.ExecuteAsync(req);
```

响应同理——`Json.Deserialize<T>` 在编译期绑定，声明你关心的字段即可，
JSON 里多出来的键会被忽略，缺失的键保留默认值：

```zan
class MyResult {
    public List<Province> province_areas;
    public bool success;
}

MyResult r = Json.Deserialize<MyResult>(resp.Data);
```

不想定义类时，用 `resp.Value` 动态取：

```zan
string name = resp.Value.Get("province_areas").At(0).Get("name").AsString("");
```

## 请求参数

业务参数走 `JsonValue` 构建，不做字符串拼接——值里带引号、换行、中文都不会
拼出非法 JSON：

```zan
JdRequest req = new JdRequest("jingdong.ware.read.searchWare4Valid");
req.Set("keyword", "带\"引号\"的词")     // 字符串
   .SetInt("pageNo", 1)                 // 整数
   .SetLong("skuId", 4294967296)        // 64 位整数
   .SetBool("onlyValid", true);         // 布尔
```

嵌套结构用 `SetValue`：

```zan
req.SetValue("filter", JsonValue.Parse("{\"minPrice\":10,\"tags\":[\"a\",\"b\"]}"));
```

空串会被忽略（京东网关不接受空参数），与官方行为一致。

## 可选配置

全部有合理默认值，链式设置：

```zan
client.Timeout(15000)                                   // 超时，默认 30000ms
      .MaxRetries(3)                                    // 限流重试，默认 3；0 关闭
      .TimeZoneOffset(480)                              // 时间戳时区，默认 +0800
      .Server("api.jd.com", 443, "/routerjson", true);  // 网关地址（沙箱/代理）
```

默认指向正式网关 `https://api.jd.com/routerjson`。

时间戳（`timestamp` 协议参数，网关会拿它和自己的钟比对）默认按 `+0800` 生成，
墙钟时间和 `+HHMM` 后缀用的是同一个偏移，永远自洽。之所以要配置而不是读系统
时区：`DateTime.Now()` 是 `DateTime.UtcNow()` 的别名，标准库不做本地时区换算，
照搬原版 C# 的“取本机时区”会给每个请求打上 UTC 时间戳。跑在别的时区就改这个值
（印度 `330`、UTC `0`），机器时区本身不影响结果。

## 与 C# 原版的差异

| C# 原版 | Zan 版 | 原因 |
| --- | --- | --- |
| Domain/Request/Response 平铺 | 212 个共享实体 + 211 组方法契约 + 44 个模块客户端 | 实体只生成一次，模块按需加载 |
| `Nullable<long>` 参数属性 | 强类型链式 setter（`SetLong` 不截断） | 属性赋值无法校验，setter 可以按参数类型分派 |
| 参数校验/网关错误 → 返回字段被填好的空 Response | 抛 `JdException` | 原版调用方不检查 `IsError` 就会拿假数据继续跑 |
| `goto retry` + `Thread.Sleep` 阻塞重试 | `await Task.Delay` 退避 | 原版重试期间整个线程不可用 |
| 时间戳取本机时区 | `TimeZoneOffset(分钟)`，默认 480 | stdlib 无本地时区（`Now` 即 `UtcNow`），照搬会发 UTC 时间戳 |
| XML 分支（`format` 恒为 json，永不执行） | 删除 | 死代码 |
| 自造 `JdDictionary : Dictionary<string,string>` | 标准 `Dictionary` + `JdSign` | 只有签名规则是京东特有的，容器不是 |
| `StringBuilder` 手拼业务参数 JSON | `JsonValue` | 手拼在值含引号时产出非法 JSON |
| `Execute` 同步阻塞 | `Jd*Api` 强类型异步方法 | 自动解包/反序列化，Zan HttpClient 走 reactor |

签名算法与原版逐字节一致：参数按 key 升序拼成 `secret + k1v1k2v2... + secret`，
取 MD5 的大写十六进制，空 key / 空值不参与签名。
`tests/conformance/sdk_jd.zan` 里有对照向量。

## 组成

| 文件 | 职责 |
| --- | --- |
| `JdClient.zan` | 门面：协议参数组装、签名、发送、重试、信封解包 |
| `JdRequest.zan` | 请求描述：API 名 + 业务参数（JsonValue 构建） |
| `JdResponse.zan` | 响应：剥信封、识别网关/业务错误 |
| `JdSign.zan` | 京东签名规则（排序拼接 + MD5） |
| `JdException.zan` | 统一失败类型，携带错误码和原始报文 |
| `Domain/`、`Api/` | 生成的接口层（`scripts/generate_jd_sdk.py` 产出，勿手改） |

## 测试

```text
tests/conformance/sdk_jd.zan         签名向量、参数转义、信封解包、错误路径（离线）
tests/conformance/sdk_jd_client.zan  ExecuteAsync 全链路（本地 mock 网关，含限流重试）
tests/conformance/sdk_jd_api.zan     强类型模块 API 全链路（自动解包、64 位参数、共享 Domain 绑定）
tests/conformance/sdk_jd_modules.zan 44 个模块、211 组生成契约的完整编译面
```

mock 网关会用 `JdSign` 独立重算签名并比对，因此签名一旦写错测试立刻失败。

## 已知限制

两处实现受当前编译器缺陷约束，写法比自然形态绕，改前请先看对应的 bug 记录：

- `ExecuteAsync` 的重试退避不能写在 `catch` 块里——`await` 在 `catch` 内且外层是
  循环时，第二轮迭代会崩溃（`docs/bugs/await-in-catch-loop.md`），当前把重试
  决策提到了 `try` 之外。
- `JdResponse.Parse` 先判定再抛，失败原因经 static 字段传出——`throw` 不释放
  当前帧里活着的对象（`docs/bugs/throw-leaks-live-locals.md`），直接在持有
  JsonValue 树时抛会泄漏整棵树。

vendored C# 快照保留了 `IJdUploadRequest` 接口，但 211 个 Request 中没有任何类实现它，
因此本快照没有可迁移的 multipart 方法；后续升级快照若出现上传 Request，生成器会因未知
传输形态失败而不是伪装成普通 JSON 接口。

完整可运行示例见 `examples/sdk/jd_open_api.zan`。
