# 控件清单机制小设计（B-ID172①，替代手工注册梯子）

状态：设计稿待评审 · 2026-11-19 · 关联：B-ID171②（布局树金标=等价门）、
B-ID223（通用"声明→生成表"契约）、B-ID225 P2（AOT 生成建树消费同一张表）。
前置事实：管线探查报告 §1 查表段（ControlFactory/ControlBootstrap/UserComponentRegistry）。

## 1. 现状与裁定

注册梯子现状（事实）：
- `ControlFactory`（Core/Control/ControlFactory.zan:8-55）：静态
  `Dict<string, ControlFactoryFn> registry` + 精确命中后**大小写不敏感线性兜底**。
- 注册来源四路：`ControlBootstrap.Install/InstallDesign`（核心控件族）、兄弟包
  自注册（GraphView/DataTableBootstrap/WebViewBootstrap/Industrial）、
  `UserComponentRegistry`（用户组件）、TagToKind 归一（Build.zan:639-834）。
- 未命中落 `Element` 占位（Build.zan:374-381），kind 拼错**静默失效**。

2026-11-09 用户裁定：`Install()` 类注册方案属不可行历史方案，移除。

## 2. 机制：编译器按属性收集，生成 kind→工厂表

**通用机制（编译器核心，与包无关）**：编译器对"带注册属性的类型"自动收集，
生成一张 `kind 字符串 → 工厂委托` 的静态清单表，随 exe 发布。机制只认属性、
不认任何包（B-ID222 裁定"编译器永不点名包"的正面落法）：

```zan
// 属性定义归 Zan.Gui（包侧知识），机制归编译器（通用收集）。
[GuiKind("button")]                      // kind 串 = TagToKind 归一后的小写规范形
class Button : Control { ... }
```

- 编译器在 irgen/checker 已有的类型元数据面上收集 `[GuiKind]` 类型（与
  genmeta 反射元数据同一条产出链，genmeta.h:21-23 的 JSON→AST 回注先例证明
  这条链可用），生成：

```zan
static class __ControlManifest {        // 合成产物，勿手改
    static Control Create(string kind, bool caseless) { ... }
    // 精确表 + 小写规范键表：大小写不敏感查表从线性兜底变 O(1) 规范键命中
}
```

- 工厂 = `typeof(T).CreateInstance()` 既有反射通道（docapp 测试已实证字段
  初始化器在反射实例化下正确执行），表内不存 lambda、不新增 IL。

## 3. 语义变化（有意，逐条列出）

1. **miss 不再静默**：HTML kind 查清单未命中 → 显式报错（带"已注册 kind 列表"
   提示），替代 Element 占位。web 语义原语（div/span/p/img…）在 TagToKind 里
   归一到 `element`/`text`/`image` 等固定 kind，清单内置这组，行为不变。
2. **大小写兜底语义保留**：规范键表让 `heavy_caseless_lookup_test` 语义不变，
   查表复杂度降为 O(1)。
3. **设计态双入口**：`InstallDesign` 的差异面（设计器需要未实例化预览能力）
   改为清单表第二列（设计工厂），编译期同表生成。
4. **裁剪联动**：类型不在编译图内 = 清单无此项 = 该 kind 不可用——发布反射
   裁剪（B-ID218 面）与清单天然一致，不再需要 Install 侧的"防裁剪"登记。

## 4. 迁移步骤（每步独立可验证、可回滚）

1. 编译器加通用属性收集 + 清单生成（机制面，一次落地）。
2. Zan.Gui 核心控件族改属性标注，`ControlBootstrap.Install` 删除
   （`using Gui.Hmi` 随之消失）——金标：`heavy_caseless_lookup` +
   `gui_docapp` 布局树等价。
3. 兄弟包迁移（DataTable/WebView/Industrial/Charts…）：包内 Bootstrap 删除，
   属性标注随类型走；包不引用即不可用（与现有"引用才有"语义一致）。
4. `UserComponentRegistry` 改属性标注；文档半件 `data-zan-design` 的
   `AddFieldChild` 流式装配走同一清单。
5. `ControlFactory.registry`/`registeredKinds` 运行期表退役，查表直读清单。

## 5. 验收

- 全仓 grep 无 `Install(` 注册调用残留（gui_layer_rules 可加第七类棘轮锁退役）。
- 布局树金标等价（B-ID171② 的用例面）。
- kind 拼错用例：新 conformance 断言显式报错（替代静默 Element）。
- 体积对照：清单表 ≤ 原 Bootstrap 调用流的代码量（B-ID218 预算门禁口径）。

## 6. 与 B-ID223/B-ID225 的关系

- 本机制的"属性 → 生成表"是 B-ID223 契约（声明式源码生成）的第一个实例；
  B-ID223 定案时若选定更通用的钩子执行模型，清单生成迁移到该模型下，属性
  面不变。
- B-ID225 P2 的 AOT 生成建树代码直接消费本清单做 `new <Control>()` 直调，
  HTML kind 拼错从运行期报错前移为编译期诊断——两设计在此汇合。
