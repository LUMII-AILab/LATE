#include <string>
#include <iostream>
#include <memory>
#include <queue>
#include <mutex>
#include <map>
#include <thread>
#include <functional>
#include <optional>
#include <iomanip>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>

#include <whisper.h>
#include <nlohmann/json.hpp>

#include "whisper.hpp"
#include "wav_util.hpp"
#include "vad/vad.hpp"
#include "random-generator.hpp"
#include "callback-manager.hpp"
#include "log.hpp"
#include "optional-ref.hpp"
#include "ref-keeper.hpp"

using json = nlohmann::json;


#if USE_CUDA
#include <dlfcn.h>
#include <ggml-cuda.h>
ggml_backend_reg_t (*_ggml_backend_cuda_reg)(void);
bool load_ggml_cuda_backend_symbols(void *handle) {
    _ggml_backend_cuda_reg = (decltype(_ggml_backend_cuda_reg))dlsym(handle, "ggml_backend_cuda_reg");
    return _ggml_backend_cuda_reg != nullptr;
}
__attribute__((visibility("hidden"))) __attribute__((weak)) ggml_backend_reg_t ggml_backend_cuda_reg(void) {
    if (_ggml_backend_cuda_reg) {
        return _ggml_backend_cuda_reg();
    }
    return nullptr;
}
#else
#include <ggml-cuda.h>
__attribute__((visibility("hidden"))) __attribute__((weak)) ggml_backend_reg_t ggml_backend_cuda_reg(void) {
    return nullptr;
}
#endif

class WhisperImpl;

class WhisperModelImpl {
public:
    WhisperModelImpl() {}
    WhisperModelImpl(const std::string& model, const std::string& dtw = "", bool use_gpu = true, int gpu_device = 0) { init(model, dtw, use_gpu, gpu_device); }
    ~WhisperModelImpl() { free(); }

    operator bool() const { return ctx != nullptr; }

    bool init(const std::string& model, const std::string& dtw = "", bool use_gpu = true, int gpu_device = 0) {
        if (model.empty())
            return false;
        struct whisper_context_params cparams = whisper_context_default_params();
        cparams.use_gpu = use_gpu;
        cparams.flash_attn = true;
        cparams.gpu_device = gpu_device;
        if (!dtw.empty()) {
            cparams.dtw_aheads_preset = WHISPER_AHEADS_NONE;

            // TODO: what about dtw_n_top ?
            // dtw_aheads_preset = WHISPER_AHEADS_N_TOP_MOST,  // All heads from the N-top-most text-layers
            // int dtw_n_top;
            // struct whisper_aheads dtw_aheads;

            if (dtw == "tiny")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_TINY;
            else if (dtw == "tiny.en")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_TINY_EN;
            else if (dtw == "base")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_BASE;
            else if (dtw == "base.en")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_BASE_EN;
            else if (dtw == "small")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_SMALL;
            else if (dtw == "small.en")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_SMALL_EN;
            else if (dtw == "medium")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_MEDIUM;
            else if (dtw == "medium.en")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_MEDIUM_EN;
            else if (dtw == "large.v1")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V1;
            else if (dtw == "large.v2")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V2;
            else if (dtw == "large.v3")
                cparams.dtw_aheads_preset = WHISPER_AHEADS_LARGE_V3;

            // TODO: log error if dtw is unknown
            // if (cparams.dtw_aheads_preset == WHISPER_AHEADS_NONE) {
            //     fprintf(stderr, "error: unknown DTW preset '%s'\n", params.dtw.c_str());
            //     return 3;
            // }

            cparams.dtw_token_timestamps = cparams.dtw_aheads_preset != WHISPER_AHEADS_NONE;
        }
        dtw_enabled = cparams.dtw_token_timestamps;
        ctx = whisper_init_from_file_with_params_no_state(model.c_str(), cparams);
        if (ctx != nullptr)
            eot = whisper_token_eot(ctx);
        return ctx != nullptr;
    }

    void free() {
        if (ctx != nullptr)
            whisper_free(ctx);
        ctx = nullptr;
    }

    static std::string system_info() { return whisper_print_system_info(); }
private:
    friend class WhisperImpl;
    // struct whisper_context * context() { return ctx; }
    struct whisper_context *ctx = nullptr;
    bool dtw_enabled = false;
    whisper_token eot;
};

struct whisper_state_deleter {
    void operator()(struct whisper_state *state) const {
        if (state != nullptr) {
            whisper_free_state(state);
        }
    }
};


class WhisperImpl {
    inline static logger log = new_logger("whisper");
    WhisperModelImpl& model;
    VADModel vad_model;
    // struct whisper_state *state = nullptr;
    std::shared_ptr<struct whisper_state> state;
    CStyleCallbackManager<void, struct whisper_context *, struct whisper_state *, int> newSegmentCallbacks;
    CStyleCallbackManager<bool> abortCallbacks;
    WhisperSegments segments;

public:
    WhisperImpl(WhisperModelImpl& model) : model(model) {}
    WhisperImpl(WhisperModelImpl& model, VADModel& vad_model) : model(model), vad_model(vad_model) {}
    ~WhisperImpl() { free(); }

    void free() {
        // if (state != nullptr)
        //     whisper_free_state(state);
        state = nullptr;
        // state.reset(nullptr);
    }

    void setVADModel(VADModel& vad_model) { this->vad_model = vad_model; }

    // TODO: forward callback
    WhisperReturnValue operator()(const void* wav_data, size_t wav_size, const WhisperJobConfig& config = WhisperJobConfig()) {
        PCMBuffer buffer(wav_data, wav_size);

        if (buffer.count() == 0)
            return -100;

        // return operator()(buffer.samples(), buffer.count(), lang, reset, use_vad, vad_config);
        return operator()(buffer.samples(), buffer.count(), config);
    }

