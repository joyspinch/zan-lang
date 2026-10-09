# Async 帧存储与发布成本实测（2026-10-08）

## 结果

在冻结的 OnePlus 输入上，基线与当前组合编译器均成功完成静态发布。缓存状态一致的一组测量中，完整发布耗时下降 16.99%，峰值进程提交内存下降 24.07%，535 个 async resume 函数的指令总量下降 56.26%。

| 指标 | 基线 | 当前组合编译器 | 降幅 |
| --- | ---: | ---: | ---: |
| 完整发布墙钟时间 | 110.899 s | 92.057 s | 16.99% |
| 峰值 private commit | 1261.98 MiB | 958.16 MiB | 24.07% |
| 峰值 working set | 1243.95 MiB | 953.91 MiB | 23.32% |
| IRGen 结束时指令数 | 3,626,875 | 2,276,001 | 37.25% |
| 去除不可达代码后 manifest 指令数 | 3,596,827 | 2,246,588 | 37.54% |
| async resume 数量 | 535 | 535 | — |
| async resume 指令总数 | 1,254,174 | 548,521 | 56.26% |
| `FormCampCreateDisplay_DoTaskCore$resume` 指令数 | 97,109 | 12,238 | 87.40% |
| 同一函数基本块数 | 2,365 | 1,767 | 25.29% |
| 分片对象数 | 41 | 34 | — |
| 输出 exe 字节数 | 21,573,120 | 18,656,256 | 13.52% |

两次均为 543 次元数据扫描、0 次缓存命中、543 次未命中、0 次写入；均实际解析 830 个文件并创建 1,586,484 个 AST 节点。源文件及原生依赖散列在每次发布前后保持不变。此输入集与先前实时项目的约 379 万指令、544 个 resume 的输入集不同，不能混用两组分母。

## 改动与正确性

当前组合编译器将 async 局部存储直接落在持久 heap frame 中，不再在每次挂起和循环抢占位置遍历全部局部槽保存、恢复。发射期间保留类型化 alloca 代理，完成 ARC/EH 发射后将其使用替换为帧字段地址。正常返回、取消和未捕获异常进入共享完成块，避免反复展开完成及帧清理代码。

发布模式还在完整函数发射结束后运行 SROA、EarlyCSE 和 SimplifyCFG，降低进入整模块优化及分片阶段的 IR 体积。该路径在本轮开始时已存在于共享工作树。本轮补修两项正确性问题：

- async 帧局部的委托 lambda 初始化必须携带声明的委托类型；否则默认 i64 返回路径遗漏借用引用返回值的 retain，也可能误发射浮点参数 ABI。字符串闭包返回在后续循环分配后出现错误内容及 double free 的探针已复现并修复。
- FINEXC 帧数组容量必须从扫描得到的最大 finally 深度传入工作项；lock 使用同一 finally 栈索引，也必须纳入扫描。GEP 使用实际帧成员数组类型，并检查索引边界。await 检测、ANF、transfer 计数与局部扫描同步覆盖 lock/checked 语句。

本对照比较冻结的旧编译器与包含已有优化和本轮修复的当前编译器，不能将所有降幅单独归因于两项正确性修复。

## 阶段数据

下列阶段时间来自编译器 `--time`，内存来自阶段探针。整进程峰值由宿主每 50 ms 采样；阶段内存按 MiB 取整。

| 阶段 | 基线时间（ms） | 当前时间（ms） | 基线结束 commit（MiB） | 当前结束 commit（MiB） |
| --- | ---: | ---: | ---: | ---: |
| IRGen | 4360.5 | 6450.9 | 1212 | 946 |
| Manifest | 291.5 | 423.3 | 978 | 712 |
| 分片计划 | 543.8 | 892.0 | 979 | 713 |
| 分片发射 | 66426.1 | 64199.5 | 881 | 675 |
| Optimize | 14387.3 | 6935.4 | 943 | 698 |
| Emit obj | 164.8 | 86.8 | 944 | 698 |
| Write obj | 20277.7 | 9196.2 | 937 | 665 |
| Link | 1159.3 | 742.3 | 506 | 389 |

IRGen 因提前压缩函数而增加约 2.09 s，随后优化和对象写出减少约 18.53 s。当前架构仍先持有完整 LLVM module 再分片；本轮减少了所持有的 IR 体积，没有实现边生成边分片。

