# Meet and Greet - root Makefile
#
# Primary targets:
#   make build          build the C++ client (downloads deps via CMake FetchContent)
#   make test           run all tests (Python + C++)
#   make test-python    run Python tests only (server unit/integration/system)
#   make test-cpp       run C++ tests only (ctest)
#   make test-system    run full system + scale tests only
#   make test-binary    run tests that exercise the compiled binary
#   make clean          remove build artifacts and temp files
#
# Windows note:
#   This Makefile targets Linux/macOS.  On Windows, use:
#     cmake -B build -DMAG_XORKEY="..." && cmake --build build
#     .venv\Scripts\python -m pytest tests\server tests\system -v
#     cd build && ctest --output-on-failure
#
# Variables you can override:
#   XORKEY   - 32-char hex key matching app.toml encryption_key (default provided)
#   BUILD_DIR - cmake build directory (default: build)
#   WORKERS  - parallel jobs for cmake --build (default: auto-detected)

SHELL    := /bin/bash
XORKEY   := deadbeefcafebabedeadbeefcafebabe
BUILD_DIR := build

# Detect Python in virtualenv
PYTHON  := .venv/bin/python
PYTEST  := .venv/bin/pytest

# Parallel job count (Linux: nproc, macOS: sysctl)
WORKERS := $(shell nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

# C++ test binary (cross-platform name handled by cmake)
CPP_TESTS := $(BUILD_DIR)/tests/client/mag_tests

# ── Build ─────────────────────────────────────────────────────────────────────

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

# ── Cross-platform release builds ────────────────────────────────────────────
#
# Produces distributable binaries in dist/.
#
# Linux target: compiled natively (no Docker required when on a Linux host).
# Windows target: cross-compiled with MinGW-w64 (available on most Linux distros).
#   Install: sudo apt install mingw-w64    (Debian/Ubuntu)
#            sudo dnf install mingw64-gcc-c++  (Fedora)
# macOS target: must be run on a macOS host; produces a universal binary
#   (arm64 + x86_64) via lipo.
#
# All targets embed the XOR key from the XORKEY variable.  Pass it explicitly:
#   make release XORKEY="$(grep encryption_key app.toml | cut -d'"' -f2)"

DIST_DIR := dist
CMAKE_RELEASE_FLAGS := -DCMAKE_BUILD_TYPE=Release \
                       -DFETCHCONTENT_QUIET=ON \
                       -DFETCHCONTENT_UPDATES_DISCONNECTED=ON

.PHONY: release release-linux release-windows release-macos dist-dir

dist-dir:
	mkdir -p $(DIST_DIR)

# ---- Linux x86-64 -----------------------------------------------------------

release-linux: dist-dir
	@echo "==> Building Linux x86-64 release..."
	cmake -B build-release-linux \
		-DMAG_XORKEY="$(XORKEY)" \
		$(CMAKE_RELEASE_FLAGS)
	cmake --build build-release-linux -j$(WORKERS) --target mag_client
	cp build-release-linux/mag_client $(DIST_DIR)/mag_client-linux-x86_64
	@echo "==> $(DIST_DIR)/mag_client-linux-x86_64"

# ---- Windows x86-64 (cross-compile with MinGW) ------------------------------
#
# Requires mingw-w64:
#   sudo apt install mingw-w64       (Debian/Ubuntu)
#   sudo dnf install mingw64-gcc-c++ (Fedora/RHEL)

MINGW_TOOLCHAIN := $(shell \
    cmake -P cmake/FindMingwToolchain.cmake 2>/dev/null || \
    find /usr -name "x86_64-w64-mingw32-g++*" -print -quit 2>/dev/null | \
    sed 's|/x86_64-w64-mingw32-g++.*||')

release-windows: dist-dir
	@command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 || \
		{ echo "ERROR: x86_64-w64-mingw32-g++ not found. Install mingw-w64."; exit 1; }
	@echo "==> Building Windows x86-64 release (MinGW cross-compile)..."
	cmake -B build-release-windows \
		-DMAG_XORKEY="$(XORKEY)" \
		-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake \
		$(CMAKE_RELEASE_FLAGS)
	cmake --build build-release-windows -j$(WORKERS) --target mag_client
	cp build-release-windows/mag_client.exe $(DIST_DIR)/mag_client-windows-x86_64.exe
	@echo "==> $(DIST_DIR)/mag_client-windows-x86_64.exe"

# ---- macOS universal (arm64 + x86_64, must run on macOS) -------------------

release-macos: dist-dir
	@uname | grep -q Darwin || \
		{ echo "ERROR: macOS release must be built on a macOS host."; exit 1; }
	@echo "==> Building macOS arm64..."
	cmake -B build-release-macos-arm64 \
		-DMAG_XORKEY="$(XORKEY)" \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		$(CMAKE_RELEASE_FLAGS)
	cmake --build build-release-macos-arm64 -j$(WORKERS) --target mag_client
	@echo "==> Building macOS x86_64..."
	cmake -B build-release-macos-x86_64 \
		-DMAG_XORKEY="$(XORKEY)" \
		-DCMAKE_OSX_ARCHITECTURES=x86_64 \
		$(CMAKE_RELEASE_FLAGS)
	cmake --build build-release-macos-x86_64 -j$(WORKERS) --target mag_client
	@echo "==> Combining into universal binary..."
	lipo -create \
		build-release-macos-arm64/mag_client \
		build-release-macos-x86_64/mag_client \
		-output $(DIST_DIR)/mag_client-macos-universal
	chmod +x $(DIST_DIR)/mag_client-macos-universal
	@echo "==> $(DIST_DIR)/mag_client-macos-universal"

# ---- All platforms ----------------------------------------------------------

release: release-linux release-windows
	@echo ""
	@echo "Release binaries:"
	@ls -lh $(DIST_DIR)/

# ── Utilities ─────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) __pycache__ .pytest_cache
	rm -rf build-release-linux build-release-windows
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
	@echo "Meet and Greet build and test targets:"
	@echo ""
	@echo "  make build          Build the C++ client binary"
	@echo "  make test           Run all tests (Python + C++)"
	@echo "  make test-python    Python unit + integration + system tests"
	@echo "  make test-cpp       C++ unit + integration tests (ctest)"
	@echo "  make test-system    System + scale tests only"
	@echo "  make test-binary    Tests exercising the compiled binary"
	@echo "  make test-scale     120-student scale test only"
	@echo "  make test-fast      Fast Python unit/integration tests (no system)"
	@echo "  make clean          Remove build artifacts"
	@echo ""
	@echo "Release:"
	@echo "  make release        Build Linux + Windows release binaries -> dist/"
	@echo "  make release-linux  Linux x86-64 only"
	@echo "  make release-windows  Windows x86-64 (requires mingw-w64)"
	@echo "  make release-macos  macOS universal arm64+x86_64 (macOS host only)"
	@echo "  make clean-dist     Remove dist/"
	@echo ""
	@echo "Variables:"
	@echo "  XORKEY=<32-char hex>   encryption key (must match app.toml)"
	@echo "  BUILD_DIR=<path>       cmake build directory (default: build)"
	@echo "  DIST_DIR=<path>        release output directory (default: dist)"
	@echo ""
