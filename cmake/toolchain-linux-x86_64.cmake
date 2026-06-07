# Linux x86-64 cross-compilation from an arm64 host
# Requires: gcc-x86-64-linux-gnu g++-x86-64-linux-gnu (apt)
# Usage: cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-x86_64.cmake

set(CMAKE_SYSTEM_NAME    Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER   x86_64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER x86_64-linux-gnu-g++)
set(CMAKE_AR           x86_64-linux-gnu-ar)
set(CMAKE_RANLIB       x86_64-linux-gnu-ranlib)
set(CMAKE_STRIP        x86_64-linux-gnu-strip)

# pkg-config / find_package should look in the cross sysroot only
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
