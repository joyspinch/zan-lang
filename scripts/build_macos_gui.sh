#!/usr/bin/env bash
# Build/stage independent Cocoa GUI, image, audio and game shared/static drivers.
# Usage: scripts/build_macos_gui.sh [macos-arm64|macos-x64|both] (default both)
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$(uname -s)" != "Darwin" ]; then
    echo "build_macos_gui.sh must run on macOS (needs Cocoa/WebKit headers)" >&2
    exit 1
fi
want="${1:-both}"

build() {
    local target="$1" arch="$2" driver dest
    local work="build/native_drivers/$target"
    local gui="packages/Zan.Gui/src/Gui/drivers/$target"
    local image="packages/Zan.Image/src/System/Drawing/Imaging/drivers/$target"
    local audio="packages/Zan.Desktop/src/System/Media/drivers/$target"
    local game="packages/Zan.Game/src/Game/Graphics/drivers/$target"
    local flags=(-O2 -g0 -DNDEBUG -fPIC -arch "$arch" -mmacosx-version-min=11.0)
    local frameworks=(-framework Cocoa -framework CoreText -framework QuartzCore -framework IOSurface -framework WebKit)
    mkdir -p "$work" "$gui/static" "$image/static" "$audio/static" "$game/static"
    xcrun clang "${flags[@]}" -DZAN_GUI_COCOA -DZAN_GUI_STATIC -c src/runtime/gui_runtime.c -o "$work/gui.o"
    xcrun clang "${flags[@]}" -DZAN_GUI_COCOA -c src/runtime/gui_runtime_mac.m -o "$work/cocoa.o"
    xcrun clang "${flags[@]}" -DZAN_IMAGE_STATIC -c src/runtime/zan_image.c -o "$work/image.o"
    xcrun clang "${flags[@]}" -DZAN_AUDIO_STATIC -c src/runtime/zan_audio.c -o "$work/audio.o"
    xcrun clang "${flags[@]}" -DZAN_GAME_STATIC -DZAN_GUI_COCOA -c src/runtime/zan_game.c -o "$work/game.o"

    xcrun clang "${flags[@]}" -dynamiclib -install_name @rpath/libzan_gui.dylib -o "$gui/libzan_gui.dylib" "$work/gui.o" "$work/cocoa.o" -lm "${frameworks[@]}"
    xcrun clang "${flags[@]}" -dynamiclib -install_name @rpath/libzan_image.dylib -o "$image/libzan_image.dylib" "$work/image.o" -lm
    # CoreAudio is loaded by zan_audio; no GUI or Cocoa dependency is needed.
    xcrun clang "${flags[@]}" -dynamiclib -install_name @rpath/libzan_audio.dylib -o "$audio/libzan_audio.dylib" "$work/audio.o" -lpthread -ldl -lm
    xcrun clang "${flags[@]}" -dynamiclib -install_name @rpath/libzan_game.dylib -Wl,-rpath,@loader_path -o "$game/libzan_game.dylib" "$work/game.o" -L"$gui" -L"$image" -lzan_gui -lzan_image -lm
    rm -f "$gui/static/libzan_gui.a" "$image/static/libzan_image.a" "$audio/static/libzan_audio.a" "$game/static/libzan_game.a"
    xcrun ar rcs "$gui/static/libzan_gui.a" "$work/gui.o" "$work/cocoa.o"
    xcrun ar rcs "$image/static/libzan_image.a" "$work/image.o"
    xcrun ar rcs "$audio/static/libzan_audio.a" "$work/audio.o"
    xcrun ar rcs "$game/static/libzan_game.a" "$work/game.o"
    printf 'm\nobjc\n@framework/Cocoa\n@framework/CoreText\n@framework/QuartzCore\n@framework/IOSurface\n@framework/WebKit\n' > "$gui/static/zan_gui.libs"
    printf 'm\n' > "$image/static/zan_image.libs"
    printf 'pthread\ndl\nm\n' > "$audio/static/zan_audio.libs"
    { printf 'zan_gui\nzan_image\n'; cat "$gui/static/zan_gui.libs"; } > "$game/static/zan_game.libs"
    for driver in zan_gui zan_image zan_audio zan_game; do
        case "$driver" in
            zan_gui) dest="$gui" ;; zan_image) dest="$image" ;;
            zan_audio) dest="$audio" ;; zan_game) dest="$game" ;;
        esac
        codesign --force --sign - "$dest/lib$driver.dylib"
        if [ "$driver" = zan_game ]; then
            printf '@driver/zan_gui\n@driver/zan_image\nlibzan_game.dylib\n' > "$dest/$driver.bundle"
        else
            printf 'lib%s.dylib\n' "$driver" > "$dest/$driver.bundle"
        fi
        echo "built $dest/lib$driver.dylib + static/lib$driver.a"
    done
}
case "$want" in
    macos-arm64) build macos-arm64 arm64 ;;
    macos-x64) build macos-x64 x86_64 ;;
    both) build macos-arm64 arm64; build macos-x64 x86_64 ;;
    *) echo "usage: $0 [macos-arm64|macos-x64|both]" >&2; exit 2 ;;
esac
