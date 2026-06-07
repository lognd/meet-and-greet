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

# ── Utilities ─────────────────────────────────────────────────────────────────

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) __pycache__ .pytest_cache
	find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
	find . -name "*.pyc" -delete 2>/dev/null || true
	rm -f mag.db mag.log targets.json master_state.json

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
	@echo "Variables:"
	@echo "  XORKEY=<32-char hex>   encryption key (must match app.toml)"
	@echo "  BUILD_DIR=<path>       cmake build directory (default: build)"
	@echo ""
