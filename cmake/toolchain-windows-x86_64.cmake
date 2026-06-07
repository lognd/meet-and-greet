# Windows x86-64 cross-compilation using MinGW-w64 (POSIX threads variant)
# Requires: g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix (apt)
# Usage: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-windows-x86_64.cmake

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-w64-mingw32-gcc-posix)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-posix)
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)
set(CMAKE_AR           x86_64-w64-mingw32-ar)
set(CMAKE_RANLIB       x86_64-w64-mingw32-ranlib)
set(CMAKE_STRIP        x86_64-w64-mingw32-strip)

# Static link the MinGW and C++ runtimes so the .exe needs no DLLs
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
