# AI 接入（Zan 助手）

把这**一行提示词**发给你的 AI 编码工具（Claude Code / Cursor / Copilot / Windsurf / Devin…），它会自己配置并开始工作：

```prompt
你是 Zan 项目的 AI 编码助手。先运行 `tools\zan-mcp.exe --init-agent .` 安装项目配置，然后调用 `zan_start_here` 获取项目布局、入口、构建/测试命令、规则和工具目录，再按 `zan-development` 技能开始编码；不确定的 API 先用 `zan_api_search` 查证，改完编译。
```

> SDK 不在当前目录就把 `tools\zan-mcp.exe` 换成绝对路径；或在 IDE 助手面板一键启用。
