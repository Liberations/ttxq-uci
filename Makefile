# Makefile for ttxq-armv8 (requires Android NDK; set ANDROID_NDK_HOME)
NDK ?= $(ANDROID_NDK_HOME)
ifeq ($(NDK),)
$(error Set ANDROID_NDK_HOME to your Android NDK path)
endif

HOST := $(shell uname -s | grep -qi mingw && echo windows-x86_64 || (uname -s | grep -qi darwin && echo darwin-x86_64 || echo linux-x86_64))
BIN := $(NDK)/toolchains/llvm/prebuilt/$(HOST)/bin
SYSROOT := $(NDK)/toolchains/llvm/prebuilt/$(HOST)/sysroot
CXX := $(BIN)/clang++
LD := $(BIN)/ld.lld
TARGET ?= aarch64-linux-android21
OUT ?= bin/ttxq-armv8

.PHONY: all clean
all: $(OUT)

$(OUT): build/ttxq_uci.o build/libpikafishso_so.o
	@mkdir -p bin
	$(CXX) --target=$(TARGET) -std=c++17 -static-libstdc++ $^ -ldl -o $@

build/libpikafishso_so.o: vendor/libpikafishso.so
	@mkdir -p build
	$(LD) -m aarch64linux -r -b binary $< -o $@

build/ttxq_uci.o: src/ttxq_uci.cpp
	@mkdir -p build
	$(CXX) --target=$(TARGET) -std=c++17 -O2 -static-libstdc++ \
	  -I $(SYSROOT)/usr/include -I $(SYSROOT)/usr/include/c++/v1 -c $< -o $@

clean:
	rm -rf build bin
