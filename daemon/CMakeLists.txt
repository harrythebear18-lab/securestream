cmake_minimum_required(VERSION 3.20)
project(securestream_daemon CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Daemon library
add_library(securestream_daemon STATIC
    src/daemon.cpp
)

target_include_directories(securestream_daemon PUBLIC include)

# Link to transport layer
target_link_libraries(securestream_daemon PUBLIC
    securestream_transport
)

# Daemon test executable (sender)
add_executable(daemon_test
    test/daemon_test.cpp
)

target_link_libraries(daemon_test PRIVATE
    securestream_daemon
)

# Daemon receiver executable (VM side)
add_executable(daemon_receiver
    test/daemon_receiver.cpp
)

target_link_libraries(daemon_receiver PRIVATE
    securestream_transport
)
