# 本地 CPU 字节解码

从本地文件加载 `zan.byte-attention.v1` safetensors 检查点，用 `Zan.Tensor` 在当前进程的 CPU 上续写。请自行提供符合[检查点契约](../../packages/Zan.Tensor/README.md#检查点契约)的权重。本例运行时只读取本地权重。运行标准 GGUF 模型沿用 Ollama 加 [`Zan.Ai` 的 `AiClient`](../../packages/Zan.Ai/README.md) 的 OpenAI 兼容接口。

在仓库根目录使用已有的 `zanc` 编译并运行（PowerShell）。

```powershell
New-Item -ItemType Directory -Force _scratch | Out-Null
.\build\zanc.exe examples/byte_decoder/ByteDecoderCli.zan --auto-stdlib -o _scratch/byte_decoder.exe
.\_scratch\byte_decoder.exe 'D:\models\model.safetensors' 'hello world' 16
```

三个位置参数依次为检查点路径、prompt 和新增 token 预算。prompt 的 UTF-8 字节加上一个起始 token 必须能装入模型上下文。预算须为非负 32 位整数，生成在用尽预算、到达上下文上限或遇到 EOS 时结束。成功时 stdout 只打印续写及换行，退出码为 `0`。参数错误写入 stderr 并返回 `2`，读取检查点或生成失败写入 stderr 并返回 `1`。

[conformance fixture](../../tests/conformance/byte_decoder.zan) 可生成小型测试权重。用这份权重运行 `ax` 和 `bx`，预算都设为 `9`，分别得到 `A` 和 `B`。
