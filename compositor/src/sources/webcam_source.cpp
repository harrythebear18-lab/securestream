#include "sources/webcam_source.hpp"

// Windows headers for Media Foundation webcam capture
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <iostream>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace securestream {

// Helper: set frame size on a media type using the raw attribute API
static HRESULT set_frame_size(IMFMediaType* pType, UINT32 width, UINT32 height) {
    UINT64 size = (static_cast<UINT64>(width) << 32) | height;
    return pType->SetUINT64(MF_MT_FRAME_SIZE, size);
}

// Helper: get frame size from a media type
static HRESULT get_frame_size(IMFMediaType* pType, UINT32* width, UINT32* height) {
    UINT64 size = 0;
    HRESULT hr = pType->GetUINT64(MF_MT_FRAME_SIZE, &size);
    if (SUCCEEDED(hr)) {
        *width = static_cast<UINT32>(size >> 32);
        *height = static_cast<UINT32>(size & 0xFFFFFFFF);
    }
    return hr;
}

// Convert NV12 to BGRA8. NV12 is planar: Y plane (w*h) then interleaved UV (w*h/2).
static Frame nv12_to_bgra(const uint8_t* data, uint32_t width, uint32_t height) {
    Frame frame;
    frame.allocate(width, height);

    const uint8_t* yPlane = data;
    const uint8_t* uvPlane = data + width * height;

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            int y = yPlane[row * width + col];
            int uvRow = row / 2;
            int uvCol = col / 2;
            int u = uvPlane[uvRow * width + uvCol * 2] - 128;
            int v = uvPlane[uvRow * width + uvCol * 2 + 1] - 128;

            int r = y + static_cast<int>(1.402f * v);
            int g = y - static_cast<int>(0.344f * u) - static_cast<int>(0.714f * v);
            int b = y + static_cast<int>(1.772f * u);

            size_t idx = static_cast<size_t>(row) * frame.stride + static_cast<size_t>(col) * 4;
            frame.data[idx + 0] = static_cast<uint8_t>(std::clamp(b, 0, 255));
            frame.data[idx + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
            frame.data[idx + 2] = static_cast<uint8_t>(std::clamp(r, 0, 255));
            frame.data[idx + 3] = 255;
        }
    }

    return frame;
}

// Convert YUY2 to BGRA8. YUY2 is packed: Y0, U, Y1, V for every 2 horizontal pixels.
static Frame yuy2_to_bgra(const uint8_t* data, uint32_t width, uint32_t height) {
    Frame frame;
    frame.allocate(width, height);

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; col += 2) {
            size_t srcIdx = static_cast<size_t>(row) * width * 2 + static_cast<size_t>(col) * 2;
            int y0 = data[srcIdx];
            int u = data[srcIdx + 1] - 128;
            int y1 = data[srcIdx + 2];
            int v = data[srcIdx + 3] - 128;

            int r0 = y0 + static_cast<int>(1.402f * v);
            int g0 = y0 - static_cast<int>(0.344f * u) - static_cast<int>(0.714f * v);
            int b0 = y0 + static_cast<int>(1.772f * u);

            int r1 = y1 + static_cast<int>(1.402f * v);
            int g1 = y1 - static_cast<int>(0.344f * u) - static_cast<int>(0.714f * v);
            int b1 = y1 + static_cast<int>(1.772f * u);

            size_t dstIdx0 = static_cast<size_t>(row) * frame.stride + static_cast<size_t>(col) * 4;
            size_t dstIdx1 = dstIdx0 + 4;

            frame.data[dstIdx0 + 0] = static_cast<uint8_t>(std::clamp(b0, 0, 255));
            frame.data[dstIdx0 + 1] = static_cast<uint8_t>(std::clamp(g0, 0, 255));
            frame.data[dstIdx0 + 2] = static_cast<uint8_t>(std::clamp(r0, 0, 255));
            frame.data[dstIdx0 + 3] = 255;

            if (col + 1 < width) {
                frame.data[dstIdx1 + 0] = static_cast<uint8_t>(std::clamp(b1, 0, 255));
                frame.data[dstIdx1 + 1] = static_cast<uint8_t>(std::clamp(g1, 0, 255));
                frame.data[dstIdx1 + 2] = static_cast<uint8_t>(std::clamp(r1, 0, 255));
                frame.data[dstIdx1 + 3] = 255;
            }
        }
    }

    return frame;
}

