# 自举后端的运行期类标识（selfhost runtime class identity）

适用：改 `src/selfhost/irgen*.zan` 里对象头、字符串头、数组头、类型判定
（`x is T` / switch 模式）相关代码。

## 1. 为什么需要

C 宿主用「分配点 site id + 祖先名表」做运行期类型判定
（`src/compiler/irgen_expr.c` 的 `emit_runtime_is_check`）。自举 IR 后端原来
没有任何类身份：`object` 槽里的值在运行期无从判断是哪个类、是不是字符串，
于是 `case Dog d`（打在 `object` 上）、`o is Dog d2` 只能**拒绝编译**
（fail-closed 诊断）。

现在有了：对象头里放**类标识**，类型判定在运行期问对象头。

## 2. 统一 24 字节头

判定要读「值头里的类标识槽」，所以**凡是自举自己分配的堆值，头都是 24 字节**，
读类标识槽永远不越界：

```
对象（zalloc）:  [ i8* 类标识 ][ i64 引用计数 ][ i8* 析构器 ]   p-24 / p-16 / p-8
字符串（zstr_new）: [ i64 类标识(0) ][ i64 长度 ][ i64 magic ]   payload-24 / -16 / -8
数组（calloc）:   [ i64 类标识(0) ][ i64 长度 ][ 元素槽... ]     value-24 / -16
```

- 对象的类标识 = 该类 `@__tid_X` 全局的**地址**（catch 匹配走的就是这条链）。
- 字符串/数组的类标识恒为 0 → 判定时不会误判成类实例。
- **字符串头的长度/magic 相对 payload 的偏移没变**（仍是 -16 / -8），
  `zstr_len` / `zstr_clen` 不用改；只是块前面多了一个槽（`zstr_new` 的
  分配量 `len+25`，字面量头多 8 字节，取值偏移从 +16 变 +24）。
- **数组的长度仍在 value-8**（元素起点从 hdr+1 槽变成 hdr+2 槽），
  读长度的三处（`Length` 属性、foreach、byte[]→string）不用改。
- `zrelease` 释放的是 **p-24**（块首），不是 p-16 —— 改头大小后这里最容易漏。
- 元组 / 装箱值 / 闭包 env 是裸 `malloc`，**没有头**：判定时读到的是 malloc
  头（越界读），不会匹配任何 tid → 判 false。实践上无害，但别依赖它。

## 3. 判定怎么算

- `__zan_class_id(obj)`：null → null；否则读 obj-24 的 i8*。
- `__zan_is_string(obj)`：null → 0；否则比较 obj-8 的 i64 与字符串 magic
  （1515082580）—— 类实例的 obj-8 是析构函数指针，两者不会撞。
- 类判定用**闭世界比较**：拿类标识与「所有派生自被测类型的类」的
  `@__tid_X` 地址逐个比（`IsSubclassName(cn, tt)`）。只比较**确实发射过
  `@__tid_X` 全局**的类（`tidNames` 列表），否则 IR 会引用不存在的全局。
- 原来用的是 vtable 比较，只能对付多态类；现在类标识对普通类也管用，
  vtable 比较退场（`EmitVtableTestVal` 成了死代码）。

## 4. 踩过的坑（都真实发生过）

- **类型测试的返回值契约是 i1，不是 i64**。switch 模式的值直接当分支条件用
  （`br i1 <test>`），而老代码返回的是 i64（zext 过的 0/1）。以前每个模式
  路径都是 fail-closed 诊断，这个类型错配一直被掩盖；一旦放行就变成
  「clang rejected the emitted IR: '%t5' defined with type 'i64' but expected
  'i1'」。现在用 `Bool1(i64→i1)` / `Bool64(i1→i64)` 两个helper区分
  「分支条件」和「bool 值」。
- **`x is T name` 的绑定变量类型**要按 `MapType(e.type)` 算，不能拼
  `"%" + tname + "*"`：`string` 不是 LLVM 结构体类型，拼出来是
  「use of undefined type named 'string'」。
- **把引用存进类类型的槽要补 bitcast**。`Coerce` 原来只在「类指针→类指针」
  之间转；`object`（i8*）存进 `%Circle*` 槽时它原样返回，IR 变成
  `store %Circle* %i8*value` —— **clang 对 .ll 输入默认不跑 verifier**，
  非法 IR 静默进后端，表现是 0xC0000005，而不是编译错误。
  现在规则是「源是指针且目标是类指针就 bitcast」。
- **`object` 槽也拥有它持有的引用**。`object` 的静态类型不是 ARC 类型，
  按老规则 `object o = new Dog();` 不会 retain，语句末尾的临时值释放直接把
  对象释放掉 → 后面用就是 use-after-free → 堆损坏（0xC0000374）。
  现在用 `EmitArcRetainStore(值, 槽类型, 值类型)`：**按值的类型**判断要不要
  retain（字符串没有引用计数，绝不能 retain）。代价：`object` 槽只在存入时
  retain、从不释放 → 对象会泄漏（自举的字符串本来就不参与 ARC，一致）。
- **switch 表达式的模式绑定要在 arm 结束时释放并丢弃**。绑定槽只在命中的
  路径上写；留到函数出口统一释放时，其他 arm 的槽还是未初始化 →
  `zrelease(垃圾)` → 0xC0000005。switch 语句版本本来就按 case 收作用域，
  表达式版本补上了同样的 `ReleaseLocalsFrom` + `TrimLocals`。

## 5. 判定覆盖不到的地方

- 泛型擦除槽、装箱值、元组没有类身份 → `x is T` 对它们只能靠静态类型。
- 容器（List/Dictionary/StringBuilder）的类标识是 0 → `o is List<int>`
  判 false（C 宿主能判）。
- 接口不是基类链的一环，`IsSubclassName` 看不见接口实现关系 →
  `x is IFoo` 仍不成立（vtable 时代也如此）。
