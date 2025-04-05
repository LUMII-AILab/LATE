#pragma once

#include <string>
#include <vector>
#include <memory>

#include "util.hpp"


class WavBuffer {
public:
    // WavBuffer(WavBuffer&& other) : _data(other._data), _size(other._size) { other._data = nullptr; }
    WavBuffer() {}
    ~WavBuffer() { free(); }
    // no copying
    WavBuffer(const WavBuffer&) = delete;
    WavBuffer& operator=(const WavBuffer&) = delete;
    // only moving allowed
    WavBuffer(WavBuffer&&) noexcept = default;
    WavBuffer& operator=(WavBuffer&&) noexcept = default;
    bool from_pcm(const float* samples, size_t count, int sample_rate, int channels = 1);
    bool from_pcm(const std::vector<float>& samples, int sample_rate, int channels = 1) { return from_pcm(samples.data(), samples.size(), sample_rate, channels); }
    void free();
    const void* data() const { return _data; }
    size_t size() const { return _size; }
    operator bool() const { return _data != nullptr && _size > 0; }
private:
    friend std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<float>& data, int sample_rate, int channels);
    friend std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<int16_t>& data, int sample_rate, int channels);
    WavBuffer(void* data, size_t size) : _data(data), _size(size) {}
    void* _data = nullptr;
    size_t _size = 0;
};

class PCMBuffer {
public:
    PCMBuffer() {}
    PCMBuffer(const void* data, size_t size) { from_wav(data, size); }
    ~PCMBuffer() { free(); }
    // no copying
    PCMBuffer(const PCMBuffer&) = delete;
    PCMBuffer& operator=(const PCMBuffer&) = delete;
    // only moving allowed
    PCMBuffer(PCMBuffer&&) noexcept = default;
    PCMBuffer& operator=(PCMBuffer&&) noexcept = default;
    bool from_wav(const void* data, size_t size);
    bool from_wav(const std::vector<uint8_t>& data) { return from_wav(data.data(), data.size()); }
    void free();
    operator bool() const { return _samples != nullptr && _count > 0; }
    const float* samples() const { return _samples; }
    size_t count() const { return _count; }
    unsigned int channels() const { return _channels; }
    unsigned int sample_rate() const { return _sample_rate; }
    SharedBuffer<float> share();
private:
    float* _samples = nullptr;;
    size_t _count = 0;
    unsigned int _channels = 0;
    unsigned int _sample_rate = 0;
    bool _owner = true;
};

std::vector<int16_t> buffer_f32_to_s16(const std::vector<float>& input);
std::vector<float> buffer_s16_to_f32(const std::vector<int16_t>& input);
std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<float>& data, int sample_rate, int channels = 1);
std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<int16_t>& data, int sample_rate, int channels = 1);

bool write_wav_file(const std::vector<float>& data, int sample_rate, std::string filename);

std::string base64_encode(const WavBuffer& data);
std::string base64_encode(const std::vector<uint8_t>& data);
std::string base64_encode(const uint8_t* data, size_t size);
