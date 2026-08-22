#!/usr/bin/env sh
# Run zan-mcp as a shared HTTP server for AI clients (Linux / macOS).
#
#   export ZAN_MCP_TOKEN=<secret>
#   scripts/serve-mcp.sh --workspace /srv/projects/foo --host 0.0.0.0
#
# The token is read from the environment (--token-env), never passed on the
# command line where the process list would show it. Never commit one.
# Details and deployment notes: docs/MCP_HOSTING.md
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
workspace=${MCP_ROOT:-$PWD}
bind=${MCP_HOST:-127.0.0.1}
port=${MCP_PORT:-18848}
token_env=ZAN_MCP_TOKEN
skills=
workers=1
frozen=1
read_only=0
no_exec=0
exe=

while [ $# -gt 0 ]; do
    case "$1" in
        --workspace) workspace=$2; shift 2 ;;
        --host)      bind=$2; shift 2 ;;
        --port)      port=$2; shift 2 ;;
        --token-env) token_env=$2; shift 2 ;;
        --skills)    skills=$2; shift 2 ;;
        --workers)   workers=$2; shift 2 ;;
        --exe)       exe=$2; shift 2 ;;
        --read-only) read_only=1; shift ;;
        --no-exec)   no_exec=1; shift ;;
        --no-frozen-tools) frozen=0; shift ;;
        -h|--help)
            sed -n '2,10p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

if [ -z "$exe" ]; then
    for c in "$root/tools/zan-mcp" "$root/build/zan-mcp" "$root/_scratch/zan-mcp"; do
        if [ -x "$c" ]; then exe=$c; break; fi
    done
fi
if [ -z "$exe" ] || [ ! -x "$exe" ]; then
    echo "zan-mcp not found. Build it first:" >&2
    echo "  build/zanc --stdlib-path stdlib tools/mcp_server/mcp_server.zan -o tools/zan-mcp" >&2
    echo "(or pass --exe <path>)" >&2
    exit 1
fi
[ -d "$workspace" ] || { echo "workspace not found: $workspace" >&2; exit 1; }

case "$bind" in
    127.0.0.1|localhost|::1) ;;
    *)
        eval "tok=\${$token_env:-}"
        [ -n "$tok" ] || {
            echo "$bind is reachable from the network and \$$token_env is empty." >&2
            echo "Set a token: export $token_env=<secret>" >&2
            exit 1
        } ;;
esac

set -- "$workspace" --host "$bind" --port "$port" --token-env "$token_env"
if [ "$frozen" = 1 ];      then set -- "$@" --frozen-tools; fi
if [ "$read_only" = 1 ];   then set -- "$@" --read-only; fi
if [ "$no_exec" = 1 ];     then set -- "$@" --no-exec; fi
if [ "$workers" -gt 1 ];   then set -- "$@" --workers "$workers"; fi
if [ -n "$skills" ];       then set -- "$@" --skills "$skills"; fi

echo "zan-mcp  http://$bind:$port/mcp   workspace=$workspace"
echo "clients send: Authorization: Bearer \$$token_env"
exec "$exe" "$@"
