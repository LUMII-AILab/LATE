#pragma once

#include <string>
#include <unordered_map>
#include <thread>
#include <mutex>

#include <unistd.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>


class ProcessThreadCounter {
public:
    static int processNumber() {
        static std::unordered_map<pid_t, int> pids;
        static int nextId = 1;
        static std::mutex mutex;

        std::lock_guard<std::mutex> lock(mutex);
        pid_t thisId = getpid();

        auto it = pids.find(thisId);
        if (it == pids.end()) {
            pids[thisId] = nextId++;
            return pids[thisId];
        }
        return it->second;
    }

    static int threadNumber() {
        static std::unordered_map<std::thread::id, int> threadIds;
        static int nextId = 1;
        static std::mutex mutex;

        std::lock_guard<std::mutex> lock(mutex);
        std::thread::id thisId = std::this_thread::get_id();

        auto it = threadIds.find(thisId);
        if (it == threadIds.end()) {
            threadIds[thisId] = nextId++;
            return threadIds[thisId];
        }
        return it->second;
    }
};


class short_proc_thread_formatter_flag : public spdlog::custom_flag_formatter
{
public:
    enum class Mode {
        Thread,
        Process,
    };

    short_proc_thread_formatter_flag(Mode mode = Mode::Thread) : mode(mode) { }

    void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
        static const char* space = " ";
        if (mode == Mode::Thread) {
            std::string s = std::to_string(ProcessThreadCounter::threadNumber());
            if (s.size() == 1)
                dest.append(space, space + 1);
            dest.append(s.data(), s.data() + s.size());
        } else if (mode == Mode::Process) {
            std::string s = std::to_string(ProcessThreadCounter::processNumber());
            dest.append(s.data(), s.data() + s.size());
        }
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<short_proc_thread_formatter_flag>(mode);
    }

private:
    Mode mode;
};



const std::string log_pattern = "[%Y-%m-%d %H:%M:%S.%e] [%@:%*] [%^%l%$] [%n] %v";

// typedef std::shared_ptr<spdlog::logger> logger;
typedef spdlog::logger logger;

namespace level = spdlog::level;

class new_logger : public logger {
public:
    new_logger(const std::string& name, spdlog::level::level_enum level = /* spdlog:: */level::trace) :
        spdlog::logger(name, std::make_shared<spdlog::sinks::stdout_color_sink_mt>()) {

        auto formatter = std::make_unique<spdlog::pattern_formatter>();
        formatter->add_flag<short_proc_thread_formatter_flag>('*', short_proc_thread_formatter_flag::Mode::Thread);
        formatter->add_flag<short_proc_thread_formatter_flag>('@', short_proc_thread_formatter_flag::Mode::Process);
        formatter->set_pattern(log_pattern);

        set_formatter(std::move(formatter));
        set_level(level);
    }
};

// logger new_logger(const std::string& name, spdlog::level::level_enum level = [# spdlog:: #]level::trace) {
//
//     auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
//
//     spdlog::logger logger(name, console_sink);
//
//     auto formatter = std::make_unique<spdlog::pattern_formatter>();
//     formatter->add_flag<short_proc_thread_formatter_flag>('*', short_proc_thread_formatter_flag::Mode::Thread);
//     formatter->add_flag<short_proc_thread_formatter_flag>('@', short_proc_thread_formatter_flag::Mode::Process);
//     formatter->set_pattern(log_pattern);
//
//     logger.set_formatter(std::move(formatter));
//     logger.set_level(level);
//
//     return logger;
// }
