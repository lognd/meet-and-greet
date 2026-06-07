# Windows arm64 cross-compilation using llvm-mingw
# Requires: llvm-mingw installed at LLVM_MINGW_ROOT (default /opt/llvm-mingw)
#   Install: sudo scripts/setup-cross.sh
# Usage:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-arm64.cmake
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-arm64.cmake \
#         -DLLVM_MINGW_ROOT=/path/to/llvm-mingw

if(NOT DEFINED LLVM_MINGW_ROOT)
    if(DEFINED ENV{LLVM_MINGW_ROOT})
        set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
    else()
        set(LLVM_MINGW_ROOT "/opt/llvm-mingw")
    endif()
endif()

if(NOT EXISTS "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang++")
    message(FATAL_ERROR
        "llvm-mingw not found at ${LLVM_MINGW_ROOT}.\n"
        "Run:  sudo scripts/setup-cross.sh\n"
        "Or set -DLLVM_MINGW_ROOT=/path/to/llvm-mingw")
endif()

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER   "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-windres")
set(CMAKE_AR           "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-ar")
set(CMAKE_RANLIB       "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-ranlib")
set(CMAKE_STRIP        "${LLVM_MINGW_ROOT}/bin/aarch64-w64-mingw32-strip")

# llvm-mingw links against its own bundled runtime (libc++, compiler-rt)
# Static linking keeps the .exe self-contained on Windows arm64 devices
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/aarch64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
