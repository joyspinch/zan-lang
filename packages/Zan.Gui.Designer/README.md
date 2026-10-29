# Zan.Gui.Designer - 可视化窗体与控件设计器子系统

`Zan.Gui.Designer` 是面向 Zan GUI 的原生所见即所得（WYSIWYG）窗体与控件设计引擎。全面遵循现代声明式 UI 与 HTML 窗体标准。

---

## 核心能力

- **设计画布（Designer Canvas）**：控件拖拽排布、多选、吸附对齐（Snap to Grid / Alignment Guides）、标尺与多级撤销/重做（Undo/Redo）。
- **属性检查器（Property Inspector）**：动态元数据反射、分类属性编辑（尺寸、定位、字体、颜色、事件槽与无障碍角色）。
- **窗体双向映射（Form Round-trip）**：HTML 声明式设计稿与运行时 `WDoc` 语法树精确双向映射，支持编译期代码生成（`InitForm` / `InitControl`）。
- **即时预览（Live Preview）**：运行时热加载与快照渲染，与 IDE 设计器视图无缝集成。

## 命名空间

- `Gui.Designer` — 设计器主画布、属性检查器与文档导出基元。
