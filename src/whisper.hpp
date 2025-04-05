#pragma once

#include <string>

#include <nlohmann/json.hpp>
#include <msgpack/msgpack.hpp>

#include "vad/vad.hpp"
#include "util.hpp"
#include "optional-ref.hpp"

#if USE_CUDA
bool load_ggml_cuda_backend_symbols(void *handle);
#endif

#define TO_STRING(name)   #name
#define ITEM(name)        item(TO_STRING(name), name)


class WhisperModelImpl;
class WhisperImpl;
class Whisper;

struct WhisperToken {
    int32_t id;
    int32_t tid;        // forced timestamp token id

    float p;           // probability of the token
    float plog;        // log probability of the token
    float pt;          // probability of the timestamp token
    float ptsum;       // sum of probabilities of all timestamp tokens

    // token-level timestamp data
    // do not use if you haven't computed token-level timestamps
    int64_t t0;        // start time of the token
    int64_t t1;        //   end time of the token

    // [EXPERIMENTAL] Token-level timestamps with DTW
    // do not use if you haven't computed token-level timestamps with dtw
    // Roughly corresponds to the moment in audio in which the token was output
    int64_t t_dtw;

    float vlen;        // voice length of the token

    bool special = false;

    std::string text;

    int n_merged = 0;

    template <class T>
    void pack(T& pack) {
        pack.as_map(
                pack.ITEM(id),
                pack.ITEM(tid),

                pack.ITEM(p),
                pack.ITEM(plog),
                pack.ITEM(pt),
                pack.ITEM(ptsum),

                // pack.ITEM(t0),
                // pack.ITEM(t1),
                pack.item("start", t0),
                pack.item("end", t1),

                pack.ITEM(t_dtw),

                pack.ITEM(vlen),

                pack.ITEM(special),

                pack.ITEM(text)
            );
    }

    nlohmann::json to_json() const { return nlohmann::json::from_msgpack(msgpack::pack(const_cast<WhisperToken&>(*this))); }

private:
    void operator+=(const WhisperToken& other) {
        // t0
        t1 = other.t1;
        // t_dtw - first, average (middle) or last?

        int n =  n_merged + 1;
        p = p*n + other.p;
        plog = plog*n + other.plog;
        pt = pt*n + other.pt;
        ptsum = ptsum*n + other.ptsum;

        p /= n;
        plog /= n;
        pt /= n;
        ptsum /= n;

        vlen += other.vlen;
        text += other.text;

        n_merged++;
    }

    friend class WhisperImpl;
};

struct WhisperSegment {
    int64_t t0;
    int64_t t1;
    bool turn_next;
    std::string text;
    std::vector<WhisperToken> tokens;
    std::string lang;

    template <class T>
    void pack(T& pack) {
        pack.as_map(
                // pack.ITEM(t0),
                // pack.ITEM(t1),
                pack.item("start", t0),
                pack.item("end", t1),
                pack.ITEM(text),

                pack.ITEM(turn_next),
                pack.ITEM(tokens),

                pack.ITEM(lang)
            );
    }

    nlohmann::json to_json() const { return nlohmann::json::from_msgpack(msgpack::pack(const_cast<WhisperSegment&>(*this))); }
};

typedef std::vector<WhisperSegment> WhisperSegments;

struct WhisperResult {
    std::string lang;
    WhisperSegments segments;

    template <class T>
    void pack(T& pack) {
        pack.as_map(
                pack.ITEM(lang),
                pack.ITEM(segments)
            );
    }

    nlohmann::json to_json() const { return nlohmann::json::from_msgpack(msgpack::pack(const_cast<WhisperResult&>(*this))); }
};


class WhisperReturnValue {
public:
    WhisperReturnValue(int exit_code) : exit_code(exit_code) {}
    WhisperReturnValue(const WhisperReturnValue& other) : exit_code(other.exit_code) {}
    // WhisperReturnValue(WhisperReturnValue&& other) : exit_code(other.exit_code) {}
    const int exit_code;
    operator bool() const { return exit_code == 0; }
    bool aborted() const { return exit_code == -6; }
};

