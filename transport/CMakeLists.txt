cmake_minimum_required(VERSION 3.20)
project(securestream_transport CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Transport library (shared by daemon and VM compositor)
add_library(securestream_transport STATIC
    src/clock.cpp
    src/ring_buffer.cpp
    src/transport_packet.cpp
    src/vsock_channel.cpp
)

target_include_directories(securestream_transport PUBLIC include)

# Windows libraries
target_link_libraries(securestream_transport PUBLIC
    ws2_32
)

# Transport test executable
add_executable(transport_test
    test/transport_test.cpp
)

target_link_libraries(transport_test PRIVATE
    securestream_transport
)
