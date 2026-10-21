# Zan.Game.Render3D

3D 渲染管线：相机 + 场景树 + 材质/光照 + 视锥剔除 + 绘制编排 + 后处理参数。
`using Game.Render3D;`

## 内容

| 文件 | 内容 |
|---|---|
| `Camera3D.zan` | 3D 相机：位置/目标/上方向、透视或正交、视口宽高比、轨道与推拉；产出视图阵与投影阵 |
| `SceneNode.zan` | 场景节点：局部变换（复用 `Game.Animation.Transform3D`）、子节点、网格句柄、材质、包围球、渲染层 |
| `Material.zan` | 材质参数：反照率、不透明度、金属度、粗糙度、自发光、纹理键 |
| `Light.zan` + `LightKind.zan` | 光源参数：平行光 / 点光 / 聚光，颜色、强度、方向或位置、射程、锥角 |
| `Frustum.zan` | 视锥：从观察-投影阵提取六个平面，球体 / 点包含判定 |
| `Lighting.zan` | CPU 光照：半兰伯特漫反射 + Blinn-Phong 高光，ARGB 打包 |
| `PostProcess.zan` + `TonemapKind.zan` | 后处理参数：曝光、色调映射、雾（当前真正参与计算的三项） |
| `RenderItem.zan` | 绘制项：网格句柄、纹理键、模型矩阵、着色 tint、层、视深 |
| `RenderPlan.zan` | 渲染计划：遍历 → 世界矩阵 → 剔除 → 光照 → 排序，产出绘制项列表 |
| `RenderTransform.zan` | 变换 → 矩阵换算、矩阵原地乘（帧内零分配） |
| `Renderer3D.zan` | 绘制层：把渲染计划装进 `Gui.MeshBatch` 提交到画布（**唯一碰 `Canvas` 的文件**） |

## 先算后画

渲染决策全部在 `RenderPlan` 里算完——遍历场景树、算世界矩阵、视锥剔除、
CPU 光照、排序，最后交出一份绘制项列表。`Renderer3D` 只做一件事：把这份
列表装进批次提交。

这么分的理由和 `Zan.Game.Debug` 一样：**渲染最该被测的恰恰是"谁被剔除了、
谁排在前面、颜色算对没有"**——这些不需要像素，可以在无头环境里逐条断言；
反过来去比像素，断言既脆又测不到逻辑。conformance `game_render3d`
（53 项断言）测的全是这一层。

## 硬取舍

1. **矩阵只用一套**。生态里 4x4 矩阵只有两个：`Gui.Mat4`（列向量
   `v' = M*v`，行主序存储）与 `System.Numerics.Matrix4x4`（行向量
   `v' = v*M`）。渲染管线吃的是列向量那套（`Canvas.DrawMesh3D` 直接收
   列主序数组），本包一律用 `Gui.Mat4`，不造第三套。
   **布局要点**：列向量约定下平移在 `m[3] / m[7] / m[11]`（不是
   `m[12..14]`，那是最后一行）；缩放作用在**列**上（第 col 列 =
   `m[col] / m[4+col] / m[8+col]`）。
2. **光照在 CPU 上算**。当前 GPU 后端（`src/runtime/zan_game.c`）的 3D
   片元着色器把键光方向写死在 GLSL 里，没有材质与光源 uniform——原生侧
   不改就没有真多光源。本包在 CPU 上算好着色，作为每实例 tint 传给 GPU，
   画面依然受材质与光照影响；原生侧接上多光源后可以直接切换。
3. **逐实例着色，不是逐顶点**。绘制项只有一个 tint，着色用的是节点的
   "代表法线"（`SceneNode.Normal`）。真要逐顶点光照得改 GPU 管线与顶点
   属性格式，是原生侧的事。
4. **排序是确定的**。不透明项按 (网格, 纹理分组) 升序——同组相邻，批次
   能连成一段实例化绘制；透明项按视深从远到近。相同键保持原顺序（稳定）。
