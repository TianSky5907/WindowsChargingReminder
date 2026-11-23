#include "audio.h"

#include "log.h"
#include "util.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#endif
#ifndef AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kMaxWavBytes = 64 * 1024 * 1024;

struct WavClip {
    WAVEFORMATEX format{};
    std::vector<BYTE> pcm;
};

uint32_t ReadU32(const BYTE* data) {
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool ReadWholeFile(const std::wstring& path, std::vector<BYTE>& bytes) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > static_cast<LONGLONG>(kMaxWavBytes)) {
        CloseHandle(file);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));

    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD read = 0;
        const DWORD request = static_cast<DWORD>(
            std::min<size_t>(bytes.size() - offset, 1u << 20));
        if (!ReadFile(file, bytes.data() + offset, request, &read, nullptr) ||
            read == 0) {
            CloseHandle(file);
            return false;
        }
        offset += read;
    }

    CloseHandle(file);
    return true;
}

int16_t ToInt16Sample(const BYTE* data, WORD tag, WORD bits) {
    if (tag == WAVE_FORMAT_IEEE_FLOAT && bits == 32) {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(value));
        if (!std::isfinite(value))
            return 0;
        value = std::clamp(value, -1.0f, 1.0f);
        return static_cast<int16_t>(std::lrint(value * 32767.0f));
    }

    switch (bits) {
        case 8: {
            const int value = static_cast<int>(data[0]) - 128;
            return static_cast<int16_t>(value * 256);
        }
        case 16: {
            int16_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return value;
        }
        case 24: {
            int32_t value = static_cast<int32_t>(data[0]) |
                            (static_cast<int32_t>(data[1]) << 8) |
                            (static_cast<int32_t>(data[2]) << 16);
            if ((value & 0x00800000) != 0)
                value |= static_cast<int32_t>(0xFF000000);
            return static_cast<int16_t>(value >> 8);
        }
        case 32: {
            int32_t value = 0;
            std::memcpy(&value, data, sizeof(value));
            return static_cast<int16_t>(value >> 16);
        }
        default:
            return 0;
    }
}

