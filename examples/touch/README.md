# touch — 触摸 / 多点触控

演示 SDL3 的手指事件：`FINGER_DOWN` / `FINGER_MOTION` / `FINGER_UP` /
`FINGER_CANCELED`。程序开一个窗口，把每根按下的手指画成一个方块（大小随压感
变化），并把事件打到控制台，5 秒后退出。

```bash
build/zanc examples/touch/touch_demo.zan --auto-stdlib -o build/touch_demo
build/touch_demo
```

## 模型

一根手指一条事件，事件里带 **触摸设备 id** 和 **手指 id**：

| API | 含义 |
|-----|------|
| `SdlEvent.TouchId()` | 事件来自哪块触摸设备 |
| `SdlEvent.FingerId()` | 手指 id，从按下到抬起保持不变 |
| `SdlEvent.FingerX/Y()` | 窗口内归一化坐标 `0..1`，乘窗口尺寸得像素 |
| `SdlEvent.FingerDeltaX/Y()` | 相对上一次的位移（同为归一化值） |
| `SdlEvent.FingerPressure()` | 压感 `0..1`，不支持压感的屏幕恒为 `1` |

多点触控不需要额外 API：按 `FingerId()` 把事件归到各自的手指上即可，例子里就是
一张 10 槽的跟踪表。设备侧的查询在 `SdlTouch`：

```zan
int n = SdlTouch.DeviceCount();          // 触摸设备数，0 = 没有触摸屏
long dev = SdlTouch.DeviceAt(0);         // 设备 id
int down = SdlTouch.FingerCount(dev);    // 该设备上正按着几根手指
```

## 手势

SDL3 已经移除了内置手势事件（SDL2 的 dollar/multi-gesture 不再存在），捏合、
旋转、滑动都在手指流上自己算：两根手指时用两点距离的变化率做缩放、用两点连线
的角度差做旋转，单指用 `FingerDelta*` 的累计量做滑动。

## 没有触摸屏时怎么试

桌面机可以让 SDL 把鼠标合成成触摸事件（SDL 的 hint 可直接用同名环境变量给）：

```bash
SDL_MOUSE_TOUCH_EVENTS=1 build/touch_demo
```

此时合成事件的设备 id 等于 `SdlTouch.MouseDeviceId()`，可以据此把真实触摸和
鼠标合成区分开。无显示环境下加 `SDL_VIDEODRIVER=dummy` 可以跑通流程（不会有
手指事件，但窗口/渲染/轮询路径都会走一遍）。