WebcamSource::WebcamSource(std::string name, uint32_t width, uint32_t height)
    : Source(std::move(name), SourceOrigin::HostWebcam)
    , width_(width)
    , height_(height) {

    // Try to start real webcam capture
    capturing_.store(true);
    capture_thread_ = std::thread(&WebcamSource::capture_loop, this);
}

WebcamSource::~WebcamSource() {
    capturing_.store(false);
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

void WebcamSource::capture_loop() {
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "WebcamSource: MFStartup failed (0x" << std::hex << hr << ")\n";
        real_capture_ = false;
        return;
    }

    // Enumerate video capture devices
    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) {
        MFShutdown();
        real_capture_ = false;
        return;
    }

    hr = pAttributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) {
        pAttributes->Release();
        MFShutdown();
        real_capture_ = false;
        return;
    }

    IMFActivate** ppDevices = nullptr;
    UINT32 deviceCount = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &deviceCount);
    pAttributes->Release();

    if (FAILED(hr) || deviceCount == 0) {
        std::cerr << "WebcamSource: no webcam devices found\n";
        if (ppDevices) CoTaskMemFree(ppDevices);
        MFShutdown();
        real_capture_ = false;
        return;
    }

    std::cout << "WebcamSource: found " << deviceCount << " device(s)\n";

    // Create a media source from the first device
    IMFMediaSource* pSource = nullptr;
    hr = ppDevices[0]->ActivateObject(IID_PPV_ARGS(&pSource));
    if (FAILED(hr)) {
        for (UINT32 i = 0; i < deviceCount; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        MFShutdown();
        real_capture_ = false;
        return;
    }

    // Get the friendly name
    WCHAR* deviceName = nullptr;
    UINT32 nameLen = 0;
    ppDevices[0]->GetAllocatedString(
        MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &deviceName, &nameLen);
    if (deviceName) {
        std::wcout << L"WebcamSource: using " << deviceName << L"\n";
        CoTaskMemFree(deviceName);
    }

    // Create source reader
    IMFSourceReader* pReader = nullptr;
    hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader);
    if (FAILED(hr)) {
        pSource->Release();
        for (UINT32 i = 0; i < deviceCount; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        MFShutdown();
        real_capture_ = false;
        return;
    }

    // Try output formats in order of preference. Most webcams support NV12.
    // Media Foundation's source reader can convert some formats automatically.
    GUID formatPrefs[] = {MFVideoFormat_RGB32, MFVideoFormat_NV12, MFVideoFormat_YUY2};
    GUID selectedFormat = GUID_NULL;

    for (const auto& fmt : formatPrefs) {
        IMFMediaType* pType = nullptr;
        hr = MFCreateMediaType(&pType);
        if (SUCCEEDED(hr)) {
            pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pType->SetGUID(MF_MT_SUBTYPE, fmt);
            set_frame_size(pType, width_, height_);

            hr = pReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
            pType->Release();
        }

        if (SUCCEEDED(hr)) {
            selectedFormat = fmt;
            break;
        }

        // Try without forced resolution
        hr = MFCreateMediaType(&pType);
        if (SUCCEEDED(hr)) {
            pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pType->SetGUID(MF_MT_SUBTYPE, fmt);
            hr = pReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
            pType->Release();
        }

        if (SUCCEEDED(hr)) {
            selectedFormat = fmt;
            break;
        }
    }

    if (selectedFormat == GUID_NULL) {
        std::cerr << "WebcamSource: no supported output format found\n";
        pReader->Release();
        pSource->Release();
        for (UINT32 i = 0; i < deviceCount; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        MFShutdown();
        real_capture_ = false;
        return;
    }

    // Log the selected format
    const char* fmtName = "Unknown";
    if (selectedFormat == MFVideoFormat_RGB32) fmtName = "RGB32";
    else if (selectedFormat == MFVideoFormat_NV12) fmtName = "NV12";
    else if (selectedFormat == MFVideoFormat_YUY2) fmtName = "YUY2";
    std::cout << "WebcamSource: using " << fmtName << " format\n";

    // Read frames
    real_capture_ = true;
    std::cout << "WebcamSource: capture started\n";

    while (capturing_.load()) {
        DWORD streamIndex = 0;
        DWORD streamFlags = 0;
        LONGLONG timestamp = 0;
        IMFSample* pSample = nullptr;

        hr = pReader->ReadSample(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &streamIndex, &streamFlags, &timestamp, &pSample);

        if (FAILED(hr)) {
            std::cerr << "WebcamSource: ReadSample failed (0x" << std::hex << hr << ")\n";
            break;
        }

        if (pSample == nullptr) continue;

        // Convert sample to a Frame
        IMFMediaBuffer* pBuffer = nullptr;
        hr = pSample->ConvertToContiguousBuffer(&pBuffer);
        if (FAILED(hr)) {
            pSample->Release();
            continue;
        }

        BYTE* pData = nullptr;
        DWORD maxLen = 0;
        DWORD curLen = 0;
        hr = pBuffer->Lock(&pData, &maxLen, &curLen);
        if (FAILED(hr)) {
            pBuffer->Release();
            pSample->Release();
            continue;
        }

        // Get the actual frame dimensions from the current media type
        UINT32 frameWidth = width_;
        UINT32 frameHeight = height_;
        IMFMediaType* pCurrentType = nullptr;
        hr = pReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
        if (SUCCEEDED(hr)) {
            get_frame_size(pCurrentType, &frameWidth, &frameHeight);
            pCurrentType->Release();
        }

        // Create a Frame from the buffer data, converting from the
        // device's output format to BGRA8.
        Frame frame;

        if (selectedFormat == MFVideoFormat_RGB32) {
            // RGB32 is already BGRA in memory on Windows
            frame.allocate(frameWidth, frameHeight);
            uint32_t expectedSize = frameWidth * frameHeight * 4;
            if (curLen >= expectedSize) {
                std::memcpy(frame.data.data(), pData, expectedSize);
            }
        } else if (selectedFormat == MFVideoFormat_NV12) {
            // NV12: Y plane (w*h) + UV plane (w*h/2)
            uint32_t expectedSize = frameWidth * frameHeight * 3 / 2;
            if (curLen >= expectedSize) {
                frame = nv12_to_bgra(pData, frameWidth, frameHeight);
            }
        } else if (selectedFormat == MFVideoFormat_YUY2) {
            // YUY2: packed, 2 bytes per pixel average
            uint32_t expectedSize = frameWidth * frameHeight * 2;
            if (curLen >= expectedSize) {
                frame = yuy2_to_bgra(pData, frameWidth, frameHeight);
            }
        }

        pBuffer->Unlock();
        pBuffer->Release();
        pSample->Release();

        if (frame.valid()) {
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_frame_ = std::move(frame);
        }
    }

    pReader->Release();
    pSource->Release();
    for (UINT32 i = 0; i < deviceCount; ++i) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);
    MFShutdown();
}

