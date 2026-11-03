#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <shellapi.h>
#endif
namespace video_link {
    // Use UTF-8 consistently, including when the checkout/config path contains Chinese.
    class NativeArguments {
        std::vector<std::string> text;
        std::vector<char *> pointers;

      public:
        NativeArguments(int argc, char **argv) {
#ifdef _WIN32
            (void)argv;
            wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &argc);
            if (!wide)
                throw std::runtime_error("Cannot read command line");
            for (int i = 0; i < argc; i++) {
                int n = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
                std::string value(size_t(n), '\0');
                WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, value.data(), n, nullptr, nullptr);
                value.pop_back();
                text.push_back(std::move(value));
            }
            LocalFree(wide);
#else
            for (int i = 0; i < argc; i++)
                text.emplace_back(argv[i]);
#endif
            for (auto &value : text)
                pointers.push_back(value.data());
        }
        int count() const {
            return int(pointers.size());
        }
        char **data() {
            return pointers.data();
        }
    };
} // namespace video_link
