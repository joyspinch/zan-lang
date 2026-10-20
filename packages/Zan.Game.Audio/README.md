# Game.Audio

`Game.Audio` 是**纯 Zan 的音频处理层**：采样缓冲、波形合成、ADSR 包络、
滤波、混音、变调、WAV 读写。

它补的是 `System.Media`（原生音频栈）**够不着**的那一半：原生层给的是
"播出来"（WAV/OGG 解码 → 声卡），**不给"拿到样本"**。而响度归一、
变调、切片、离线混音、程序化生成音效——全都要在纯 Zan 侧看到采样数据。

## 设计取舍

- **内部统一 double**：样本存 `double[]`（归一化到 [-1, 1]），只在
  **导出时**才量化回目标位深。混音要乘增益、滤波要累加，int 会中途
  溢出/截断。
- **交错布局**（左右声道交替）：这是文件与硬件的布局，转换只做一次。
- **相位累加振荡器**：不每样本算 `sin(2*pi*f*t)`——那在大时间偏移时丢
  精度，且每样本一次三角函数太慢。累加相位再取小数部分，精度只跟
  增量有关，与已播时长无关。
- **噪声取 LCG 高位**：低位周期极短，直接用会出现可见的带状分布
  （合成噪声会听起来有"音调"）。
- **ADSR 是显式状态机**：不用"当前时间 vs 阶段时间"倒推——倒推在改
  参数（跳段）时会算出负时长，行为不可预测。
- **滤波状态按声道独立**：滤波器的记忆是"上一个样本"，左右共用一份
  会让右声道被左声道的历史污染。
- **等功率交叉淡化**（sqrt）而不是线性：线性淡入淡出叠加时中间掉能量
  （两路都是 0.5 → 0.25），听起来有"空洞"。
- **确定性**：随机源带种子，同 seed + 同参数 → 同一段音频（可回放）。
- **零反射、纯 Zan、无原生依赖**：包可无头跑，能被 conformance 断言。

## 能力

| 能力 | 入口 |
|---|---|
| 采样缓冲 | `AudioBuffer`：峰值 / RMS / 直流、归一化、限幅、淡入淡出、拼接 |
| 振荡器 | `Oscillator` + `Waveform`：正弦 / 锯齿 / 方波 / 三角 / 噪声 |
| 包络 | `AdsrEnvelope` + `EnvelopeStage`：攻击 / 衰减 / 保持 / 释放 |
| 滤波 | `BiquadFilter`（二阶，RBJ 公式）、`OnePoleFilter`（一阶） |
| 混音 | `AudioMixer`：多路叠加、增益、削波 |
| 合成 | `Synth` + `SynthOptions`：参数化生成音效（含频率扫频） |
| 变调 | `PitchShift`：`Resample` / `Shift`（变调不变速）/ `ConvertSampleRate` |
| WAV | `Wav`：`ReadFile` / `ReadBytes` / `WriteBytes16` / `WriteBytesFloat32` |

## 用法

```zan
using System;
using Game.Audio;

// 合成一段"上扫"的激光音
SynthOptions opt = new SynthOptions();
opt.WithWaveform(Waveform.Saw).WithSweep(400.0, 2400.0).WithDuration(0.25);
opt.WithFormat(44100, 1).WithGain(0.4);
opt.WithEnvelope(0.005, 0.05, 0.6, 0.08);
AudioBuffer sfx = Synth.Render(opt);

// 滤波（挖掉 60 Hz 工频嗡声）
BiquadFilter notch = new BiquadFilter(FilterKind.Notch, 60.0, 8.0, 44100, 1);
notch.ProcessBuffer(sfx);

// 混音 + 限幅
AudioMixer mixer = new AudioMixer(44100, 1, sfx.FrameCount());
mixer.Mix(sfx, 0.8);
mixer.Clip();

// 导出 WAV（纯 Zan，不经原生音频栈）
byte[] wav = Wav.WriteBytes16(mixer.Output());
```

## 已知边界

- **无 MP3 解码**：OGG 由原生层（stb_vorbis）解码；纯 Zan 侧只处理
  PCM 与 IEEE float 的 WAV。MP3 需要引入解码库，是另一件事。
- **无流式播放**：本包是离线处理（整段缓冲），不做边播边解。
- **重采样无抗混叠滤波**：线性插值对中低频够用，高频会有混叠。
  抗混叠需要先低通再插值，代价是全缓冲拷贝，没做。
- **变调有伪影**：`Shift` 用重叠相加（OLA），**不做**波形相似度对齐
  （WSOLA）——会有轻微"金属味"，对游戏音效够用。
- **IIR 滤波有启动暂态**：滤波器从零状态启动，前几百帧有振铃。
  断言滤波效果要看稳态段，不是全缓冲峰值。

## 验证

`tests/conformance/audio_dsp.zan` 覆盖：缓冲的峰值 / RMS / 直流 / 归一化 /
限幅 / 淡入淡出 / 拼接与格式不匹配拒绝、振荡器的波形形状与相位归一与噪声
确定性、包络的四段状态机与提前释放、滤波的低通/高通/陷波与一阶直流增益、
混音的叠加/增益/削波/格式拒绝、合成的确定性与频率扫频、变调的时长不变与
重采样比例、WAV 的 16 位与浮点往返与非法输入拒绝。

## 放置

游戏包约定：`packages/Zan.Game.Audio/src/Game/Audio/*.zan`。
引擎级能力（物理 / 渲染 / ECS / 资源 / 音频）走独立包并单独版本化，
不进 stdlib。
