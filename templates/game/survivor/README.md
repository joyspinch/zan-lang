# Survivor Roguelite 割草模板

基于 Zan.Game 原生底层（深度遮挡队列、弹幕打击感顿帧、震屏与浮动伤害跳字）驱动的现代吸血鬼幸存者肉鸽割草模板。

## 特性

- **手感与视觉反馈**：
  - `GameFeel.ScreenShake`：大范围击杀/受击多阶创伤衰减震屏。
  - `GameFeel.HitStop`：暴击入肉微秒级冻结顿帧，打造强烈打击阻尼感。
  - `FloatingTextPool`：零分配伤害数字抛物线跳字与暴击弹性缩放。
  - `YSortQueue`：2.5D 零 GC 原地排序深度遮挡队列，角色与怪物前后穿插自然无遮挡撕裂。
- **开箱即用玩法闭环**：
  - WASD / 方向键平滑移动。
  - 自动索敌连射武器。
  - 边缘波次怪物潮蜂拥。
  - 经验宝石磁吸掉落与升级强化。

## 运行与编译

```bash
zanc src/SurvivorGame.zan src/main.zan --auto-stdlib --package-project <zan-lang-root> -o survivor.exe
./survivor.exe
```
