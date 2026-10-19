# Image2 生成 Prompt 存档（兔子 VI）

吉祥物与图标原图由 gpt-image-2 一次性生成（1024×1024，纯色绿幕底，
`#00FF00`），本文件是可复现的权威 prompt。抠底/切尺寸/ico 由
`postprocess.py` 完成；矢量源（SVG）为手绘几何版，与生成图同一套
图案规则，两者共同构成权威资产。

## 角色卡（五姿态共用前缀）

```text
Zanzi the bunny: a chubby chibi baby rabbit with an adorably plump pear-shaped
body, squishy round belly, chubby cheeks and fat round thighs, covered in round
fluffy cream-white fur (body color #FFF6E8), two long upright ears with soft
pastel-pink inner ears (#FFB3C7), huge round glossy dark-brown eyes, a tiny
pink triangle nose, round pink blush cheeks, a cozy cobalt-blue scarf (#2B5CFF)
tied around the neck with the knot on its right side, and a tiny round powder-
puff tail in electric blue (#2B5CFF).
Cute kawaii chibi sticker illustration, plush-toy softness, soft volumetric
shading with gentle gradients and glossy highlights, everything round and
squishy, no flat areas, bold dark-brown outline around the character, clean
white die-cut sticker border.
```

姿态后缀（加在角色卡后）：

- hero：`Pose: standing upright, waving one front paw in a friendly hello,
  happy open-mouth smile, ears perked up, full body visible.`
- run：`Pose: sprinting to the right at full speed, body leaning forward,
  ears streaming back in the wind, determined joyful grin, full body visible.`
- sleep：`Pose: curled up peacefully asleep, eyes closed, ears folded down
  softly, gentle smile, fluffy tail tucked, full body visible.`
- cheer：`Pose: jumping in joy with both front paws raised high, eyes
  sparkling, big open-mouth cheer, a few small golden four-pointed sparkle
  stars floating around, full body visible.`
- code：`Pose: sitting and typing on a tiny silver laptop resting on the
  ground in front of it, focused happy expression, ears perked up, full body
  visible.`

全部以 `Isolated and centered on a solid pure chroma-key green background
(#00FF00), no ground shadow, no text, no letters, no symbols, no watermark.`
收尾。

## App 图标（image2 版，zan.ico / zan_ios_icon 的源）

```text
Modern flat vector app icon: one rounded-corner square tile filled with a
smooth diagonal gradient from periwinkle blue (#5E86FF) at the top-left to
deep electric blue (#2141E6) at the bottom-right; the tile is centered and
fills 88% of the canvas, on a solid pure chroma-key green background (#00FF00).
On the tile: a cute minimal white bunny head with plump chubby cheeks and soft
rounded shapes, two long upright rounded ears with pastel-pink inner ears
(#FFB3C7), two small dark round eyes, a tiny pink nose, subtle soft shading,
crisp clean edges. A small golden four-pointed sparkle star at the upper-right
area of the tile. Clean modern design, no text, no letters, no border, no
shadow.
```

## 踩坑记录

- 输出永远是**不透明 RGB**：透明底靠绿幕 + PIL 色度抠底（`postprocess.py`
  的 `key_green`），阈值 `g>150 且 r<170 且 b<170` + 两轮 MinFilter 腐蚀去
  绿边。贴纸白边 + 深棕描边把角色包了一圈，绿幕才敢用全图色度判断而不
  是只清边缘。
- 首版 prompt 写 `flat vector / flat cel shading`，生成结果太扁没肉感
  （用户反馈「没肉感、立体一点圆润一点」）——改成 `plump pear-shaped body,
  plush-toy softness, soft volumetric shading, everything round and squishy`
  才出想要的圆胖质感。**风格词决定体积感，角色卡决定一致性**，两段都要。
