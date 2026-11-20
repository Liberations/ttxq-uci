@echo off
REM Build ttxq-armv8 for Android/arm64 on Windows.
REM Set ANDROID_NDK_HOME to your NDK, or edit NDK below.
setlocal
set ROOT=%~dp0
cd /d "%ROOT%"

if defined ANDROID_NDK_HOME (
  set NDK=%ANDROID_NDK_HOME%
) else (
  set NDK=C:\Users\%USERNAME%\AppData\Local\Android\Sdk\ndk\27.0.12077973
)
set BIN=%NDK%\toolchains\llvm\prebuilt\windows-x86_64\bin
set SYSROOT=%NDK%\toolchains\llvm\prebuilt\windows-x86_64\sysroot

if not exist "%BIN%\clang++.exe" (
  echo error: clang++.exe not found under %BIN% ^(set ANDROID_NDK_HOME^)
  exit /b 1
)

set BUILD=build
set OUT=bin\ttxq-armv8
if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "bin" mkdir "bin"

echo [1/3] Embedding vendor\libpikafishso.so ...
"%BIN%\ld.lld.exe" -m aarch64linux -r -b binary vendor\libpikafishso.so -o "%BUILD%\libpikafishso_so.o"
if errorlevel 1 goto fail

echo [2/3] Compiling src\ttxq_uci.cpp ...
"%BIN%\clang++.exe" --target=aarch64-linux-android21 -std=c++17 -O2 -static-libstdc++ ^
  -I "%SYSROOT%\usr\include" -I "%SYSROOT%\usr\include\c++\v1" ^
  -c src\ttxq_uci.cpp -o "%BUILD%\ttxq_uci.o"
if errorlevel 1 goto fail

echo [3/3] Linking -^> %OUT% ...
"%BIN%\clang++.exe" --target=aarch64-linux-android21 -std=c++17 -static-libstdc++ ^
  "%BUILD%\ttxq_uci.o" "%BUILD%\libpikafishso_so.o" -ldl -o "%OUT%"
if errorlevel 1 goto fail

echo OK: %OUT%
exit /b 0

:fail
echo BUILD FAILED
exit /b 1
