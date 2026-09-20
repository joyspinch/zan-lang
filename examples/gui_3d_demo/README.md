# examples/gui_3d_demo — 软光栅 3D 渲染演示

基于 Zan 原生 GUI 画布（`Canvas`）和向量数学库实现的软件光栅化 3D 渲染示例。

## 功能演示

- **3D 几何建模**：包含多面体/立方体网格模型定义与顶点变换；
- **矩阵变换**：世界坐标系变换、透视投影矩阵、模型自转动画；
- **实时光栅化**：纯算法计算面法线、简易光照着色与屏幕多边形填充；
- **交互控制**：支持鼠标拖拽旋转视角与参数微调。

## 构建与运行

Windows 平台编译：

```powershell
build\zanc.exe examples\gui_3d_demo\src\main.zan --auto-stdlib --subsystem windows -o _scratch\gui_3d_demo.exe
```

运行：

```powershell
_scratch\gui_3d_demo.exe
```
