# Zan.Tensor

张量与算子（纯 Zan，无原生依赖）。`using Ai.Tensor;` 按需拉入。

## 内容

| 文件 | 内容 |
|---|---|
| `Tensor.zan` | 多维张量：行主序存储、形状与下标换算（`OffsetOf`）、`Reshape` / `Transpose2D` / `Slice` / `Clone` |
| `TensorMath.zan` | 逐元素算子、矩阵乘、Softmax / 因果 Softmax / RMSNorm / LayerNorm、SiLU / GELU / SwiGLU、按行取表、沿第 0 维拼接、归约（`ArgMax` / `TopK` / `Sum` / `Mean`） |
| `ComputeBackend.zan` | 计算后端契约（GPU 加速的挂载点）与登记处 `ComputeBackendRegistry` |
| `WeightFile.zan` | 权重文件入口 `Load`：按魔数分发 GGUF / safetensors，`Require` / `Info` / `Has` 按名取张量（数据区整体驻内存，>2GB 的单文件不支持） |
| `GgufReader.zan` | GGUF 目录解析：元数据 KV（字符串数组保留为 `metadataArrays`）与张量描述（形状 / ggml 类型 / 数据区偏移），对齐值取 `general.alignment` |
| `SafeTensorsReader.zan` | safetensors 头解析：dtype / shape / `data_offsets`（相对数据区起点）与 `__metadata__` |
| `BinaryReader.zan` | 内存字节流的小端读取原语（`U8..U64` / `F16` / `F32` / `F64` / 长度前缀字符串）与越界守卫 `Need` |
| `FloatBits.zan` | 位级互转：f16 / bf16 → f32（次正规、±∞、NaN、−0 精确保真） |

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

## 进程内 CPU 字节解码器

`ByteDecoder` 和 `ByteTokenizer` 随 `Ai.Tensor` 引入，模型权重由调用方从本地文件
或内存提供。解码在当前进程使用包内 CPU 算子执行。

```zan
using Ai.Tensor;
using System;

public class Program {
    static void Main() {
        ByteDecoder model = ByteDecoder.Open("model.safetensors");
        Console.WriteLine(model.Generate("hello", 16));
    }
}
```

### 检查点契约

使用 safetensors，`__metadata__` 中的 `architecture` 必须为 `zan.byte-attention.v1`。
头部最多 16 KiB，完整文件最多 8 MiB。数据区由下列七个有限值 F32 张量连续覆盖，
顺序任意。数组为行主序，矩阵形状按下表填写，`data_offsets` 用相对于数据区起点的
整数字节区间，不允许重复条目、重叠、空洞或尾随数据。

| 张量 | 形状 |
|---|---|
| `token_embedding.weight` | `[258, d]` |
| `position_embedding.weight` | `[L, d]` |
| `attention.query.weight` | `[d, d]` |
| `attention.key.weight` | `[d, d]` |
| `attention.value.weight` | `[d, d]` |
| `attention.output.weight` | `[d, d]` |
| `lm_head.weight` | `[258, d]` |

隐藏宽度 `d` 取自 token 嵌入的列数，范围 `1..512`。上下文长度 `L` 取自位置嵌入的
行数，范围 `1..256`。单头、单层架构使用可训练绝对位置嵌入和残差注意力。给定 token
序列，计算如下。

```text
X = token_embedding[token_ids] + position_embedding[0..sequence_length)
Q = X Wq^T, K = X Wk^T, V = X Wv^T
A = causal_softmax(Q K^T / sqrt(d))
logits = (X + (A V) Wo^T) Wlm^T
```

第 `i` 行的注意力只取 `j <= i`，logits 第 `i` 行预测紧随该位置的 token。
此架构仅含上表所列参数。每步贪心生成重算整个前缀，适用于小检查点和短上下文。

### 分词与生成

- `ByteTokenizer.Encode` 将 UTF-8 字节直接映射为 `0..255`。默认前置 BOS `256`，
  可选追加 EOS `257`。`Decode` 跳过这两个标记，并用 U+FFFD 替换非法或截断的 UTF-8。
- `ByteDecoder.Open(path)` 读取本地检查点，`Load(bytes)` 从内存加载。`Forward(tokens)`
  返回 `[序列长度, 258]` 的 logits。序列以 BOS 开头，EOS 如出现必须在末尾。
- `GenerateTokens(prompt, maxNewTokens)` 返回续写 token ID。每步从最后一行取最高
  logit，跳过 BOS，同分取最小 ID。选中 EOS 时将其计入预算并结束。
- UTF-8 prompt 加 BOS 后必须装入上下文。`maxNewTokens` 必须非负，生成最多占用剩余
  上下文位置。`Generate` 将续写 ID 解码为文本。CPU 计算产生非有限值时抛出异常。

可运行的参考检查点由 `tests/conformance/byte_decoder.zan` 在本地构造。`ax` 和 `bx`
共享最后一个输入 token `x`，模型根据前文分别生成 `A` 和 `B`，随后生成 EOS。

## 不在包内

- **GPU 后端的原生实现**：仓库里的 GPU 能力都在外部预编译的 `zan_gui` /
  `zan_game` 驱动里（`src/runtime/` 没有 D3D / Vulkan / Metal 代码），所以
  这里只留契约与调度路径；原生后端按平台补齐后 `Use(...)` 挂上即可，
  算子不用改。
