# Zan.Hardware

跨平台系统硬件感知与系统服务包（无胶水、纯 C-ABI 原生 NDK 绑定）：
- **IMU 惯性运动传感器**：`Accelerometer`（三轴加速度）、`Gyroscope`（三轴角速度）、`Compass`（电子罗盘航向）
- **系统通知中心**：`Notification`（支持链式配置标题、正文、角标、提示音）
- **能源与电源感知**：`Battery`（电量百分比、充电检测、低电量降频预警）
- **网络状态感知**：`Network`（WiFi、蜂窝移动流量、有线以太网、断网探测）
- **系统暗黑模式感知**：`SystemTheme`（深色/浅色模式跟随）
- **系统原生分享面板**：`Share`（ShareSheet 调起）
- **系统相册与文件选择器**：`NativePicker`（系统原生 PhotoPicker / DocumentPicker）
- **统一动态权限中枢**：`Permissions`（定位、相机、麦克风、通知、相册授权）
- **屏幕防休眠与亮度控制**：`Screen`（防熄屏 KeepAwake、窗口亮度调节与还原）
- **GPS 高精度物理定位**：`Geolocation`（经纬度、海拔、精度、速度及运动快照）
- **设备标识与运行环境**：`DeviceInfo`（型号、制造商、系统版本、折叠屏硬件识别）
- **生物识别与安全认证**：`Biometrics`（TEE 安全人脸 Face ID / 屏下指纹鉴权）