    WhisperReturnValue operator()(const float* samples, size_t count, const WhisperJobConfig& config = WhisperJobConfig(), std::function<bool(WhisperSegments&&)> callback = nullptr) {

        // if (use_vad && !vad_model)
        //     use_vad = false;  // TODO: should we fail here, or continue silently? or issue a warning?
        log.trace("whisper config = {}", (size_t)&config);
        log.debug("whisper config.lang: {}", config.lang);

        int64_t offset_ms = 0;  // this will be filled (for new segments callback to adjust timestamps) when VAD is used; does it relate to params/config.offset_ms ?

        bool use_vad = config.use_vad && vad_model;

        log.debug("whisper use VAD: {}", use_vad);

        std::string lang = config.lang;

        log.debug("whisper lang: {}", lang);

        struct whisper_context * ctx = model.ctx;
        struct whisper_state *state = this->state.get();

        // if (config.reset)
        //     free();
        if (!this->state) {
            this->state = std::shared_ptr<struct whisper_state>(whisper_init_state(ctx), whisper_state_deleter());
            state = this->state.get();
        }
        // if (!state)
        //     state = whisper_init_state(ctx);


        enum whisper_sampling_strategy strategy = WHISPER_SAMPLING_GREEDY;
        // enum whisper_sampling_strategy strategy = WHISPER_SAMPLING_BEAM_SEARCH:

        struct whisper_full_params params = whisper_full_default_params(strategy);

        if (config.n_threads > 0)
            params.n_threads = config.n_threads;
        // int n_max_text_ctx;                    // max tokens to use from past text as prompt for the decoder
        params.offset_ms = config.offset_ms;      // start offset in ms
        params.duration_ms = config.duration_ms;  // audio duration to process in ms

        params.translate = config.translate;
        params.no_context = config.reset; // do not use past transcription (if any) as initial prompt for the decoder
        // bool no_timestamps;            // do not generate timestamps
        // bool single_segment;           // force single segment output (useful for streaming)

        // TODO: in privacy mode this must be turned off
        params.print_special = false;     // print special tokens (e.g. <SOT>, <EOT>, <BEG>, etc.)
        params.print_progress = true;     // print progress information
        params.print_realtime = true;     // print results from within whisper.cpp (avoid it, use callback instead)
        params.print_timestamps = true;   // print timestamps for each text segment when printing realtime

        params.token_timestamps = true;   // enable token-level timestamps
        // float thold_pt;                // timestamp token probability threshold (~0.01)
        // float thold_ptsum;             // timestamp token sum probability threshold (~0.01)
        // int   max_len;                 // max segment length in characters
        // bool  split_on_word;           // split on word rather than on token (when used with max_len)
        // int   max_tokens;              // max tokens per segment (0 = no limit)

        // [EXPERIMENTAL] speed-up techniques
        // note: these can significantly reduce the quality of the output
        // bool speed_up;          // speed-up the audio by 2x using Phase Vocoder
        // bool debug_mode;        // enable debug_mode provides extra info (eg. Dump log_mel)
        // int  audio_ctx;         // overwrite the audio context size (0 = use default)

        // [EXPERIMENTAL] [TDRZ] tinydiarize
        params.tdrz_enable = true;        // enable tinydiarize speaker turn detection

        // A regular expression that matches tokens to suppress
        // const char * suppress_regex;

        // tokens to provide to the whisper decoder as initial prompt
        // these are prepended to any existing text context from a previous call
        // use whisper_tokenize() to convert text to tokens
        // maximum of whisper_n_text_ctx()/2 tokens are used (typically 224)
        // const char * initial_prompt;
        // const whisper_token * prompt_tokens;
        // int prompt_n_tokens;

        // for auto-detection, set to nullptr, "" or "auto"
        params.language = lang.c_str();
        // bool detect_language;    // *ONLY* detect language (detect and exit)

        // common decoding parameters:
        params.suppress_blank = false;
        // params.suppress_non_speech_tokens = true;  // TODO: not in lates whisper.cpp ?

        // float temperature;      // initial decoding temperature, ref: https://ai.stackexchange.com/a/32478
        // float max_initial_ts;   // ref: https://github.com/openai/whisper/blob/f82bc59f5ea234d4b97fb2860842ed38519f7e65/whisper/decoding.py#L97
        // float length_penalty;   // ref: https://github.com/openai/whisper/blob/f82bc59f5ea234d4b97fb2860842ed38519f7e65/whisper/transcribe.py#L267

        // fallback parameters
        // ref: https://github.com/openai/whisper/blob/f82bc59f5ea234d4b97fb2860842ed38519f7e65/whisper/transcribe.py#L274-L278
        // float temperature_inc;
        // float entropy_thold;    // similar to OpenAI's "compression_ratio_threshold"
        // float logprob_thold;
        // float no_speech_thold;  // TODO: not implemented


        // // called for every newly generated text segment
        // whisper_new_segment_callback new_segment_callback;
        // void * new_segment_callback_user_data;
        // typedef void (*whisper_new_segment_callback)(struct whisper_context * ctx, struct whisper_state * state, int n_new, void * user_data);
        //
        // // called on each progress update
        // whisper_progress_callback progress_callback;
        // void * progress_callback_user_data;
        // typedef void (*whisper_progress_callback)(struct whisper_context * ctx, struct whisper_state * state, int progress, void * user_data);
        //
        // // called each time before the encoder starts
        // whisper_encoder_begin_callback encoder_begin_callback;
        // void * encoder_begin_callback_user_data;
        // typedef bool (*whisper_encoder_begin_callback)(struct whisper_context * ctx, struct whisper_state * state, void * user_data);
        //
        // // called each time before ggml computation starts
        // ggml_abort_callback abort_callback;
        // void * abort_callback_user_data;
        // typedef bool (*ggml_abort_callback)(void * data);
        //
        // // called by each decoder to filter obtained logits
        // whisper_logits_filter_callback logits_filter_callback;
        // void * logits_filter_callback_user_data;

        // const whisper_grammar_element ** grammar_rules;
        // size_t                           n_grammar_rules;
        // size_t                           i_start_rule;
        // float                            grammar_penalty;

        // typedef void (*whisper_new_segment_callback)(struct whisper_context * ctx, struct whisper_state * state, int n_new, void * user_data);
        // params.new_segment_callback_user_data = &wsctx;
        // params.new_segment_callback = write_segments;
        // if (true) {
        if (callback) {
            params.new_segment_callback = newSegmentCallbacks.callback;
            params.new_segment_callback_user_data = newSegmentCallbacks([&](struct whisper_context * ctx, struct whisper_state * state, int n_new) {
                WhisperSegments segments;
                log.trace("got {} new segments", n_new);
                getSegments(segments, -n_new, -1, offset_ms);
                // std::cout << "GOT OFFSET " << offset_ms << std::endl;
                // if (offset_ms > 0)
                //     offsetSegments(segments, offset_ms);
                for (auto& segment : segments)
                    log.trace("new segment: {}", segment.text);

                if (callback && !callback(std::move(segments))) {
                    log.trace("abort by new segment callback");
                    do_abort = true;
                }
            });
        } else {
            params.new_segment_callback = nullptr;
            params.new_segment_callback_user_data = nullptr;
        }

        // // called each time before ggml computation starts
        // ggml_abort_callback abort_callback;
        // void * abort_callback_user_data;
        // typedef bool (*ggml_abort_callback)(void * data);
        // params.abort_callback = [&](void * data) {
        //     return do_abort;
        // };
        params.abort_callback = abortCallbacks.callback;
        params.abort_callback_user_data = abortCallbacks([&]() -> bool {
            if (do_abort)
                log.trace("abort by abort callback");
            return do_abort;
        });

        // NOTE: not much point of this
        // params.progress_callback_user_data = 
        // params.progress_callback = [](struct whisper_context * ctx, struct whisper_state * state, int progress, void * user_data) {
        //     cout << "whisper progress " << progress << endl;
        // };

        token_timestamps = params.token_timestamps;
        dtw_enabled = model.dtw_enabled;

        do_abort = false;

        int r = 0;

        segments.clear();

        if (use_vad) {

            VAD vad(vad_model, config.vad_config);

            size_t prev_end = 0;

            double ms = 1000.0 / (double)vad.sample_rate();

            log.trace("running VAD");

            vad.start(samples, count);

            for (auto& sr : vad) {
                // sr.start, sr.end, vad.sample_rate()

                log.debug("VAD range detected ({},{}): from {} ms till {} ms, duration {} ms of speech after {} ms of non-speech",
                        sr.start, sr.end, sr.start * ms, sr.end * ms, (sr.end - sr.start) * ms, (sr.start - prev_end) * ms);
                // continue;
                // std::cout << "VAD range detected: " << sr.start * vad.sample_rate() * 1000 << " ms -> " << sr.end * vad.sample_rate() * 1000
                //     << " ms  (duration: " << (sr.end - sr.start) * vad.sample_rate() * 1000 << " ms)" << std::endl;

                size_t no_speech_size = sr.start - prev_end;

                // if (prev_end > 0 && no_speech_size >= 10 * vad.sample_rate() [# reset context after 10s of no speech #])
                if (prev_end > 0 && no_speech_size * ms >= config.reset_min_nospeech_ms /* reset context after 10s of no speech */)
                    params.no_context = true;

                // offset_ms = sr.start * 100 [# s to ms #] / vad.sample_rate();
                offset_ms = sr.start * ms;

                if (do_abort) {
                    r = -6;
                    break;
                }

                r = whisper_full_with_state(ctx, state, params, &samples[sr.start], sr.end - sr.start);

                if (r != 0)
                    break;

                prev_end = sr.end;

                if (params.no_context)
                    params.no_context = false;

                // do not re-detect language
                // if (lang == "" || lang == "auto" || params.detect_language) {
                //     lang = detectedLanguage();
                //     log.trace("reusing language {} for next segment", lang);
                //     params.language = lang.c_str();
                //     params.detect_language = false;
                // }

                getSegments(segments, 0, -1, offset_ms);

                // std::cout << "GOT OFFSET " << offset_ms << std::endl;
                // if (offset_ms > 0)
                //     offsetSegments(segments, offset_ms);

                log.trace("running VAD");
            }

            // TODO: if state is reused, segments ar cleared on whisper_full call?

        } else {
            log.trace("whisper input samples = {}", (size_t)samples);
            r = whisper_full_with_state(ctx, state, params, samples, count);
        }

        log.debug("done");

        if (r != 0) {
            log.trace("whisper exited with code: {}", r);
            free();  // reset on error
            // cerr << "whisper error" << endl;
            if (params.abort_callback_user_data)
                abortCallbacks.remove(params.abort_callback_user_data);
            if (params.new_segment_callback_user_data)
                newSegmentCallbacks.remove(params.new_segment_callback_user_data);
            return r;
        }

        if (params.abort_callback_user_data)
            abortCallbacks.remove(params.abort_callback_user_data);
        if (params.new_segment_callback_user_data)
            newSegmentCallbacks.remove(params.new_segment_callback_user_data);

        // write_segments(whisper_ctx, state, 0, &wsctx);

        // string result = segments_to_json(whisper_ctx, state).dump(2, ' ', false, json::error_handler_t::ignore);

        return r;
    }

