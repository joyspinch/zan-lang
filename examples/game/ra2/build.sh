#!/usr/bin/env bash
# Builds the RA2 HD demo. zanc's --auto-stdlib only resolves `using X.Y;` to
# stdlib/X/Y, so the example's own multi-file sources -- and the shared GameKit
# window/renderer host under examples/game/common -- must be listed explicitly.
set -e
root="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$root"
zanc=build/zanc
[ -x "$zanc" ] || zanc=build/zanc.exe
"$zanc" examples/game/ra2/main.zan \
  examples/game/ra2/formats/*.zan \
  examples/game/ra2/assets/*.zan \
  examples/game/ra2/render/*.zan \
  examples/game/ra2/game/*.zan \
  examples/game/common/GameKit.zan \
  --auto-stdlib -o build/ra2.exe
