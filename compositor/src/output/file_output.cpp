#include "output/file_output.hpp"

// Windows headers for Media Foundation encoding
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <codecapi.h>
#include <iostream>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace securestream {

// Helper: set frame size on a media type
static HRESULT set_frame_size(IMFMediaType* pType, UINT32 width, UINT32 height) {
    UINT64 size = (static_cast<UINT64>(width) << 32) | height;
    return pType->SetUINT64(MF_MT_FRAME_SIZE, size);
}

// Helper: set frame rate on a media type
static HRESULT set_frame_rate(IMFMediaType* pType, UINT32 num, UINT32 den) {
    UINT64 ratio = (static_cast<UINT64>(num) << 32) | den;
    return pType->SetUINT64(MF_MT_FRAME_RATE, ratio);
}

FileOutput::FileOutput(std::string name, std::string file_path,
                       uint32_t width, uint32_t height, uint32_t fps,
                       uint32_t video_bitrate,
                       uint32_t audio_sample_rate, uint32_t audio_channels,
                       uint32_t audio_bitrate)
    : Output(std::move(name))
    , file_path_(std::move(file_path))
    , width_(width)
    , height_(height)
    , fps_(fps)
    , video_bitrate_(video_bitrate)
    , audio_sample_rate_(audio_sample_rate)
    , audio_channels_(audio_channels)
    , audio_bitrate_(audio_bitrate) {}

FileOutput::~FileOutput() {
    stop();
}

bool FileOutput::initialize() {
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "FileOutput: MFStartup failed (0x" << std::hex << hr << ")\n";
        return false;
    }
    mf_initialized_ = true;

    IMFSinkWriter* pSinkWriter = nullptr;
    hr = MFCreateSinkWriterFromURL(
        std::wstring(file_path_.begin(), file_path_.end()).c_str(),
        nullptr, nullptr, &pSinkWriter);
    if (FAILED(hr)) {
        std::cerr << "FileOutput: could not create sink writer (0x"
                  << std::hex << hr << ")\n";
        return false;
    }

    // --- Video stream: H.264 ---
    IMFMediaType* pVideoType = nullptr;
    hr = MFCreateMediaType(&pVideoType);
    if (SUCCEEDED(hr)) {
        pVideoType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pVideoType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        pVideoType->SetUINT32(MF_MT_AVG_BITRATE, video_bitrate_);
        set_frame_size(pVideoType, width_, height_);
        pVideoType->SetUINT32(MF_MT_INTERLACE_MODE,
                              MFVideoInterlace_Progressive);
        set_frame_rate(pVideoType, fps_, 1);

        hr = pSinkWriter->AddStream(pVideoType, reinterpret_cast<DWORD*>(&video_stream_index_));
        pVideoType->Release();
    }
    if (FAILED(hr)) {
        std::cerr << "FileOutput: could not add video stream\n";
        pSinkWriter->Release();
        return false;
    }

    // Set video input format: NV12 (common encoder input)
    IMFMediaType* pVideoInput = nullptr;
    hr = MFCreateMediaType(&pVideoInput);
    if (SUCCEEDED(hr)) {
        pVideoInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        pVideoInput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        set_frame_size(pVideoInput, width_, height_);
        pVideoInput->SetUINT32(MF_MT_INTERLACE_MODE,
                               MFVideoInterlace_Progressive);
        set_frame_rate(pVideoInput, fps_, 1);

        hr = pSinkWriter->SetInputMediaType(video_stream_index_,
                                             pVideoInput, nullptr);
        pVideoInput->Release();
    }
    if (FAILED(hr)) {
        // Try RGB32 as input
        hr = MFCreateMediaType(&pVideoInput);
        if (SUCCEEDED(hr)) {
            pVideoInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            pVideoInput->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            set_frame_size(pVideoInput, width_, height_);
            pVideoInput->SetUINT32(MF_MT_INTERLACE_MODE,
                                   MFVideoInterlace_Progressive);
            set_frame_rate(pVideoInput, fps_, 1);

            hr = pSinkWriter->SetInputMediaType(video_stream_index_,
                                                 pVideoInput, nullptr);
            pVideoInput->Release();
        }
    }
    if (FAILED(hr)) {
        std::cerr << "FileOutput: could not set video input format\n";
        pSinkWriter->Release();
        return false;
    }

    // --- Audio stream: AAC ---
    IMFMediaType* pAudioType = nullptr;
    hr = MFCreateMediaType(&pAudioType);
    if (SUCCEEDED(hr)) {
        pAudioType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudioType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        pAudioType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels_);
        pAudioType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_sample_rate_);
        pAudioType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                              audio_bitrate_ / 8);
        pAudioType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);

        hr = pSinkWriter->AddStream(pAudioType, reinterpret_cast<DWORD*>(&audio_stream_index_));
        pAudioType->Release();
    }
    if (FAILED(hr)) {
        std::cerr << "FileOutput: could not add audio stream\n";
        pSinkWriter->Release();
        return false;
    }

    // Set audio input format: PCM 16-bit
    IMFMediaType* pAudioInput = nullptr;
    hr = MFCreateMediaType(&pAudioInput);
    if (SUCCEEDED(hr)) {
        pAudioInput->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        pAudioInput->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        pAudioInput->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, audio_channels_);
        pAudioInput->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, audio_sample_rate_);
        pAudioInput->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        pAudioInput->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,
                               audio_channels_ * 2);
        pAudioInput->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                               audio_sample_rate_ * audio_channels_ * 2);
        pAudioInput->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

        hr = pSinkWriter->SetInputMediaType(audio_stream_index_,
                                             pAudioInput, nullptr);
        pAudioInput->Release();
    }
    if (FAILED(hr)) {
        std::cerr << "FileOutput: could not set audio input format\n";
        pSinkWriter->Release();
        return false;
    }

    // Begin writing
    hr = pSinkWriter->BeginWriting();
    if (FAILED(hr)) {
        std::cerr << "FileOutput: BeginWriting failed\n";
        pSinkWriter->Release();
        return false;
    }

    pSinkWriter_ = pSinkWriter;
    std::cout << "FileOutput: initialized " << width_ << "x" << height_
              << "@" << fps_ << "fps H.264 + AAC " << audio_sample_rate_
              << "Hz -> " << file_path_ << "\n";
    return true;
}

