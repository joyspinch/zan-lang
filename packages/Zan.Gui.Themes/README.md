# Zan.Gui.Themes - 精选高级视觉主题与皮肤套件

`Zan.Gui.Themes` 是面向 Zan GUI 的官方主题包，内含 14 款开箱即用的专业视觉风格，专为现代桌面客户端、企业软件、游戏启动器及极客工具设计。

---

## 包含主题风格清单

| 主题名称 | 风格特征 | 适用场景 |
| :--- | :--- | :--- |
| `chinese` | 新中式古典水墨、朱红青黛、内敛雅致 | 文化软件、国风应用、传统典籍 |
| `brutalism` | 新粗野主义（Neo-Brutalism）、黑粗硬边框、高对比亮黄 | 现代先锋软件、开发者工具、潮牌应用 |
| `darkgold` | 黑金奢华、深邃暗黑基底搭配尊贵流金 | 高级会员后台、金融投资、专业交易终端 |
| `neon` | 霓虹赛博朋克、荧光蓝紫碰撞、高光微辉 | 游戏辅助器、多媒体剪辑、电竞外设驱动 |
| `matrix` | 黑客帝国经典荧光绿黑终端、CRT 扫描质感 | 系统监控、网络安全、黑客控制台 |
| `gamehud` | 战术机甲 HUD、倒角切线、高科技全息蓝 | 游戏启动器、仿真仪表台、工业控制 |
| `liquidglass` | 液态毛玻璃、通透半透明与柔和光影过渡 | 现代操作系统风格、消费级桌面应用 |
| `retroamber` | 70年代复古琥珀单色显示器、温润暖橙色系 | 复古计算器、音响控制台、极客文本工具 |
| `emerald` | 祖母绿宝石质感、深邃雅致墨绿 | 办公效率、文档处理、医学与自然科学 |
| `sunset` | 暮色余晖渐变、黄昏紫红与暖阳橙相融 | 摄影后期、音乐播放器、生活方式应用 |
| `dreamy` | 梦幻粉彩马卡龙、柔和微甜渐变 | 少女风、萌系应用、笔记日记 |
| `peachblossom` | 阳春桃花、淡粉素雅微风拂面 | 诗词排版、轻量生活小工具 |
| `fortune` | 新春吉庆、金榜题名大红与金黄 | 节日庆典、抽奖活动、开业大吉 |
| `mono` | 极致黑白单色、绝对克制与极高排版信息密度 | 专业代码审查、技术文档阅读、财务报表 |

---

## 如何使用

在 Zan GUI 项目中，可以通过加载对应皮肤的 `skin.css` 快速切换应用程序整体风格：

```zan
using System;
using System.IO;
using Gui;

class Program {
    static void Main() {
        Window win = new Window();
        win.Title = "主题演示";
        win.SetBounds(100, 100, 800, 600);

        // 加载指定主题（例如：新中式 chinese 或 黑客帝国 matrix）
        string themeName = "chinese";
        string cssPath = "packages/Zan.Gui.Themes/skins/" + themeName + "/skin.css";
        
        if (File.Exists(cssPath)) {
            string css = File.ReadAllText(cssPath);
            win.SetStyleSheet(css);
        }

        win.Show();
        Application.Run();
    }
}
```
