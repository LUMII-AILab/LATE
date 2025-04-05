#pragma once

#include <limits>
#include <memory>

struct speech_range {
    // size_t start;
    // size_t end;
    // speech_range(size_t start, size_t end) : start(start), end(end) {}
    int start;
    int end;
    speech_range(int start, int end) : start(start), end(end) {}
};

typedef struct VADConfig {
    int sample_rate = 16000;
    int windows_frame_size_ms = 64;
    float threshold = 0.5;
    int min_silence_duration_ms = 2000; // default was 0
    int speech_pad_ms = 64;
    int min_speech_duration_ms = 64;
    float max_speech_duration_s = std::numeric_limits<float>::infinity();
} VADConfig;


class VADModelImpl;
class VADImpl;

class VADModel {
public:
    VADModel();
    VADModel(const std::string& model_path);
    ~VADModel();

    operator bool() const;
    std::string error() const { return what; }

private:
    friend class VAD;
    std::shared_ptr<VADModelImpl> impl;
    std::string what;
};


class VAD {
    int _sample_rate = 16000;

public:
    class IteratorImpl;

    class Iterator {
        IteratorImpl* impl;
    public:
        Iterator(IteratorImpl* impl) : impl(impl) {}
        ~Iterator();

        speech_range& operator*() const;

        Iterator& operator++();

        Iterator operator++(int) {
            Iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const Iterator& other) const;
        bool operator!=(const Iterator& other) const { return !(*this == other); }
    };

    // VAD(const std::string& model_path, VADConfig config = VADConfig());
    VAD(VADModel& model, VADConfig config = VADConfig());
    ~VAD();

    VAD(const VAD&) = delete;
    VAD& operator=(const VAD&) = delete;

    VAD(VAD&&) noexcept = default;
    VAD& operator=(VAD&&) noexcept = default;

    void reset();
    size_t size() const;

    void start(const float* samples, size_t count);
    void start(const std::vector<float>& samples) { start(samples.data(), samples.size()); }

    Iterator begin();
    Iterator end();

    int sample_rate() const { return _sample_rate; }

private:
    std::unique_ptr<VADImpl> impl;
};


void test_vad_range(const std::string& path, const std::vector<float>& data, VADConfig config = VADConfig());