    void abort() { log.trace("setting abort flag"); do_abort = true; }

    size_t numberOfSegments() {
        struct whisper_state *state = this->state.get();
        return whisper_full_n_segments_from_state(state);
    }

    std::string detectedLanguage() {
        struct whisper_state *state = this->state.get();
        return whisper_lang_str(whisper_full_lang_id_from_state(state));
    }

    void getToken(WhisperToken& token, int i_segment, int i_token) {
        struct whisper_context *ctx = model.ctx;
        struct whisper_state *state = this->state.get();
        *(whisper_token_data*)(&token) = whisper_full_get_token_data_from_state(state, i_segment, i_token);
        token.text = whisper_full_get_token_text_from_state(ctx, state, i_segment, i_token);
        token.special = token.id >= model.eot;
    }

    void getSegment(WhisperSegment& segment, int i_segment) {
        struct whisper_context *ctx = model.ctx;
        struct whisper_state *state = this->state.get();
        segment.t0 = whisper_full_get_segment_t0_from_state(state, i_segment);
        segment.t1 = whisper_full_get_segment_t1_from_state(state, i_segment);
        segment.turn_next = whisper_full_get_segment_speaker_turn_next_from_state(state, i_segment);
        segment.text = whisper_full_get_segment_text_from_state(state, i_segment);
        segment.lang = whisper_lang_str(whisper_full_lang_id_from_state(state));
        auto& tokens = segment.tokens;
        tokens.clear();
        int n_tokens = whisper_full_n_tokens_from_state(state, i_segment);
        for (int i = 0; i < n_tokens; i++) {
            WhisperToken token;
            getToken(token, i_segment, i);

            // std::cout << "token: " << i << ": >" << token.text << "< len: " << token.text.size() << std::endl;
            // auto x = missing_utf8_bytes(token.text);
            // std::cout << "missing: " << x << std::endl;

            // TODO: this is for debug only
            if (auto x = missing_utf8_bytes(token.text); x > 0)
                std::cout << "token: '" << token.text << "' missing " << x << " bytes"<< std::endl;

            if (tokens.empty() || token.special)
                tokens.push_back(std::move(token));
            else if (auto& prev = tokens.back(); !prev.special && missing_utf8_bytes(prev.text) > 0)
                prev += token;
            else
                tokens.push_back(std::move(token));
        }
    }

