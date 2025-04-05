

#include <iostream>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <map>
#include <stdexcept>
#include <functional>
#include <string_view>
#include <chrono>
#include <string>
#include <type_traits>
#include <regex>

#include <cstring>
#include <cstdlib>
#include <csignal>

#include <unistd.h>
#include <signal.h>
#include <dlfcn.h>

#include <utf8.h>
#include <libgen.h>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <popl.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "log.hpp"
#include "utf8_util.hpp"
#include "wav_util.hpp"
#include "whisper.hpp"
#include "storage.hpp"
#include "string_util.hpp"
#include "vfs.hpp"
#include "engine_device_conf.hpp"


using namespace std;
using namespace httplib;
using json = nlohmann::json;
namespace fs = std::filesystem;


bool verbose = false;
fs::path exec_dir;

EngineDeviceConfigurations engineDeviceConf;

const char* signalString(int signal) {
    if (signal == SIGTERM) return "termination request, sent to the program";
    if (signal == SIGSEGV) return "invalid memory access (segmentation fault)";
    if (signal == SIGINT) return "external interrupt, usually initiated by the user";
    if (signal == SIGILL) return "invalid program image, such as invalid instruction";
    if (signal == SIGABRT) return "abnormal termination condition, as is e.g. initiated by std::abort()";
    if (signal == SIGFPE) return "erroneous arithmetic operation such as divide by zero";
    return "unknown signal";
}

void signalHandler(int signal) {
    auto log = new_logger("signal");
    log.error("got signal: {}", signalString(signal));
    std::abort();
}

// string http_log(const Request &req, const Response &res) {
//   string s;
//   char buf[BUFSIZ];
//
//   snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
//            req.version.c_str(), req.path.c_str());
//   s += buf;
//
//   return s;
// }


DECLARE_EMBEDDED_DATA(_vfs_static);


std::vector<std::string> splitString(const std::string& str, const std::string& delimiter) {
    std::vector<std::string> tokens;
    size_t start = 0;
    size_t end = str.find(delimiter);
    while (end != std::string::npos) {
        tokens.push_back(str.substr(start, end - start));
        start = end + delimiter.length();
        end = str.find(delimiter, start);
    }
    tokens.push_back(str.substr(start));
    return tokens;
}

std::string joinStrings(const std::vector<std::string>& strings, const std::string& delimiter) {
    std::string result;
    for (const auto& s : strings) {
        if (!result.empty())
            result += delimiter;
        result += s;
    }
    return result;
}

bool starts_with(std::string_view str, std::string_view prefix) {
    return str.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view str, std::string_view substr) {
    return str.find(substr) != std::string_view::npos;
}

bool is_directory(const fs::path p) {
    return fs::is_directory(p) || (fs::is_symlink(p) && fs::is_directory(fs::read_symlink(p)));
}

bool is_file(const fs::path p) {
    return fs::is_regular_file(p) || (fs::is_symlink(p) && fs::is_regular_file(fs::read_symlink(p)));
}

bool is_safe_path(const fs::path& base_dir, const fs::path& user_input) {
    try {
        // canonicalize the base directory
        fs::path canonical_base = fs::canonical(base_dir);
        // combine and canonicalize the input path
        fs::path combined_path = fs::weakly_canonical(canonical_base / user_input);

        // check if the combined path starts with the canonical base directory
        auto canonical_base_str = canonical_base.string();
        auto combined_path_str = combined_path.string();
        if (combined_path_str.compare(0, canonical_base_str.size(), canonical_base_str) == 0) {
            return true;
        } else {
            return false;
        }
    } catch (const fs::filesystem_error& e) {
        // std::cerr << "Filesystem error: " << e.what() << '\n';
        return false;
    } catch (const std::exception& e) {
        // std::cerr << "General error: " << e.what() << '\n';
        return false;
    }
}

std::string readFile(const std::string& filename) {
    // open the file in binary mode
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open the file");
    }

    // move the pointer to the end of the file to find its length
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // create a string to hold the contents
    std::string content(fileSize, '\0');

    // read the file
    file.read(&content[0], fileSize);

    return content;
}

// function to decode escaped characters
std::string ssml_decode_escaped_characters(const std::string& text) {
    std::string decoded_text = text;
    static std::string search[] = { "&quot;", "&amp;", "&apos;", "&lt;", "&gt;" };
    static std::string replace[] = { "\"", "&", "'", "<", ">" };

    for (size_t i = 0; i < sizeof(search) / sizeof(search[0]); ++i) {
        decoded_text = std::regex_replace(decoded_text, std::regex(search[i]), replace[i]);
    }

    return decoded_text;
}

