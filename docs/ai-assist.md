# AI-Assisted Development in Zan

This document describes the three pieces that make Zan projects easy for an AI
(or an AI-driven MCP client) to understand and modify:

1. **RepoMap** — a repository structure index (`tools/repomap`).
2. **The MCP bridge contract** — how an MCP server exposes files + indices.
3. **AI settings in Zan IDE** — endpoint / model / token configuration and the
   in-editor AI assistant.

---

## 1. RepoMap — repository structure index

Reading every source file to answer a question is slow and blows the model's
context window. RepoMap emits a compact outline of the whole codebase (a few KB)
so the AI grasps the shape of the project first, then asks for the exact files
it needs.

Build and run:

```
zanc tools/repomap/RepoMap.zan --auto-stdlib -o build/repomap.exe
build/repomap.exe [root]        # root defaults to "."
```

It writes to `<root>/.zanmap/` (git-ignored — regenerate any time):

| File            | Purpose                                                          |
|-----------------|-----------------------------------------------------------------|
| `repo.map.txt`  | Human/AI-readable outline: every file with its classes + method signatures and line numbers. Feed this as AI context. |
| `symbols.json`  | Flat index `[{name,kind,file,line,sig}]` — jump straight to a definition. |
| `routes.json`   | HTTP route table extracted from controller attributes (only when `src/controller` exists): method, path, action, title, auth, login, rank. |

`repo.map.txt` excerpt:

```
src/broker/Broker.zan
  class MqttBroker  :32
    static MqttBroker Create(string host, int port)  :72
    async int Start()  :78
    int Route(string topic, string payload, int plen)  :193
    string MetricsJson()  :398
```

Implementation notes:

- Written with `Substring`/`Length`/char scanning only (several string instance
  methods are unreliable in the current toolchain).
- Brace depth is tracked with a real tokenizer that ignores strings, char
  literals and `//` / `/* */` comments, so JSON-building code such as
  `sb.Append("{")` does not corrupt nesting. Types are captured at depth 0,
  methods at depth 1.
- Skips `_scratch`, `build`, `dist`, `.git`, `assets`, `toolchain`,
  `node_modules`, `.zanmap`, `obj`, `bin`.

---

## 2. MCP bridge contract

An MCP server rooted at the workspace gives the AI safe, structured access to the
project. Required guarantees:

- **Workspace-root confinement**: every path is relative to a single configured
  root; absolute paths and `..` escapes are rejected.
- **Bearer authorization** on every request.

File / command operations (CRUD + shell) are the baseline. On top of them, the
RepoMap artefacts should be surfaced as read-only **MCP resources** so the AI can
pull structure without a filesystem walk:

| Resource URI     | Backed by                    |
|------------------|------------------------------|
| `repo://map`     | `.zanmap/repo.map.txt`       |
| `repo://symbols` | `.zanmap/symbols.json`       |
| `repo://routes`  | `.zanmap/routes.json`        |

A typical AI edit loop:

1. `repo://map` → learn the layout.
2. `repo://symbols` (or `search`) → locate the definition to change.
3. read the file → apply an `apply_patch`/write → run build via `run_command`.

Regenerate the indices (`repomap.exe`) after structural edits so the resources
stay current; the routes index is also produced by the server templates' own
`tools/routegen`.

---

## 3. AI settings in Zan IDE

The IDE ships an AI assistant panel (right-side "AI" tab). It talks to any
OpenAI-compatible `/chat/completions` endpoint.

Configure it in **Editor Settings → AI Assistant**:

- **Endpoint** — OpenAI-compatible base URL ending in `/v1`.
- **Model** — e.g. `gpt-4o-mini`, or any model your endpoint serves.
- **API Key / Token** — bearer token for the endpoint.

Values are stored in `ai.cfg` in the IDE user-config directory as
`key = value` lines. Environment variables `OPENAI_API_KEY`, `OPENAI_BASE_URL`
and `OPENAI_MODEL` are used as a fallback when a field is blank.

When the AI panel sends a message it attaches, as system context:

- the file currently open in the editor, and
- the **RepoMap outline** (`.zanmap/repo.map.txt`) of the project, when present,

so the assistant can reason across the whole codebase — run `repomap.exe` in the
project root once to enable this.
