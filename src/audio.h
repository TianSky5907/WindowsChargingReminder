#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool Start();
    void Play(const std::wstring& path, double volume);
    void Shutdown();

private:
    struct Request {
        std::wstring path;
        double volume = 1.0;
    };

    void WorkerMain();
    static bool PlayWavFile(const std::wstring& path, double volume);

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Request> queue_;
    bool stop_ = false;
    bool started_ = false;
};
