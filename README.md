# ttxq-armv8

> 中文说明：[下方](#简介) ｜ English: [below](#english)

把从「天天象棋」App 中提取的 `libpikafishso.so` 包装成一个**标准 stdin/stdout UCI 引擎**，运行于 Android/arm64。引擎自报：

```
id name ttxq-armv8
```

原始 `.so` **未做任何修改**——它被内嵌进本程序，通过原有 JNI 接口驱动；`id name` 是在输出时实时替换的。

A **standard stdin/stdout UCI engine** for Android/arm64, built by wrapping the
`libpikafishso.so` library extracted from the 天天象棋 (Tencent QQ Chess) app.
The engine reports itself as `id name ttxq-armv8`. The upstream `.so` is **not
modified** — it is embedded into this binary and driven through its original JNI
interface; the brand line is rewritten on the fly.

---

## 简介 / Why this exists

**中文**：App 内的 so 其实是一个完整的 [Pikafish](https://github.com/official-pikafish/Pikafish)（中国象棋）引擎，但它**只暴露 JNI 入口**（`startEngine` / `engineRun` / `getReply` / `stopEngine`），没有 `main()`，因此无法直接作为 UCI 引擎运行。本项目用一个轻量的 native 包装层，把它变成任何象棋 GUI 都能用的 UCI 二进制。

**English**: The .so shipped in the app is a full [Pikafish](https://github.com/official-pikafish/Pikafish)
(Xiangqi / Chinese chess) engine, but it exposes **only JNI entry points**
(`startEngine` / `engineRun` / `getReply` / `stopEngine`) — there is no `main()`, so
you cannot run it directly as a UCI engine. This project provides a thin native
wrapper that turns it into a drop-in UCI binary usable by any chess GUI.

---

## 特性 / Features

**中文**
- **单文件**：`.so` 内嵌在可执行文件中，运行时通过 `memfd` 加载，不落盘。
- **标准 UCI**：纯 stdin/stdout，可在 `adb shell` 交互使用，也可接 GUI。
- **无需 Android 运行时**：合成了一个最小 `JNIEnv` 虚表，作为原生 Android 可执行文件运行——不需要 JVM、不需要安装 App。
- **NNUE 自动探测**：自动在可执行文件同目录、环境变量 `TTXQ_NNUE`、常见目录查找 `pikafish.nnue`；GUI 的 `setoption name EvalFile ...` 依然有效。
- **交互安全**：独立线程负责取出引擎输出，真实终端里敲命令不会卡住。

**English**
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

## 目录结构 / Repository layout

```
.
├── src/ttxq_uci.cpp          # 包装层源码 / the wrapper (all logic here)
├── vendor/libpikafishso.so   # 从 APK 提取的引擎 / engine extracted from the APK
├── tools/extract_assets.sh   # 从 APK 重新提取 so + nnue / re-extract from an APK
├── build.sh / build.bat      # 构建脚本 / build scripts
├── Makefile                  # 另一种构建 / alternative build
└── bin/ttxq-armv8            # 构建产物 / build output
```

**中文**：`pikafish.nnue`（约 33 MB）**未提交到仓库**，获取方式见[下方](#获取-nnue-网络--getting-the-nnue-network)。
**English**: `pikafish.nnue` (~33 MB) is **not** committed. See [Getting the NNUE network](#getting-the-nnue-network).

---

## 编译 / Building

**中文**：需要 Android NDK（r21+，已在 r27 测试）、C++17 编译器和 `ld.lld`（均随 NDK 提供）。设置 `ANDROID_NDK_HOME` 指向 NDK 目录。

**English**: Requires Android NDK (r21+; tested with r27), a C++17 compiler, and `ld.lld`
(all ship with the NDK). Set `ANDROID_NDK_HOME` to your NDK directory.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk
./build.sh                       # Linux / macOS / Git Bash
```

Windows / Windows 系统:

```bat
set ANDROID_NDK_HOME=C:\path\to\android-ndk
build.bat
```

或用 make / Or with make:

```bash
make ANDROID_NDK_HOME=/path/to/android-ndk
```

产物为 `bin/ttxq-armv8`（ELF64 AArch64，约 1.4 MB）。
Output: `bin/ttxq-armv8` (ELF64 AArch64, ~1.4 MB).

<details>
<summary>构建脚本做了什么 / What the build script does</summary>

1. `ld.lld -m aarch64linux -r -b binary vendor/libpikafishso.so -o build/libpikafishso_so.o`
   —— 把 so 转为数据对象，暴露符号 `_binary_vendor_libpikafishso_so_start/end`（符号名由输入路径推导）。
   Turns the .so into a data object exposing `_binary_vendor_libpikafishso_so_start/end`
   (the symbol name is derived from the input path).
2. 用 `clang++ --target=aarch64-linux-android21` 编译 `src/ttxq_uci.cpp`。
   Compiles `src/ttxq_uci.cpp` with `clang++ --target=aarch64-linux-android21`.
3. 两者以 `-static-libstdc++ -ldl` 链接成 `bin/ttxq-armv8`。
   Links both with `-static-libstdc++ -ldl` into `bin/ttxq-armv8`.

</details>

---

## 部署与使用 / Deploy & use

```bash
# 推送引擎和网络文件 / push the engine and the network file
adb push bin/ttxq-armv8 /data/local/tmp/ttxq-armv8
adb push pikafish.nnue  /data/local/tmp/pikafish.nnue
adb shell chmod +x /data/local/tmp/ttxq-armv8

# 交互式运行 / run it interactively
adb shell /data/local/tmp/ttxq-armv8
```

然后输入 UCI 命令 / then type UCI commands:

```
uci
isready
position startpos
go depth 8
quit
```

输出示例 / example output:

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

### NNUE 位置 / NNUE location

**中文**：若 `pikafish.nnue` 不在可执行文件同目录，可用环境变量或标准 UCI 命令显式指定：

**English**: If `pikafish.nnue` is not next to the binary, set its path explicitly
via the environment variable or a standard UCI option:

```bash
TTXQ_NNUE=/data/local/tmp/pikafish.nnue adb shell /data/local/tmp/ttxq-armv8
```

```
setoption name EvalFile value /data/local/tmp/pikafish.nnue
```

**中文**：没有可加载的 NNUE 时，引擎仍会回应 `uci`/`isready`，但 `go` 不会给出真实着法（可能返回 `a0a1` 之类的空着）。
**English**: Without a loadable NNUE the engine still answers `uci`/`isready`, but
`go` will not produce a real move (it may return a null move such as `a0a1`).

### 获取 NNUE 网络 / Getting the NNUE network

从 App 的 APK 提取（与 so 同源）/ From the app APK (same source as the .so):

```bash
./tools/extract_assets.sh /path/to/ttxq.apk .
# -> ./pikafish.nnue
```

也可从 Pikafish 官方 Release 下载 / Or fetch an official Pikafish network from
<https://github.com/official-pikafish/Networks/releases>
（网络格式须与本引擎版本匹配；App 内置的那份已知可用 / the format must match this
engine build; the one bundled in the app is known to work）。

---

## 实现要点（踩过的坑）/ Implementation notes (the hard parts)

**中文**（修改包装层时请留意这些非显而易见之处）：

1. **伪 JNIEnv 偏移**：`NewStringUTF` / `GetStringUTFChars` / `ReleaseStringUTFChars`
   在虚表中的偏移是 `0x538` / `0x548` / `0x550`（对照 NDK 的 `jni.h` 验证过），只需这三个槽位。
2. **`startEngine()` 在 TTY 下卡死**：原生初始化在 stdin 是终端时会等待输入。因此包装层先把真实输入 dup 到 fd 10，再把 fd 0 指向 `/dev/null` 后启动引擎——这是交互式 `adb shell` 能用的关键。
3. **独立输出线程**：`getReply()` 会阻塞，若在主线程轮询会卡住输入处理。由专门的 pump 线程独占 `getReply()` 并转发到 stdout，主线程只负责读 stdin 和调用 `engineRun()`。
4. **从 memfd 加载**：`dlopen("/proc/self/fd/N")` 让 so 留在内存中、不产生临时文件。

**English** (non-obvious details discovered while building; keep them in mind if
you modify the wrapper):

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

## 致谢与法律声明 / Credits & legal

**中文**
- 内嵌引擎为 **Pikafish**，由 official-pikafish 团队开发，采用 **GNU GPL v3.0** 授权，见 <https://github.com/official-pikafish/Pikafish>。`libpikafishso.so` 与 `pikafish.nnue` 从「天天象棋」App 提取，仅用于互操作性；所有权归各自权利人。
- 本包装层采用 **GPL-3.0-or-later**（见 [LICENSE](LICENSE)），以与内嵌引擎的授权兼容。
- 本项目是技术互操作性实验；上游二进制与网络文件的使用须遵守其许可证及适用法律。
- `ttxq-armv8` 与腾讯及 Pikafish 项目无关联，也未获其背书。

**English**
- The bundled engine is **Pikafish**, developed by the official-pikafish team and
  licensed under the **GNU GPL v3.0**. See <https://github.com/official-pikafish/Pikafish>.
  The `libpikafishso.so` and `pikafish.nnue` files are extracted from the 天天象棋
  app for interoperability; all rights remain with their respective owners.
- This wrapper is released under **GPL-3.0-or-later** (see [LICENSE](LICENSE)) to be
  compatible with the embedded engine.
- This project is a technical interoperability experiment. Use of the upstream
  binary and network must comply with their licenses and applicable law.
- `ttxq-armv8` is not affiliated with or endorsed by Tencent or the Pikafish project.

---

<a id="english"></a>

## English quick start

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk && ./build.sh
adb push bin/ttxq-armv8 /data/local/tmp/ttxq-armv8
adb push pikafish.nnue  /data/local/tmp/pikafish.nnue
adb shell /data/local/tmp/ttxq-armv8     # then: uci / go depth 8 / quit
```
