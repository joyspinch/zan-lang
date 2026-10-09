#!/usr/bin/env bash
# Keep the historical shared-driver entry point; the NativeActivity recipe
# stages all four independent shared/static drivers instead of the old SDL GUI.
# Usage: scripts/build_gui_android.sh [arm64|x64|all] [ndk-root]
set -euo pipefail
exec bash "$(dirname "$0")/build_gui_android_static.sh" "$@"
