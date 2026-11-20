#!/usr/bin/env bash
# Build ttxq-armv8 for Android/arm64. Works on Linux, macOS and Git Bash (Windows).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# --- Locate the Android NDK -------------------------------------------------
if [[ -n "${ANDROID_NDK_HOME:-}" && -d "$ANDROID_NDK_HOME" ]]; then
  NDK="$ANDROID_NDK_HOME"
elif [[ -n "${NDK_ROOT:-}" && -d "$NDK_ROOT" ]]; then
  NDK="$NDK_ROOT"
else
  SDK="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
  if [[ -z "$SDK" && -d "$HOME/AppData/Local/Android/Sdk" ]]; then
    SDK="$HOME/AppData/Local/Android/Sdk"          # Windows default
  fi
  if [[ -z "$SDK" && -d "$HOME/Android/Sdk" ]]; then
    SDK="$HOME/Android/Sdk"                         # Linux/macOS default
  fi
  if [[ -z "$SDK" || ! -d "$SDK/ndk" ]]; then
    echo "error: Android NDK not found. Set ANDROID_NDK_HOME or ANDROID_HOME." >&2
    exit 1
  fi
  NDK="$(find "$SDK/ndk" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1)"
fi

# --- Pick the host toolchain triplet ----------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) HOST="windows-x86_64" ;;
  Darwin*)              HOST="darwin-x86_64"  ;;
  *)                    HOST="linux-x86_64"   ;;
esac
BIN="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
SYSROOT="$NDK/toolchains/llvm/prebuilt/$HOST/sysroot"

if [[ ! -x "$BIN/clang++.exe" && ! -x "$BIN/clang++" ]]; then
  echo "error: clang++ not found under $BIN (host=$HOST)" >&2
  exit 1
fi
CXX="$BIN/clang++"; [[ -x "$CXX" ]] || CXX="$BIN/clang++.exe"
LD="$BIN/ld.lld";   [[ -x "$LD"  ]] || LD="$BIN/ld.lld.exe"

TARGET="${TARGET:-aarch64-linux-android21}"
OUT="${OUT:-bin/ttxq-armv8}"
BUILD="build"
mkdir -p "$BUILD" "$(dirname "$OUT")"

# --- 1. Embed the .so into an object file -----------------------------------
# Output symbols: _binary_libpikafishso_so_start / _end
echo "[1/3] Embedding vendor/libpikafishso.so ..."
"$LD" -m aarch64linux -r -b binary vendor/libpikafishso.so -o "$BUILD/libpikafishso_so.o"

# --- 2. Compile --------------------------------------------------------------
echo "[2/3] Compiling src/ttxq_uci.cpp ..."
"$CXX" --target="$TARGET" -std=c++17 -O2 -static-libstdc++ \
  -I "$SYSROOT/usr/include" -I "$SYSROOT/usr/include/c++/v1" \
  -c src/ttxq_uci.cpp -o "$BUILD/ttxq_uci.o"

# --- 3. Link -----------------------------------------------------------------
echo "[3/3] Linking -> $OUT ..."
"$CXX" --target="$TARGET" -std=c++17 -static-libstdc++ \
  "$BUILD/ttxq_uci.o" "$BUILD/libpikafishso_so.o" -ldl -o "$OUT"

echo "OK: $OUT"
echo "Deploy: adb push $OUT /data/local/tmp/ttxq_armv8 && adb shell chmod +x /data/local/tmp/ttxq_armv8"
