# Zan.Json

JSON 契约层（纯 Zan，无原生依赖）。基础 JSON（`JsonValue` / `JsonReader` /
`JsonSerializer`）在 `src/System/Json/`（或同包统一由 `using System.Json;` 提供）；
本包补上契约层的三块：**JSON Schema 校验**、**JSONPath 查询**、**JSON Patch 变换**。

`using System.Json;` 按需拉入——基础 JSON 与高级契约统一在此命名空间。

## 实体与动态数据

已知结构使用 `Json.Deserialize<T>` 和 `Json.Serialize<T>`。绑定会复用类型的字段映射，并在解析工作区内填充实体。`JsonWriter.WriteEntity<T>` 使用同一映射，适合在一个对象或数组中直接写入实体。

只读动态数据使用 `JsonDoc`。数组可用 `FirstChild`、`NextSibling` 按顺序遍历。对象用 `FirstProperty`、`NextProperty` 遍历属性，`PropertyValue` 读取值槽位，`PropertyName` 返回可独立保存的键名，`PropertyNameView` 返回借用视图。属性枚举保留输入顺序和重复键，键视图只在文档及底层缓冲有效期间使用。修改树时使用 `JsonValue`。

反复查找同一个宽对象的属性时，用 `IndexObject` 建一次索引，再用 `Get` 按键取得值槽位。`IndexObject` 对非对象或无效槽位返回 `null`。索引查找命中第一个同名属性，缺失时返回 `-1`。

```zan
JsonObjectIndex index = doc.IndexObject(doc.root);
if (index != null) {
    int count = doc.IntAt(index.Get("count"), 0);
}
```

索引复用文档的键与槽位。使用期间保持属性表、键字节和槽位布局稳定，包括 `ParseBytes` 借用的源字节。作用域文档的索引在同一回调内使用。只查少量键时可直接调用 `JsonDoc.Get`，遍历全部属性时可使用属性游标。

含大量转义字符串且只读取少量字段时，可以用 `JsonDoc.ParseScopedDeferred`。

```zan
int count = JsonDoc.ParseScopedDeferred<int>(json, (doc) => doc.PathInt("count", 0));
```

解析器在回调前校验全部输入，并立即解码转义键。带转义的字符串值在首次访问时解码并缓存。请在调用线程的同步回调内通过 `JsonDoc` 访问器读取，文档和 `StringView` 的有效期都限于该回调。需要带出文本时，返回 `StrAt` 或 `PathStr` 得到的 `string`。`Count` 用于数组和对象，按需模式的原始槽会在访问时更新，`escPool` 按键的解析顺序和值的首次读取顺序填充。需要读取大部分字符串时，可使用即时解码的 `JsonDoc.ParseScoped`。

`MaxValueDepth()` 返回解析时最深值的深度，根值深度为 0。 作用域解析为最多 4 MiB 的源文本复用缓冲，超出时使用本次调用独占的缓冲。

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
