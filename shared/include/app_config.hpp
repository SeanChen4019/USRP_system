#pragma once
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace video_link {
    // UTF-8 key=value configuration. Unknown/duplicate keys fail instead of being ignored.
    class Config {
        std::map<std::string, std::string> values;
        static std::string trim(const std::string &s) {
            auto first = s.find_first_not_of(" \t\r\n");
            return first == std::string::npos
                       ? ""
                       : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
        }

      public:
        explicit Config(const std::string &file) {
            std::ifstream in(std::filesystem::u8path(file));
            if (!in)
                throw std::runtime_error("Cannot open config: " + file);
            std::string line;
            int number = 0;
            while (std::getline(in, line)) {
                if (++number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0)
                    line.erase(0, 3);
                line = trim(line);
                if (line.empty() || line[0] == '#')
                    continue;
                auto pos = line.find('=');
                if (pos == std::string::npos)
                    throw std::runtime_error("Expected key=value at line " +
                                             std::to_string(number));
                if (!values.emplace(trim(line.substr(0, pos)), trim(line.substr(pos + 1))).second)
                    throw std::runtime_error("Duplicate config key at line " +
                                             std::to_string(number));
            }
        }
        std::string get(const std::string &key, const std::string &fallback = "") const {
            auto it = values.find(key);
            return it == values.end() ? fallback : it->second;
        }
        double number(const std::string &key, double fallback) const {
            auto s = get(key, std::to_string(fallback));
            size_t end = 0;
            double n = std::stod(s, &end);
            if (end != s.size() || !std::isfinite(n))
                throw std::runtime_error("Invalid number: " + key);
            return n;
        }
        void validate(bool tx) const {
            std::set<std::string> known = {
                "backend", "device_args", "frequency_hz",     "sample_rate", "gain_db",
                "antenna", "channel",     "duration_seconds", "video_port",  "test_link_address",
                "uhd_bin", "ffmpeg",      "ffplay",           "camera_name", "width",
                "height",  "fps",         "bitrate_mbps"};
            for (auto &item : values)
                if (!known.count(item.first))
                    throw std::runtime_error("Unknown config key: " + item.first);
            auto backend = get("backend");
            if (backend != "usrp" && backend != "udp_test")
                throw std::runtime_error("backend must be usrp or udp_test");
            if (backend == "usrp" && number("frequency_hz", 0) <= 0)
                throw std::runtime_error(
                    "Set frequency_hz to a supported test frequency in config.ini");
            if (backend == "usrp" && (get("antenna").empty() || get("device_args").empty()))
                throw std::runtime_error(
                    "Set antenna and device_args using your USRP probe result");
            if (number("sample_rate", 0) <= 0)
                throw std::runtime_error("sample_rate must be positive");
            number("gain_db", 0);
            auto integer = [&](const std::string &key, double fallback, double lo, double hi) {
                double n = number(key, fallback);
                if (n < lo || n > hi || std::floor(n) != n)
                    throw std::runtime_error("Invalid integer: " + key);
            };
            integer("channel", 0, 0, 63);
            integer("video_port", tx ? 5600 : 5601, 1, 65535);
            double seconds = number("duration_seconds", 0);
            if (seconds < 0 || seconds > 86400)
                throw std::runtime_error("duration_seconds must be 0..86400 (0 = until stopped)");
            if (tx) {
                integer("width", 1280, 16, 7680);
                integer("height", 720, 16, 4320);
                integer("fps", 30, 1, 120);
                if (int(number("width", 1280)) % 2 || int(number("height", 720)) % 2)
                    throw std::runtime_error("width and height must be even");
                double mbps = number("bitrate_mbps", 3);
                if (mbps <= 0)
                    throw std::runtime_error("bitrate_mbps must be positive");
                // Fixed QPSK frame capacity with a 25% margin for media overhead and processing.
                if (backend == "usrp" &&
                    mbps > number("sample_rate", 0) * 1316 * 8 / 9856 / 1e6 * .75)
                    throw std::runtime_error("Video bitrate exceeds current QPSK budget; lower "
                                             "bitrate_mbps or raise sample_rate");
            }
        }
    };
} // namespace video_link
