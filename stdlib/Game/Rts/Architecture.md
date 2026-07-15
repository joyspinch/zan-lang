# Game.Rts architecture

The library is a modern, clean-room RA2-compatible runtime. It does not patch
or embed the original game executable and it does not redistribute original
assets.

The planned ownership boundaries are:

1. `Install`: validate a user-owned RA2/Yuri installation.
2. `Formats`: parse MIX, INI, CSF, SHP(TS), VXL, HVA, TMP, PAL and map files.
3. `Assets`: normalize original and HD replacement assets behind stable IDs.
4. `Simulation`: deterministic fixed-tick game state with no rendering calls.
5. `Presentation`: SDL3 rendering, input and audio.
6. `Networking`: deterministic lockstep commands, replays and desync checks.
7. `Tools`: cache builder, map viewer and format diagnostics.

The first implementation only establishes `Install`, configuration and the
SDL3 host. Format parsing should be added in the order `MIX -> INI/CSF ->
PAL/SHP -> TMP/map -> VXL/HVA`, because each step produces a separately
testable viewer or diagnostic tool.
