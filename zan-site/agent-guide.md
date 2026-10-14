# Zan: a practical entry point for AI agents

Zan is an independent language with C#-style syntax, LLVM AOT compilation, and ARC. It is not a .NET runtime and does not directly consume NuGet assemblies. This document describes a local SDK workflow; the showcase website does not compile code or connect to a user's MCP server.

## Get the IDE and packages

The official distribution repository is https://github.com/joyspinch/zanlang. It contains the packaged Windows SDK, including `ZanIDE.exe`, compiler, DLLs and standard library. Its documented download path is GitHub's **Code → Download ZIP**; extract the whole archive and open `ZanIDE.exe`. Keep `toolchain/`, `stdlib/` and the bundled DLLs together. Ordinary use does not require installing LLVM / clang separately or building the compiler.

Read https://github.com/joyspinch/zanlang/blob/main/AI_README.md and the repository's current README before setup. At the time of this website update, the Releases list is empty, so do not send the user there as the only download route or invent release asset filenames. Runtime/sysroot directories for other targets do not establish the availability of native IDE downloads for those systems. Additional packages and platform downloads depend on what the distribution repository actually provides.

For a beginner, inspect their operating system and existing tools, then guide one setup step at a time in plain language. Prefer the packaged SDK and its documentation; the user does not need to browse the language's source repository or build the compiler. If the available SDK does not match the user's system, explain what is missing before proceeding.

## Find the actual environment

Ask the project environment for the SDK and project paths. Do not assume the example paths below exist. A Windows SDK root contains `ZanIDE.exe`, `toolchain/zanc.exe`, `tools/zan-mcp.exe`, `stdlib/`, `knowledge/`, and onboarding documentation. Create applications outside the SDK directory.

Read the project's `AGENTS.md` and applicable `.agents/skills/`. SDK guidance is in `AI_README.md`, `llms.txt`, and `docs/AI_ONBOARDING.md`.

## Initialize a new project's AI configuration

Example Windows PowerShell commands, assuming the SDK is at `D:\Zan`:

```powershell
New-Item -ItemType Directory -Force "D:\work\hello-zan" | Out-Null
& "D:\Zan\tools\zan-mcp.exe" --init-agent "D:\work\hello-zan"
```

The target directory must exist. The initializer installs project rules, skills, and configuration for Claude Code (`.mcp.json`), Cursor (`.cursor/mcp.json`), and VS Code (`.vscode/mcp.json`). It skips existing files instead of merging their JSON. Inspect and merge existing configuration explicitly; `--force` overwrites it and is not a routine setup step.

This command does not create application source, compile the project, or install an AI editor. Open the project directory in the client and enable its local MCP server. The client launches the stdio process using `--stdio <project>`. Client support and approval UI depend on the editor version.

## Work from evidence

1. Call `zan_start_here` when MCP is available. Verify the project root and actual entry point.
2. Use `zan_example` to inspect an appropriate example, and `zan_api_search` / `zan_lookup_symbol` / `zan_list_members` to obtain real API signatures. Read focused source through `zan_read_context` when needed.
3. Implement one small, testable application behavior using existing packages and components.
4. Compile the whole project using `zan_build_project`, explicitly supplying a nondefault entry. `zan_compile` is useful for snippets. Read `ok`, `exitCode`, `diagnostics`, and raw output rather than assuming success.
5. Run the affected program and verify the requested behavior. Compiler success is not a runtime test. For a GUI, inspect the actual window and interaction.
6. Report changed files, commands executed, observed results, and remaining limitations. Never report an unexecuted check as passing.

MCP entry detection only covers common paths such as `src/main.zan` and `src/App.zan`; IDE-created GUI entries can be renamed. Check the project's actual files. Reference/impact queries can return text candidates and do not substitute for compiler semantic analysis.

## If MCP is unavailable

Use the SDK's local documentation and API index:

```powershell
& "D:\Zan\toolchain\zanc.exe" --api WriteLine
```

For a console application whose actual entry is `src/main.zan`:

```powershell
Set-Location "D:\work\hello-zan"
New-Item -ItemType Directory -Force .\build | Out-Null
& "D:\Zan\toolchain\zanc.exe" .\src\main.zan --auto-stdlib -o .\build\app.exe
if ($LASTEXITCODE -eq 0) { & .\build\app.exe }
```

Add `--publish` for a release build. Native dependencies must match the target. Platform compiler support, GUI implementation, packaging and actual device validation are distinct capabilities.

## Official entry points

- IDE / SDK and package distribution: https://github.com/joyspinch/zanlang
- Online AI onboarding: https://github.com/joyspinch/zanlang/blob/main/AI_README.md
- Feedback: https://github.com/joyspinch/zanlang/issues
- AI onboarding: the installed SDK's `AI_README.md`, `llms.txt`, and `docs/AI_ONBOARDING.md`.
- API and CLI reference: the documentation and API index bundled with the installed version.
- Platform and package availability: the distribution repository's current README, packaged contents and version notes.

The showcase's AI tool calls and ARC animation are conceptual demonstrations, not live compiler results. The website's package catalog describes capabilities, not a guarantee that every package is downloadable in every release. Use the installed SDK's APIs and actual diagnostics when documentation and SDK versions differ.
