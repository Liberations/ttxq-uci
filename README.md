# ttxq-armv8

A **standard stdin/stdout UCI engine** for Android/arm64, built by wrapping the
`libpikafishso.so` library extracted from the 天天象棋 (Tencent QQ Chess) app.

The engine reports itself as:

```
id name ttxq-armv8
```

The upstream `.so` is **not modified** — it is embedded into this binary and driven
through its original JNI interface. The brand line is rewritten on the fly.

---

## Why this exists

The .so shipped in the app is a full [Pikafish](https://github.com/official-pikafish/Pikafish)
(Xiangqi / Chinese chess) engine, but it exposes **only JNI entry points**
(`startEngine` / `engineRun` / `getReply` / `stopEngine`) — there is no `main()`, so
you cannot run it directly as a UCI engine. This project provides a thin native
wrapper that turns it into a drop-in UCI binary usable by any chess GUI.

---

## Features

- **Single file**: the `.so` is embedded in the executable and loaded from a `memfd`
  at runtime (nothing is written to disk).
- **Standard UCI**: plain stdin/stdout; works interactively in `adb shell` and with GUIs.
- **No Android runtime needed**: a minimal `JNIEnv` vtable is synthesized, so the
  binary runs as a native Android executable — no JVM, no app install.
- **NNUE auto-detection**: finds `pikafish.nnue` next to the binary, on env
  `TTXQ_NNUE`, or in common staging dirs; `setoption name EvalFile ...` still works.
- **Interactive-safe**: a dedicated thread drains engine output so typing commands
  in a real terminal never blocks.

---

## Repository layout

```
.
├── src/ttxq_uci.cpp          # the wrapper (all logic here)
├── vendor/libpikafishso.so   # engine binary extracted from the APK
├── tools/extract_assets.sh   # re-extract .so + nnue from an APK
├── build.sh / build.bat      # build scripts
├── Makefile                  # alternative build
└── bin/ttxq-armv8            # build output
```

`pikafish.nnue` (~33 MB) is **not** committed. See [Getting the NNUE network](#getting-the-nnue-network).

---

## Building

Requirements: Android NDK (r21+; tested with r27), a C++17 compiler, and `ld.lld`
(all ship with the NDK). Set `ANDROID_NDK_HOME` to your NDK directory.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./build.sh                       # Linux / macOS / Git Bash
```

Windows:

```bat
set ANDROID_NDK_HOME=C:\path\to\android-ndk
build.bat
```

Or with make:

```bash
make ANDROID_NDK_HOME=/path/to/android-ndk
```

The output is `bin/ttxq-armv8` (ELF64 AArch64, ~1.4 MB).

<details>
<summary>What the build script does</summary>

1. `ld.lld -m aarch64linux -r -b binary vendor/libpikafishso.so -o build/libpikafishso_so.o`
   — turns the .so into a data object exposing `_binary_vendor_libpikafishso_so_start/end`
   (the symbol name is derived from the input path).
2. Compiles `src/ttxq_uci.cpp` with `clang++ --target=aarch64-linux-android21`.
3. Links both with `-static-libstdc++ -ldl` into `bin/ttxq-armv8`.

</details>

---

## Deploy & use

```bash
# Push the engine and the network file
adb push bin/ttxq-armv8 /data/local/tmp/ttxq-armv8
adb push pikafish.nnue  /data/local/tmp/pikafish.nnue
adb shell chmod +x /data/local/tmp/ttxq-armv8

# Run it interactively
adb shell /data/local/tmp/ttxq-armv8
```

Then type UCI commands:

```
uci
isready
position startpos
go depth 8
quit
```

Example output:

```
id name ttxq-armv8
id author the Pikafish developers (see AUTHORS file)
option name Threads type spin default 1 min 1 max 1024
...
uciok
readyok
info depth 1 ... pv g3g4
...
bestmove b2e2
```

### NNUE location

If `pikafish.nnue` is not next to the binary, set its path explicitly either via
the environment variable or a standard UCI option:

```bash
TTXQ_NNUE=/data/local/tmp/pikafish.nnue adb shell /data/local/tmp/ttxq-armv8
```

```
setoption name EvalFile value /data/local/tmp/pikafish.nnue
```

Without a loadable NNUE the engine answers `uci`/`isready` but a `go` command
will not produce a real move.

### Getting the NNUE network

From the app APK (same source as the .so):

```bash
./tools/extract_assets.sh /path/to/ttxq.apk .
# -> ./pikafish.nnue
```

Or fetch an official Pikafish network from
<https://github.com/official-pikafish/Networks/releases> (network format must match
this engine build; the one bundled in the app is known to work).

---

## Implementation notes (the hard parts)

These are non-obvious details discovered while building; keep them in mind if you
modify the wrapper:

1. **Fake JNIEnv offsets** — `NewStringUTF` / `GetStringUTFChars` /
   `ReleaseStringUTFChars` live at vtable offsets `0x538` / `0x548` / `0x550`
   (verified against the NDK's `jni.h`). Only these three slots are needed.
2. **TTY hang in `startEngine()`** — the native init waits on terminal input when
   stdin is a TTY, so the wrapper dup's the real input to fd 10 and points fd 0 at
   `/dev/null` before starting the engine. This is what makes interactive `adb shell`
   sessions work.
3. **Separate output thread** — `getReply()` can block; polling it from the main
   thread would stall input handling. A dedicated pump thread owns `getReply()` and
   forwards to stdout; the main thread only reads stdin and calls `engineRun()`.
4. **Load from memfd** — `dlopen("/proc/self/fd/N")` keeps the .so in memory with no
   temp file, mirroring techniques used by other embedded-engine wrappers.

---

## Credits & legal

- The bundled engine is **Pikafish**, developed by the official-pikafish team and
  licensed under the **GNU GPL v3.0**. See <https://github.com/official-pikafish/Pikafish>.
  The `libpikafishso.so` and `pikafish.nnue` files are extracted from the 天天象棋
  app for interoperability; all rights remain with their respective owners.
- This wrapper is released under **GPL-3.0-or-later** (see [LICENSE](LICENSE)) to be
  compatible with the embedded engine.
- This project is a technical interoperability experiment. Use of the upstream
  binary and network must comply with their licenses and applicable law.

`ttxq-armv8` is not affiliated with or endorsed by Tencent or the Pikafish project.