class WhisperModel {
public:
    WhisperModel();
    WhisperModel(const std::string& model, const std::string& dtw = "", bool use_gpu = true, int gpu_device = 0);

    WhisperModel(const WhisperModel&) = delete;
    WhisperModel& operator=(const WhisperModel&) = delete;

    WhisperModel(WhisperModel&&) noexcept = default;
    WhisperModel& operator=(WhisperModel&&) noexcept = default;

    ~WhisperModel();

    operator bool() const;

    bool init(const std::string& model, const std::string& dtw = "", bool use_gpu = true, int gpu_device = 0);

private:
    friend class Whisper;
    friend class WhisperQueueProcessor;
    std::unique_ptr<WhisperModelImpl> impl;
};

struct WhisperJobConfig {
    std::string lang = "auto";
    bool translate = false;
    bool reset = false;
    bool use_vad = false;
    int n_threads = 0;
    int offset_ms = 0;          // start offset in ms
    int duration_ms = 0;        // audio duration to process in ms
    int reset_min_nospeech_ms = 10000;  // 10s
    VADConfig vad_config = VADConfig();
};

class Whisper {
public:
    Whisper();
    Whisper(WhisperModel& model);
    Whisper(WhisperModel& model, VADModel& vad_model);

    void setModel(WhisperModel& model);
    void setVADModel(VADModel& model);

    Whisper(const Whisper&) = delete;
    Whisper& operator=(const Whisper&) = delete;

    Whisper(Whisper&&) noexcept = default;
    Whisper& operator=(Whisper&&) noexcept = default;

    ~Whisper();

    // bool operator()(const void* wav_data, size_t wav_size, const std::string& lang = "auto", bool reset = false, bool use_vad = false, VADConfig vad_config = VADConfig());
    // bool operator()(const float* samples, size_t count, const std::string& lang = "auto", bool reset = false, bool use_vad = false, VADConfig vad_config = VADConfig());
    WhisperReturnValue operator()(const void* wav_data, size_t wav_size, const WhisperJobConfig& config = WhisperJobConfig());
    WhisperReturnValue operator()(const float* samples, size_t count, const WhisperJobConfig& config = WhisperJobConfig());

    void abort();
    size_t numberOfSegments() const;
    std::string detectedLanguage() const;
    void getSegment(WhisperSegment& segment, int i_segment) const;
    void getSegments(WhisperSegments& segments, int first = 0, int last = -1) const;
    WhisperSegments getSegments(int first = 0, int last = -1) const;
    WhisperResult getResult(int first_segment = 0, int last_segment = -1) const;

    nlohmann::json segments_to_json();

private:
    std::unique_ptr<WhisperImpl> impl;
};




typedef std::string WhisperJobID;

struct WhisperJob {
    SharedBuffer<float> samples;
    SharedBuffer<void> wav;
    WhisperJobConfig config = WhisperJobConfig();
    WhisperJobID id;
};

enum class WhisperJobStatus {
    Waiting,
    Running,
    Done,
    Failed,
    Aborted,
    Stored
};

class WhisperQueueProcessorImpl;

class WhisperQueueProcessor {
public:

    WhisperQueueProcessor(WhisperModel& model, int max_instances = 2);
    WhisperQueueProcessor(WhisperModel& model, VADModel& vad_model, int max_instances = 2);
    ~WhisperQueueProcessor();

    Whisper newWhisperInstance();

    void setVADModel(VADModel& vad_model);

    typedef int instance_id;
    typedef int job_id;

    std::optional<WhisperJobStatus> getJobStatus(WhisperJobID id);

    WhisperJobID add(WhisperJob&& job);
    std::optional<WhisperJobStatus> wait(WhisperJobID id, const std::function<bool(const WhisperSegments&, size_t)>& callback);
    optional_ref<const WhisperSegments> getResults(WhisperJobID id);
    bool abort(WhisperJobID id);

private:
    std::unique_ptr<WhisperQueueProcessorImpl> impl;
    WhisperModel* model = nullptr;
    VADModel* vad_model = nullptr;
};
