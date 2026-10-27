# 3D First-Person Explorer (第一人称漫游与交互模板)

基于 Zan.Game.Render3D 工业级 3D 渲染管线驱动的第一人称漫游与 3D 空间拾取母版。

## 特性

- **3D 渲染与光照**：
  - 基于 `Primitives3D` 自动生成立体地面与建筑立方体网格。
  - 基于 `RenderPlan` 实现自动视锥裁剪（Frustum Culling）与 GPU 批次渲染（`Renderer3D`）。
  - 动态方向日光照与环境光混合。
- **第一人称控制器 (`FpsController3D`)**：
  - WASD + Space/Shift 平滑前后左右行走与升降。
  - 鼠标平滑转动视向，严格 ±89° 俯仰锁定杜绝万向节翻滚。
- **3D 空间实时射线求交 (`Raycast3D`)**：
  - 屏幕正中准星实时发射 3D 世界射线，与场景中的实体包围盒（AABB）求交。
  - 瞄准目标时准星自动高亮并显示锁定距离。

## 运行与编译

```bash
zanc src/FpsGame.zan src/main.zan --auto-stdlib --package-project <zan-lang-root> -o fps3d.exe
./fps3d.exe
```
