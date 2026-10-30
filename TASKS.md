# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。发现缺陷、缺口或未决设计先登记编号；完成时必须有代码、conformance 用例和对应验证。状态：[ ] 未完成 · [~] 进行中 · [-] 已作废。
>
> 维护原则：本清单只记当前欠账（未修缺陷与未开工任务）；闭账即移出台账，不在台账保留 [x] 条目或验证流水，结论、测试证据与详细过程由 git 历史和提交正文承担。编号稳定不复用。
>
> 平账纪律：闭账条目当批移出，已闭环内容移至 git 历史。

## 未完成

### B-ID40 zanc 编译 `typeof(<struct>)` 段错误（管线收尾阶段，标准档 reflect_typeinfo 阻塞）

- [~] 现象：`TypeInfo tp = typeof(Point);`（Point 为 struct）编译期 SIGSEGV；
  class 无此问题。HEAD（f896826d3 切 TypeInfo 原生反射之后）即崩，与本轮
  工作树改动无关（stash 验证）。标准档 reflect_typeinfo /
  reflect_members / reflect_object_payload 三例被阻塞。
- 复现（`_scratch/rk_b.zan`，15 行）：

  ```zan
  using System;
  struct Point { public int X; public int Y; }
  class Program {
      static void Main() {
          TypeInfo tp = typeof(Point);
          Console.WriteLine(tp.Kind);
      }
  }
  ```

- 已钉死的事实：Zan 侧发射全部完成（ZANC_TRACE 证明 `typeof emit done`、
  全部 refl_meta_for 记录构建完毕，含 stdlib JsonScratch）；崩点在管线收尾
  （站点/泄漏表发射之后的 LLVM 优化或目标码/链接阶段），`-O0/-O1/-O3` 全崩，
  实例化 struct（`new Point()`）不豁免。工具链拿不到符号（lld 产物无 debug
  段，gdb/llvm-symbolizer 均无法定位到函数）——下一步：给 zanc 构建真正带
  上 DWARF（检查 CMake C flags 是否被 target 覆盖）后用 gdb 拿栈，或按
  f896826d3 对收尾阶段（emit_site_meta_table 之后的 LLVM 管线入口）逐段
  二分。关联：json_scoped_scratch 的 collect 路径 locals=NULL 段错误已在本
  轮修复（infer_method_tp_bindings 空守卫），勿混淆两个崩溃。

### B-ID41 空接口继承（`interface IA : IB {}`）运行时分派失败

- [ ] 现象：`interface IOrmRows : IRows {}` 这类空继承别名接口，实现类
  实现 IRows 后，经 IOrmRows 接口调用报 "interface dispatch has no
  implementation"——itable 构建只挂直接声明的接口，未沿继承链展开。
  ORM 里已绕开（全部直接用 IRows，别名壳已删），语言级缺陷仍在：任何
  用户代码写空继承接口都会踩。修法方向：itable 注册时沿 base interface
  链展开登记，加 conformance 用例锁行为。