Frame WebcamSource::get_video_frame() {
    if (real_capture_) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (latest_frame_.valid()) {
            return latest_frame_;
        }
    }

    frame_counter_++;
    return generate_test_pattern();
}

Frame WebcamSource::generate_test_pattern() {
    Frame frame;
    frame.allocate(width_, height_);

    uint64_t t = frame_counter_ % 256;

    for (uint32_t y = 0; y < height_; ++y) {
        for (uint32_t x = 0; x < width_; ++x) {
            size_t idx = static_cast<size_t>(y) * frame.stride +
                        static_cast<size_t>(x) * 4;

            uint8_t r = static_cast<uint8_t>((x + t) & 0xFF);
            uint8_t g = static_cast<uint8_t>((y + t) & 0xFF);
            uint8_t b = static_cast<uint8_t>((x + y + t) & 0xFF);

            frame.data[idx + 0] = b;
            frame.data[idx + 1] = g;
            frame.data[idx + 2] = r;
            frame.data[idx + 3] = 255;
        }
    }

    // Border to indicate webcam region
    uint32_t border = 4;
    for (uint32_t i = 0; i < border; ++i) {
        for (uint32_t x = 0; x < width_; ++x) {
            for (uint32_t yy : {i, height_ - 1 - i}) {
                size_t idx = static_cast<size_t>(yy) * frame.stride +
                            static_cast<size_t>(x) * 4;
                frame.data[idx + 0] = 255;
                frame.data[idx + 1] = 255;
                frame.data[idx + 2] = 255;
                frame.data[idx + 3] = 255;
            }
        }
        for (uint32_t y = 0; y < height_; ++y) {
            for (uint32_t xx : {i, width_ - 1 - i}) {
                size_t idx = static_cast<size_t>(y) * frame.stride +
                            static_cast<size_t>(xx) * 4;
                frame.data[idx + 0] = 255;
                frame.data[idx + 1] = 255;
                frame.data[idx + 2] = 255;
                frame.data[idx + 3] = 255;
            }
        }
    }

    return frame;
}

} // namespace securestream
