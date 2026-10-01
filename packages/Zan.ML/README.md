# Zan.ML

ONNX Runtime 推理的纯 Zan 封装——机器学习 / OCR / AI 应用的统一推理底座。
零 C shim、零链接期依赖：运行期 `Interop.Load` 定位 onnxruntime 动态库，
经 `OrtGetApiBase()->GetApi(ORT_API_VERSION)` 取版本化 C API 函数表，
按官方头文件 offsetof 实测的槽位偏移读函数指针再以 delegate 调用
（与 Zan.Scripting 的 Lua 嵌入同一模式）。

```zan
using System.ML;

if (!Ort.IsAvailable()) { /* 回退：库缺席不是加载失败 */ }

// 模型可来自文件，也可内嵌字节（Convert.FromBase64String → LoadSession）
OrtSession s = Ort.LoadSession(File.ReadAllBytes("model.onnx"));

float[] x = { 1, 2, 3, 4, 5, 6 };               // 行主序
OrtTensor y = s.Run1(OrtTensor.Matrix(2, 3, x)); // 按模型输入序对位
foreach (float v in y.Data()) { /* ... */ }      // 输出为托管副本

s.Close();
Ort.Shutdown();
```

## 设计要点

- **版本契约**：ONNX Runtime 承诺同一 `ORT_API_VERSION` 的函数表布局只追加
  不改动。本封装按 v30（onnxruntime 1.30.x）实测偏移；运行时较旧时
  `GetApi(30)` 返回 null → fail-closed（`IsAvailable()` == false），绝不
  错位调用。支持浮点推理的 ≥1.30 运行时均可直接换用。
- **内存所有权**：输入张量数据由封装侧分配复制（`CreateTensor…` 不拷贝，
  数据归调用方），`Run` 返回前统一释放；输出张量读取托管副本后立即
  `ReleaseValue`。会话名册经默认分配器取出即拷贝即释放。所有 API 返回前
  不留原生资源。
- **名字发现**：输入/输出名装载时从模型读出并缓存，`Run` 按会话序对位，
  调用方无需写字符串字面量。
- **仅 float32**：`OrtTensor` 承载 `float[]` 数据 + `long[]` 形状。量化/
  int64 输入的模型后续按需扩展。

## 驱动（drivers/）

`drivers/driver.manifest` 声明 `onnxruntime if Ort`——运行期 dlopen 驱动，
镜像里含 `Ort*` 符号时 zanc 才把 `drivers/<plat>/onnxruntime.bundle`
列出的载荷复制到发布产物旁；载荷缺席只是编译期一句 note
（dlopen 驱动天然可选），程序落到 `IsAvailable()` == false。

载荷体积超过 8 MiB 闸门（同 CEF 驱动），**不入 git**，按 CEF 模式在树内
暂存（`*.dll` 已忽略）：win-x64 的一条获取命令——

```bash
py -m pip download onnxruntime==1.30.0 --no-deps -d _scratch/ort_dl \
  && py -c "import zipfile,glob; w=glob.glob('_scratch/ort_dl/onnxruntime-*.whl')[0]; \
open('packages/Zan.ML/src/System/ML/drivers/win-x64/onnxruntime.dll','wb').write(zipfile.ZipFile(w).read('onnxruntime/capi/onnxruntime.dll'))"
```

| 平台      | 载荷              | 来源                                             |
|-----------|-------------------|--------------------------------------------------|
| win-x64   | onnxruntime.dll   | PyPI onnxruntime 1.30.0 轮内同名 DLL             |
| 其他平台  | 未暂存            | 各发行版包或 GitHub Release；重命名/软链为候选名  |

POSIX 候选名 `libonnxruntime.so` / `libonnxruntime.so.1` /
`libonnxruntime.dylib` 已在解析序列里，放入 `drivers/<plat>/` 并登记进
对应 `onnxruntime.bundle` 即随包分发；系统已装的运行时也能被裸名 dlopen 命中。

## 槽位偏移的再生成

偏移常量在 `Onnx.zan` 头部，由 C 探针对官方头文件实测（勿手抄）：

```bash
cat > off_probe.c <<'EOF'
#include <stdio.h>
#include <stddef.h>
#include "onnxruntime_c_api.h"
#define O(m) printf(#m "=%zu\n", offsetof(OrtApi, m))
int main(void) {
    printf("VERSION=%d\n", ORT_API_VERSION);
    O(CreateEnv); O(CreateSessionFromArray); O(CreateSessionOptions);
    O(GetAllocatorWithDefaultOptions); O(SessionGetInputCount);
    O(SessionGetInputName); O(SessionGetOutputCount); O(SessionGetOutputName);
    O(AllocatorFree); O(CreateCpuMemoryInfo); O(CreateTensorWithDataAsOrtValue);
    O(Run); O(GetTensorMutableData); O(GetTensorTypeAndShape);
    O(GetDimensionsCount); O(GetDimensions); O(ReleaseTensorTypeAndShapeInfo);
    O(GetErrorMessage); O(ReleaseEnv); O(ReleaseStatus); O(ReleaseMemoryInfo);
    O(ReleaseSession); O(ReleaseSessionOptions); O(ReleaseValue);
    return 0;
}
EOF
clang off_probe.c -I <onnxruntime/include> -o off_probe && ./off_probe
```

升级 ORT_API_VERSION 时：换新头文件重跑探针 → 更新 `API_VERSION` 与偏移
常量 → 跑 `tests/conformance/onnx_smoke.zan`（内嵌 172 字节 MatMul 模型，
参考输出 `[[22,28],[49,64]]`）。

## 测试

`tests/conformance/onnx_smoke.zan` 四档（conformance / determinism /
leakcheck / arcguard）绿。库缺席的机器上打印同一 `ort-ok: 1` 金标
（静默跳过，同 lua_embed_smoke），库在场时走完整推理断言。
