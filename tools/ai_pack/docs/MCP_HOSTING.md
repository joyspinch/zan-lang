# Hosting the Zan MCP server

The MCP server can host a workspace over stdio or HTTP. Knowledge files are
server-side artifacts: clients do not need local copies of `symbols.json`,
`gallery.json`, or `form.json`.

From the repository root, build the server or generate the source-driven gallery.
Both tools compile the shared Gallery sources. The MCP server also uses the
semantic index emitter shared with `zanindex`:

```sh
build/zanc --stdlib-path stdlib tools/mcp_server/*.zan tools/shared/gallery/*.zan tools/shared/semantic_index/*.zan -o build/zan-mcp
build/zanc --stdlib-path stdlib tools/zanindex/ZanIndex.zan tools/shared/semantic_index/*.zan -o build/zanindex
build/zanc --stdlib-path stdlib tools/genknowledge/GenKnowledge.zan tools/shared/gallery/*.zan -o build/genknowledge
build/genknowledge --root . --stdlib stdlib --out knowledge --seed tools/mcp_server/gallery.json
```

After the server starts, `zan_start_here` reports whether the knowledge
artifacts are present and fresh. A mutable server with the source inputs
advertises `zan_refresh_knowledge`, which regenerates `gallery.json` in process.
It does not build RepoMap or replace `symbols.json`;
generate that index separately when it is missing.

For a shared HTTP deployment, bind to a non-loopback address only with bearer
authentication:

```text
tools/zan-mcp.exe . --host 0.0.0.0 --token-env ZAN_MCP_TOKEN
```

Use `--read-only` for deployments that must not write workspace files.
`--frozen-tools` keeps the advertised catalog stable across workspaces; calls
to unavailable tools still return a diagnostic.
