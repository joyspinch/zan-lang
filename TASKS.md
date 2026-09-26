# zan-lang 全仓审计 · 可执行任务清单

> 2026-07-27 起建立。**凡在仓库中发现缺陷、缺口、未决设计，必须先在此登记编号再行动**；完成时标记为 `[x]` 或注明完成提交 / 闭账日期，并运行影响到的测试闭环。
> 状态标记：`[ ]` 未开始 · `[~]` 进行中 · `[x]` 已完成 · `[-]` 已作废

---

## 维护约定

* **编号规则**：`A` = 编译器 / 运行时 / 语言语义；`B` = 标准库 / 生态模块；`C` = 文档 / 工程规范；历史扩展沿用 `A<编号>`。
* **增补纪律**：发现新问题随手在对应大类末尾立账，写明现象、复现路径或最小探针。
* **销账纪律**：修复必须附带 conformance 测试用例；通过对应测试层后在条目中写明 commit hash 与完成日期。
* **归档纪律**：已完成的条目压缩为一行摘要（保留编号、一句话结论与日期，原详细排查过程进 git 历史，避免文档膨胀）。

---

## C 类 · 文档 / 工程规范

- [ ] **C-1** `scripts/build_mge.ps1`、`build_glassprobe.ps1`、`build_gui.ps1`、`build_jsonbind.ps1` 的手工 mingw GUI 归档缺 `gui_runtime_dwrite.o`：gui_runtime.c 只声明 `zan_dw_render`，实现拆在 `gui_runtime_dwrite.cpp`（b674314a 拆分引入），IDE 与 gallery 脚本已补（2026-09-26），这四个跑 `--link` 会报 undefined reference。修法照抄 `build_gallery.ps1`：clang++ `-fno-exceptions -fno-rtti` 编译 dwrite.cpp 进归档（dwrite.dll 运行时 LoadLibrary，无需导入库）。

---

## 2026-09-26 硬件加速改造挂账

- [ ] **B-HW1** 纯 Zan TLS 重写：`stdlib` 的 `TlsStream` 仍持 OpenSSL DllImport
  （`SSL_new`/`SSL_connect` 等），与"零 C 依赖、实现全在 Zan"的基座冲突。
  改造路径：以 `AesGcm`/`ChaCha20`（待建）+ `Hkdf` + `Sha256` 重组握手与
  记录层，或明确定位为"可选系统 TLS 桥"并从 stdlib 核心摘出。
- [ ] **B-HW2** ARM64 硬件内核落地：`rt_hw_accel.c` 的 x86 侧已全覆盖
  （SHA-1/256 NI、AES-NI 参数化 CBC/ECB/CTR、PCLMUL GHASH、CRC32C、
  SM4 查表），ARM 侧只有门与 KAT 常量，缺内核：SHA-1/256（FEAT_SHA1/256）、
  SHA-512（FEAT_SHA512）、AES（FEAT_AES 参数化）、GHASH（PMULL，寄存器
  域反射折叠同 x86 语义）、SM3（FEAT_SM3）、CRC32C（ARMv8 CRC 指令）。
  全部照 x86 先例挂 KAT 门控（发布常量向量），真机回归。
- [ ] **B-HW3** SM4 AES-NI 仿射分解：x86 现为查表驱动（唯一引擎，保留）；
  AESNI 仿射变换分解可再提速，非阻塞优化项。
- [x] **A-HW4** `byte[]`→`string` 零拷贝视图的 `.Length` 走 strlen，首个
  NUL 处静默截断（原始摘要转 hex 丢尾字节；`emit_string_length` 未开
  array_count 模式，而边界检查早开了——铺了一半的缺陷）。已修：长度读取
  统一识别 `ZAN_ARRAY_MAGIC` 头并取 -16 元素数（ABI 注释即此设计意图）。
  conformance：`tests/conformance/string_view_length.zan`。2026-09-26。
- [x] **A-HW5** `Md5.Hash` 空消息无限递归（`Hash(new byte[0], 0)` 自调用；
  旧"总是成功"的 Md5 内建把它掩成死代码）。已修：删递归行，空消息直落
  纯主体（与 Sha256.zan 同构）。conformance：hw_crypto_pixel md5 empty。
  2026-09-26。
- [ ] **B-HW4** linux GUI 驱动静态归档的构建配方未入库：`stdlib/Gui/drivers/
  linux-{x64,arm64}/static/libzan_gui.a` 不是单纯编译产物，而是 gui_runtime
  编译对象与整套 X11/Xau 等系统静态库 ar 合并的自包含成品（归档成员
  AuRead.o/Wraps.o 等即 libXau/libX11 目标），构建命令从未写进 scripts/
  （0a024d6da、d2befdd46 两次刷新均为手工完成，查无脚本）。gui_runtime
  持续演进（tray/font/shims 新导出），归档自 2026-08-15 起落后，其后新增
  的 zan_gui_* 导出在 linux 交叉链接时不可用。修法：仿
  `build_gui_android_static.sh`（NDK clang 单 TU + FreeType 成员裁剪）写
  `build_linux_gui_static.sh`，并挂进 drivers.yml 的 linux job（其 apt 包
  列表已含全部 X11/Wayland/GBM dev 包）自动构建回填；WSLg 可做端到端
  链接+运行验证。伴随项：macos dylib×2 的刷新同样依赖 drivers.yml
  （workflow_dispatch 后自动提交回 main），本机代理/gh 可用后触发一次。
