#include "sources/media_source.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <iostream>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace securestream {

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

MediaSource::MediaSource(std::string name, std::string file_path, bool loop)
    : Source(std::move(name), SourceOrigin::VMLocal)
    , file_path_(std::move(file_path))
    , loop_(loop) {

    playing_.store(true);
    decoding_.store(true);
    decode_thread_ = std::thread(&MediaSource::decode_loop, this);
}

MediaSource::~MediaSource() {
    decoding_.store(false);
    playing_.store(false);
    if (decode_thread_.joinable()) {
        decode_thread_.join();
    }
}

void MediaSource::decode_loop() {
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "MediaSource: MFStartup failed\n";
        return;
    }

    // Create a source reader from the file
    IMFSourceReader* pReader = nullptr;
    std::wstring wpath(file_path_.begin(), file_path_.end());

    IMFAttributes* pAttrs = nullptr;
    MFCreateAttributes(&pAttrs, 1);
    if (pAttrs) {
        pAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromURL(wpath.c_str(), pAttrs, &pReader);
    if (pAttrs) pAttrs->Release();

    if (FAILED(hr)) {
        std::cerr << "MediaSource: could not open " << file_path_
                  << " (0x" << std::hex << hr << ")\n";
        MFShutdown();
        return;
    }

    std::cout << "MediaSource: opened " << file_path_ << "\n";

    // Set video output format to RGB32 (BGRA in memory)
    bool videoOK = false;
    IMFMediaType* pVideoType = nullptr;
    hr = MFCreateMediaType(&pVideoType);
    if (SUCCEEDED(hr)) {
        pVideoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pVideoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);

        hr = pReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pVideoType);
        pVideoType->Release();
    }

    if (SUCCEEDED(hr)) {
        has_video_ = true;
        videoOK = true;
    } else {
        // Try NV12 if RGB32 not available
        hr = MFCreateMediaType(&pVideoType);
        if (SUCCEEDED(hr)) {
            pVideoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pVideoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
            hr = pReader->SetCurrentMediaType(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pVideoType);
            pVideoType->Release();
        }
        if (SUCCEEDED(hr)) {
            has_video_ = true;
            videoOK = true;
        }
    }

    // Set audio output format to PCM 16-bit
    bool audioOK = false;
    IMFMediaType* pAudioType = nullptr;
    hr = MFCreateMediaType(&pAudioType);
    if (SUCCEEDED(hr)) {
        pAudioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pAudioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

        hr = pReader->SetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, pAudioType);
        pAudioType->Release();
    }

    if (SUCCEEDED(hr)) {
        has_audio_ = true;
        audioOK = true;
    }

    // Get the video format info for conversion
    bool useNV12 = false;
    if (videoOK) {
        IMFMediaType* pCurrentType = nullptr;
        hr = pReader->GetCurrentMediaType(
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
        if (SUCCEEDED(hr)) {
            GUID subtype = GUID_NULL;
            pCurrentType->GetGUID(MF_MT_SUBTYPE, &subtype);
            if (subtype == MFVideoFormat_NV12) useNV12 = true;
            pCurrentType->Release();
        }
    }

    std::cout << "MediaSource: video=" << (has_video_ ? "yes" : "no")
              << " audio=" << (has_audio_ ? "yes" : "no")
              << " format=" << (useNV12 ? "NV12" : "RGB32") << "\n";

    // Decode loop
    while (decoding_.load()) {
        if (!playing_.load()) {
            Sleep(10);
            continue;
        }

        // Read video
        if (videoOK) {
            DWORD streamIndex = 0;
            DWORD streamFlags = 0;
            LONGLONG timestamp = 0;
            IMFSample* pSample = nullptr;

            hr = pReader->ReadSample(
                (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0, &streamIndex, &streamFlags, &timestamp, &pSample);

            if (FAILED(hr)) {
                std::cerr << "MediaSource: video ReadSample failed\n";
                break;
            }

            if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (loop_) {
                    // Seek back to beginning
                    PROPVARIANT var;
                    PropVariantInit(&var);
                    var.vt = VT_I8;
                    var.hVal.QuadPart = 0;
                    pReader->SetCurrentPosition(GUID_NULL, var);
                    PropVariantClear(&var);
                    continue;
                } else {
                    playing_.store(false);
                    break;
                }
            }

            if (pSample) {
                IMFMediaBuffer* pBuffer = nullptr;
                hr = pSample->ConvertToContiguousBuffer(&pBuffer);
                if (SUCCEEDED(hr)) {
                    BYTE* pData = nullptr;
                    DWORD maxLen = 0, curLen = 0;
                    hr = pBuffer->Lock(&pData, &maxLen, &curLen);
                    if (SUCCEEDED(hr)) {
                        UINT32 fw = 0, fh = 0;
                        IMFMediaType* pCurrentType = nullptr;
                        hr = pReader->GetCurrentMediaType(
                            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);
                        if (SUCCEEDED(hr)) {
                            get_frame_size(pCurrentType, &fw, &fh);
                            pCurrentType->Release();
                        }

                        if (fw > 0 && fh > 0) {
                            Frame frame;
                            frame.allocate(fw, fh);

                            if (useNV12) {
                                // NV12 to BGRA8
                                const uint8_t* yPlane = pData;
                                const uint8_t* uvPlane = pData + fw * fh;
                                for (uint32_t row = 0; row < fh; ++row) {
                                    for (uint32_t col = 0; col < fw; ++col) {
                                        int y = yPlane[row * fw + col];
                                        int uvRow = row / 2;
                                        int uvCol = col / 2;
                                        int u = uvPlane[uvRow * fw + uvCol * 2] - 128;
                                        int v = uvPlane[uvRow * fw + uvCol * 2 + 1] - 128;
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
                            } else {
                                // RGB32 is already BGRA in memory
                                uint32_t expected = fw * fh * 4;
                                if (curLen >= expected) {
                                    std::memcpy(frame.data.data(), pData, expected);
                                }
                            }

                            if (frame.valid()) {
                                std::lock_guard<std::mutex> lock(video_mutex_);
                                latest_video_frame_ = std::move(frame);
                            }
                        }
                    }
                    pBuffer->Unlock();
                    pBuffer->Release();
                }
                pSample->Release();
            }
        }

        // Read audio
        if (audioOK) {
            DWORD streamIndex = 0;
            DWORD streamFlags = 0;
            LONGLONG timestamp = 0;
            IMFSample* pSample = nullptr;

            hr = pReader->ReadSample(
                (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0, &streamIndex, &streamFlags, &timestamp, &pSample);

            if (SUCCEEDED(hr) && pSample) {
                IMFMediaBuffer* pBuffer = nullptr;
                hr = pSample->ConvertToContiguousBuffer(&pBuffer);
                if (SUCCEEDED(hr)) {
                    BYTE* pData = nullptr;
                    DWORD maxLen = 0, curLen = 0;
                    hr = pBuffer->Lock(&pData, &maxLen, &curLen);
                    if (SUCCEEDED(hr)) {
                        IMFMediaType* pCurrentType = nullptr;
                        hr = pReader->GetCurrentMediaType(
                            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pCurrentType);
                        if (SUCCEEDED(hr)) {
                            UINT32 sr = 0, ch = 0;
                            pCurrentType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
                            pCurrentType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &ch);

                            uint32_t sampleCount = curLen / 2;  // 16-bit
                            uint32_t frames = ch > 0 ? sampleCount / ch : 0;

                            AudioChunk chunk;
                            chunk.sample_rate = sr;
                            chunk.channels = ch;
                            chunk.frames = frames;
                            chunk.samples.resize(sampleCount);

                            const int16_t* pPcm = reinterpret_cast<const int16_t*>(pData);
                            for (uint32_t i = 0; i < sampleCount; ++i) {
                                chunk.samples[i] = static_cast<float>(pPcm[i]) / 32768.0f;
                            }

                            {
                                std::lock_guard<std::mutex> lock(audio_mutex_);
                                audio_queue_.push_back(std::move(chunk));
                                if (audio_queue_.size() > 30) {
                                    audio_queue_.erase(audio_queue_.begin());
                                }
                            }

                            pCurrentType->Release();
                        }
                    }
                    pBuffer->Unlock();
                    pBuffer->Release();
                }
                pSample->Release();
            }
        }
    }

    pReader->Release();
    MFShutdown();
}

Frame MediaSource::get_video_frame() {
    std::lock_guard<std::mutex> lock(video_mutex_);
    if (latest_video_frame_.valid()) {
        return latest_video_frame_;
    }
    return {};
}

AudioChunk MediaSource::get_audio_chunk() {
    std::lock_guard<std::mutex> lock(audio_mutex_);
    if (!audio_queue_.empty()) {
        AudioChunk chunk = std::move(audio_queue_.front());
        audio_queue_.erase(audio_queue_.begin());
        return chunk;
    }
    return {};
}

void MediaSource::play() { playing_.store(true); }
void MediaSource::pause() { playing_.store(false); }
void MediaSource::stop() {
    playing_.store(false);
}

} // namespace securestream
