#include <iostream>
#include <vector>
#include <sstream>
#include <cstring>
#include <limits>
#include <chrono>
#include <memory>
#include <string>
#include <stdexcept>
#include <iostream>
#include <string>
#include <functional>

#include <cstdio>
#include <cstdarg>
#if __cplusplus < 201703L
#include <memory>
#endif

#include <onnxruntime/core/session/onnxruntime_cxx_api.h>

#include "vad.hpp"


class timestamp_t
{
public:
    int start;
    int end;

    // default + parameterized constructor
    timestamp_t(int start = -1, int end = -1)
        : start(start), end(end)
    {
    };

    // assignment operator modifies object, therefore non-const
    timestamp_t& operator=(const timestamp_t& a)
    {
        start = a.start;
        end = a.end;
        return *this;
    };

    // equality comparison. doesn't modify object. therefore const.
    bool operator==(const timestamp_t& a) const
    {
        return (start == a.start && end == a.end);
    };
    std::string c_str()
    {
        //return std::format("timestamp {:08d}, {:08d}", start, end);
        return format("{start:%08d,end:%08d}", start, end);
    };
private:

    std::string format(const char* fmt, ...)
    {
        char buf[256];

        va_list args;
        va_start(args, fmt);
        const auto r = std::vsnprintf(buf, sizeof buf, fmt, args);
        va_end(args);

        if (r < 0)
            // conversion failed
            return {};

        const size_t len = r;
        if (len < sizeof buf)
            // we fit in the buffer
            return { buf, len };

#if __cplusplus >= 201703L
        // C++17: Create a string and write to its underlying array
        std::string s(len, '\0');
        va_start(args, fmt);
        std::vsnprintf(s.data(), len + 1, fmt, args);
        va_end(args);

        return s;
#else
        // C++11 or C++14: We need to allocate scratch memory
        auto vbuf = std::unique_ptr<char[]>(new char[len + 1]);
        va_start(args, fmt);
        std::vsnprintf(vbuf.get(), len + 1, fmt, args);
        va_end(args);

        return { vbuf.get(), len };
#endif
    };
};


class VADModelImpl {
public:
    VADModelImpl(const std::string& model_path) {
        init_onnx_model(model_path);
    }
private:
    void init_engine_threads(int inter_threads, int intra_threads)
    {
        // The method should be called in each thread/proc in multi-thread/proc work
        session_options.SetIntraOpNumThreads(intra_threads);
        session_options.SetInterOpNumThreads(inter_threads);
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    };

    void init_onnx_model(const std::string& model_path)
    {
        // Init threads = 1 for 
        init_engine_threads(1, 1);
        // Load model
        session = std::make_shared<Ort::Session>(env, model_path.c_str(), session_options);
    };

    friend class VADImpl;
    // OnnxRuntime resources
    Ort::Env env;
    Ort::SessionOptions session_options;
    std::shared_ptr<Ort::Session> session = nullptr;
};


class VADImpl
{
private:
    // Ort::AllocatorWithDefaultOptions allocator;
    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeCPU);
    // VADModelImpl& model;
    std::shared_ptr<Ort::Session> session = nullptr;

