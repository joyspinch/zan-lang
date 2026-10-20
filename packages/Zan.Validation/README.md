# Zan.Validation

声明式校验（纯 Zan，无原生依赖）。`using System.Validation;` 按需拉入，
命名空间保留零破坏。

C# 参考 FluentValidation：规则链 + 错误聚合。**取值器在编译期绑定，
运行期零反射**——属性改名编译期就报错。

## 文件

| 文件 | 内容 |
|---|---|
| `ValidationPlan.zan` | 引擎：规则链（`ValidationPlan<T>`）、规则声明、错误聚合、嵌套校验 |
| `ValidationResult.zan` | 结果：`ValidationResult`（失败项列表 / 字典视图 / 文本视图 / JSON 视图） |
| `ValidationFailure.zan` | 单条失败：`ValidationFailure`（property / message / code / attemptedValue） |
| `ValidationContext.zan` | 上下文：`ValidationContext`（首错即停、属性名前缀） |
| `ValidationException.zan` | 失败即抛：`ValidationException`（携带原始结果） |
| `ValidationText.zan` | 文本原语：`ValidationText`（空/空白判定、通配符匹配） |
| `Validation.zan` | 门面：`Validation.Ok/Ensure/Validate` |

## 用法

继承 `ValidationPlan<T>` 就是校验器：

```zan
using System;
using System.Validation;

public class Signup {
    public string userName;
    public string email;
    public int age;
}

public class SignupValidator : ValidationPlan<Signup> {
    public SignupValidator() {
        this.NotEmpty("userName", s => s.userName, "is required");
        this.MaxLength("userName", s => s.userName, 16, "is too long");
        this.Matches("email", s => s.email, "*@*.com", "must look like an email");
        this.RangeInt("age", s => s.age, 18, 150, "must be 18..150");
        this.Must("age", s => s.userName != "admin", "reserved name");
    }
}

ValidationResult r = new SignupValidator().Validate(signup);
if (!r.IsValid) {
    Console.WriteLine(r.ToJson());
    // {"valid":false,"errors":[{"property":"userName","message":"is required","code":"NotEmpty"}]}
}
```

规则集也能独立使用（跨校验器复用）：

```zan
ValidationPlan<Signup> plan = new ValidationPlan<Signup>();
plan.NotEmpty("userName", s => s.userName, "is required");
ValidationResult r = Validation.Validate(plan, signup);
```

## 规则一览

| 类别 | 规则 |
|---|---|
| 文本 | `NotEmpty` `Required` `MaxLength` `MinLength` `LengthBetween` `Contains` `StartsWith` `EndsWith` `Matches` `EqualText` |
| 数值 | `RangeInt` `GreaterThanInt` `LessThanInt` `RangeDouble` |
| 布尔 | `True` |
| 任意 | `Must`（跨字段断言）`Nested`（嵌套对象） |
| 自定义 | `AddRule`（返回 `null` 表示通过） |

`Matches` 用的是内置通配符（`*` 任意长度、`?` 单字节），不引正则引擎：
校验里的模式都是"是否以…开头/是否形如 a*b"这类，写死匹配器既没有正则的
回溯风险，也没有编译开销。

## 错误视图

| 视图 | 用途 |
|---|---|
| `r.failures` | 结构化失败项列表（声明顺序，非字典序） |
| `r.ErrorsFor("userName")` | 按属性筛失败项 |
| `r.ToDictionary()` | 属性 → 首条消息 |
| `r.ToText()` | 逐行 `property: message` |
| `r.ToJson()` | `{"valid":…,"errors":[…]}`（走 `System.Json`） |

## 上下文

```zan
ValidationContext fast = new ValidationContext().FailFast();          // 首错即停
ValidationContext pre  = new ValidationContext().WithPrefix("signup"); // 属性名前缀
ValidationResult r = validator.ValidateWith(signup, fast);
```

## 约束与已知边界

- 规则取值器是 `FuncT<T, V>`：V 只能是编译期已知的类型（string / int /
  double / bool）。要跨类型断言用 `Must`（谓词拿整个对象）。
- 长度类规则按**字节**计（与 `string.Length` 一致）；需要按码点计用
  `RuneCount` 自行判断。
- 依赖型泛型基类（`class V<T> : Plan<T>`）目前编译器尚未支持特化，因此
  校验器直接继承 `ValidationPlan<T>`，不再多包一层 `AbstractValidator<T>`。
  详见仓库 `TASKS.md`。
