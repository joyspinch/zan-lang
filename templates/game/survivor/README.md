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

在当前主仓根目录运行以下 PowerShell 命令，直接编译模板原始源码，不复制模板树：

```powershell
Set-Location D:/project/zan-lang
& ./scripts/build_gate.ps1 -Name build -Script {
    build/zanc.exe templates/game/survivor/src/main.zan templates/game/survivor/src --auto-stdlib --package-project . --publish --subsystem windows -o build/survivor.exe
}
build/survivor.exe
```

`main.zan` 必须排在首位，随后 `src` 目录递归纳入 `SurvivorGame.zan`、`SurvivorHud.zan` 等全部源码，已列入口不会重复纳入。`--package-project .` 使用当前主仓的 `packages/`；当前模板没有需要额外内嵌的素材目录。生成的原生驱动文件应与程序保持在同一目录。
