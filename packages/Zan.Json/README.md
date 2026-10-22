# Zan.Json

JSON 契约层（纯 Zan，无原生依赖）。基础 JSON（`JsonValue` / `JsonReader` /
`JsonSerializer`）在 stdlib `System/Json/`；本包补上契约层的三块：
**JSON Schema 校验**、**JSONPath 查询**、**JSON Patch 变换**。

`using System.Json;` 按需拉入——命名空间与 stdlib 同名，两边合并。

## 文件

| 文件 | 内容 |
|---|---|
| `JsonSchema.zan` | schema 入口（`Parse` / `Of` / `Validate` / `IsValid`）+ 校验结果与错误项 |
| `JsonSchemaValidator.zan` | 校验器（draft 2020-12 子集） |
| `JsonPath.zan` | JSONPath 解析与求值 |
| `JsonPatch.zan` | RFC 6902 补丁应用器 |
| `JsonPointer.zan` | JSON Pointer（RFC 6901）拆/拼与转义 |
| `JsonEqual.zan` | JSON 值深度比较（数字按数值比） |

## JSON Schema

```zan
using System;
using System.Json;

JsonSchema schema = JsonSchema.Parse(
    "{\"type\":\"object\",\"required\":[\"name\"],"
    + "\"properties\":{\"name\":{\"type\":\"string\",\"minLength\":2},"
    + "\"age\":{\"type\":\"integer\",\"minimum\":0}},"
    + "\"additionalProperties\":false}");

JsonSchemaResult r = schema.Validate(JsonValue.Parse("{\"name\":\"a\"}"));
if (!r.IsValid()) {
    Console.WriteLine(r.Message());   // /name: string is shorter than minLength [minLength]
}
```

支持的关键字：`type` / `enum` / `const` / `required` / `properties` /
`patternProperties` / `additionalProperties` / `items` / `prefixItems` /
`minItems` / `maxItems` / `uniqueItems` / `minimum` / `maximum` /
`exclusiveMinimum` / `exclusiveMaximum` / `multipleOf` / `minLength` /
`maxLength` / `pattern` / `allOf` / `anyOf` / `oneOf` / `not` / `$ref`。

边界：`$ref` 只解析本地指针；`$ref` 与同级关键字并存时只按 `$ref` 校验；
递归 schema 按实例深度限 64 层；字符串长度按码点数算。

## JSONPath

```zan
List<JsonValue> hits = JsonPath.Query(doc, "$.store.book[?(@.price > 10)].title");
JsonValue one = JsonPath.First(doc, "$.store.book[-1].title", null);
bool has = JsonPath.Exists(doc, "$..isbn");
```

支持：`$` 根、`.name` / `['name']`、`[*]`、`[0]` / `[-1]`、`[start:end]`、
`..name` / `..*`、`[?(条件)]`、逗号并接。条件支持 `@.field` 存在性与
`== != > >= < <=`（数字与字符串）。路径写错抛 `ArgumentException`。

## JSON Patch

```zan
JsonValue patched = JsonPatch.Apply(doc,
    "[{\"op\":\"replace\",\"path\":\"/a\",\"value\":9},"
    + "{\"op\":\"add\",\"path\":\"/b/1\",\"value\":7}]");
```

支持 `add` / `remove` / `replace` / `move` / `copy` / `test`，路径用 JSON
Pointer（`"-"` 表示数组末尾追加）。补丁应用在**副本**上并返回新文档，
原文档不动；失败抛 `JsonPatchException`。

## 依赖

`pattern` / `patternProperties` 用 `System.Text.RegularExpressions`
（`Zan.Text` 的正则引擎）；字符串长度用 stdlib `System.Text.Encoding`。
