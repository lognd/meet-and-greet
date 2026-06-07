# MinGW-w64 cross-compilation toolchain for Windows x86-64
# Usage: cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers - prefer the POSIX threading model variant if available
find_program(MINGW_CC  x86_64-w64-mingw32-gcc-posix
             NAMES     x86_64-w64-mingw32-gcc)
find_program(MINGW_CXX x86_64-w64-mingw32-g++-posix
             NAMES     x86_64-w64-mingw32-g++)

if(NOT MINGW_CC OR NOT MINGW_CXX)
    message(FATAL_ERROR
        "MinGW-w64 cross-compiler not found.\n"
        "Install it with:\n"
        "  sudo apt install mingw-w64      (Debian/Ubuntu)\n"
        "  sudo dnf install mingw64-gcc-c++ (Fedora/RHEL)")
endif()

set(CMAKE_C_COMPILER   ${MINGW_CC})
set(CMAKE_CXX_COMPILER ${MINGW_CXX})
set(CMAKE_RC_COMPILER  x86_64-w64-mingw32-windres)

# Search for libraries and includes only in the MinGW sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Static linking so the binary runs without a MinGW DLL runtime
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