这是一组缓存状态一致的顺序 A/B 实测，并非多次随机顺序统计。首轮同输入实测为 121.902 s → 74.578 s、1261.87 MiB → 960.23 MiB，但其元数据缓存状态不同，因此不作为最终耗时降幅依据。两组指令数相同，峰值内存相近；墙钟时间受机器当时负载等因素影响。

## 可复现输入与工具

原始配对结果已保存在 [基线 JSON](compiler-async-cost-2026-10-08-before.json) 和 [当前编译器 JSON](compiler-async-cost-2026-10-08-after.json)，包含阶段时间、内存采样峰值、输入一致性、manifest 汇总及最大函数记录。

- 冻结 bundle：`_scratch/publish-memory-148f/`，沿用既有 SDK、应用源及原生依赖，不以实时 OnePlus 工作树替代。
- 应用提交：`eb11f4c5d03d2e59555ed0b55fdae000af1e0e7c`。
- bundle 依赖提交：`6cd90920cdd08fbb1e783b942778c6e632eefabf`。
- `app/inputs.rsp`：400 项输入；SHA-256 `5fc7fe899384ed2f6cc1e27a112c80d9dac24f2abe85bb2a4be9c6f8f8063ac3`。
- 输入/原生依赖登记 `owner.json`：2037 个源文件散列、2103 个原生依赖散列；文件 SHA-256 `f86001290cec1f413e9891f6bd40819c73b147fa8e6b48b40b378c91d3aa87ca`。
- 基线编译器 SHA-256：`14d61aa4f74a50da542cbd339da4ae743e78fcbffac1d30950e5fe7e775b7663`。
- 当前测量编译器 SHA-256：`e7e98d66abef33f3854e19dc493c6d521e15d954df00968d17e1fbafbf61f6f9`。
- 测量标签：`async-cold-before-20261008`、`async-cold-after-20261008`。

原始 harness 为 bundle 内 `measure.py`。缓存一致的变体仅将 `ZAN_META_CACHE_DIR` 改为每个 label 各自的、初始不存在的目录 `metadata-async-paired/<label>`。测量前排除正在运行的 zanc、ctest、ninja；清除继承的 `ZAN_*` / `ZANC_*` 环境变量；设置以下必要变量：

```text
LOCALAPPDATA=<bundle>/profile
ZAN_META_CACHE_DIR=<bundle>/metadata-async-paired/<label>
ZAN_LIB_PATH=<bundle>/sdk/build/mingw/lib
ZAN_PROBE_MEM=1
ZAN_CODEGEN_MANIFEST_JSON=<bundle>/<label>-manifest.json
```

编译器工作目录为 `<bundle>/app`，参数模板：

```text
<compiler>
--stdlib-path <bundle>/sdk/stdlib
--publish
@<bundle>/app/inputs.rsp
-o <bundle>/out/<label>/OnePlus.exe
--subsystem windows
--icon <bundle>/zan.ico
--link-mode static
--package-project <bundle>/app
--time
```

登记的 win-x64 GUI 原生归档修复在两边一致：`libzan_gui.a` SHA-256 为 `013107b9c5722cfae7a8a90e62dcec04992e65ff27498fb7e6ad50c523315462`。历史 bundle 的旧基线链接失败记录没有作为本报告的成功发布基线。

## 验证

- 21 个受影响的 async conformance/leakcheck 用例全部通过，包含 frame storage、ref frame、awaiting finally return、抢占、取消及异常跨帧传播；串行执行，没有运行测试 tier。
- 新增 `async_delegate_frame_signature` 的 conformance 与 leakcheck 均通过，覆盖字符串/对象返回、未注解浮点参数、遮蔽局部以及协程结束后逃逸的闭包。
- `async_frame_storage` 与 `async_delegate_frame_signature` 直接用 `--publish --check-leaks` 编译运行，输出匹配 golden，无泄漏报告。
- `conformance_async_ir_scaling`：局部槽和 await 点同时从 48 增至 96，resume 指令数 1159 → 2215，增长按测试算法向上取整为 192%，低于 250% 阈值；两份发布程序计算结果正确。

- 新增 `async_finally_frame_capacity` 的 conformance、leakcheck 与直接 `--publish --check-leaks` 运行均通过，覆盖三层 awaiting finally 的原异常保存、lock 内嵌两层 finally 后重新加锁，以及 checked 内局部跨 await。定向 conformance/leakcheck 合计 25 个通过。
