# Zan.Configuration

配置提供者链（纯 Zan，无原生依赖）。一个配置由**一组配置提供程序（provider）**级联组成：
根按**逆序**读取（**后添加的生效/覆盖**），每个 provider 实现
`TryGet(key) / Set(key, value) / Load() / GetKeys()`。

`using System.Configuration;` 按需拉入——命名空间由本包独立提供。

## 文件结构

| 文件 | 内容 |
|---|---|
| `ConfigurationBuilder.zan` | 链式构建器（`AddInMemory` / `AddEnvironmentVariables` / `AddJsonFile` / `AddEnvFile`），`Build()` 产出 `IConfiguration` |
| `ConfigurationRoot.zan` | 根配置聚合器：逆序遍历、`${VAR:-default}` 环境变量占位符解析、严格类型化读取、并发安全重载回调 |
| `ConfigurationSection.zan` | 配置小节视图（按 `:` 规范化前缀递归下钻） |
| `ConfigurationProvider.zan` | 配置提供程序通用抽象基类 |
| `IConfigurationProvider.zan` | 提供程序接口契约 |
| `IConfiguration.zan` / `IConfigurationSection.zan` | 配置与配置小节对外接口契约 |
| `ConfigurationKeys.zan` | 键空间规范化（`:` 与 `__` 等价，驼峰统一）与环境变量名映射 |
| `ConfigurationValues.zan` | 严格文本→数值/布尔解析（拒绝静默回退 0） |
| `MemoryConfigurationProvider.zan` | 进程内字典配置源 |
| `EnvironmentConfigurationProvider.zan` | 进程环境变量配置源 |
| `JsonFileConfigurationProvider.zan` | JSON 配置文件源（依赖 `Zan.Json` 的 `JsonDoc`） |
| `EnvFileConfigurationProvider.zan` | dotenv 风格配置文件源（支持引号转义与行尾注释剥离） |

## 快速使用

```zan
using System;
using System.Configuration;

IConfiguration cfg = new ConfigurationBuilder()
    .AddJsonFile("appsettings.json", true)
    .AddEnvFile(".env", true)
    .AddEnvironmentVariables("APP")
    .Build();

int port = cfg.GetInt("Database:Port", 5432);
string user = cfg.GetString("Database:User", "postgres");
```

## 键空间规范

- `:` 与 `__` 视为等价层级分隔符。
- 每段自动做首字母大写、其余小写的规范化映射（如 `database:port` 与 `DATABASE__PORT` 会落到同一个键 `Database:Port`）。
- 环境变量键使用 `PREFIX__SECTION__KEY` 规则自动绑定与剥离前缀。

## 依赖关系

- 核心配置体系零外部包依赖，仅依赖标准库。
- `JsonFileConfigurationProvider` 依赖 `Zan.Json`（`System.Json.JsonDoc`）实现无 DOM 分配扁平解析。
