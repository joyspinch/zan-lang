# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：`[ ]` 未完成 · `[~]` 进行中 · `[-]` 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 `[x]` 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出

## 未完成

### B-ID126 [ ] 派生类内裸名访问继承静态字段静默读出 null
2026-10-08 在 Mvc 自限定调用重构的探针中发现。下列行存为 `_scratch/static_inherited_probe.zan`，`build/zanc.exe _scratch/static_inherited_probe.zan --auto-stdlib -o _scratch/static_inherited_probe.exe` 后运行：预期两行 `base`/`b:base`，实际实例方法内裸名 `Tag` 打印 `(null)`、静态方法内打印 `/b:base`（Tag 为空）——类型检查全程无诊断。

```zan
using System;

class Base {
    static string Tag = "base";
    static string Build() { return "b:" + Tag; }
}

class Derived : Base {
    static string Make() { return Build(); }
    void Inst() { Console.WriteLine(Tag); Console.WriteLine(Build()); }
    static void Stat() { Console.WriteLine(Tag + "/" + Make()); }
}

class Program {
    static void Main() { new Derived().Inst(); Derived.Stat(); }
}
```

边界：`this.Tag` 编译报 `'Derived' has no member 'Tag'`；同类内裸名静态字段正常；基类静态**方法**裸名调用正常——只有"基类静态**字段**的裸名标识符"这一形态坏：checker 把名字解析到了继承的静态字段，irgen 未按静态存储装载（疑似按 this 布局偏移取值），静默产出 null。修复方向：裸名解析到非本类静态字段时要么正确发射静态装载，要么像实例接收者访问静态字段一样报错要求类型名限定；补正反 conformance。Mvc 重构（62d7390e）的变换脚本以"只剥本类声明成员"从源头规避了该形态。

### B-ID124 [ ] `new object()` 条件表达式产生 ptr/i32 PHI 类型不一致
2026-10-08 在 nullable lock 回归夹具中独立复现；与 `AST_LOCK_STMT` 空值扫描修复无关，不在本轮扩展 irgen。将下列 8 行保存到 `_scratch/null_guard_lock_object_ternary.zan`，运行 `build/zanc.exe _scratch/null_guard_lock_object_ternary.zan --no-packages -o _scratch/null_guard_lock_object_ternary.exe`：类型检查通过，LLVM verifier 报 `PHI node operands are not the same type as the result`，指纹 `%tern = phi ptr [ %load3, %tern.then ], [ 0, %tern.else ]`。

```zan
class Program {
    static void Main() {
        object sync = new object();
        bool hit = true;
        object selected = hit ? sync : new object();
        lock (selected) { }
    }
}
```

源码根因：`src/compiler/irgen_expr.c` 的 `emit_expr_new_expr` 中 `new ClassName(args)` 分支只处理 TYPE_CLASS/TYPE_STRUCT；内建 `object` 未命中该分支或其他专用分支，落入 `LLVMConstInt(i32, 0)` 兜底。`emit_expr_conditional` 推断 PHI 为 ptr，而 `coerce_ternary_value`/`coerce_int_to` 没有将此整数分支转成 ptr，故 verifier 拒绝；这不是单纯 int 宽度错误（CLI 显示裸 `0`，源码确认 i32）。后续应修正内建 object 的实例分配/身份及条件分支类型契约，补正反臂与对象身份/锁行为 conformance，不能只转换常量 0 让 verifier 通过。本轮仅在测试里使用两个已有对象作为锁头分支，未改产品写法；日志 `_scratch/null_guard_lock_object_ternary.log` 可在清理后按内联探针重建。

### B-ID119 [~] GUI 控件级脏标记 + partialFrames 自动化
已落地主干（2026-10-08）：RenderTreeInner 每控件自段置换命令流累加器，Canvas 28 个绘制原语入口折 FNV 哈希，自段出口与上帧存档比对、条带未盖住即 NoteDamage 补画；哈希只数"会画出的命令"、与条带无关故整帧/条带帧存档可比，partialFrames 关闭的 App 每原语仅多一次静态布尔读。探针 E2E（悬停条带帧里改 label 文本不声明任何损伤，两帧内上屏、其余区域字节级不变）通过。尾差：blur 槽/快照/原生层走各自既有机制未入哈希；新增 Canvas 原语须记得补喂入口——B-ID118/121 新增的 blit_surface/fill_rects/fill_circles/fill_radials 当日漏喂、次日补齐（2026-10-08 契约对齐：批量逐条折叠 + blit 折几何与源 id；红/绿对照证明对现有消费方无可见行为变化——游戏循环每帧 RequestRedraw 整帧，哈希求值与 OnPaint 同生共死，此类漏喂在整帧流程里不可观测，靠台账规则与评审守住，不靠探针）。

