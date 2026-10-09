# Game.Input

> 源码: `packages/Zan.Game/src/Game/Input/InputMapper.zan`


## InputMapper (class)

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

- void OnMouseMove(double sx, double sy)

- void OnMouseButton(int button, bool isDown)

- void OnKeyDown(int keyCode)

- void OnKeyUp(int keyCode)

- void FlushFrameEvents()

- bool IsActionDown(string action)

- bool IsActionJustPressed(string action)

- void GetWorldMouse(Camera2D cam, out double wx, out double wy)

- void GetIsoGridMouse(Camera2D cam, IsoTileMap map, out int gx, out int gy)

- bool GetSelectionWorldBox(Camera2D cam, out double minX, out double minY, out double maxX, out double maxY)