    void getSegments(WhisperSegments& segments, int first = 0, int last = -1, int64_t offset_ms = 0) {
        struct whisper_state *state = this->state.get();
        int n_segments = whisper_full_n_segments_from_state(state);
        if (last < 0 || last > n_segments)
            last = n_segments;
        if (first < 0)
            first = std::max(0, n_segments + first);
        segments.reserve(segments.size() + last - first);
        // segments.resize(last - first);
        for (int i = first; i < last; i++) {
            segments.emplace_back();
            getSegment(segments.back(), i);
            if (offset_ms > 0)
                offsetSegment(segments.back(), offset_ms);
            // getSegment(segments[i], i);
        }
    }

    WhisperSegments getSegments(int first = 0, int last = -1) {
        // WhisperSegments segments(last - first);
        WhisperSegments segments;
        getSegments(segments, first, last);
        return segments;
    }

    WhisperResult getResult(int first_segment = 0, int last_segment = -1) {
        struct whisper_state *state = this->state.get();
        WhisperResult result;
        result.lang = whisper_lang_str(whisper_full_lang_id_from_state(state));
        if (segments.empty())
            getSegments(result.segments, first_segment, last_segment);
        else
            result.segments = segments;
        return result;
    }

    void offsetSegment(WhisperSegment& segment, int64_t offset_ms = 0) {
        int64_t offset_t = offset_ms / 10;
        if (offset_t == 0)
            return;
        segment.t0 += offset_t;
        segment.t1 += offset_t;
        for (auto& token : segment.tokens) {
            token.t0 += offset_t;
            token.t1 += offset_t;
            token.t_dtw += offset_t;
        }
    }

    void offsetSegments(WhisperSegments& segments, int64_t offset_ms = 0) {
        for (auto& segment : segments)
            offsetSegment(segment, offset_ms);
    }

