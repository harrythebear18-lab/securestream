#include "sources/microphone_source.hpp"

// Windows headers for WASAPI
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <iostream>
#include <cstring>

#pragma comment(lib, "ole32.lib")

namespace securestream {

MicrophoneSource::MicrophoneSource(std::string name, uint32_t sample_rate,
                                   uint32_t channels)
    : Source(std::move(name), SourceOrigin::HostMicrophone)
    , sample_rate_(sample_rate)
    , channels_(channels) {

    capturing_.store(true);
    capture_thread_ = std::thread(&MicrophoneSource::capture_loop, this);
}

MicrophoneSource::~MicrophoneSource() {
    capturing_.store(false);
    if (capture_thread_.joinable()) {
        capture_thread_.join();
    }
}

void MicrophoneSource::capture_loop() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInitialized = SUCCEEDED(hr);

    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX* pDeviceFormat = nullptr;
    HANDLE hEvent = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                          IID_PPV_ARGS(&pEnumerator));
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: could not create device enumerator\n";
        goto cleanup;
    }

    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: no default capture device\n";
        goto cleanup;
    }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(&pAudioClient));
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: could not activate audio client\n";
        goto cleanup;
    }

    hr = pAudioClient->GetMixFormat(&pDeviceFormat);
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: could not get mix format\n";
        goto cleanup;
    }

    {
        REFERENCE_TIME bufferDuration = 10000000;  // 1 second in 100ns units
        hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                      AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                      bufferDuration, 0, pDeviceFormat, nullptr);
    }
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: could not initialize audio client (0x"
                  << std::hex << hr << ")\n";
        goto cleanup;
    }

    hr = pAudioClient->GetService(IID_PPV_ARGS(&pCaptureClient));
    if (FAILED(hr)) {
        std::cerr << "MicrophoneSource: could not get capture client\n";
        goto cleanup;
    }

    hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!hEvent) goto cleanup;

    hr = pAudioClient->SetEventHandle(hEvent);
    if (FAILED(hr)) goto cleanup;

    hr = pAudioClient->Start();
    if (FAILED(hr)) goto cleanup;

    real_capture_ = true;
    std::cout << "MicrophoneSource: capture started ("
              << pDeviceFormat->nSamplesPerSec << "Hz, "
              << pDeviceFormat->nChannels << " ch, "
              << pDeviceFormat->wBitsPerSample << " bit)\n";

    // Capture loop
    while (capturing_.load()) {
        DWORD waitResult = WaitForSingleObject(hEvent, 100);
        if (waitResult != WAIT_OBJECT_0) continue;

        UINT32 packetLength = 0;
        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) break;

        while (packetLength > 0) {
            BYTE* pData = nullptr;
            UINT32 framesAvailable = 0;
            DWORD flags = 0;

            hr = pCaptureClient->GetBuffer(&pData, &framesAvailable, &flags,
                                            nullptr, nullptr);
            if (FAILED(hr)) break;

            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && framesAvailable > 0) {
                AudioChunk chunk;
                chunk.sample_rate = pDeviceFormat->nSamplesPerSec;
                chunk.channels = pDeviceFormat->nChannels;
                chunk.frames = framesAvailable;
                chunk.samples.resize(static_cast<size_t>(framesAvailable) *
                                     pDeviceFormat->nChannels);

                if (pDeviceFormat->wBitsPerSample == 16) {
                    const int16_t* pSamples = reinterpret_cast<const int16_t*>(pData);
                    for (size_t i = 0; i < chunk.samples.size(); ++i) {
                        chunk.samples[i] = static_cast<float>(pSamples[i]) / 32768.0f;
                    }
                } else if (pDeviceFormat->wBitsPerSample == 32) {
                    if (pDeviceFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                        std::memcpy(chunk.samples.data(), pData,
                                    chunk.samples.size() * sizeof(float));
                    } else {
                        const int32_t* pSamples = reinterpret_cast<const int32_t*>(pData);
                        for (size_t i = 0; i < chunk.samples.size(); ++i) {
                            chunk.samples[i] = static_cast<float>(pSamples[i]) /
                                               static_cast<float>(INT32_MAX);
                        }
                    }
                } else if (pDeviceFormat->wBitsPerSample == 24) {
                    for (UINT32 i = 0; i < framesAvailable * pDeviceFormat->nChannels; ++i) {
                        const BYTE* pByte = pData + i * 3;
                        int32_t sample = static_cast<int32_t>(
                            (pByte[0] << 8) | (pByte[1] << 16) | (pByte[2] << 24));
                        chunk.samples[i] = static_cast<float>(sample) /
                                           static_cast<float>(INT32_MAX);
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(audio_mutex_);
                    audio_queue_.push_back(std::move(chunk));
                    if (audio_queue_.size() > 10) {
                        audio_queue_.erase(audio_queue_.begin());
                    }
                }
            }

            pCaptureClient->ReleaseBuffer(framesAvailable);
            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) break;
        }
    }

    pAudioClient->Stop();

cleanup:
    if (pDeviceFormat) CoTaskMemFree(pDeviceFormat);
    if (hEvent) CloseHandle(hEvent);
    if (pCaptureClient) pCaptureClient->Release();
    if (pAudioClient) pAudioClient->Release();
    if (pDevice) pDevice->Release();
    if (pEnumerator) pEnumerator->Release();
    if (coInitialized) CoUninitialize();

    if (!real_capture_) {
        std::cerr << "MicrophoneSource: using silence fallback\n";
    }
}

AudioChunk MicrophoneSource::get_audio_chunk() {
    if (real_capture_) {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        if (!audio_queue_.empty()) {
            AudioChunk chunk = std::move(audio_queue_.front());
            audio_queue_.erase(audio_queue_.begin());
            return chunk;
        }
    }
    return {};
}

} // namespace securestream
