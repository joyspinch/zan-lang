# examples/game/ra2 — 红色警戒 2 HD 演示

用 Zan 重写的《红色警戒 2》等轴测 RTS 演示：直接读取玩家自己的原版
MIX 资源包，用 SDL3 以 HD 分辨率渲染。

## 准备资源（必需）

本仓库**不附带任何原版素材**。你需要自备一份合法的《红色警戒 2》安装目录，
整个拷到：

```
examples/game/ra2/data/
```

程序开窗前会先检查以下资源包是否就位，缺任何一个都会打印提示并退出：

- `ra2.mix`
- `language.mix`
- `multi.mix`

`data/` 已在 `.gitignore` 中，永不入库。

## 编译运行

在仓库根目录执行：

```
bash examples/game/ra2/build.sh
./build/ra2.exe
```

资源就位时输出：

```
assets found in examples/game/ra2/data
```
