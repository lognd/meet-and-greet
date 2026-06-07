# Meet and Greet - root Makefile
#
# Build / test targets:
#   make build          build the C++ client for the local machine
#   make test           run all tests (Python + C++)
#   make test-python    Python unit/integration/system tests
#   make test-cpp       C++ tests via ctest
#   make test-system    system + scale tests only
#   make test-binary    headless binary interaction tests
#   make test-scale     120-student scale test
#   make test-fast      fast Python unit/integration tests (no subprocess)
#   make clean          remove all build artefacts
#
# Release targets (see below for per-platform prereqs):
#   make release        all platforms supported from this host
#   make release-linux-arm64    Linux arm64  (native)
#   make release-linux-x86_64  Linux x86-64 (cross, apt: g++-x86-64-linux-gnu)
#   make release-windows-x86_64 Windows x86-64 (cross, apt: g++-mingw-w64-x86-64-posix)
#   make release-windows-arm64  Windows arm64  (cross, llvm-mingw at LLVM_MINGW_ROOT)
#   make release-macos          macOS universal (macOS host only)
#   make clean-dist     remove dist/
#
# Quick setup of cross-compilers:
#   sudo scripts/setup-cross.sh
#
# Variables:
#   XORKEY         32-char hex key matching app.toml encryption_key
#   BUILD_DIR      local dev build directory (default: build)
#   DIST_DIR       release output directory (default: dist)
#   WORKERS        parallel jobs (default: nproc)
#   LLVM_MINGW_ROOT  path to llvm-mingw install (default: /opt/llvm-mingw)

SHELL    := /bin/bash
XORKEY   := deadbeefcafebabedeadbeefcafebabe
BUILD_DIR := build
DIST_DIR  := dist
LLVM_MINGW_ROOT ?= /opt/llvm-mingw

PYTHON  := .venv/bin/python
PYTEST  := .venv/bin/pytest
WORKERS := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
CPP_TESTS := $(BUILD_DIR)/tests/client/mag_tests

CMAKE_RELEASE_FLAGS := \
    -DCMAKE_BUILD_TYPE=Release \
    -DFETCHCONTENT_QUIET=ON \
    -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
    -DMAG_XORKEY="$(XORKEY)"

# ── Dev build ─────────────────────────────────────────────────────────────────

.PHONY: build
build:
	cmake -B $(BUILD_DIR) \
		-DMAG_XORKEY="$(XORKEY)" \
		-DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DFETCHCONTENT_QUIET=OFF
	cmake --build $(BUILD_DIR) -j$(WORKERS)

# ── Test targets ──────────────────────────────────────────────────────────────

.PHONY: test
test: test-python test-cpp

.PHONY: test-python
test-python:
	PYTHONPATH=src $(PYTEST) tests/server/ tests/system/ -v --tb=short

.PHONY: test-cpp
test-cpp:
	@if [ ! -f "$(CPP_TESTS)" ]; then \
		echo "C++ tests not built. Run 'make build' first."; exit 1; \
	fi
	cd $(BUILD_DIR) && ctest --output-on-failure -j$(WORKERS)

.PHONY: test-system
test-system:
	PYTHONPATH=src $(PYTEST) tests/system/ -v --tb=long -s

.PHONY: test-binary
test-binary:
	PYTHONPATH=src $(PYTEST) tests/system/test_binary.py -v --tb=long -s

.PHONY: test-scale
test-scale:
	PYTHONPATH=src $(PYTEST) tests/system/test_scale.py -v --tb=long -s

.PHONY: test-fast
test-fast:
	PYTHONPATH=src $(PYTEST) tests/server/ -v --tb=short -q

# ── Release builds ────────────────────────────────────────────────────────────
#
# Each target cross-compiles from the current (arm64 Linux) host.
# Run  sudo scripts/setup-cross.sh  once to install all required toolchains.
#
# Dependency chain: every release-* target depends on dist-dir and downloads
# FetchContent deps only once per build directory.  Re-running a target after a
# source change is fast (cmake --build incremental).

