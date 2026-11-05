cmake_minimum_required(VERSION 3.21)
project(neebles_proxy_client VERSION 1.0.3 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.8 COMPONENTS Core Widgets Network REQUIRED)
qt_standard_project_setup()
qt_add_executable(neebles-proxy-client src/main.cpp src/config_secrets.cpp src/config_secrets.h src/config_bridge.cpp src/config_bridge.h src/runtime_session.cpp src/runtime_session.h)
target_link_libraries(neebles-proxy-client PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
install(TARGETS neebles-proxy-client RUNTIME DESTINATION runtime)
# Install the actual module artwork alongside the canonical executable.
# No Qt runtime library from the host is packaged as certified material.
install(FILES
    assets/neebles-client-icon.png
    assets/neebles-client-logo-off.png
    assets/neebles-client-logo-on.png
    DESTINATION assets)

enable_testing()
add_test(NAME neebles-proxy-client_help COMMAND neebles-proxy-client --help)
set_tests_properties(neebles-proxy-client_help PROPERTIES PASS_REGULAR_EXPRESSION "NEEBLES Proxy")

qt_add_executable(neebles-proxy-client-preflight src/preflight.cpp)
target_link_libraries(neebles-proxy-client-preflight PRIVATE Qt6::Core Qt6::Network)
add_test(NAME neebles-proxy-client-preflight_help COMMAND neebles-proxy-client-preflight --help)
set_tests_properties(neebles-proxy-client-preflight_help PROPERTIES PASS_REGULAR_EXPRESSION "read-only")
add_test(NAME neebles-proxy-client-preflight_invalid_ip COMMAND neebles-proxy-client-preflight --server 127.0.0.1)
set_tests_properties(neebles-proxy-client-preflight_invalid_ip PROPERTIES WILL_FAIL TRUE)

# NATIVE PLATFORM OPERATION CANDIDATE: invoked only by OS-owned Boss provider
qt_add_executable(neebles-proxy-client-platform src/platform_exit_node.cpp)
target_link_libraries(neebles-proxy-client-platform PRIVATE Qt6::Core Qt6::Network)
install(TARGETS neebles-proxy-client-platform RUNTIME DESTINATION runtime)
install(TARGETS neebles-proxy-client-preflight RUNTIME DESTINATION runtime)
add_test(NAME neebles-proxy-client-platform_help COMMAND neebles-proxy-client-platform --help)
set_tests_properties(neebles-proxy-client-platform_help PROPERTIES PASS_REGULAR_EXPRESSION "NEEBLES Proxy Client native")
add_test(NAME neebles-proxy-client-platform_fail_closed COMMAND neebles-proxy-client-platform --activate)
set_tests_properties(neebles-proxy-client-platform_fail_closed PROPERTIES WILL_FAIL TRUE)
