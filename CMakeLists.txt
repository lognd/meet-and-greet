cmake_minimum_required(VERSION 3.22)
project(meet_and_greet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------

set(MAG_XORKEY "deadbeefcafebabedeadbeefcafebabe" CACHE STRING
    "XOR key (32-char hex). Must match server app.toml encryption_key.")
set(MAG_HTTP_PORT 9876 CACHE STRING "HTTP API port")
set(MAG_UDP_PORT  9875 CACHE STRING "UDP discovery port")

# --------------------------------------------------------------------------
# Dependencies
# --------------------------------------------------------------------------

include(FetchContent)

# httplib: header-only HTTP library.
# We use FetchContent_Populate (not MakeAvailable) to download the source
# without running httplib's own CMakeLists.txt.  That cmake runs
# find_package(OpenSSL) and, if it succeeds, propagates
# -DCPPHTTPLIB_OPENSSL_SUPPORT and -lssl/-lcrypto to every consumer.
# Cross-sysroots never carry the host arch's OpenSSL headers, causing build
# failures.  Since httplib is header-only, all we need is the include path.
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.7)

FetchContent_GetProperties(httplib)
if(NOT httplib_POPULATED)
    FetchContent_Populate(httplib)
endif()

# Plain interface target - include path only, no OpenSSL.
add_library(httplib_iface INTERFACE)
target_include_directories(httplib_iface INTERFACE ${httplib_SOURCE_DIR})
add_library(httplib::httplib ALIAS httplib_iface)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)

# FTXUI: cross-platform TUI (components, DOM renderer, screen)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui.git
    GIT_TAG        v6.1.9
    GIT_SHALLOW    TRUE)

# Catch2: C++ unit test framework
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.3
    GIT_SHALLOW    TRUE)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # don't re-fetch on every build
FetchContent_MakeAvailable(nlohmann_json ftxui Catch2)

# --------------------------------------------------------------------------
# Platform libs
# --------------------------------------------------------------------------

if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS "")
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    set(THREAD_LIB Threads::Threads)
endif()

# --------------------------------------------------------------------------
# Client executable
# --------------------------------------------------------------------------

add_executable(mag_client
    src/client/main.cpp
    src/client/net/http.cpp
    src/client/net/udp.cpp
    src/client/tui/screens.cpp
    src/client/tui/master.cpp
)

target_include_directories(mag_client PRIVATE include/client)

target_compile_definitions(mag_client PRIVATE
    MAG_XORKEY="${MAG_XORKEY}"
    MAG_HTTP_PORT=${MAG_HTTP_PORT}
    MAG_UDP_PORT=${MAG_UDP_PORT}
)

target_link_libraries(mag_client PRIVATE
    httplib::httplib
    nlohmann_json::nlohmann_json
    ftxui::component
    ftxui::dom
    ftxui::screen
    ${PLATFORM_LIBS}
    ${THREAD_LIB}
)

# --------------------------------------------------------------------------
# C++ tests
# --------------------------------------------------------------------------

enable_testing()
add_subdirectory(tests/client)
