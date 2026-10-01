# Zan.Commercial - 商业软件授权与授权分发客户端 SDK

> **成熟度：实验性** —— 尚无 conformance 测试锁与仓库内真实消费方；接口
> 可能调整，生产采用前请自行评估并补对拍。

`Zan.Commercial` 是 Zan 语言官方商业软件授权客户端模块，为桌面客户端、商业工具及 C 端成品软件提供开箱即用的软件授权接入能力。

本模块与官方授权服务端模板（`templates/server/server-licensing`）天然配对，仅需数行代码即可完成商业软件的授权校验、激活与心跳保活。

---

## 核心特性

- **四种主流授权模式**：
  1. `account` —— 账号密码/令牌登录授权（按用户管理，支持席位限制）。
  2. `code` —— 激活码/序列号激活（按码授权，支持设备指纹绑定）。
  3. `signed` —— 离线签名证书授权（基于非对称加密验签，全程无网环境可用）。
  4. `online` —— 在线动态会话模式（支持心跳续期与掉线宽限期 graceSeconds）。
- **设备指纹感知**：自动采集并匹配机器特征指纹，防范一码多机盗版。
- **本地持久化与防篡改**：授权状态加密保存在用户目录（`~/.zan-license/<product>/license.json`）。
- **非阻塞与高健壮性**：网络故障、超时统一返回结构化结果对象，不中断程序正常退出流程。

---

## 快速上手

### 1. 基础授权检查（推荐程序启动时调用）

```zan
using System;
using System.Commercial;

class Program {
    static async void Main() {
        LicenseClient lic = LicenseClient.Shared();
        lic.Server("https://license.example.com", "MyCompany");
        lic.Product("my-commercial-app");

        // 异步检查当前设备授权状态
        LicenseCheckResult result = await lic.Check();
        if (!result.ok) {
            Console.WriteLine("未授权或授权已过期: " + result.message);
            Console.WriteLine("错误代码: " + result.reason);
            // 引导用户输入激活码或登录
            return;
        }

        Console.WriteLine("授权有效！到期时间戳: " + result.expiresAt);
        // 启动主程序界面...
    }
}
```

### 2. 激活码激活（Code 模式）

```zan
LicenseClient lic = LicenseClient.Shared();
lic.Server("https://license.example.com", "MyCompany");
lic.Product("my-commercial-app");

// 绑定用户输入的激活码
string activationCode = "ABCD-EFGH-1234-5678";
LicenseCheckResult r = await lic.ActivateByCode(activationCode);
if (r.ok) {
    Console.WriteLine("软件激活成功！");
} else {
    Console.WriteLine("激活失败: " + r.message);
}
```

### 3. 在线心跳维持（适用于 SaaS / 按并发在线席位计费）

在程序后台定时器或心跳协程中静默保活：

```zan
LicenseCheckResult pingResult = await lic.Heartbeat();
if (!pingResult.ok && pingResult.reason == "seat_limit") {
    // 超过并发终端限制，提示用户在其它设备下线
    Console.WriteLine("当前账号在其他设备登录，当前终端已退出。");
}
```

---

## 错误代码（`reason`）一览

| 错误代码 | 含义 | 建议动作 |
| :--- | :--- | :--- |
| `no_license` | 本地无任何授权信息 | 提示用户输入激活码或登录购买 |
| `expired` | 授权已过期 | 引导用户续费 |
| `device_mismatch` | 设备指纹不匹配 | 提示在原绑定设备解绑或联系客服换绑 |
| `seat_limit` | 并发在线席位数超限 | 提示退出多余终端或升级套餐 |
| `network_fail` | 网络连接失败且超出宽限期 | 提示检查网络 |
| `server_reject` | 授权被服务端撤回或拉黑 | 提示联系管理员 |
