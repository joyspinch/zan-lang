# Zan.Tensor

张量与算子（纯 Zan，无原生依赖）。`using System.Tensor;` 按需拉入。

## 内容

| 文件 | 内容 |
|---|---|
| `Tensor.zan` | 多维张量：行主序存储、形状与下标换算（`OffsetOf`）、`Reshape` / `Transpose2D` / `Slice` / `Clone` |
| `TensorMath.zan` | 逐元素算子、矩阵乘、Softmax / 因果 Softmax / RMSNorm / LayerNorm、SiLU / GELU / SwiGLU、按行取表、沿第 0 维拼接、归约（`ArgMax` / `TopK` / `Sum` / `Mean`） |
| `ComputeBackend.zan` | 计算后端契约（GPU 加速的挂载点）与登记处 `ComputeBackendRegistry` |

## 约定

- **存储统一 f32**（`float[]`）。权重在加载时从 f16 / bf16 / 量化格式转成 f32，
  换来的是算子实现与数值一致性上的简化（一份实现、一套语义）；存储精度的
  进一步优化（f16 存储 + 算子直读）留给后续。
- **归一化与激活沿最后一维**（Transformer 的习惯）。
- **矩阵乘走登记的后端**：`TensorMath.MatMul` 先问
  `ComputeBackendRegistry.Current()`——后端可用就走后端，否则用包内的 CPU
  参考实现（`MatMulCpu`）。后端拿到的必须是**连续行主序**张量，输出写进
  调用方给的结果张量（后端不分配内存）。
- `Tensor.Reshape` 是**视图**（共享底层数据），其余结构操作都是拷贝。

## 矩阵乘的三种形态

| 方法 | 形状 | 结果 |
|---|---|---|
| `MatMul(a, b)` | a[m,k]、b[k,n] | a · b |
| `MatMulRightTransposed(left, right)` | left[m,k]、right[n,k] | left · rightᵀ（自注意力打分） |
| `MatVec(a, v)` | a[m,k]、v[k] | a · v |

## 不在包内

- **权重格式读取**（GGUF / safetensors）：在后续里程碑里做，落地时挂到这里
  的 `Tensor` 上。
- **GPU 后端的原生实现**：仓库里的 GPU 能力都在外部预编译的 `zan_gui` /
  `zan_game` 驱动里（`src/runtime/` 没有 D3D / Vulkan / Metal 代码），所以
  这里只留契约与调度路径；原生后端按平台补齐后 `Use(...)` 挂上即可，
  算子不用改。
