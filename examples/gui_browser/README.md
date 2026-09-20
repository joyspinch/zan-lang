# examples/gui_browser — 多标签轻量网页浏览器

基于 `Gui.Component.WebView` 实现的极简多标签原生网页浏览器示例。

## 功能演示

- **多标签页管理**：支持动态新建标签页、切换标签页、关闭标签页以及响应式标题更新；
- **导航工具栏**：支持前进、后退、刷新、地址栏输入与跳转；
- **状态栏联动**：实时展示当前页面加载状态、URL 监控与网络交互。

## 构建与运行

Windows 平台编译：

```powershell
build\zanc.exe examples\gui_browser\gui_browser.zan --auto-stdlib --subsystem windows -o _scratch\gui_browser.exe
```

运行：

```powershell
_scratch\gui_browser.exe
```
