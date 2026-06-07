add_executable(mag_tests
    test_data.cpp
    test_network.cpp
    # Pulls in the non-TUI client source files so we can test them directly
    ${PROJECT_SOURCE_DIR}/src/client/net/http.cpp
    ${PROJECT_SOURCE_DIR}/src/client/net/udp.cpp
)

target_include_directories(mag_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include/client
)

target_compile_definitions(mag_tests PRIVATE
    MAG_XORKEY="${MAG_XORKEY}"
    MAG_HTTP_PORT=${MAG_HTTP_PORT}
    MAG_UDP_PORT=${MAG_UDP_PORT}
)

target_link_libraries(mag_tests PRIVATE
    Catch2::Catch2WithMain
    httplib::httplib
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
    ${THREAD_LIB}
)

include(Catch)
catch_discover_tests(mag_tests)
