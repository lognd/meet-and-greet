cmake_minimum_required(VERSION 3.22)
project(meet_and_greet)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Configuration options (override at cmake configure time)
# --------------------------------------------------------------------------

# Must match encryption_key in app.toml (32-char hex string = 16 bytes).
set(MAG_XORKEY "deadbeefcafebabedeadbeefcafebabe" CACHE STRING
    "XOR encryption key (32-char hex). Must match server app.toml encryption_key.")

set(MAG_HTTP_PORT 9876 CACHE STRING "HTTP API port")
set(MAG_UDP_PORT  9875 CACHE STRING "UDP discovery port")

# --------------------------------------------------------------------------
# Dependencies via FetchContent
# --------------------------------------------------------------------------

include(FetchContent)

# cpp-httplib: header-only HTTP/1.1 client+server
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.7
)

# nlohmann/json: header-only JSON (used for API request/response parsing)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

FetchContent_MakeAvailable(httplib nlohmann_json)

# --------------------------------------------------------------------------
# Platform-specific networking libraries
# --------------------------------------------------------------------------

if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS "")
endif()

# --------------------------------------------------------------------------
# Client executable
# --------------------------------------------------------------------------

add_executable(mag_client
    src/client/main.cpp
    src/client/network/http.cpp
    src/client/network/udp.cpp
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
    ${PLATFORM_LIBS}
)

# On Linux/macOS, threads are needed by cpp-httplib
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(mag_client PRIVATE Threads::Threads)
endif()

# --------------------------------------------------------------------------
# Install
# --------------------------------------------------------------------------

install(TARGETS mag_client DESTINATION bin)
