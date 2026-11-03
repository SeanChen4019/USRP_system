#include "entrypoint.hpp"
#include "radio_device.hpp"
#include "receiver_app.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        video_link::NativeArguments arguments(argc, argv);
        argc = arguments.count();
        argv = arguments.data();
        if (argc < 2) {
            std::cout << "video_receiver <config.ini> [--check | --stop-file path]\n";
            return 0;
        }
        video_link::Config config(argv[1]);
        config.validate(false);
        if (argc == 3 && std::string(argv[2]) == "--check") {
            if (config.get("backend") == "usrp" && !video_link::has_uhd())
                throw std::runtime_error("UHD not compiled; run build-windows.ps1 -WithUhd");
            std::cout << "RX config OK; no hardware opened\n";
            return 0;
        }
        if (argc != 2 && (argc != 4 || std::string(argv[2]) != "--stop-file"))
            throw std::runtime_error("Invalid arguments");
        return run_receiver(config, argc == 4 ? argv[3] : "");
    } catch (const std::exception &e) {
        std::cerr << "RX ERROR: " << e.what() << std::endl;
        return 1;
    }
}
