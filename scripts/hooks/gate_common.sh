#!/bin/sh
# Shared helpers for the git gate hooks in this directory. The hooks are
# enabled via `git config core.hooksPath scripts/hooks` (set automatically at
# CMake configure time -- see the top of the root CMakeLists.txt). Pure POSIX
# sh + git, so they behave identically on Git-for-Windows and Linux.

# ---- helpers ------------------------------------------------------------

# Hooks can fire from odd cwd's; every gate bails out (exit 0, no blocking)
# when invoked outside a work tree.
repo_guard() {
    [ "$(git rev-parse --is-inside-work-tree 2>/dev/null)" = "true" ]
}

gate_tmp() {
    # One scratch dir per hook invocation, cleaned by the caller on exit.
    _ZAN_GATE_TMP="$(mktemp -d 2>/dev/null)" || _ZAN_GATE_TMP=""
    if [ -z "$_ZAN_GATE_TMP" ]; then
        # Some minimal environments lack mktemp; fall back deterministically.
        _ZAN_GATE_TMP="${TMPDIR:-/tmp}/zan-gate-$$"
        rm -rf "$_ZAN_GATE_TMP"
        mkdir -p "$_ZAN_GATE_TMP" || return 1
    fi
}

gate_cleanup() {
    [ -n "$_ZAN_GATE_TMP" ] && rm -rf "$_ZAN_GATE_TMP"
}

# Repo-root-relative listing of tracked files changed on the ref being pushed
# (pre-push: $local_sha is the tip being pushed). Empty output if none.
push_changed_paths() {
    _pcp_local="$1"; _pcp_remote="$2"
    if [ -z "$_pcp_local" ] || [ -z "$_pcp_remote" ] ||
       [ "$_pcp_local" = "0000000000000000000000000000000000000000" ] ||
       [ "$_pcp_remote" = "0000000000000000000000000000000000000000" ]; then
        return 0
    fi
    # Commits the remote does not have, oldest first.
    git rev-list --reverse "$_pcp_remote..$_pcp_local" 2>/dev/null
}

# ---- check: conflict markers ---------------------------------------------
# AGENTS.md rules 11/12 / WORKSPACE_CONVENTIONS.md 9.1: committing <<<<<<<
# markers is a forbidden "fix". Bulk one-sided resolves cannot be detected
# directly, but any unresolved UU left behind shows up as leftover markers in
# the content being committed.

# Args: mode -- "staged" (pre-commit: inspect the index) or "push" (pre-push:
# inspect each commit about to leave the machine).
gate_check_conflict_markers() {
    _gcm_mode="$1"
    : > "$_ZAN_GATE_TMP/markers"
    if [ "$_gcm_mode" = "staged" ]; then
        # Only what is actually being committed.
        git grep -IlnE --cached -e '^(<{7}( |$)|\|{7}( |$)|>{7}( |$))' \
            > "$_ZAN_GATE_TMP/markers" 2>/dev/null || true
        _gcm_label="staged content"
    else
        while IFS= read -r _gcm_c; do
            [ -n "$_gcm_c" ] || continue
            git grep -IlnE -e '^(<{7}( |$)|\|{7}( |$)|>{7}( |$))' \
                "$_gcm_c" -- >> "$_ZAN_GATE_TMP/markers" 2>/dev/null || true
        done < "$_ZAN_GATE_TMP/commits"
        _gcm_label="commits being pushed"
    fi
    if [ -s "$_ZAN_GATE_TMP/markers" ]; then
        echo "zan-gate: conflict markers found in $_gcm_label:" >&2
        sed 's/^/  /' "$_ZAN_GATE_TMP/markers" >&2
        echo "  (AGENTS.md rules 11/12, WORKSPACE_CONVENTIONS.md 9.1)" >&2
        echo "  UU conflicts are merged BY HAND, not resolved away:" >&2
        echo "    git show :2:<file>   # HEAD side" >&2
        echo "    git show :3:<file>   # in-flight side" >&2
        echo "  Keep committed features unconditionally, fold in genuinely new" >&2
        echo "  work, then 'git add <file>'. One-sided bulk resolves" >&2
        echo "  (checkout --ours/--theirs, reset --hard) are forbidden." >&2
        return 1
    fi
    return 0
}

# ---- check: tracked .zan/.c/.h blobs larger than 8 MiB --------------------
# Accidental binary drops (logs, database files, build products) ride in as
# "source" when they carry a source-looking extension. The repo keeps big
# payloads out of the source tree (AGENTS.md rules 1-3).
GATE_MAX_BLOB_BYTES=8388608

gate_check_blob_size() {
    _gbs_mode="$1"
    : > "$_ZAN_GATE_TMP/bigblobs"
    if [ "$_gbs_mode" = "staged" ]; then
        git diff --cached --name-only -z | while IFS= read -r -d '' _gbs_p; do
            _gbs_sz="$(git cat-file -s ":$_gbs_p" 2>/dev/null)" || continue
            [ -n "$_gbs_sz" ] || continue
            if [ "$_gbs_sz" -gt "$GATE_MAX_BLOB_BYTES" ]; then
                printf '%s %s\n' "$_gbs_sz" "$_gbs_p"
            fi
        done >> "$_ZAN_GATE_TMP/bigblobs"
        _gbs_label="staged"
    else
        while IFS= read -r _gbs_c; do
            [ -n "$_gbs_c" ] || continue
            git diff-tree --no-commit-id --name-only -r -z "$_gbs_c" |
            while IFS= read -r -d '' _gbs_p; do
                _gbs_sz="$(git cat-file -s "$_gbs_c:$_gbs_p" 2>/dev/null)" || continue
                [ -n "$_gbs_sz" ] || continue
                if [ "$_gbs_sz" -gt "$GATE_MAX_BLOB_BYTES" ]; then
                    printf '%s %s\n' "$_gbs_sz" "$_gbs_p"
                fi
            done
        done < "$_ZAN_GATE_TMP/commits" >> "$_ZAN_GATE_TMP/bigblobs"
        _gbs_label="pushed"
    fi
    if [ -s "$_ZAN_GATE_TMP/bigblobs" ]; then
        echo "zan-gate: blobs larger than 8 MiB found in $_gbs_label content:" >&2
        sort -rn "$_ZAN_GATE_TMP/bigblobs" | awk '{ printf "  %6.1f MiB  %s\n", $1/1048576, $2 }' >&2
        echo "  (AGENTS.md rules 1-3: build products, logs and dumps belong in" >&2
        echo "  build/ or _scratch/ -- both git-ignored -- not in the source tree.)" >&2
        return 1
    fi
    return 0
}