bool FileOutput::start() {
    if (active_) return true;
    if (!initialize()) return false;
    active_ = true;
    first_pts_ = -1;
    return true;
}

void FileOutput::stop() {
    if (!active_) return;
    active_ = false;

    if (pSinkWriter_) {
        IMFSinkWriter* pSinkWriter = static_cast<IMFSinkWriter*>(pSinkWriter_);
        pSinkWriter->Finalize();
        pSinkWriter->Release();
        pSinkWriter_ = nullptr;
    }

    if (mf_initialized_) {
        MFShutdown();
        mf_initialized_ = false;
    }

    std::cout << "FileOutput: wrote " << video_frames_.load()
              << " video frames, " << audio_chunks_.load()
              << " audio chunks to " << file_path_ << "\n";
}

// Convert BGRA8 frame to NV12
static void bgra_to_nv12(const Frame& frame, uint8_t* nv12,
                         uint32_t width, uint32_t height) {
    // Y plane
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t srcIdx = static_cast<size_t>(y) * frame.stride +
                            static_cast<size_t>(x) * 4;
            uint8_t b = frame.data[srcIdx + 0];
            uint8_t g = frame.data[srcIdx + 1];
            uint8_t r = frame.data[srcIdx + 2];

            // BT.601 YUV conversion
            int yVal = static_cast<int>(0.299f * r + 0.587f * g + 0.114f * b);
            nv12[y * width + x] = static_cast<uint8_t>(std::clamp(yVal, 0, 255));
        }
    }

    // UV plane (interleaved, subsampled 2x2)
    uint8_t* uvPlane = nv12 + width * height;
    for (uint32_t y = 0; y < height; y += 2) {
        for (uint32_t x = 0; x < width; x += 2) {
            // Average 2x2 block
            int uSum = 0, vSum = 0;
            for (uint32_t dy = 0; dy < 2 && y + dy < height; ++dy) {
                for (uint32_t dx = 0; dx < 2 && x + dx < width; ++dx) {
                    size_t srcIdx = static_cast<size_t>(y + dy) * frame.stride +
                                    static_cast<size_t>(x + dx) * 4;
                    uint8_t b = frame.data[srcIdx + 0];
                    uint8_t g = frame.data[srcIdx + 1];
                    uint8_t r = frame.data[srcIdx + 2];
                    uSum += static_cast<int>(-0.169f * r - 0.331f * g + 0.5f * b + 128);
                    vSum += static_cast<int>(0.5f * r - 0.419f * g - 0.081f * b + 128);
                }
            }
            int count = ((y + 1 < height) ? 2 : 1) * ((x + 1 < width) ? 2 : 1);
            size_t uvIdx = (y / 2) * width + x;
            uvPlane[uvIdx] = static_cast<uint8_t>(std::clamp(uSum / count, 0, 255));
            uvPlane[uvIdx + 1] = static_cast<uint8_t>(std::clamp(vSum / count, 0, 255));
        }
    }
}

