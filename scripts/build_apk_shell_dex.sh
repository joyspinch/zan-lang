#!/bin/bash
# Rebuild toolchain/apk-shell/classes.dex from the vendored Java sources
# (toolchain/apk-shell/java: the SDL3 activity shell + org.zan.app.ZanWeb,
# the system-WebView bridge backing Gui.Component.WebView on Android).
#
# Needs a JDK (javac) and an Android SDK (android.jar + build-tools d8).
# SDK location: $ANDROID_SDK_ROOT, defaulting to %LOCALAPPDATA%/Android/Sdk.
#
#   scripts/build_apk_shell_dex.sh
set -e
cd "$(dirname "$0")/.."
SDK="${ANDROID_SDK_ROOT:-$LOCALAPPDATA/Android/Sdk}"
BT="${ZAN_BT:-$SDK/build-tools/35.0.0}"
PLAT="${ZAN_PLATFORM_JAR:-$SDK/platforms/android-35/android.jar}"
OUT=_scratch/apk-dex

test -x "$(command -v javac)" || { echo "javac not found (need a JDK)" >&2; exit 1; }
test -f "$PLAT" || { echo "android.jar not found at $PLAT" >&2; exit 1; }
test -f "$BT/d8.bat" || test -f "$BT/d8" || { echo "d8 not found in $BT" >&2; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT/classes"
find toolchain/apk-shell/java -name '*.java' > "$OUT/sources.txt"
echo "compiling $(wc -l < "$OUT/sources.txt") java files..."
if ! javac -source 11 -target 11 -nowarn -encoding UTF-8 -classpath "$PLAT" \
        -d "$OUT/classes" @"$OUT/sources.txt" > "$OUT/javac.log" 2>&1; then
    cat "$OUT/javac.log" >&2
    exit 1
fi

D8="$BT/d8.bat"; [ -f "$D8" ] || D8="$BT/d8"
"$D8" --release --lib "$PLAT" --min-api 28 \
    --output "$OUT" $(find "$OUT/classes" -name '*.class')
test -f "$OUT/classes.dex"

cp "$OUT/classes.dex" toolchain/apk-shell/classes.dex
cp "$OUT/classes.dex" build/apk-shell/classes.dex 2>/dev/null || true
echo "classes.dex -> toolchain/apk-shell/ ($(wc -c < "$OUT/classes.dex") bytes)"
