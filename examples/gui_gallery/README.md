# Zan GUI Component Gallery

Zan 官方 GUI 组件全景画廊（基于 `stdlib/Gui` 原生自研渲染管线）。

提供 NaiveUI 风格的交互式控件展示台：
- **左侧导航**：按基础控件、表单输入、数据展示、图表呈现、高级交互（富文本、代码编辑器、图表、HMI仪表等）分类浏览；
- **右侧详情区**：包含组件功能说明、实时交互状态预览与可直接复制的代码模板片段；
- **内置数据资产**：内置中国地图数据（GeoJSON）、矢量图标、各类示例数据集。

## 构建与运行

在 Windows 上使用官方构建脚本构建：

```powershell
powershell -File scripts\build_gallery.ps1
```

构建成功后将生成 `build\gallery_test.exe`，直接双击或在终端启动即可：

```powershell
build\gallery_test.exe
```
