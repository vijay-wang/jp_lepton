#!/usr/bin/env bash
set -e

BUILD_DIR=build_sdk

rm -rf "$BUILD_DIR"
mkdir "$BUILD_DIR"
cd "$BUILD_DIR"

BAT_FILE="../build_windows.bat"

if [ ! -f "$BAT_FILE" ]; then
    echo "ERROR: $BAT_FILE not found"
    exit 1
fi

WIN_BAT=$(cygpath -w "$(realpath "$BAT_FILE")")

echo "[$WIN_BAT]"

cmd.exe //c "$WIN_BAT"
