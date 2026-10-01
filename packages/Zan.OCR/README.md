# Zan.OCR

PP-OCR ONNX 模型的纯 Zan OCR 管线：PNG 解码（`System.Ocr.Png`）、检测
后处理（二值化/连通域/矩形精确 unclip）、识别前处理与 CTC 解码
（`System.Ocr`）。推理 rides Zan.ML——运行期 dlopen onnxruntime，零 C shim。

```zan
using System.Ocr;

byte[] det = File.ReadAllBytes("ch_PP-OCRv4_det_mobile.onnx");
byte[] rec = File.ReadAllBytes("ch_PP-OCRv4_rec_mobile.onnx");
string dict = File.ReadAllText("ppocr_keys_v1.txt");
Ocr ocr = Ocr.Load(det, rec, dict);

Png img = Png.DecodeFile("screenshot.png");
List<OcrLine> lines = ocr.Run(img.Rgba(), img.Width(), img.Height());
foreach (OcrLine l in lines) {
    // l.Text() / l.Score() / l.MinX()..l.MaxY()（原图像素坐标）
}

ocr.Close();
```

实测（win-x64，onnxruntime 1.30，360×80 截图渲染字）：`中国制造2026`
置信度 0.9997；双语两图两行阅读序正确；含两次模型装载全程 0.77s。

## 组件

| 文件        | 职责                                                                  |
|-------------|-----------------------------------------------------------------------|
| Png.zan     | 纯 Zan PNG 解码（8 位、非隔行、色型 0/2/3/4/6、五种行滤波、CRC 不校验）|
| Ctc.zan     | CTC 贪心解码 + 字典装载（blank=0、空格收尾，RapidOCR 惯例）           |
| Det.zan     | DB 后处理：二值化 → 连通域 → `d=area·ratio/perimeter` 矩形 unclip（轴对齐下精确，不需 Clipper）→ 坐标还原 |
| ImgUtil.zan | 灰度、双线性裁剪缩放 + NCHW 归一化（检测 ImageNet 统计 / 识别 0.5-0.5）|
| Pipeline.zan| `Ocr` 门面：检测 → 阅读序排序 → 逐框识别 → `OcrLine` 列表            |

## 模型获取（不入 git，CEF 式树内暂存）

rec 模型 10.9 MB 超仓库 8 MiB 闸门（det 4.7 MB 同走暂存保持一致），
模型**不提交**，一条命令取齐（ModelScope，RapidOCR v3.9.2 清单）：

```bash
mkdir -p _scratch/ocr_models
curl -sL -o _scratch/ocr_models/det.onnx  "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/det/ch_PP-OCRv4_det_mobile.onnx"
curl -sL -o _scratch/ocr_models/rec.onnx  "https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/onnx/PP-OCRv4/rec/ch_PP-OCRv4_rec_mobile.onnx"
```

字典 `dict/ppocr_keys_v1.txt`（26 KB，6623 字符 + UTF-8）随包提交。
`Ocr.Run` 只做水平文本：文档/截图场景轴对齐足够。

## 设计取舍

- **轴对齐框**：DB unclip 对矩形是精确公式；斜排文本的 min-area-rect +
  透视裁剪（warpPerspective）留作路线图（见 TASKS.md），届时只换
  Det/Rec 的框来源与裁剪方式，管线不动。
- **PNG 只解不编**：OCR 是消费端；需要生成测试图时用外部工具（探针里
  PIL 渲染）。JPEG 按需再补（stb_image 已在 gui_runtime 里，可走 Zan.ML
  式运行期暴露，暂无必要）。
- **阅读序**：minY 粗排 + 逐行右移贪心；多栏排版先按行合并再细分。

## 测试

- `tests/conformance/ocr_png_decode.zan` — 内嵌 5 张手工 PNG（覆盖全部
  滤波类型与色型）+ 畸形输入异常契约；fixture 由带参考解码自检的生成器
  产出。
- `tests/conformance/ocr_ctc_det.zan` — CTC（折叠重复/丢 blank/空格类/
  字典装载）、DB 后处理（双连通域/低分丢弃/unclip 手算值/坐标还原）、
  灰度与 NCHW 平面布局、DetInputSize 32 对齐。全合成，不需要模型。
- 端到端（模型在场）：`_scratch` 探针拉真模型对渲染字断言——通过后
  即弃，不入 conformance（模型不入 git，机器间不可复现）。