    /* deprecated */
    json segments_to_json() {
        struct whisper_context *ctx = model.ctx;
        struct whisper_state *state = this->state.get();
        std::string lang = whisper_lang_str(whisper_full_lang_id_from_state(state));
        int n_segments = whisper_full_n_segments_from_state(state);
        whisper_token eot = whisper_token_eot(ctx);
        struct token {
            std::string text;
            whisper_token_data data;
        };
        auto missing_bytes = [](const char* text, int expected = 0) -> int {
            // https://en.wikipedia.org/wiki/UTF-8#Encoding
            // int expected = 0;
            for (int sz = std::strlen(text), i = 0; i < sz; i++) {
                char b = text[i];
                if (expected == 0) {
                    if ((b & 0b010000000) == 0b000000000) {
                        expected = 0;
                    } else if ((b & 0b011100000) == 0b011000000) {
                        expected = 1;
                    } else if ((b & 0b011110000) == 0b011100000) {
                        expected = 2;
                    } else if ((b & 0b011111000) == 0b011110000) {
                        expected = 3;
                    } else {
                        // invalid byte
                        // reset
                        expected = 0;
                    }
                } else if ((b & 0b011000000) == 0b010000000) {
                    expected--;
                } else {
                    // invalid byte
                    // reset
                    expected = 0;
                }
            }
            return expected;
        };
        // vector<json> segments;
        json segments = json::array();
        for(int s = 0; s < n_segments; s++) {
            int n_tokens = whisper_full_n_tokens_from_state(state, s);
            std::vector<json> tokens;
            const char * text = whisper_full_get_segment_text_from_state(state, s);
            // std::string text; // = whisper_full_get_segment_text_from_state(state, s);
            std::vector<token> stack;
            int stack_missing = 0;
            for(int i = 0; i < n_tokens; i++) {
                whisper_token_data token_data = whisper_full_get_token_data_from_state(state, s, i);
                const char *token_text = whisper_full_get_token_text_from_state(ctx, state, s, i);

                int missing = missing_bytes(token_text, stack_missing);

                if (missing > 0) {
                    stack.push_back({ token_text, token_data });
                    stack_missing = missing;
                    std::cout << "warning: token text missing " << missing << " utf-8 codepoint bytes, will join with next token" << std::endl;
                    continue;
                } else if (stack.size() > 0) {
                    std::string text;
                    double start = stack.front().data.t0, end = token_data.t1;
                    double dtw = stack.front().data.t_dtw;
                    double p = 0, plog = 0, pt = 0, ptsum = 0;
                    int vlen = 0;

                    for (auto& token : stack) {
                        text += token.text;
                        vlen += token.data.vlen;
                        p += token.data.p;
                        plog += token.data.plog;
                        pt += token.data.pt;
                        ptsum += token.data.ptsum;
                    }

                    text += token_text;
                    vlen += token_data.vlen;
                    p += token_data.p;
                    plog += token_data.plog;
                    pt += token_data.pt;
                    ptsum += token_data.ptsum;

                    p = p / (stack.size() + 1);
                    plog = plog / (stack.size() + 1);
                    pt = pt / (stack.size() + 1);
                    ptsum = ptsum / (stack.size() + 1);

                    stack.resize(0);
                    stack_missing = 0;

                    std::cout << "merged token text: >" << text << "<" << std::endl;

                    tokens.emplace_back(json({
                            {"text", text},
                            {"start", start},
                            {"end", end},
                            {"p", p},
                            {"plog", plog},
                            {"pt", pt},
                            {"ptsum", ptsum},
                            {"vlen", vlen},
                            {"dtw", dtw},
                            {"special", false},
                    }));
                } else {
                    stack_missing = 0;
                    // std::cout << ">" << token_text << "< ";
                    tokens.emplace_back(json({
                            {"text", token_text},
                            {"start", token_data.t0},
                            {"end", token_data.t1},
                            {"p", token_data.p},
                            {"plog", token_data.plog},
                            {"pt", token_data.pt},
                            {"ptsum", token_data.ptsum},
                            {"vlen", token_data.vlen},
                            {"dtw", token_data.t_dtw},
                            {"special", token_data.id >= eot},
                    }));
                }
                // text += token_text;
            }
            if (stack.size() > 0) {
                std::string text;
                double start = stack.front().data.t0, end = stack.back().data.t1;
                double dtw = stack.front().data.t_dtw;
                double p = 0, plog = 0, pt = 0, ptsum = 0;
                int vlen = 0;

                for (auto& token : stack) {
                    text += token.text;
                    vlen += token.data.vlen;
                    p += token.data.p;
                    plog += token.data.plog;
                    pt += token.data.pt;
                    ptsum += token.data.ptsum;
                }

                p = p / (stack.size());
                plog = plog / (stack.size());
                pt = pt / (stack.size());
                ptsum = ptsum / (stack.size());

                stack.resize(0);
                stack_missing = 0;

                std::cout << "merged token text: >" << text << "<" << std::endl;

                tokens.emplace_back(json({
                        {"text", text},
                        {"start", start},
                        {"end", end},
                        {"p", p},
                        {"plog", plog},
                        {"pt", pt},
                        {"ptsum", ptsum},
                        {"vlen", vlen},
                        {"dtw", dtw},
                        {"special", false},
                        // TODO: add DTW timestamp
                }));
            }
            // std::cout << std::endl;
            bool speaker_turn = whisper_full_get_segment_speaker_turn_next_from_state(state, s);
            json segment = json{
                {"text", text},
                {"tokens", tokens},
                {"speaker_turn", speaker_turn},
            };
            segments.emplace_back(segment);
        }
        return json{
            {"lang", lang},
            {"segments", segments},
        };
    }

    static int missing_utf8_bytes(const std::string& text, int expected = 0) {
        return missing_utf8_bytes(text.c_str(), text.size(), expected);
    }

    static int missing_utf8_bytes(const char* text, size_t size = 0, int expected = 0) {
        // https://en.wikipedia.org/wiki/UTF-8#Encoding
        // int expected = 0;
        // std::cout << std::hex << std::setw(2);// << std::setfill('0');
        for (int sz = size > 0 ? size : std::strlen(text), i = 0; i < sz; i++) {
            char b = text[i];
            // std::cout << "["<<i<<"/"<<sz<<"]" << (int)(unsigned char)b << "=";
            if (expected == 0) {
                if ((b & 0b010000000) == 0b000000000) {
                    expected = 0;
                } else if ((b & 0b011100000) == 0b011000000) {
                    expected = 1;
                } else if ((b & 0b011110000) == 0b011100000) {
                    expected = 2;
                } else if ((b & 0b011111000) == 0b011110000) {
                    expected = 3;
                } else {
                    // std::cout << "INVA"<<std::endl;
                    // invalid byte
                    // reset
                    expected = 0;
                }
            } else if ((b & 0b011000000) == 0b010000000) {
                // std::cout << "DEC"<<std::endl;
                expected--;
            } else {
                // std::cout << "INVB"<<std::endl;
                // invalid byte
                // reset
                expected = 0;
            }
            // std::cout <<expected<< "; ";
        }
        // std::cout<<std::endl;
        return expected;
    }

private:
    bool token_timestamps = false;
    bool dtw_enabled = false;
    bool do_abort = false;
};


