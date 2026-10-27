# Match-3 Puzzle 模板

基于 Zan.Game 三消专用底层 `Match3Board` 驱动的经典消除游戏模板。

## 特性

- **开箱即用规则**：相邻合法性验证（`ValidateSwap`）、三连以上消除扫描、重力掉落与连击级联结算（`ResolveCascades`）。
- **动效与手感**：内置 `FloatingTextPool` 伤害/消除加分飘字，连击暴击与动画淡出。
- **高拓展性**：开发者只需替换 `gemColors` 或替换为贴图素材，即可快速制作各类商业消除游戏。

## 运行与编译

```bash
zanc src/main.zan --auto-stdlib --package-project <zan-lang-root> -o match3.exe
./match3.exe
```