void FileOutput::write_video(const Frame& frame, Timestamp pts) {
    if (!active_ || !pSinkWriter_) return;
    if (!frame.valid()) return;

    IMFSinkWriter* pSinkWriter = static_cast<IMFSinkWriter*>(pSinkWriter_);

    if (first_pts_ < 0) first_pts_ = pts;
    int64_t relativePts = (pts - first_pts_) * 10;  // us to 100ns units

    // Create a media buffer with the frame data
    // Convert BGRA8 to NV12 for the encoder
    uint32_t nv12Size = width_ * height_ * 3 / 2;
    IMFMediaBuffer* pBuffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(nv12Size, &pBuffer);
    if (FAILED(hr)) return;

    BYTE* pData = nullptr;
    hr = pBuffer->Lock(&pData, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        bgra_to_nv12(frame, pData, width_, height_);
        pBuffer->Unlock();
    }
    pBuffer->SetCurrentLength(nv12Size);

    // Create a sample
    IMFSample* pSample = nullptr;
    hr = MFCreateSample(&pSample);
    if (SUCCEEDED(hr)) {
        pSample->AddBuffer(pBuffer);
        pSample->SetSampleTime(relativePts);
        pSample->SetSampleDuration(10000000 / fps_);  // frame duration in 100ns

        hr = pSinkWriter->WriteSample(video_stream_index_, pSample);
        if (SUCCEEDED(hr)) {
            video_frames_++;
        }
        pSample->Release();
    }
    pBuffer->Release();
}

void FileOutput::write_audio(const AudioChunk& chunk, Timestamp pts) {
    if (!active_ || !pSinkWriter_) return;
    if (!chunk.valid()) return;

    IMFSinkWriter* pSinkWriter = static_cast<IMFSinkWriter*>(pSinkWriter_);

    if (first_pts_ < 0) first_pts_ = pts;
    int64_t relativePts = (pts - first_pts_) * 10;  // us to 100ns units

    // Convert float samples to 16-bit PCM
    uint32_t sampleCount = chunk.frames * chunk.channels;
    uint32_t bufferSize = sampleCount * 2;  // 16-bit

    IMFMediaBuffer* pBuffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &pBuffer);
    if (FAILED(hr)) return;

    BYTE* pData = nullptr;
    hr = pBuffer->Lock(&pData, nullptr, nullptr);
    if (SUCCEEDED(hr)) {
        int16_t* pPcm = reinterpret_cast<int16_t*>(pData);
        for (uint32_t i = 0; i < sampleCount; ++i) {
            float sample = std::clamp(chunk.samples[i], -1.0f, 1.0f);
            pPcm[i] = static_cast<int16_t>(sample * 32767.0f);
        }
        pBuffer->Unlock();
    }
    pBuffer->SetCurrentLength(bufferSize);

    IMFSample* pSample = nullptr;
    hr = MFCreateSample(&pSample);
    if (SUCCEEDED(hr)) {
        pSample->AddBuffer(pBuffer);
        pSample->SetSampleTime(relativePts);
        pSample->SetSampleDuration(
            static_cast<int64_t>(chunk.frames) * 10000000 /
            static_cast<int64_t>(chunk.sample_rate));

        hr = pSinkWriter->WriteSample(audio_stream_index_, pSample);
        if (SUCCEEDED(hr)) {
            audio_chunks_++;
        }
        pSample->Release();
    }
    pBuffer->Release();
}

} // namespace securestream