WhisperModel::WhisperModel() {}

WhisperModel::WhisperModel(const std::string& model, const std::string& dtw, bool use_gpu, int gpu_device)
    : impl(std::make_unique<WhisperModelImpl>(model, dtw, use_gpu, gpu_device)) {
}

WhisperModel::~WhisperModel() {}

WhisperModel::operator bool() const {
    return impl->operator bool();
}

bool WhisperModel::init(const std::string& model, const std::string& dtw, bool use_gpu, int gpu_device) {
    impl = std::make_unique<WhisperModelImpl>();
    return impl->init(model, dtw, use_gpu, gpu_device);
}







Whisper::Whisper() {}

Whisper::Whisper(WhisperModel& model) : impl(std::make_unique<WhisperImpl>(*model.impl.get())) {}

Whisper::Whisper(WhisperModel& model, VADModel& vad_model) : impl(std::make_unique<WhisperImpl>(*model.impl.get(), vad_model)) {}

Whisper::~Whisper() {}

void Whisper::setModel(WhisperModel& model) {
    impl = std::make_unique<WhisperImpl>(*model.impl.get());
}

void Whisper::setVADModel(VADModel& model) {
    if (impl)
        impl->setVADModel(model);
}

WhisperReturnValue Whisper::operator()(const void* wav_data, size_t wav_size, const WhisperJobConfig& config) {
    return impl->operator()(wav_data, wav_size, config);
}

WhisperReturnValue Whisper::operator()(const float* samples, size_t count, const WhisperJobConfig& config) {
    return impl->operator()(samples, count, config);
}
// bool Whisper::operator()(const void* wav_data, size_t wav_size, const std::string& lang, bool reset, bool use_vad, VADConfig vad_config) {
//     return impl->operator()(wav_data, wav_size, lang, reset, use_vad, vad_config);
// }
//
// bool Whisper::operator()(const float* samples, size_t count, const std::string& lang, bool reset, bool use_vad, VADConfig vad_config) {
//     return impl->operator()(samples, count, lang, reset, use_vad, vad_config);
// }

void Whisper::abort() {
    impl->abort();
}

size_t Whisper::numberOfSegments() const {
    return impl->numberOfSegments();
}

std::string Whisper::detectedLanguage() const {
    return impl->detectedLanguage();
}

void Whisper::getSegment(WhisperSegment& segment, int i_segment) const {
    impl->getSegment(segment, i_segment);
}

void Whisper::getSegments(WhisperSegments& segments, int first, int last) const {
    impl->getSegments(segments, first, last);
}

WhisperSegments Whisper::getSegments(int first, int last) const {
    return impl->getSegments(first, last);
}

WhisperResult Whisper::getResult(int first_segment, int last_segment) const {
    return impl->getResult(first_segment, last_segment);
}

json Whisper::segments_to_json() {
    return impl->segments_to_json();
}




struct WhisperJobInternal : public WhisperJob {
    // SharedBuffer<float> samples;
    // SharedBuffer<void> wav;
    // const WhisperJobConfig& config = WhisperJobConfig();
    // std::string id;

    WhisperJobStatus status = WhisperJobStatus::Waiting;

    ReferenceKeeper<std::shared_mutex> mutex = nullptr;
    ReferenceKeeper<std::condition_variable_any> cv = nullptr;
    // std::shared_mutex *mutex = nullptr;
    // std::condition_variable_any *cv = nullptr;

    WhisperSegments segments;

    // WhisperJobInternal(WhisperJob&& job) : WhisperJob(std::move(*(WhisperJob*)&job)) {}
    WhisperJobInternal(WhisperJob&& job) : WhisperJob(std::move(job)) {}
        // *(whisper_token_data*)(&token) = whisper_full_get_token_data_from_state(state, i_segment, i_token);

    bool do_abort = false;

    void free() { samples.free(); wav.free(); }
    // TODO: write to disk
};


struct WhisperProcessingThreadData {
    std::thread thread;
    std::shared_mutex job_mutex;
    std::condition_variable_any cv;
    bool do_abort = false;
    WhisperImpl* whisper = nullptr;
    WhisperJobID* job_id = nullptr;
};

class WhisperQueueProcessorImpl {
    inline static logger log = new_logger("whisper-queue");
    WhisperModelImpl& model;
    VADModel vad_model;
public:
    WhisperQueueProcessorImpl(WhisperModelImpl& model, int max_instances = 2) : model(model), max_instances(max_instances) {}
    WhisperQueueProcessorImpl(WhisperModelImpl& model, VADModel& vad_model, int max_instances = 2) : model(model), vad_model(vad_model), max_instances(max_instances) {}

    void setVADModel(VADModel& model) { vad_model = model; }

    typedef int job_id;
    typedef int instance_id;

    // std::map<std::thread::id, WhisperProcessingThreadData> threads;
    void cleanup() {
        for (auto it = threads.begin(); it != threads.end(); ) {
            auto& data = it->second;
            // TODO: protect with global mutex
            if (!data.thread.joinable() /*&& !data.job_mutex && !data.cv*/ /* TODO: check if still in use */) {
                it = threads.erase(it); // erase returns the next iterator
            } else {
                ++it; // move to the next element
            }
        }
    }

    WhisperJobID add(WhisperJob&& job) {
        WhisperJobID id;
        {
            std::unique_lock<std::shared_mutex> lock(jobs_mutex);
            id = newJobID();
            while (jobs.count(id) > 0)
                id = newJobID();

            auto r = jobs.emplace(std::make_pair(id, std::move(job)));
            {
                WhisperJobInternal& job = r.first->second;
                job.id = id;
                job.status = WhisperJobStatus::Waiting;
            }
        }
        {
            std::lock_guard<std::mutex> lock(job_queue_mutex);
            job_queue.push(id);
        }
        // if (threads.size() < max_instances)
        cleanup();
        if (active_threads.load() < max_instances)
            start_thread();

        return id;
    }

