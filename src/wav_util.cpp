#include "wav_util.hpp"

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>


void WavBuffer::free() {
    if(_data)
        drwav_free(_data, nullptr);
    _data = nullptr;
    _size = 0;
}

bool WavBuffer::from_pcm(const float* samples, size_t count, int sample_rate, int channels) {

    free();

    drwav wav;
    drwav_data_format format;

    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = channels;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    if(!drwav_init_memory_write_sequential_pcm_frames(&wav, &_data, &_size, &format, count, NULL))
        return false;

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, count, samples);

    drwav_uninit(&wav);

    return true;
}

std::vector<int16_t> buffer_f32_to_s16(const std::vector<float>& input) {
    std::vector<int16_t> output(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        float x = input[i];
        x = (x < -1.0f) ? -1.0f : ((x > 1.0f) ? 1.0f : x);  // clip
        output[i] = (int16_t)(x * 32767.0f);  // scale to 16-bit range
    }
    return output;
}

std::vector<float> buffer_s16_to_f32(const std::vector<int16_t>& input) {
    std::vector<float> output(input.size());
    for (size_t i = 0; i < input.size(); i++) {
        output[i] = (float)(input[i]) / 32767.0f;
    }
    return output;
}

std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<float>& data, int sample_rate, int channels) {

    drwav wav;
    drwav_data_format format;

    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = channels;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    size_t outputSize;
    void* outputData;

    if(!drwav_init_memory_write_sequential_pcm_frames(&wav, &outputData, &outputSize, &format, data.size(), NULL))
        return std::unique_ptr<WavBuffer>(nullptr);

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, data.size(), data.data());

    drwav_uninit(&wav);

    return std::unique_ptr<WavBuffer>(new WavBuffer(outputData, outputSize));
}

std::unique_ptr<WavBuffer> make_wav_buffer(const std::vector<int16_t>& data, int sample_rate, int channels) {

    drwav wav;
    drwav_data_format format;

    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = channels;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 16;

    size_t outputSize;
    void* outputData;

    if(!drwav_init_memory_write_sequential_pcm_frames(&wav, &outputData, &outputSize, &format, data.size(), NULL))
        return std::unique_ptr<WavBuffer>(nullptr);

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, data.size(), data.data());

    drwav_uninit(&wav);

    return std::unique_ptr<WavBuffer>(new WavBuffer(outputData, outputSize));
}

bool write_wav_file(const std::vector<float>& data, int sample_rate, std::string filename) {

    drwav wav;
    drwav_data_format format;

    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = sample_rate;
    format.bitsPerSample = 32;

    drwav_init_file_write(&wav, filename.c_str(), &format, NULL);

    drwav_uint64 framesWritten = drwav_write_pcm_frames(&wav, data.size(), data.data());

    drwav_uninit(&wav);

    return true;
}

bool PCMBuffer::from_wav(const void* data, size_t size) {

    free();

    drwav_uint64 total_frame_count = 0;

    _samples = drwav_open_memory_and_read_pcm_frames_f32(data, size, &_channels, &_sample_rate, &total_frame_count, nullptr);

    if(!_samples)
        return false;

    _count = (int)total_frame_count;

    return true;
}

void PCMBuffer::free() {
    if (_samples && _owner)
        drwav_free(_samples, nullptr);
    _owner = false;
    _samples = nullptr;
    _count = 0;
    _channels = 0;
    _sample_rate = 0;
}

SharedBuffer<float> PCMBuffer::share() {
    if (_samples && _owner) {
        return std::move(SharedBuffer(_samples, _count, &drwav_free));
        _owner = false;
    }
    return std::move(SharedBuffer(_samples, _count, false));
}


std::string base64_encode(const WavBuffer& data) {
    return base64_encode((const uint8_t*)data.data(), data.size());
}

std::string base64_encode(const std::vector<uint8_t>& data) {
    return base64_encode(data.data(), data.size());
}

std::string base64_encode(const uint8_t* data, size_t size) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    std::string output;
    output.reserve(4 * ((size + 2) / 3));
    size_t i = 0;
    size_t j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (size_t pos = 0; pos < size; ++pos) {
        char_array_3[i++] = data[pos];
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                output += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            output += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            output += '=';
    }

    return output;
}