bool LoadWav(const std::wstring& path, WavClip& clip, std::wstring* error) {
    std::vector<BYTE> file;
    if (!ReadWholeFile(path, file)) {
        if (error) *error = L"无法读取 WAV 文件";
        return false;
    }

    if (file.size() < 12 || std::memcmp(file.data(), "RIFF", 4) != 0 ||
        std::memcmp(file.data() + 8, "WAVE", 4) != 0) {
        if (error) *error = L"不是有效的 RIFF/WAVE 文件";
        return false;
    }

    size_t formatOffset = 0;
    size_t formatSize = 0;
    size_t dataOffset = 0;
    size_t dataSize = 0;

    size_t offset = 12;
    while (offset + 8 <= file.size()) {
        const uint32_t chunkSize = ReadU32(file.data() + offset + 4);
        const size_t payload = offset + 8;
        if (payload + chunkSize > file.size())
            break;

        if (std::memcmp(file.data() + offset, "fmt ", 4) == 0) {
            formatOffset = payload;
            formatSize = chunkSize;
        } else if (std::memcmp(file.data() + offset, "data", 4) == 0) {
            dataOffset = payload;
            dataSize = chunkSize;
        }

        offset = payload + chunkSize + (chunkSize & 1u);
    }

    if (formatOffset == 0 || formatSize < 16 ||
        dataOffset == 0 || dataSize == 0) {
        if (error) *error = L"WAV 缺少 fmt 或 data 块";
        return false;
    }

    WAVEFORMATEX source{};
    std::memcpy(&source, file.data() + formatOffset,
                std::min<size_t>(formatSize, sizeof(source)));

    WORD tag = source.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE && formatSize >= 40) {
        GUID subFormat{};
        std::memcpy(&subFormat, file.data() + formatOffset + 24,
                    sizeof(subFormat));

        static const GUID kPcm = {
            0x00000001, 0x0000, 0x0010,
            {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
        static const GUID kFloat = {
            0x00000003, 0x0000, 0x0010,
            {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

        if (IsEqualGUID(subFormat, kPcm))
            tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(subFormat, kFloat))
            tag = WAVE_FORMAT_IEEE_FLOAT;
    }

    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT) {
        if (error) *error = L"不支持的 WAV 编码（仅 PCM/浮点）";
        return false;
    }
    if (source.nChannels == 0 || source.nChannels > 8 ||
        source.nSamplesPerSec == 0 ||
        (source.wBitsPerSample != 8 && source.wBitsPerSample != 16 &&
         source.wBitsPerSample != 24 && source.wBitsPerSample != 32)) {
        if (error) *error = L"不支持的 WAV 参数";
        return false;
    }

    const WORD blockAlign = source.nBlockAlign != 0
                                ? source.nBlockAlign
                                : static_cast<WORD>(source.nChannels *
                                                    (source.wBitsPerSample / 8));
    if (blockAlign == 0) {
        if (error) *error = L"WAV 块对齐为 0";
        return false;
    }

    const size_t frames = dataSize / blockAlign;
    if (frames == 0) {
        if (error) *error = L"WAV 没有采样数据";
        return false;
    }

    WAVEFORMATEX output{};
    output.wFormatTag = WAVE_FORMAT_PCM;
    output.nChannels = 2;
    output.nSamplesPerSec = source.nSamplesPerSec;
    output.wBitsPerSample = 16;
    output.nBlockAlign = 4;
    output.nAvgBytesPerSec = source.nSamplesPerSec * 4;
    output.cbSize = 0;

    clip.format = output;
    clip.pcm.resize(frames * 4);

    const BYTE* input = file.data() + dataOffset;
    BYTE* out = clip.pcm.data();
    const size_t bytesPerSample = source.wBitsPerSample / 8;

    for (size_t frame = 0; frame < frames; ++frame) {
        const BYTE* sourceFrame = input + frame * blockAlign;
        const size_t leftIndex = 0;
        const size_t rightIndex = source.nChannels > 1 ? 1 : 0;

        const int16_t left = ToInt16Sample(
            sourceFrame + leftIndex * bytesPerSample, tag,
            source.wBitsPerSample);
        const int16_t right = ToInt16Sample(
            sourceFrame + rightIndex * bytesPerSample, tag,
            source.wBitsPerSample);

        std::memcpy(out + frame * 4, &left, sizeof(left));
        std::memcpy(out + frame * 4 + 2, &right, sizeof(right));
    }

    return true;
}

void ApplyVolume(std::vector<BYTE>& pcm, double volume) {
    if (volume >= 0.999)
        return;

    volume = std::clamp(volume, 0.0, 1.0);
    for (size_t offset = 0; offset + 1 < pcm.size(); offset += 2) {
        int16_t sample = 0;
        std::memcpy(&sample, pcm.data() + offset, sizeof(sample));
        int scaled = static_cast<int>(std::lrint(sample * volume));
        scaled = std::clamp(scaled, -32768, 32767);
        const int16_t result = static_cast<int16_t>(scaled);
        std::memcpy(pcm.data() + offset, &result, sizeof(result));
    }
}

bool PlayClip(WavClip& clip) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(enumerator.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR(L"CoCreateInstance(MMDeviceEnumerator) failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf());
    if (FAILED(hr)) {
        LOG_ERROR(L"GetDefaultAudioEndpoint failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR(L"Activate(IAudioClient) failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    const DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0,
                            &clip.format, nullptr);
    if (FAILED(hr)) {
        LOG_ERROR(L"IAudioClient::Initialize failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    UINT32 bufferFrames = 0;
    hr = client->GetBufferSize(&bufferFrames);
    if (FAILED(hr) || bufferFrames == 0) {
        LOG_ERROR(L"IAudioClient::GetBufferSize failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    ComPtr<IAudioRenderClient> render;
    hr = client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
    if (FAILED(hr)) {
        LOG_ERROR(L"GetService(IAudioRenderClient) failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    hr = client->Start();
    if (FAILED(hr)) {
        LOG_ERROR(L"IAudioClient::Start failed: " +
                  util::HexDword(static_cast<uint32_t>(hr)));
        return false;
    }

    const size_t totalFrames = clip.pcm.size() / 4;
    size_t written = 0;

    while (written < totalFrames) {
        UINT32 padding = 0;
        hr = client->GetCurrentPadding(&padding);
        if (FAILED(hr))
            break;

        const UINT32 available =
            padding >= bufferFrames ? 0 : bufferFrames - padding;
        if (available == 0) {
            Sleep(2);
            continue;
        }

        const UINT32 frames = static_cast<UINT32>(
            std::min<size_t>(available, totalFrames - written));
        BYTE* buffer = nullptr;
        hr = render->GetBuffer(frames, &buffer);
        if (FAILED(hr))
            break;

        std::memcpy(buffer, clip.pcm.data() + written * 4,
                    static_cast<size_t>(frames) * 4);
        hr = render->ReleaseBuffer(frames, 0);
        if (FAILED(hr))
            break;

        written += frames;
    }

    for (int i = 0; i < 1000; ++i) {
        UINT32 padding = 0;
        if (FAILED(client->GetCurrentPadding(&padding)) || padding == 0)
            break;
        Sleep(2);
    }

    client->Stop();
    return written > 0;
}

}  // namespace

AudioPlayer::~AudioPlayer() {
    Shutdown();
}

bool AudioPlayer::Start() {
    if (started_)
        return true;

    stop_ = false;
    worker_ = std::thread(&AudioPlayer::WorkerMain, this);
    started_ = true;
    return true;
}

void AudioPlayer::Play(const std::wstring& path, double volume) {
    if (!started_)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_)
            return;
        if (queue_.size() >= 4)
            queue_.pop_front();
        queue_.push_back(Request{path, std::clamp(volume, 0.0, 1.0)});
    }
    condition_.notify_one();
}

void AudioPlayer::Shutdown() {
    if (!started_)
        return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        queue_.clear();
    }
    condition_.notify_all();

    if (worker_.joinable())
        worker_.join();

    started_ = false;
}

void AudioPlayer::WorkerMain() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty())
                break;
            request = std::move(queue_.front());
            queue_.pop_front();
        }

        PlayWavFile(request.path, request.volume);
    }

    if (SUCCEEDED(com))
        CoUninitialize();
}

bool AudioPlayer::PlayWavFile(const std::wstring& path, double volume) {
    WavClip clip;
    std::wstring error;
    if (!LoadWav(path, clip, &error)) {
        LOG_ERROR(L"WAV load failed: " + path + L" (" + error + L")");
        return false;
    }

    ApplyVolume(clip.pcm, volume);
    return PlayClip(clip);
}