    std::optional<WhisperJobStatus> wait(WhisperJobID id, const std::function<bool(const WhisperSegments&, size_t)>& callback) {
        if (auto opt = getJob(id); opt) {
            auto& job = opt.value();
            // auto& mutex = *job.mutex;
            // auto& cv = *job.cv;
            auto mutex_keeper = job.mutex.scoped();
            auto cv_keeper = job.cv.scoped();
            auto& mutex = job.mutex.ref();
            auto& cv = job.cv.ref();

            size_t consumed = 0;

            while (true) {

                std::shared_lock<std::shared_mutex> lock(mutex);

                // TODO: add abort option, i.e., done
                // optionally can be used with lambda: cv.wait(lock, [] { return ready; });
                while (consumed >= job.segments.size() && job.status == WhisperJobStatus::Running && !job.do_abort) {
                    cv.wait(lock);
                }

                // if (job.do_abort)
                //     return false;

                if (consumed < job.segments.size() && !callback(job.segments, job.segments.size() - consumed /* new segments for the waiter */))
                    break;
                    // return false;
                    // break; // stop waiting for the results

                consumed = job.segments.size();

                if (job.status != WhisperJobStatus::Running)
                    break;
            }

            // spdlog::info("wait() finishing, got segments {} consumed {}", job.segments.size(), consumed);

            if (consumed != job.segments.size()) {
                callback(job.segments, 0);
            }

            // TODO: finished, now there is no more need for locking, as all the job content must be read only at this point

            return job.status;
        }
        return std::nullopt;
    }

    bool abort(WhisperJobID id) {
        for (auto& pair : threads) {
            auto& data = pair.second;
            auto job_id = data.job_id;
            if (job_id != nullptr && *job_id == id) {
                log.trace("setting abort flag for running process");
                data.do_abort = true;
                return true;
            }
        }
        if (auto opt = getJob(id); opt) {
            std::shared_lock<std::shared_mutex> lock(job_status_mutex);
            log.trace("setting abort flag for job {}", opt.value().id);
            return opt.value().do_abort = true;
        }
        return true;
    }

    std::optional<WhisperJobStatus> getJobStatus(WhisperJobID id) {
        if (auto opt = getJob(id); opt) {
            std::shared_lock<std::shared_mutex> lock(job_status_mutex);
            return opt.value().status;
        }
        return std::nullopt;
    }

    optional_ref<const WhisperSegments> getResults(WhisperJobID id) {
        if (auto opt = getJob(id); opt) {
            auto& job = opt.value();
            // {
            //     std::shared_lock<std::shared_mutex> lock(job_status_mutex);
            //     if (job.status != WhisperJobStatus::Done)
            //         return std::nullopt;
            // }
            return job.segments;
        }
        return std::nullopt;
    }

private:
    optional_ref<WhisperJobInternal> getJob(WhisperJobID id) {
        std::shared_lock<std::shared_mutex> lock(jobs_mutex);
        auto it = jobs.find(id);
        if (it == jobs.end())
            return std::nullopt;
        return it->second;
    }

    WhisperJobID newJobID() { return rnd(6); }

    bool start_thread() {
        std::thread thread(std::bind(&WhisperQueueProcessorImpl::processor, this /*, ...arguments */));
        std::lock_guard<std::mutex> lock(threads_mutex);
        threads[thread.get_id()].thread = std::move(thread);
        // thread_job_mutexes.emplace(thread.get_id(), std::shared_mutex());
        // thread_job_mutexes[thread.get_id()];  // default construct shared_mutext in place
        return true;
    }

    optional_ref<WhisperJobInternal> getNextJob() {
        if (job_queue.empty())
            return std::nullopt;
        std::lock_guard<std::mutex> lock(job_queue_mutex);
        WhisperJobID id = job_queue.front();
        job_queue.pop();
        WhisperJobInternal& job = jobs.at(id);
        return job;
    }

    // std::shared_mutex& get_thread_job_mutex(std::thread::id id = std::this_thread::get_id()) {
    //     std::lock_guard<std::mutex> lock(threads_mutex);
    //     return thread_job_mutexes[id];
    // }

    WhisperProcessingThreadData& get_thread(std::thread::id id = std::this_thread::get_id()) {
        std::lock_guard<std::mutex> lock(threads_mutex);
        return threads[id];
    }

