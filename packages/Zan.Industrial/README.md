# Zan.Industrial - 工业组态、SCADA 上位机与 HMI 组件套件

`Zan.Industrial` 是面向工控上位机、产线监控屏及嵌入式触摸屏开发的原生工业组态（HMI/SCADA）控件库。

基于 Zan 纯原生 GUI 与确定性整数定点数计算，彻底告别传统 Web/组态软件的高内存、掉帧与浮点精度抖动，专为高可靠连续工业现场打造。

---

## 核心组件与体系

- **过程位号表 (`IoTag` / `TagTable`)**：
  - 工控核心数据基石：全画面所有仪表、趋势、指示灯统一绑定至位号。
  - 一处采集刷新，全图响应；支持工程量量程 (`Range`)、高低报警限 (`Limits`)、定点缩放 (`scale`) 与通讯断线坏值 (`good=false`) 标黄/打叉。
- **圆盘仪表 (`Gauge`)**：
  - 270° 标准工业刻度盘、顺滑指针与数值读数。
  - 根据位号报警限自动呈现低报（蓝）、正常（绿）、高报（红）颜色分区。
- **实时趋势图 (`Trend`)**：
  - 多笔采样曲线同步滚动、定周期推入、固定内存环形缓冲，毫秒级稳定刷新。
- **报警管理系统 (`AlarmTable` / `AlarmBanner`)**：
  - 报警条目实时弹窗与列表归档；支持报警级别区分、闪烁提醒与一键应答消音。
- **设备控制卡片 (`EquipPanel`)**：
  - 针对电机、阀门、泵、输送带的标准控制面板，内置手/自动切换与启停确认安全联锁。
- **触控工业小键盘 (`NumPad`)**：
  - 适配产线工人戴手套触控的大按键虚拟数字键盘，支持上下限输入拦截防错。

---

## 快速上手

```zan
using System;
using Gui;
using Gui.Hmi;

class HmiScreen : Window {
    public HmiScreen() {
        this.Title = "1号反应釜实时监控画面";
        this.SetBounds(0, 0, 1024, 768);

        // 1. 初始化工业位号表
        TagTable io = new TagTable();
        
        // 添加反应釜温度位号：单位 ℃，scale=10 表示以 0.1℃ 为单位的定点整数
        IoTag tTemp = io.Add("TI101", "℃", 10);
        tTemp.Range(0, 1500);       // 0.0 ~ 150.0 ℃
        tTemp.Limits(200, 1200);    // 低于20℃低报，高于120℃高报

        // 2. 绑定仪表控件
        Gauge gauge = new Gauge("反应釜温度", tTemp);
        gauge.SetBounds(50, 50, 260, 260);
        this.Add(gauge);

        // 3. 模拟采集循环更新数据（例如传入 85.6 ℃）
        tTemp.SetRaw(856); 
    }
}
```