5. **帧内零分配**。绘制项、矩阵、排序缓冲全部复用：`Build()` 把上一帧的
   绘制项回收进池子，矩阵按递归深度预分配，排序用包内自带的归并排序
   （`List<T>` 没有 `Sort`；`Zan.Linq` 的排序每次调用都新分配一组数组，
   放在每帧热路径上不合适）。
6. **不反射**。Zan 没有反射，网格与材质由调用方显式喂进来——包负责组织
   与计算，不负责知道引擎里有什么。

## 典型用法

```zan
using System;
using System.Collections.Generic;
using System.Numerics;
using Game.Render3D;
using Gui;

// 1) 相机
Camera3D camera = new Camera3D();
camera.SetViewport(canvas.Width(), canvas.Height());
camera.SetLookAt(new Vector3(6.0f, 4.0f, 8.0f), new Vector3(0.0f, 1.0f, 0.0f));

// 2) 场景树（网格句柄是 Canvas.MeshUpload 的返回值）
SceneNode root = new SceneNode("root");
SceneNode cube = new SceneNode("cube");
cube.MeshKey = canvas.MeshUpload(mesh);
cube.SetPosition(0.0f, 1.0f, 0.0f);
cube.Material = Material.Solid(0.9f, 0.6f, 0.3f);
root.AddChild(cube);

// 3) 光照 + 计划（每帧重建）
RenderPlan plan = new RenderPlan(camera);
plan.Lights.Add(Light.DirectionalLight(1.0f, 0.95f, 0.85f, 1.0f,
                                        new Vector3(0.0f, -1.0f, 0.0f)));
plan.Build(root);

// 4) 提交（返回 0 = 当前后端不支持 3D，调用方换 2D 回退）
Renderer3D renderer = new Renderer3D(1024);
if (renderer.Draw(canvas, plan) == 0) {
    // CPU 兜底档：走 2D 线框回退
}
```

## 已知边界

- **移动端 3D 未实测**：GPU 后端的 EGL/GLES 上下文路径是有的
  （`src/runtime/gui_gl_context.c`），单发 3D（`Canvas.DrawMesh3D`）在 GLES
  3.0 上没有 API 障碍；但**实例化批次**（`Gui.MeshBatch`——本包绘制层走的
  正是这条）依赖 `DrawElementsInstanced` / `VertexAttribDivisor`，那是 GL
  3.3 / GLES 3.1 的 core 特性，GLES 3.0 只有 ANGLE 扩展才有等价物。路线图
  里"Android/OHOS 是 stub"是旧结论，本机无设备无法验证，见 `TASKS.md`
  A490。
- **GPU 后端自带一层固定键光**：片元着色器里写死的半兰伯特会和本包的
  CPU 着色相乘，所以画面是"CPU 着色 × 固定键光"，不是纯 CPU 着色。
- **后处理只有曝光 / 色调映射 / 雾真正参与计算**。泛光与暗角是屏幕空间
  效果，需要整帧缓冲与后处理 pass，当前 GPU 后端没有——`PostProcess` 里
  那两个字段是给将来的原生侧 pass 留的参数契约。
- **不做材质资源加载与纹理缓存**：那是 `Zan.Game.Assets` 的活。
- **包围球是保守代理**：用节点的 `BoundsRadius` 乘最大缩放，网格的真实
  包围球由调用方给。
- 依赖 `Zan.Gui` 编译时会看到 `ZANPKG_MISSING` 警告（来自 `Zan.Gui`
  自身的无效 `using`，见 `TASKS.md` A488），只走 stderr，不影响构建。

## 消费者

- conformance `game_render3d`（53 项断言，纯逻辑层）
- 线程安全：`RenderPlan` 是有状态实例（同一实例不要跨线程并发 `Build`）；
  其余类型都是无状态静态方法或纯数据，可并发调用。
