# SecureStream Compositor
# Purpose-built scene compositor with security model in the render path.

add_library(securestream_compositor STATIC
    src/compositor.cpp
    src/scene.cpp
    src/source.cpp
    src/filter.cpp
    src/output.cpp
    src/transition.cpp
    src/scene_presets.cpp
    src/sources/image_source.cpp
    src/sources/text_source.cpp
    src/sources/webcam_source.cpp
    src/sources/microphone_source.cpp
    src/sources/media_source.cpp
    src/output/srt_output.cpp
    src/output/file_output.cpp
)

target_include_directories(securestream_compositor PUBLIC include PRIVATE third_party)

# Link Windows multimedia libs for capture and encoding
if(WIN32)
    target_link_libraries(securestream_compositor PUBLIC
        winmm
        mfplat
        mf
        mfreadwrite
        mfuuid
        d3d11
        dxgi
        ole32
        oleaut32
    )
endif()

# Test executable to verify the compositor works on the host
add_executable(securestream_test
    src/main.cpp
)

target_link_libraries(securestream_test PRIVATE securestream_compositor)