private:
    void reset_states()
    {
        // Call reset before each audio start
        std::memset(_h.data(), 0.0f, _h.size() * sizeof(float));
        std::memset(_c.data(), 0.0f, _c.size() * sizeof(float));
        triggered = false;
        temp_end = 0;
        current_sample = 0;

        prev_end = next_start = 0;

        speeches.clear();
        speeches2.clear();
        current_speech = timestamp_t();
    };

    void predict(const std::vector<float> &data)
    {
        // Infer
        // Create ort tensors
        input.assign(data.begin(), data.end());
        Ort::Value input_ort = Ort::Value::CreateTensor<float>(
            memory_info, input.data(), input.size(), input_node_dims, 2);
        Ort::Value sr_ort = Ort::Value::CreateTensor<int64_t>(
            memory_info, sr.data(), sr.size(), sr_node_dims, 1);
        Ort::Value h_ort = Ort::Value::CreateTensor<float>(
            memory_info, _h.data(), _h.size(), hc_node_dims, 3);
        Ort::Value c_ort = Ort::Value::CreateTensor<float>(
            memory_info, _c.data(), _c.size(), hc_node_dims, 3);

        // Clear and add inputs
        ort_inputs.clear();
        ort_inputs.emplace_back(std::move(input_ort));
        ort_inputs.emplace_back(std::move(sr_ort));
        ort_inputs.emplace_back(std::move(h_ort));
        ort_inputs.emplace_back(std::move(c_ort));

        // Infer
        ort_outputs = session->Run(
            Ort::RunOptions{nullptr},
            input_node_names.data(), ort_inputs.data(), ort_inputs.size(),
            output_node_names.data(), output_node_names.size());

        // Output probability & update h,c recursively
        float speech_prob = ort_outputs[0].GetTensorMutableData<float>()[0];
        float *hn = ort_outputs[1].GetTensorMutableData<float>();
        std::memcpy(_h.data(), hn, size_hc * sizeof(float));
        float *cn = ort_outputs[2].GetTensorMutableData<float>();
        std::memcpy(_c.data(), cn, size_hc * sizeof(float));

        // Push forward sample index
        current_sample += window_size_samples;

        // Reset temp_end when > threshold 
        if ((speech_prob >= threshold))
        {
#ifdef __DEBUG_SPEECH_PROB___
            float speech = current_sample - window_size_samples; // minus window_size_samples to get precise start time point.
            printf("{    start: %.3f s (%.3f) %08d}\n", 1.0 * speech / sample_rate, speech_prob, current_sample- window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
            if (temp_end != 0)
            {
                temp_end = 0;
                if (next_start < prev_end)
                    next_start = current_sample - window_size_samples;
            }
            if (triggered == false)
            {
                triggered = true;

                current_speech.start = current_sample - window_size_samples;
            }
            return;
        }

        if (
            (triggered == true)
            && ((current_sample - current_speech.start) > max_speech_samples)
            ) {
            if (prev_end > 0) {
                current_speech.end = prev_end;
                speeches.push_back(current_speech);
                speeches2.emplace_back(current_speech.start, current_speech.end);
                current_speech = timestamp_t();
                
                // previously reached silence(< neg_thres) and is still not speech(< thres)
                if (next_start < prev_end)
                    triggered = false;
                else{
                    current_speech.start = next_start;
                }
                prev_end = 0;
                next_start = 0;
                temp_end = 0;

            }
            else{ 
                current_speech.end = current_sample;
                speeches.push_back(current_speech);
                speeches2.emplace_back(current_speech.start, current_speech.end);
                current_speech = timestamp_t();
                prev_end = 0;
                next_start = 0;
                temp_end = 0;
                triggered = false;
            }
            return;

        }
        if ((speech_prob >= (threshold - 0.15)) && (speech_prob < threshold))
        {
            if (triggered) {
#ifdef __DEBUG_SPEECH_PROB___
                float speech = current_sample - window_size_samples; // minus window_size_samples to get precise start time point.
                printf("{ speeking: %.3f s (%.3f) %08d}\n", 1.0 * speech / sample_rate, speech_prob, current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
            }
            else {
#ifdef __DEBUG_SPEECH_PROB___
                float speech = current_sample - window_size_samples; // minus window_size_samples to get precise start time point.
                printf("{  silence: %.3f s (%.3f) %08d}\n", 1.0 * speech / sample_rate, speech_prob, current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
            }
            return;
        }


        // 4) End 
        if ((speech_prob < (threshold - 0.15)))
        {
#ifdef __DEBUG_SPEECH_PROB___
            float speech = current_sample - window_size_samples - speech_pad_samples; // minus window_size_samples to get precise start time point.
            printf("{      end: %.3f s (%.3f) %08d}\n", 1.0 * speech / sample_rate, speech_prob, current_sample - window_size_samples);
#endif //__DEBUG_SPEECH_PROB___
            if (triggered == true)
            {
                if (temp_end == 0)
                {
                    temp_end = current_sample;
                }
                if (current_sample - temp_end > min_silence_samples_at_max_speech)
                    prev_end = temp_end;
                // a. silence < min_slience_samples, continue speaking 
                if ((current_sample - temp_end) < min_silence_samples)
                {

                }
                // b. silence >= min_slience_samples, end speaking
                else
                {
                    current_speech.end = temp_end;
                    if (current_speech.end - current_speech.start > min_speech_samples)
                    {
                        speeches.push_back(current_speech);
                        speeches2.emplace_back(current_speech.start, current_speech.end);
                        current_speech = timestamp_t();
                        prev_end = 0;
                        next_start = 0;
                        temp_end = 0;
                        triggered = false;
                    }
                }
            }
            else {
                // may first windows see end state.
            }
            return;
        }
    };
public:
    void reset() { reset_states(); }
    void process(const std::vector<float>& input_wav)
    {
        reset_states();

        audio_length_samples = input_wav.size();

        size_t output_speeches = 0;
        for (int j = 0; j < audio_length_samples; j += window_size_samples)
        {
            if (j + window_size_samples > audio_length_samples)
                break;
            std::vector<float> r{ &input_wav[0] + j, &input_wav[0] + j + window_size_samples };
            predict(r);

            for (int i = output_speeches; i < speeches.size(); i++) {
                auto& s = speeches[i];
                std::cout << s.c_str() << " duration: " << (s.end - s.start) / sample_rate << std::endl;
            }
            output_speeches = speeches.size();
        }

        if (current_speech.start >= 0) {
            current_speech.end = audio_length_samples;
            speeches.push_back(current_speech);
            speeches2.emplace_back(current_speech.start, current_speech.end);
            current_speech = timestamp_t();
            prev_end = 0;
            next_start = 0;
            temp_end = 0;
            triggered = false;

            std::cout << speeches.back().c_str() << std::endl;
        }

        std::cout << "number of speeches detected: "  << speeches.size() << std::endl;
    }

    void process(const std::vector<float>& input_wav, std::function<bool(const std::vector<speech_range>&, int)> callback)
    {
        reset_states();

        audio_length_samples = input_wav.size();

        size_t output_speeches = 0;
        for (int j = 0; j < audio_length_samples; j += window_size_samples)
        {
            if (j + window_size_samples > audio_length_samples)
                break;
            std::vector<float> r{ &input_wav[0] + j, &input_wav[0] + j + window_size_samples };
            predict(r);

            if (output_speeches < speeches.size()) {
                // for (int i = output_speeches; i < speeches.size(); i++) {
                //     auto& s = speeches[i];
                //     std::cout << s.c_str() << " duration: " << (s.end - s.start) / sample_rate << std::endl;
                // }
                if (!callback(speeches2, output_speeches))
                    return;
                output_speeches = speeches.size();
            }
        }

        if (current_speech.start >= 0) {
            current_speech.end = audio_length_samples;
            speeches.push_back(current_speech);
            speeches2.emplace_back(current_speech.start, current_speech.end);
            current_speech = timestamp_t();
            prev_end = 0;
            next_start = 0;
            temp_end = 0;
            triggered = false;

            if (output_speeches < speeches.size()) {
                if (!callback(speeches2, output_speeches))
                    return;
                // std::cout << speeches.back().c_str() << std::endl;
            }
        }

        // std::cout << "number of speeches detected: "  << speeches.size() << std::endl;
    }

    void process(const std::vector<float>& input_wav, std::vector<float>& output_wav)
    {
        process(input_wav);
        collect_chunks(input_wav, output_wav);
    }

    void collect_chunks(const std::vector<float>& input_wav, std::vector<float>& output_wav)
    {
        output_wav.clear();
        for (int i = 0; i < speeches.size(); i++) {
#ifdef __DEBUG_SPEECH_PROB___
            std::cout << speeches[i].c_str() << std::endl;
#endif //#ifdef __DEBUG_SPEECH_PROB___
            std::vector<float> slice(&input_wav[speeches[i].start], &input_wav[speeches[i].end]);
            output_wav.insert(output_wav.end(),slice.begin(),slice.end());
        }
    };

    const std::vector<timestamp_t> get_speech_timestamps() const
    {
        return speeches;
    }

    void drop_chunks(const std::vector<float>& input_wav, std::vector<float>& output_wav)
    {
        output_wav.clear();
        int current_start = 0;
        for (int i = 0; i < speeches.size(); i++) {

            std::vector<float> slice(&input_wav[current_start],&input_wav[speeches[i].start]);
            output_wav.insert(output_wav.end(), slice.begin(), slice.end());
            current_start = speeches[i].end;
        }

        std::vector<float> slice(&input_wav[current_start], &input_wav[input_wav.size()]);
        output_wav.insert(output_wav.end(), slice.begin(), slice.end());
    };

private:
    // model config
    int64_t window_size_samples;  // Assign when init, support 256 512 768 for 8k; 512 1024 1536 for 16k.
    int sample_rate;  //Assign when init support 16000 or 8000      
    int sr_per_ms;   // Assign when init, support 8 or 16
    float threshold; 
    int min_silence_samples; // sr_per_ms * #ms
    int min_silence_samples_at_max_speech; // sr_per_ms * #98
    int min_speech_samples; // sr_per_ms * #ms
    float max_speech_samples;
    int speech_pad_samples; // usually a 
    int audio_length_samples;

    // model states
    bool triggered = false;
    unsigned int temp_end = 0;
    unsigned int current_sample = 0;    
    // MAX 4294967295 samples / 8sample per ms / 1000 / 60 = 8947 minutes  
    int prev_end;
    int next_start = 0;

    //Output timestamp
    std::vector<timestamp_t> speeches;
    std::vector<speech_range> speeches2;
    timestamp_t current_speech;


    // Onnx model
    // Inputs
    std::vector<Ort::Value> ort_inputs;
    
    std::vector<const char *> input_node_names = {"input", "sr", "h", "c"};
    std::vector<float> input;
    std::vector<int64_t> sr;
    unsigned int size_hc = 2 * 1 * 64; // It's FIXED.
    std::vector<float> _h;
    std::vector<float> _c;

    int64_t input_node_dims[2] = {}; 
    const int64_t sr_node_dims[1] = {1};
    const int64_t hc_node_dims[3] = {2, 1, 64};

    // Outputs
    std::vector<Ort::Value> ort_outputs;
    std::vector<const char *> output_node_names = {"output", "hn", "cn"};

public:
    // Construction
    VADImpl(VADModelImpl& model/*const std::string ModelPath*/,
        int Sample_rate = 16000, int windows_frame_size = 64,
        float Threshold = 0.5, int min_silence_duration_ms = 0,
        int speech_pad_ms = 64, int min_speech_duration_ms = 64,
        float max_speech_duration_s = std::numeric_limits<float>::infinity()) : session(model.session) // model(model)
    {
        // init_onnx_model(ModelPath);
        threshold = Threshold;
        sample_rate = Sample_rate;
        sr_per_ms = sample_rate / 1000;

        window_size_samples = windows_frame_size * sr_per_ms;

        min_speech_samples = sr_per_ms * min_speech_duration_ms;
        speech_pad_samples = sr_per_ms * speech_pad_ms;

        max_speech_samples = (
            sample_rate * max_speech_duration_s
            - window_size_samples
            - 2 * speech_pad_samples
            );

        min_silence_samples = sr_per_ms * min_silence_duration_ms;
        min_silence_samples_at_max_speech = sr_per_ms * 98;

        input.resize(window_size_samples);
        input_node_dims[0] = 1;
        input_node_dims[1] = window_size_samples;

        _h.resize(size_hc);
        _c.resize(size_hc);
        sr.resize(1);
        sr[0] = sample_rate;
    }

private:
    struct {
        // const std::vector<float>* input_wav;
        const float* samples;
        int j;
        size_t output_speeches;
    } state;

public:
    void start(const std::vector<float>& samples) {
        start(samples.data(), samples.size());
    }

    void start(const float *samples, size_t count) {
        reset_states();
        state.samples = samples;
        audio_length_samples = count;
        state.j = 0;
        state.output_speeches = 0;
    }

    bool next() {

        if (state.j >= audio_length_samples)
            return false;
        if (state.j + window_size_samples > audio_length_samples)
            return false;

        auto samples = state.samples;
        size_t prev_size = speeches2.size();

        while (speeches2.size() == prev_size) {

            auto j = state.j;

            if (j + window_size_samples > audio_length_samples)
                break;

            std::vector<float> r{ &samples[0] + j, &samples[0] + j + window_size_samples };
            predict(r);

            state.j += window_size_samples;
        }

        if (state.j + window_size_samples > audio_length_samples || state.j >= audio_length_samples) {
            if (current_speech.start >= 0) {
                current_speech.end = audio_length_samples;
                speeches.push_back(current_speech);
                speeches2.emplace_back(current_speech.start, current_speech.end);
                current_speech = timestamp_t();
                prev_end = 0;
                next_start = 0;
                temp_end = 0;
                triggered = false;
            }
            return speeches2.size() != prev_size;
        }

        return true;
    }

    class Iterator {
    public:
        Iterator(VADImpl& vad, int speech) : vad(vad), speech(speech) {}

        speech_range& operator*() const { return vad.speeches2[speech]; }

        Iterator& operator++() {
            if (speech >= 0)
                ++speech;
            if (speech >= vad.speeches2.size()) {
                // while (speech >= vad.speeches2.size()) {
                if (!vad.next())
                    speech = -1;
                // }
            }
            return *this;
        }

        Iterator operator++(int) {
            Iterator temp = *this;
            ++(*this);
            return temp;
        }

        bool operator==(const Iterator& other) const {
            return speech == other.speech;
        }

        bool operator!=(const Iterator& other) const {
            return !(*this == other);
        }

    private:
        VADImpl& vad;
        int speech;
    };

    Iterator begin() { return Iterator(*this, size() > 0 ? 0 : -1); }
    Iterator end() { return Iterator(*this, -1); }

    const std::vector<speech_range>& ranges() const { return speeches2; }
    size_t size() const { return speeches2.size(); }
    const speech_range& operator[](size_t index) const { return speeches2[index]; }
};



VADModel::VADModel() {}

// VADModel::VADModel(const std::string& model_path) : impl(std::make_unique<VADModelImpl>(model_path)) {}
VADModel::VADModel(const std::string& model_path) : impl(nullptr)
{
    if (model_path.empty())
        return;
    try {
        impl = std::make_unique<VADModelImpl>(model_path);
    } catch (const std::exception& e) {
        what = e.what();
    }
}

VADModel::~VADModel() {}

VADModel::operator bool() const { return (bool)impl; }


class VAD::IteratorImpl : public VADImpl::Iterator {
public:
    IteratorImpl(VADImpl::Iterator&& base) : VADImpl::Iterator(base) {}
};

// VAD::VAD(const std::string& model_path, VADConfig config)
VAD::VAD(VADModel& model, VADConfig config)
    : impl(std::make_unique<VADImpl>(*model.impl.get() /*model_path*/,
                config.sample_rate,
                config.windows_frame_size_ms,
                config.threshold,
                config.min_silence_duration_ms,
                config.speech_pad_ms,
                config.min_speech_duration_ms,
                config.max_speech_duration_s)), _sample_rate(config.sample_rate) {
}

VAD::~VAD() {}

void VAD::reset() {
    impl->reset();
}

size_t VAD::size() const {
    return impl->size();
}

void VAD::start(const float *samples, size_t count) {
    impl->start(samples, count);
    impl->next();
}

VAD::Iterator VAD::begin() { return Iterator(new IteratorImpl(impl->begin())); }
VAD::Iterator VAD::end() { return Iterator(new IteratorImpl(impl->end())); }

VAD::Iterator::~Iterator() { delete impl; }

VAD::Iterator& VAD::Iterator::operator++() {
    (*impl)++;
    return *this;
}

bool VAD::Iterator::operator==(const VAD::Iterator& other) const {
    return *impl == *other.impl;
}

speech_range& VAD::Iterator::operator*() const {
    return *(*impl);
}

void test_vad_range(const std::string& path, const std::vector<float>& data, VADConfig config) {
    VADModelImpl model(path);
    VADImpl vad(model,
                config.sample_rate,
                config.windows_frame_size_ms,
                config.threshold,
                config.min_silence_duration_ms,
                config.speech_pad_ms,
                config.min_speech_duration_ms,
                config.max_speech_duration_s);

    // vad.process(data, [&](const std::vector<speech_range>& speeches, int i) -> bool {
    //     for (; i < speeches.size(); i++) {
    //         auto& s = speeches[i];
    //         std::cout << "speech from " << s.start << " till " << s.end << ", duration: " << (s.end - s.start) / config.sample_rate << std::endl;
    //     }
    //     // std::cout << "speeches from " << i << std::endl;
    //     return true;
    // });
    // return;

    // vad.start(data);
    // size_t i = 0;
    // while (vad.next()) {
    //     for (; i < vad.size(); i++) {
    //         auto& s = vad[i];
    //         std::cout << "speech from " << s.start << " till " << s.end << ", duration: " << (s.end - s.start) / config.sample_rate << std::endl;
    //     }
    // }

    // vad.start(data);
    // vad.next();
    // auto it = vad.begin();
    // auto end = vad.end();
    // for (; it != end; ++it) {
    //     auto& s = *it;
    //     std::cout << "speech from " << s.start << " till " << s.end << ", duration: " << (s.end - s.start) / config.sample_rate << std::endl;
    // }

    vad.start(data);
    vad.next();
    for (auto& s : vad) {
        std::cout << "speech from " << s.start << " till " << s.end << ", duration: " << (s.end - s.start) / config.sample_rate << std::endl;
    }
    std::cout << "number of speeches detected: "  << vad.size() << std::endl;
}
