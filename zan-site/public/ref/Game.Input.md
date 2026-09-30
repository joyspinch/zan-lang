# Game.Input

> 源码: `packages/Zan.Game/src/Game/Input/InputMapper.zan`


## InputMapper (class)

统一游戏输入映射器与世界空间拾取投影系统。
将屏幕原生鼠标点击、按键事件转化为抽象动作，
自动逆投影为世界坐标与 2.5D 等轴菱形网格坐标，内置框选矩形辅助。

- double screenMouseX;

- double screenMouseY;

- bool isLeftMouseDown;

- bool isRightMouseDown;

- bool isDragging;

- double dragStartScreenX;

- double dragStartScreenY;

- bool[]keyState;

- bool[]keyJustPressed;

- bool[]keyJustReleased;

- string[]actionNames;

- int[]actionKeyBindings;

- int actionCount;

- InputMapper()

- static InputMapper Create()

- double MouseX { get }

- double MouseY { get }

- bool IsLeftDown { get }

- bool IsRightDown { get }

- bool IsDragging { get }

- void BindAction(string action, int keyCode)
  - 绑定抽象动作到虚拟键码（如 BindAction("Attack", 32) 空格攻击）。

- void OnMouseMove(double sx, double sy)
  - 更新鼠标移动坐标。

- void OnMouseButton(int button, bool isDown)
  - 更新鼠标按键事件。

- void OnKeyDown(int keyCode)
  - 更新按键按下状态。

- void OnKeyUp(int keyCode)
  - 更新按键抬起状态。

- void FlushFrameEvents()
  - 在每帧末尾清除瞬态单次击发标记。

- bool IsActionDown(string action)
  - 查询指定动作当前是否处于按住状态。

- bool IsActionJustPressed(string action)
  - 查询指定动作是否在当前帧刚刚按下。

- void GetWorldMouse(Camera2D cam, out double wx, out double wy)
  - 结合摄像机，将当前屏幕鼠标位置自动逆投影为游戏世界坐标。

- void GetIsoGridMouse(Camera2D cam, IsoTileMap map, out int gx, out int gy)
  - 结合摄像机和等轴测地砖，将当前屏幕鼠标位置直接逆投影为地图瓦片坐标 (gx, gy)。
    经典传奇点地跑位、暗黑拾取与 RTS 建造摆位核心算子。

- bool GetSelectionWorldBox(Camera2D cam, out double minX, out double minY, out double maxX, out double maxY)
  - 获取当前鼠标拖拽框选的世界坐标包围矩形 [minX, minY, maxX, maxY]。
    RTS 圈选多个兵团、批量施法的核心功能。