// function to extract plain text from SSML and decode escaped characters
std::string ssml_extract_plain_text(const std::string& ssml) {
    // regular expression to match SSML tags
    static const std::regex ssml_tag(R"(<[^>]+>)");

    // replace all SSML tags with an empty string
    std::string plain_text = std::regex_replace(ssml, ssml_tag, "");

    // decode escaped characters
    plain_text = ssml_decode_escaped_characters(plain_text);

    return plain_text;
}

double decibels_to_ratio(double decibels) {
    return std::pow(10.0, decibels / 20.0);
}

double semitones_to_ratio(double semitones) {
    return std::pow(2.0, semitones / 12.0);
}


struct ServerConfig {
    int port = 9090;
    fs::path static_path = "static";
    fs::path whisper_model_path = "whisper-ggml.bin";
    fs::path vad_model_path = "silero_vad.onnx";
    std::string whisper_dtw = "";
    int limit_whisper_input_s = 0;
    int vad_trim_range_s = 20;
    int max_whisper_instances = 2;
    bool cpu_only = false;
    bool add_cors_headers = false;
};


void runServer(logger& log, const ServerConfig& config = ServerConfig())
{
    using namespace httplib;

    VFS vfs(EMBEDDED_DATA_BUFFER(_vfs_static));
    // vfs.list();
    auto summary = vfs.summary();
    log.debug("VFS summary: {} in {} files and {} directories (compressed {})", human_readable_size(summary.total_size),
            summary.file_count, summary.dir_count, human_readable_size(summary.compressed_size));

    Server server;
    Storage storage("storage.sqlite");

    VADModel vad_model(config.vad_model_path);
    WhisperModel whisperModel(config.whisper_model_path, config.whisper_dtw, engineDeviceConf.IsGPU(Engines::Whisper), engineDeviceConf[Engines::Whisper] /*, use_gpu, gpu_device */);
    WhisperQueueProcessor whisper(whisperModel, vad_model, config.max_whisper_instances);

    server.Get("/api/config", [&](const auto& req, auto& res) {
        json config_json = {
            {"whisper", {
                {"limit", config.limit_whisper_input_s},
                {"vad_trim_range", config.vad_trim_range_s},
            }}
        };
        res.set_content(config_json.dump(2), "application/json");
    });

    server.Get("/api/storage/([^/]+)", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];

        auto result = storage.get(id);

        if (!result) {
            log.error("document with id = {} not found", id);
            res.status = 404;
            return;
        }

        res.set_header("Type", result.value().first);
        res.set_content(result.value().second, "application/json");
    });

    server.Delete("/api/storage/([^/]+)", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];
        std::string key;

        if(req.has_param("key"))
            key = req.get_param_value("key");

        auto result = storage.remove(id, key);

        if (!result) {
            log.error("document with id = {} not found", id);
            res.status = 500;
            return;
        }

        if (!result.value()) {
            log.error("document with id = {} not found or wrong key", id);
            res.status = 404;
            return;
        }

        // res.set_content(result.value(), "application/json");
        res.status = 204;
    });

    server.Get("/api/storage/([^/]+)/verify", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];
        std::string key;

        if(req.has_param("key"))
            key = req.get_param_value("key");

        auto result = storage.check_key(id, key);

        if (!result) {
            log.error("document with id = {} not found", id);
            res.status = 500;
            return;
        }

        if (!result.value()) {
            log.warn("wrong key for document with id = {}", id);
            res.status = 403;
            return;
        }

        // res.set_content(result.value(), "application/json");
        // res.status = 200;
    });

    server.Put("/api/storage/([^/]+)", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];
        std::string key;

        if(req.has_param("key"))
            key = req.get_param_value("key");

        if (!storage.put(id, req.body, key)) {
            log.error("error storing document with id = {} not found", id);
            res.status = 500;
            return;
        }

        res.status = 200;
    });

    server.Put("/api/storage/([^/]+)/audio", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];
        std::string key;

        if(req.has_param("key"))
            key = req.get_param_value("key");

        if (auto r = storage.check_key(id, key); !r) {
            log.error("error storing audio for document with id = {}: not found", id);
            res.status = 500;
            return;
        } else if (!r.value()) {
            log.warn("wrong key for document with id = {}", id);
            res.status = 403;
            return;
        }

        if (!storage.put_file(id, req.body.c_str(), req.body.size(), ".wav")) {
            log.error("error storing audio for document with id = {}", id);
            res.status = 500;
            return;
        }

        res.status = 204;
    });

    server.Get("/api/storage/([^/]+)/audio", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];

        auto result = storage.get_file(id, ".wav");

        if (!result) {
            log.error("audio for document with id = {} not found", id);
            res.status = 404;
            return;
        }

        res.set_content((const char *)result.value().data, result.value().size, "audio/wav");
    });

    server.Delete("/api/storage/([^/]+)/audio", [&](const auto& req, auto& res) {
        std::string id = req.matches[1];
        std::string key;

        if(req.has_param("key"))
            key = req.get_param_value("key");

        if (auto r = storage.check_key(id, key); !r) {
            log.error("error storing audio for document with id = {}: not found", id);
            res.status = 500;
            return;
        } else if (!r.value()) {
            log.warn("wrong key for document with id = {}", id);
            res.status = 403;
            return;
        }

        if (!storage.remove_file(id, ".wav")) {
            log.error("unable to remove audio file for document with id = {}", id);
            res.status = 404;
            return;
        }

        res.status = 204;
    });

    server.Get("/api/whisper/([^/]+)/abort", [&](const auto& req, auto& res) {

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
        res.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD");

        std::string id = req.matches[1];

        WhisperJobStatus status;

        if (auto opt = whisper.getJobStatus(id); opt) {
            status = opt.value();
            if (status == WhisperJobStatus::Waiting || status == WhisperJobStatus::Running) {
                whisper.abort(id);
                // TODO: wait for item to be stopped
                res.status = 200;
                return;
            }
        } else {
            res.status = 404; // job not found
            return;
        }

        res.status = 500;
        return;
    });

    server.Get("/api/whisper/([^/]+)/status", [&](const auto& req, auto& res) {

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
        res.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD");

        std::string id = req.matches[1];

        WhisperJobStatus status;

        if (auto opt = whisper.getJobStatus(id); opt) {
            status = opt.value();
        } else {
            res.status = 404; // job not found
            return;
        }

        std::string result;

        if (status == WhisperJobStatus::Waiting)
            result = "waiting";
        else if (status == WhisperJobStatus::Running)
            result = "running";
        else if (status == WhisperJobStatus::Failed)
            result = "failed";
        else if (status == WhisperJobStatus::Aborted)
            result = "aborted";
        else if (status == WhisperJobStatus::Done)
            result = "done";
        else
            result = "unknown";

        res.set_content(result, "application/json");
        return;
    });

    server.Get("/api/whisper/([^/]+)/wait", [&](const auto& req, auto& res) {

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
        res.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD");

        std::string id = req.matches[1];

        WhisperJobStatus status;

        if (auto opt = whisper.getJobStatus(id); opt) {
            status = opt.value();
        } else {
            res.status = 404; // job not found
            return;
        }

        using namespace std::chrono_literals;

        // TODO: convert to conditional - any job status changes
        // while ((status = whisper.getJobStatus(id)).value_or(WhisperJobStatus::Failed) == WhisperJobStatus::Waiting)
        //     std::this_thread::sleep_for(100ms);

        if (status == WhisperJobStatus::Waiting) {
            log.debug("job with id {} not yet started, waiting", id);
        }

        // TODO: convert to wait condition
        while (status == WhisperJobStatus::Waiting) {
            std::this_thread::sleep_for(100ms);
            status = whisper.getJobStatus(id).value();
        }

        if (status != WhisperJobStatus::Running && status != WhisperJobStatus::Waiting) {
            // if (status == WhisperJobStatus::Failed) {
            //     log.debug("job with id {} failed", id);
            //     // TODO: faied, return 500 or whatever
            //     res.status = 500;
            //     return;
            // }
            // we already must have results here, keep the same format
            string result;
            if (auto opt = whisper.getResults(id); opt) {
                auto& segments = opt.value();
                for (auto& segment : segments) {
                    result += segment.to_json().dump(-1, ' ', false, json::error_handler_t::ignore);
                    result += "\n";
                }
            }
            if (status == WhisperJobStatus::Done) {
                result += json{{"done", true}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                result += "\n";
            }
            // if (status != WhisperJobStatus::Done && result.empty()) {
            //     if (status == WhisperJobStatus::Failed) {
            //         log.debug("job with id {} failed", id);
            //         // TODO: faied, return 500 or whatever
            //         res.status = 500;
            //         return;
            //     }
            //     if (status == WhisperJobStatus::Aborted) {
            //         log.debug("job with id {} aborted", id);
            //         // TODO: faied, return 500 or whatever
            //         res.status = 500;
            //         return;
            //     }
            // } else
            if (status != WhisperJobStatus::Done) {
                if (status == WhisperJobStatus::Failed) {
                    result += json{{"error", "failed"}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                    result += "\n";
                    // res.status = 206;
                } else if (status == WhisperJobStatus::Aborted) {
                    result += json{{"error", "aborted"}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                    result += "\n";
                    // res.status = 206;
                }
            }
            res.set_content(result, "application/jsonl");
            return;
        }

        // TODO: test if job is waiting, running or other
        // TODO: how to sync both so that, when data are available, then written
        log.debug("got id {}", id);

        res.set_chunked_content_provider(
            "application/jsonl",
            [id, &log, &whisper](size_t offset, DataSink &sink) {

                auto r = whisper.wait(id, [&](const WhisperSegments& segments, size_t n_new) -> bool {

                    log.debug("got new {} segment(s)", n_new);

                    if (!sink.is_writable())
                        return false;

                    if (n_new == 0) {
                        sink.done();
                        return true;
                    }

                    for (size_t i = segments.size() - n_new; i < segments.size(); i++) {
                        auto& segment = segments[i];
                        string result = segment.to_json().dump(-1, ' ', false, json::error_handler_t::ignore) + "\n";
                        sink.write(result.c_str(), result.size());
                    }

                    return true;
                });

                if (r && r.value() != WhisperJobStatus::Done) {
                    auto status = r.value();
                    string result;
                    if (status == WhisperJobStatus::Failed) {
                        result = json{{"error", "failed"}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                        result += "\n";
                    } else if (status == WhisperJobStatus::Aborted) {
                        result = json{{"error", "aborted"}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                        result += "\n";
                    }
                    sink.write(result.c_str(), result.size());
                } else if (r && r.value() == WhisperJobStatus::Done) {
                    string result;
                    result = json{{"done", true}}.dump(-1, ' ', false, json::error_handler_t::ignore);
                    result += "\n";
                    sink.write(result.c_str(), result.size());
                }

                if (!sink.is_writable())
                    return false;

                sink.done();

                return false; // return 'false' if you want to cancel the process.
            }
        );

        // log.debug("wait handler exit");
    });

    server.Post("/api/whisper", [&](const auto& req, auto& res) {

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
        res.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD");

        cerr << "POST /api/whisper" << endl;

        if(!req.is_multipart_form_data()) {
            cerr << "request is not multipart form data" << endl;
            res.status = 400;
            return;
        }

        if (!req.has_file("input")) {
            cerr << "request is missing 'input' file field" << endl;
            res.status = 400;
            return;
        }

        string lang = "auto";

        if (req.has_file("lang")) {
            lang = req.get_file_value("lang").content;
        }

        auto file = req.get_file_value("input");
        const void* data = file.content.c_str();
        size_t dataSize = file.content.size();

        bool enqueue = false;

        if(req.has_param("enqueue")) {
            if (auto v = req.get_param_value("enqueue"); v.size() > 0 && (v == "1" || v[0] == 'y' || v[0] == 't'))
                enqueue = true;
        }
        if(req.has_param("queue")) {
            if (auto v = req.get_param_value("queue"); v.size() > 0 && (v == "1" || v[0] == 'y' || v[0] == 't'))
                enqueue = true;
        }
        if(req.has_param("q")) {
            if (auto v = req.get_param_value("q"); v.size() > 0 && (v == "1" || v[0] == 'y' || v[0] == 't'))
                enqueue = true;
        }

        PCMBuffer pcm(data, dataSize);

        if (!pcm) {
            cerr << "error decoding input as wav" << endl;
            res.status = 400;
            return;
        }

        size_t processSampleCount =  config.limit_whisper_input_s > 0 ?
            std::min(pcm.count(), (size_t)((config.limit_whisper_input_s + config.vad_trim_range_s) * pcm.sample_rate())) : pcm.count();

        if (enqueue) {

            // TODO: how to reduce buffer to processSampleCount

            WhisperJobConfig config = { .lang = lang, .use_vad = true };

            WhisperJob job = { .samples = std::move(pcm.share()), .config = config };

            auto id = whisper.add(std::move(job));

            string result = json{{"id", id}}.dump(2, ' ', false, json::error_handler_t::ignore);

            res.set_content(result, "application/json");

        } else {
            Whisper whisper(whisperModel, vad_model);

            WhisperJobConfig config = { .lang = lang, .use_vad = true };

            if (!whisper(pcm.samples(), processSampleCount, config)) {
                cerr << "whisper error" << endl;
                res.status = 500;
                return;
            }

            string result = whisper.getResult().to_json().dump(2, ' ', false, json::error_handler_t::ignore);
            // string result = whisper.segments_to_json().dump(2, ' ', false, json::error_handler_t::ignore);

            res.set_content(result, "application/json");
        }
    });

    fs::path base_dir;
    std::string static_path_prefix;

    if (auto p = config.static_path.string(); starts_with(p, "/") || starts_with(p, "./") || starts_with(p, "../")) {
        base_dir = config.static_path;
    } else {
        base_dir = exec_dir / config.static_path;
        static_path_prefix = "<executable path>/";
    }
    log.info("static path: {}{}", static_path_prefix, config.static_path.string());

#if 1
    server.Get("(.*)", [&](const auto& req, auto& res) {
        std::string path = req.matches[1];
        // if(verbose)
            // std::cout << "GET " << path << std::endl;
        if(verbose)
            log.trace("resolving path: {}", path);
        // if(path == "/")
        //     path = "/index.html";

        fs::path fspath = base_dir / strip_prefix(path, '/');

        // log.trace("checking path: {}", fspath.string());

        if(::is_directory(fspath)) {
            if(!path.empty() && path.back() != '/') {
                res.set_redirect(path + "/", 307);
                return;
            }
            if(::is_file(fspath / "index.html"))
                fspath = fspath / "index.html";
            else {
                // TODO: serve filelist
            }
        }
        if(::is_file(fspath)) {
            std::string ext = fspath.extension().string();
            log.trace("serving host file: {}", fspath.string());
            // if(verbose)
            //     std::cerr << "serving disk file: " << fspath.string() << std::endl;// " of size " << entry.size << std::endl;
            std::string content = readFile(fspath.string());
            res.set_content(content, getMIMEType(ext));
            return;
        }

        fspath = path;

        if(vfs.is_directory(fspath)) {
            if(!path.empty() && path.back() != '/') {
                res.set_redirect(path + "/", 307);
                return;
            }
            if(vfs.is_file(fspath / "index.html"))
                fspath = (fspath / "index.html");
        }

        if(vfs.is_file(fspath)) {
            const auto& entry = vfs[fspath.string()];
            log.trace("serving embedded file: {} ({})", fspath.string() /* entry.name */, human_readable_size(entry.size));
            // if(verbose)
            //     std::cerr << "serving embedded file: " << entry.name << " of size " << entry.size << std::endl;
            std::string ext = fspath.extension().string();
            res.set_content(std::string(entry.content, entry.size), getMIMEType(ext));
            return;
        }

        res.status = 404;
        return;
    });
#else
    auto ret = server.set_mount_point("/", config.static_path);
    if (!ret) {
        // The specified base directory doesn't exist...
    }
#endif

    const std::string internal_req_start_ms_header = "INTERNAL-start_ms_since_epoch";

    server.set_post_routing_handler([&](const auto& req, auto& res) {
        // for (const auto& pair : res.headers) { std::cout << "Key: " << pair.first << " -> Value: " << pair.second << std::endl; }
        // res.set_header("ADDITIONAL_HEADER", "value");
        // log.debug("[{}:{}] [req] {} {} {}", req.remote_addr, req.remote_port, req.method, req.version, req.target);

        std::string duration_str;

        auto end = std::chrono::high_resolution_clock::now();
        int64_t start_ms = 0;
        if (req.has_header(internal_req_start_ms_header)) {
            start_ms = std::stoll(req.get_header_value(internal_req_start_ms_header));
            // auto mres = const_cast<decltype(res)>(res);
            // mres.headers.erase(internal_req_start_ms_header);
        }
        if (start_ms > 0) {
            auto start = std::chrono::time_point<std::chrono::high_resolution_clock>(std::chrono::milliseconds(start_ms));
            // std::chrono::duration<double> duration = end - start_time_point;
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            int64_t duration_ms = duration.count();
            duration_str = std::to_string(duration_ms) + " ms";
            res.set_header("Processing-Time", duration_str);
        }
    });
    server.set_pre_routing_handler([&](const auto& req, auto& res) {
        log.debug("[{}:{}] [req] {} {}", req.remote_addr, req.remote_port, req.method, req.target);
        // if (req.path == "/hello") {
        //     res.set_content("world", "text/html");
        //     return Server::HandlerResponse::Handled;
        // }
        auto start = std::chrono::high_resolution_clock::now();
        int64_t start_ms = std::chrono::duration_cast<std::chrono::milliseconds>(start.time_since_epoch()).count();
        // auto& mreq = const_cast<std::remove_const_t<decltype(req)>&>(req);
        Request& mreq = const_cast<Request&>(req);
        mreq.set_header(internal_req_start_ms_header, std::to_string(start_ms));

        if (config.add_cors_headers) {
            // res.set_header("Access-Control-Request-Headers", "Content-Type");
            // res.set_header("Access-Control-Allow-Headers", "Content-Type");
            // res.set_header("Access-Control-Allow-Origin", "*");
            // res.set_header("Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
            // res.set_header("Access-Control-Allow-Credentials", "true");

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Allow", "GET, POST, HEAD, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "X-Requested-With, Content-Type, Accept, Origin, Authorization");
            res.set_header("Access-Control-Allow-Methods", "OPTIONS, GET, POST, HEAD");
        }

        return Server::HandlerResponse::Unhandled;
    });

    // server.set_logger([](const auto& req, const auto& res) { cout << http_log(req, res); });
    // server.set_logger([](const auto& req, const auto& res) { spdlog::debug("{}", http_log(req, res)); });
    server.set_logger([&](const auto& req, const auto& res) {
        // log.debug("[{}:{}] [res] {} {} {} - {} {}", req.remote_addr, req.remote_port, req.method, req.version, req.target, res.status, status_message(res.status));
        // for (const auto& pair : req.headers) { std::cout << "Key: " << pair.first << " -> Value: " << pair.second << std::endl; }
        std::string duration_str;

        auto end = std::chrono::high_resolution_clock::now();
        int64_t start_ms = 0;
        if (req.has_header(internal_req_start_ms_header)) {
            start_ms = std::stoll(req.get_header_value(internal_req_start_ms_header));
            // auto mres = const_cast<decltype(res)>(res);
            // mres.headers.erase(internal_req_start_ms_header);
        }
        if (start_ms > 0) {
            auto start = std::chrono::time_point<std::chrono::high_resolution_clock>(std::chrono::milliseconds(start_ms));
            // std::chrono::duration<double> duration = end - start_time_point;
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            int64_t duration_ms = duration.count();
            duration_str = " in " + std::to_string(duration_ms) + " ms";
        }

        log.debug("[{}:{}] [res] {} {} - {} {} ({} bytes){}", req.remote_addr, req.remote_port, req.method, req.target, res.status, status_message(res.status),
                res.body.empty() ? res.get_header_value("Content-Length") : std::to_string(res.body.size()), duration_str);
    });

    if (config.limit_whisper_input_s > 0) {
        size_t payload_limit = 10 * 1024 * 1024; // 10 MB minimum
        payload_limit = std::max(payload_limit, (size_t)(1 * 1024 * 1024 /* 1MB extra */ + 2 /* 16bit */ * 16000 /* Hz */ * config.limit_whisper_input_s /* limit to N seconds */));
        server.set_payload_max_length(payload_limit);
        log.info("limiting upload payload to max {} MB", payload_limit / (1024 * 1024));
    } else {
        size_t payload_limit = 1024 * 1024 * 1024;
        server.set_payload_max_length(payload_limit);
        log.info("limiting upload payload to max {} MB", payload_limit / (1024 * 1024));
    }

    log.info("running server on port {}", config.port);

    server.listen("0.0.0.0", config.port);
}

std::string resolve_path(fs::path prefix, std::string path) {
    if(path.size() >= 1 && path.compare(0, 1, "/") == 0)
        return path;
    if(path.size() >= 2 && path.compare(0, 2, "./") == 0)
        return path;
    if(path.size() >= 3 && path.compare(0, 3, "../") == 0)
        return path;
    return prefix / path;
}




int main(int argc, char* argv[])
{
    // std::signal(SIGSEGV, signalHandler);
    // std::signal(SIGFPE, signalHandler);
    // std::signal(SIGABRT, signalHandler);
    // std::signal(SIGILL, signalHandler);
    // std::signal(SIGBUS, signalHandler);

    auto log = new_logger("main");

    using namespace popl;
    using namespace std;

    auto logger = spdlog::stdout_color_mt("main");
    auto err_logger = spdlog::stderr_color_mt("main-err");
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [thread %t] [%l] %v");
    err_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [thread %t] [%l] %v");

    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);

    char result[PATH_MAX];
    realpath(argv[0], result);
    exec_dir = dirname(result);

    ServerConfig config;

    string models = "models";
    string whisper_model_path;

    OptionParser op("options");
    auto help_option = op.add<Switch>("h", "help", "help message");
    auto verbose_option = op.add<Switch>("v", "verbose", "enable verbose mode");
    auto port_option = op.add<Value<int>>("p", "port", "listen port", config.port, &config.port);
    auto models_option = op.add<Value<string>>("m", "models", "path to directory with models", models, &models);
    auto whisper_option = op.add<Value<string>>("w", "whisper", "whisper model (GGML format)", whisper_model_path, &whisper_model_path);
    auto whisper_dtw_option = op.add<Value<string>>("", "dtw",
            "whisper DTW model type (tiny.en, base, base.en, small, small.en, medium, medium.en, large.v1, large.v2, large.v3)", config.whisper_dtw, &config.whisper_dtw);
    auto static_option = op.add<Value<fs::path>>("s", "static", "path to static directory", config.static_path, &config.static_path);
    auto limit_whisper_option = op.add<Value<int>>("l", "limit", "limit whisper input duration in seconds", config.limit_whisper_input_s, &config.limit_whisper_input_s);
    auto vad_option = op.add<Value<fs::path>>("", "vad", "VAD model (Silero VAD onnx)", config.vad_model_path, &config.vad_model_path);
    auto vad_trim_range_option = op.add<Value<int>>("V", "trim", "VAD trim range in seconds", config.vad_trim_range_s, &config.vad_trim_range_s);
    auto cpu_option = op.add<Implicit<string>>("", "cpu", "CPU only (no MPS/CUDA) for specified engines (whisper or all), if no argument passed, then 'all' is inferred", "all");
    auto gpu_option = op.add<Implicit<string>>("", "gpu", "set GPU device for each engine (default is 0 - first GPU), e.g., whisper:0", "all:0");
    auto device_option = op.add<Value<string>>("d", "device", "set device (CPU/GPU[#X]) for each engine, e.g., whisper:cpu", "all:cpu");
    auto parallel_option = op.add<Value<int>>("P", "parallel", "number of parallel whisper processor instances", config.max_whisper_instances, &config.max_whisper_instances);
    auto no_vad_option = op.add<Switch>("", "no-vad", "disable VAD");
    auto cors_option = op.add<Switch>("", "cors", "add permissive CORS headers");
    auto extract_option = op.add<Value<fs::path>, Attribute::hidden>("", "extract", "extract embedded static data to specified path");


    engineDeviceConf.add(Engines::Whisper, 0, "whisper", {"w", "asr"});

#ifdef USE_CUDA
    fs::path cudaLibPath = exec_dir / "liblate-cuda.so";
    auto cudalib_option = op.add<Value<fs::path>>("", "cuda-lib", "path to LATE CUDA dynamic library", cudaLibPath, &cudaLibPath);

    void *cudaLibHandle = nullptr;
#endif

    try {
        op.parse(argc, argv);

        verbose = verbose_option->is_set();
        config.cpu_only = cpu_option->is_set();
        config.add_cors_headers = cors_option->is_set();

        if(help_option->is_set()) {
            cerr << argv[0] << " [options]" << endl;
            cerr << endl;
            cerr << op << endl;
            return 0;
        }

        if(verbose)
            spdlog::set_level(spdlog::level::debug);

        if (extract_option->is_set()) {
            fs::path target_path = extract_option->value();

            log.info("extracting VFS to {}", target_path.string());

            VFS vfs(EMBEDDED_DATA_BUFFER(_vfs_static));
            auto summary = vfs.summary();
            log.debug("VFS summary: {} in {} files and {} directories (compressed {})", human_readable_size(summary.total_size),
                    summary.file_count, summary.dir_count, human_readable_size(summary.compressed_size));

            auto list = vfs.list();
            for (auto& p : list) {
                auto& entry = vfs[p];

                auto out_path = target_path / strip_prefix(p, '/');

                fs::create_directories(out_path.parent_path());

                log.info("writing file: {}", out_path.string());

                std::ofstream outfile(out_path, std::ios::binary);
                if (!outfile) {
                    log.error("unable to create file {}", out_path.string());
                    continue;
                }
                outfile.write(entry.content, entry.size);
                if (!outfile)
                    log.error("error writing to file {}", out_path.string());
            }
            return 0;
        }

        try {
            if (cpu_option->is_set()) {
                if (cpu_option->value().empty())
                    engineDeviceConf.apply("all", -1, EngineDeviceConfigurations::ImplicitOverride::NotAllowed);
                else
                    engineDeviceConf.apply(cpu_option->value(), -1, EngineDeviceConfigurations::ImplicitOverride::NotAllowed);
            }
            if (gpu_option->is_set()) {
                if (gpu_option->value().empty())
                    engineDeviceConf.apply("all", 0, EngineDeviceConfigurations::ImplicitOverride::Allowed);
                else
                    engineDeviceConf.apply(gpu_option->value(), 0, EngineDeviceConfigurations::ImplicitOverride::Allowed);
            }
            if (device_option->is_set()) {
                engineDeviceConf.apply(device_option->value(), 0, EngineDeviceConfigurations::ImplicitOverride::Required);
            }
        } catch(const std::invalid_argument& e) {
            log.error("error parsing engine device settings: {}", e.what());
            return -1;
        }

#ifdef USE_CUDA
        cudaLibHandle = dlopen(cudaLibPath.c_str(), RTLD_LAZY | RTLD_GLOBAL);
        if (!cudaLibHandle) {
            log.warn("CUDA library not loaded: {}", dlerror());
        } else {
            log.info("CUDA library loaded");
            if (!load_ggml_cuda_backend_symbols(cudaLibHandle)) {
                log.warn("unable to load ggml CUDA backend symbols: {}", dlerror());
            }
        }
#endif

        log.info("initial engine device configuration:");
        for (auto engine : engineDeviceConf) {
            log.info("- {}: {}", engine.name, engine.deviceString());
        }

        log.info("starting");

        if (no_vad_option->is_set())
            config.vad_model_path.clear();

        if(port_option->is_set()) {
            log.debug("port set to {}", config.port);
            // spdlog::debug("port set to {}", port);
        }
        config.whisper_model_path = resolve_path(models, whisper_model_path);
        if (!config.vad_model_path.empty())
            config.vad_model_path = resolve_path(models, config.vad_model_path);

        // resolve whisper model
        if(!(fs::is_regular_file(config.whisper_model_path) || (fs::is_symlink(config.whisper_model_path) && fs::is_regular_file(fs::read_symlink(config.whisper_model_path))))) {
            for (const auto &entry : fs::directory_iterator(models)) {
                if ((entry.is_regular_file() || (entry.is_symlink() && fs::is_regular_file(fs::read_symlink(entry.path()))))
                        // && entry.path().filename().string().compare(0, 7, "whisper") == 0
                        && (entry.path().extension() == ".bin" || entry.path().extension() == ".ggml" || entry.path().extension() == "")
                        && (entry.path().filename().string().find("whisper") != std::string::npos)
                        && (entry.path().filename().string().find("ggml") != std::string::npos)
                        && fs::file_size(entry.path()) >= 1024*1024*1024 /* 1GB */) {
                    config.whisper_model_path = entry.path().string();
                    break;
                }
            }
        }

        log.info("Whisper model {}", config.whisper_model_path.string());
        if (config.vad_model_path.empty())
            log.info("VAD is disabled");
        else
            log.info("VAD model {}", config.vad_model_path.string());

    } catch(const popl::invalid_option& e) {
        cerr << "invalid option: " << e.what() << endl;
        return EXIT_FAILURE;
    } catch(const std::exception& e) {
        log.error("exception: {}", e.what());
        // cerr << "Exception: " << e.what() << endl;
        return EXIT_FAILURE;
    }

    runServer(log, config);

#ifdef USE_CUDA
    if(cudaLibHandle)
        dlclose(cudaLibHandle);
#endif

    return 0;
}