    void processor() {
        active_threads++;

        // std::shared_mutex& job_mutex = get_thread_job_mutex();
        WhisperProcessingThreadData& data = get_thread();
        std::shared_mutex& job_mutex = data.job_mutex;
        std::condition_variable_any& job_cv = data.cv;

        WhisperImpl whisper(model, vad_model);
        WhisperJobInternal* currentJob = nullptr;

        data.whisper = &whisper;

        const auto newSegmentsCallback = [&](WhisperSegments&& segments) -> bool {
            WhisperJobInternal& job = *currentJob;
            // std::shared_mutex& mutex = *job.mutex;
            // std::condition_variable_any& cv = *job.cv;
            std::shared_mutex& mutex = job.mutex.ref();
            std::condition_variable_any& cv = job.cv.ref();

            {
                std::unique_lock<std::shared_mutex> lock(mutex);

                job.segments.insert(job.segments.end(),
                       std::make_move_iterator(segments.begin()),
                       std::make_move_iterator(segments.end()));
                segments.clear(); // clear the source vector as its elements have been moved
            }

                // spdlog::info("processor()::new segment callback: job segments {}", job.segments.size());

            job_cv.notify_all();

            if (data.do_abort)
                return false;

            return true;
        };
        while (auto nextJob = getNextJob()) {
            WhisperJobInternal& job = nextJob.value();
            currentJob = &job;

            if (job.do_abort || job.status == WhisperJobStatus::Aborted) {
                std::unique_lock<std::shared_mutex> lock(job_status_mutex);
                job.status = WhisperJobStatus::Aborted;
                continue;
            }

            // with what to lock?
            data.do_abort = false;
            data.job_id = &job.id;

            // spdlog::info("processor() got job with id = {}", job.id);

                // spdlog::info("processor() job.config.lang = {}", job.config.lang);
                // spdlog::info("processor() job.config = {}", (size_t)&job.config);
                // spdlog::info("processor() job.config.lang.c_str = {}", (size_t)job.config.lang.c_str());
                // spdlog::info("processor() job = {}", (size_t)&job);
                // spdlog::info("processor() job+sizeof() = {}", (size_t)&job +sizeof(WhisperJob));
                // spdlog::info("processor() job.statux = {}", (size_t)&job.status);
                // spdlog::info("processor() job.mutex = {}", (size_t)&job.mutex);

            // TODO: what to lock?
            // job.mutex = &job_mutex;
            //     spdlog::info("processor() job.config.lang = {}", job.config.lang);
            //     spdlog::info("processor() job.config.lang.c_str = {}", (size_t)job.config.lang.c_str());
            // job.cv = &job_cv;
            //     spdlog::info("processor() job.config.lang = {}", job.config.lang);
            job.mutex = job_mutex;
            job.cv = job_cv;
            auto mutex_keeper = job.mutex.scoped();
            auto cv_keeper = job.cv.scoped();
            {
                std::unique_lock<std::shared_mutex> lock(job_status_mutex);
                job.status = WhisperJobStatus::Running;
            }
                // spdlog::info("processor() job.config.lang = {}", job.config.lang);

            auto r = whisper(job.samples.data, job.samples.count, job.config, newSegmentsCallback);

            if (r) {
                std::unique_lock<std::shared_mutex> lock(job_status_mutex);
                job.status = WhisperJobStatus::Done;
                // put into done jobs
                // get final result and notify
            } else {
                std::unique_lock<std::shared_mutex> lock(job_status_mutex);
                if (r.aborted())
                    job.status = WhisperJobStatus::Aborted;
                else
                    job.status = WhisperJobStatus::Failed;
                // put into failed jobs, TODO: how to get and store reason?
            }
            job_cv.notify_all();  // unlock all waiters, allow them to finish
            // job.mutex = nullptr;
            // TODO: with what mutex to lock
            data.job_id = nullptr;
        }
        active_threads--;
        // remove itself from the threads list
        {
            // std::lock_guard<std::mutex> lock(threads_mutex);
            // threads.erase(std::this_thread::get_id());
            // thread_job_mutexes.erase(std::this_thread::get_id());
        }
    }

private:

    // std::map<int, std::shared_ptr<WhisperImpl>> pool;

    std::atomic_int active_threads = 0;

    int next_whisper_id = 0;
    int next_job_id = 0;
    int max_instances = 2;
    // input job queue -> contains job definition
    // job id -> state
    //
    // already done objects and shared results (at the time of done all registered callbacks got done signal) -> archive into sqlite db
    // std::queue<WhisperJob> job_queue;
    std::mutex job_queue_mutex;
    std::queue<WhisperJobID> job_queue;
    // will this container act as some job keeper? need to determine job by status then

    std::shared_mutex jobs_mutex;
    std::unordered_map<WhisperJobID, WhisperJobInternal> jobs;  // the main in memory storage of jobs, references to values are valid until removed

    std::mutex threads_mutex;
    std::map<std::thread::id, WhisperProcessingThreadData> threads;
    // std::map<std::thread::id, std::thread> threads;
    // std::map<std::thread::id, std::shared_mutex> thread_job_mutexes;

    std::shared_mutex job_status_mutex;

    RandomStringGenerator rnd;
};



WhisperQueueProcessor::WhisperQueueProcessor(WhisperModel& model, int max_instances)
    : impl(std::make_unique<WhisperQueueProcessorImpl>(*model.impl.get(), max_instances)), model(&model) {}
WhisperQueueProcessor::WhisperQueueProcessor(WhisperModel& model, VADModel& vad_model, int max_instances)
    : impl(std::make_unique<WhisperQueueProcessorImpl>(*model.impl.get(), vad_model, max_instances)), model(&model), vad_model(&vad_model) {}

WhisperQueueProcessor::~WhisperQueueProcessor() {}

Whisper WhisperQueueProcessor::newWhisperInstance() {
    if (model != nullptr && vad_model != nullptr) {
        return Whisper(*model, *vad_model);
    } else if (model != nullptr) {
        return Whisper(*model);
    }
    return Whisper();
}

void WhisperQueueProcessor::setVADModel(VADModel& vad_model) { if (impl) impl->setVADModel(vad_model); }

WhisperJobID WhisperQueueProcessor::add(WhisperJob&& job) { return impl->add(std::move(job)); }

std::optional<WhisperJobStatus> WhisperQueueProcessor::wait(WhisperJobID id, const std::function<bool(const WhisperSegments&, size_t)>& callback) { return impl->wait(id, callback); }

std::optional<WhisperJobStatus> WhisperQueueProcessor::getJobStatus(WhisperJobID id) { return impl->getJobStatus(id); }

optional_ref<const WhisperSegments> WhisperQueueProcessor::getResults(WhisperJobID id) {
    return impl->getResults(id);
}

bool WhisperQueueProcessor::abort(WhisperJobID id) {
    return impl->abort(id);
}
