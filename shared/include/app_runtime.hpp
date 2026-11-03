#pragma once
#include "core.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
namespace video_link {
    inline volatile std::sig_atomic_t interrupted = 0;
    inline void on_signal(int) {
        interrupted = 1;
    }
    class RunControl {
        sl::Clock::time_point start = sl::Clock::now(), next_poll = start;
        double seconds;
        std::string stop_file;
        bool stopped = false;

      public:
        RunControl(double duration, std::string file)
            : seconds(duration), stop_file(std::move(file)) {
            std::signal(SIGINT, on_signal);
            std::signal(SIGTERM, on_signal);
        }
        bool running() {
            auto now = sl::Clock::now();
            if (interrupted || stopped)
                return false;
            if (seconds > 0 && std::chrono::duration<double>(now - start).count() >= seconds)
                return false;
            if (now >= next_poll) {
                next_poll = now + std::chrono::milliseconds(200);
                if (!stop_file.empty() &&
                    std::filesystem::exists(std::filesystem::u8path(stop_file)))
                    stopped = true;
            }
            return !stopped;
        }
    };
    struct Counters {
        uint64_t bytes = 0, packets = 0, bad = 0, duplicates = 0, previous_bytes = 0,
                 last = sl::micros();
        void report(uint64_t radio_errors) {
            auto now = sl::micros();
            if (now - last < 1000000)
                return;
            std::cout << "payload_mbps=" << (bytes - previous_bytes) * 8.0 / double(now - last)
                      << " packets=" << packets << " bytes=" << bytes << " invalid=" << bad
                      << " duplicates=" << duplicates << " radio_errors=" << radio_errors
                      << std::endl;
            previous_bytes = bytes;
            last = now;
        }
    };
} // namespace video_link