.PHONY: dist-dir
dist-dir:
	mkdir -p $(DIST_DIR)

# ── Linux arm64 (native) ──────────────────────────────────────────────────────

.PHONY: release-linux-arm64
release-linux-arm64: dist-dir
	@echo "==> Linux arm64 (native)"
	cmake -B build-release-linux-arm64 $(CMAKE_RELEASE_FLAGS)
	cmake --build build-release-linux-arm64 -j$(WORKERS) --target mag_client
	cp build-release-linux-arm64/mag_client $(DIST_DIR)/mag_client-linux-arm64
	strip $(DIST_DIR)/mag_client-linux-arm64
	@echo "    $(DIST_DIR)/mag_client-linux-arm64"

# ── Linux x86-64 (cross from arm64) ──────────────────────────────────────────
# Prereq: sudo apt install g++-x86-64-linux-gnu gcc-x86-64-linux-gnu

.PHONY: release-linux-x86_64
release-linux-x86_64: dist-dir
	@command -v x86_64-linux-gnu-g++ >/dev/null 2>&1 || { \
		echo "ERROR: x86_64-linux-gnu-g++ not found."; \
		echo "       Run: sudo scripts/setup-cross.sh"; exit 1; }
	@echo "==> Linux x86-64 (cross)"
	cmake -B build-release-linux-x86_64 $(CMAKE_RELEASE_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-x86_64.cmake
	cmake --build build-release-linux-x86_64 -j$(WORKERS) --target mag_client
	cp build-release-linux-x86_64/mag_client $(DIST_DIR)/mag_client-linux-x86_64
	x86_64-linux-gnu-strip $(DIST_DIR)/mag_client-linux-x86_64
	@echo "    $(DIST_DIR)/mag_client-linux-x86_64"

# ── Windows x86-64 (MinGW-w64 cross from arm64) ───────────────────────────────
# Prereq: sudo apt install g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix

.PHONY: release-windows-x86_64
release-windows-x86_64: dist-dir
	@command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 || { \
		echo "ERROR: x86_64-w64-mingw32-g++-posix not found."; \
		echo "       Run: sudo scripts/setup-cross.sh"; exit 1; }
	@echo "==> Windows x86-64 (MinGW-w64)"
	cmake -B build-release-windows-x86_64 $(CMAKE_RELEASE_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-x86_64.cmake
	cmake --build build-release-windows-x86_64 -j$(WORKERS) --target mag_client
	cp build-release-windows-x86_64/mag_client.exe \
		$(DIST_DIR)/mag_client-windows-x86_64.exe
	x86_64-w64-mingw32-strip $(DIST_DIR)/mag_client-windows-x86_64.exe
	@echo "    $(DIST_DIR)/mag_client-windows-x86_64.exe"

# ── Windows arm64 (llvm-mingw cross from arm64) ───────────────────────────────
# Prereq: llvm-mingw at LLVM_MINGW_ROOT (installed by scripts/setup-cross.sh)

.PHONY: release-windows-arm64
release-windows-arm64: dist-dir
	@test -x "$(LLVM_MINGW_ROOT)/bin/aarch64-w64-mingw32-clang++" || { \
		echo "ERROR: llvm-mingw not found at $(LLVM_MINGW_ROOT)."; \
		echo "       Run: sudo scripts/setup-cross.sh"; \
		echo "       Or:  make release-windows-arm64 LLVM_MINGW_ROOT=/path/to/llvm-mingw"; \
		exit 1; }
	@echo "==> Windows arm64 (llvm-mingw)"
	cmake -B build-release-windows-arm64 $(CMAKE_RELEASE_FLAGS) \
		-DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-arm64.cmake \
		-DLLVM_MINGW_ROOT="$(LLVM_MINGW_ROOT)"
	cmake --build build-release-windows-arm64 -j$(WORKERS) --target mag_client
	cp build-release-windows-arm64/mag_client.exe \
		$(DIST_DIR)/mag_client-windows-arm64.exe
	"$(LLVM_MINGW_ROOT)/bin/aarch64-w64-mingw32-strip" \
		$(DIST_DIR)/mag_client-windows-arm64.exe
	@echo "    $(DIST_DIR)/mag_client-windows-arm64.exe"

# ── macOS universal (arm64 + x86-64, macOS host only) ─────────────────────────
# Must be run on a macOS machine with Xcode Command Line Tools installed.

.PHONY: release-macos
release-macos: dist-dir
	@uname | grep -q Darwin || { \
		echo "ERROR: macOS release must be built on a macOS host."; exit 1; }
	@echo "==> macOS arm64"
	cmake -B build-release-macos-arm64 $(CMAKE_RELEASE_FLAGS) \
		-DCMAKE_OSX_ARCHITECTURES=arm64
	cmake --build build-release-macos-arm64 -j$(WORKERS) --target mag_client
	@echo "==> macOS x86-64"
	cmake -B build-release-macos-x86_64 $(CMAKE_RELEASE_FLAGS) \
		-DCMAKE_OSX_ARCHITECTURES=x86_64
	cmake --build build-release-macos-x86_64 -j$(WORKERS) --target mag_client
	@echo "==> lipo -> universal"
	lipo -create \
		build-release-macos-arm64/mag_client \
		build-release-macos-x86_64/mag_client \
		-output $(DIST_DIR)/mag_client-macos-universal
	strip $(DIST_DIR)/mag_client-macos-universal
	@echo "    $(DIST_DIR)/mag_client-macos-universal"

# ── All platforms supported from this host ────────────────────────────────────

.PHONY: release
release: release-linux-arm64 release-linux-x86_64 \
         release-windows-x86_64 release-windows-arm64
	@echo ""
	@echo "Release binaries in $(DIST_DIR)/:"
	@ls -lh $(DIST_DIR)/

# ── Utilities ─────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) __pycache__ .pytest_cache
	rm -rf build-release-linux-arm64 build-release-linux-x86_64
	rm -rf build-release-windows-x86_64 build-release-windows-arm64
	rm -rf build-release-macos-arm64 build-release-macos-x86_64
	find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.pyc" -delete 2>/dev/null || true
	rm -f mag.db mag.log targets.json master_state.json

.PHONY: clean-dist
clean-dist:
	rm -rf $(DIST_DIR)

.PHONY: help
help:
	@echo ""
	@echo "Meet and Greet"
	@echo ""
	@echo "Development:"
	@echo "  make build              Build client for this machine"
	@echo "  make test               All tests (Python + C++)"
	@echo "  make test-python        Python unit/integration/system"
	@echo "  make test-cpp           C++ tests (ctest)"
	@echo "  make test-system        Full system + scale tests"
	@echo "  make test-binary        Headless binary tests"
	@echo "  make test-scale         120-student scale test"
	@echo "  make test-fast          Fast Python tests only"
	@echo "  make clean              Remove build artefacts"
	@echo ""
	@echo "Cross-compilation setup (run once):"
	@echo "  sudo scripts/setup-cross.sh"
	@echo ""
	@echo "Release (produces binaries in dist/):"
	@echo "  make release                    All platforms (Linux + Windows)"
	@echo "  make release-linux-arm64        Linux arm64  (native)"
	@echo "  make release-linux-x86_64       Linux x86-64 (cross)"
	@echo "  make release-windows-x86_64     Windows x86-64 (MinGW-w64)"
	@echo "  make release-windows-arm64      Windows arm64  (llvm-mingw)"
	@echo "  make release-macos              macOS universal (macOS host only)"
	@echo "  make clean-dist                 Remove dist/"
	@echo ""
	@echo "Variables:"
	@echo "  XORKEY=<32-char hex>     encryption key (must match app.toml)"
	@echo "  BUILD_DIR=<path>         dev build dir (default: build)"
	@echo "  DIST_DIR=<path>          release output (default: dist)"
	@echo "  LLVM_MINGW_ROOT=<path>   llvm-mingw install (default: /opt/llvm-mingw)"
	@echo ""
