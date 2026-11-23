# Zan.Hardware

跨平台系统硬件感知与系统服务包（无 GUI 反向依赖，服务端与 CLI 可直接无头使用）：
- **设备标识与运行环境**：`DeviceInfo`（型号、制造商、系统版本、折叠屏硬件识别；桌面/服务端无头读取 OS/SMBIOS）
- **能源与电源感知**：`Battery`（电量百分比、充电检测、低电量降频预警）
- **网络状态感知**：`Network`（Wi-Fi、有线以太网、断网/未知探测；桌面/服务端无头探测活动网卡）
- **系统暗黑模式感知**：`SystemTheme`（深色/浅色模式跟随）
- **宿主桥接契约（可选）**：`IHardwareHost` / `HardwareHost`（由桌面 GUI 或移动端容器启动时注入，无需在硬件包引入 `Gui.*`）
- **IMU 惯性运动传感器**：`Accelerometer`、`Gyroscope`、`Compass`（需宿主或 `Inject()` 注入；无头默认 `IsSupported() == false`）
- **系统通知中心**：`Notification`（无宿主时 `Send()` 返回 `-1`）
- **系统原生分享 / 选择器 / 状态栏 / 屏幕**：`Share`、`NativePicker`、`SystemBar`、`Screen`
- **统一动态权限中枢**：`Permissions`（无宿主时返回 `PermissionStatus.Unknown`）
- **GPS 高精度物理定位**：`Geolocation`（需权限与宿主或 `Inject()`）
- **生物识别与安全认证**：`Biometrics`（无宿主时 `CanAuthenticate() == false`）
- **外部深度链接与唤醒**：`DeepLink`（URL Scheme / Universal Link 取出与协议解析）
