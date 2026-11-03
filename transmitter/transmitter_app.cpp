#include "transmitter_app.hpp"
#include "app_runtime.hpp"
#include "net.hpp"
#include "radio_device.hpp"
#include <random>

int run_transmitter(const video_link::Config &config, const std::string &stop_file) {
    using namespace sl;
    using namespace video_link;
    Network network;
    Socket camera_input, test_output;
    camera_input.bind("127.0.0.1:" + config.get("video_port", "5600"));
    bool test = config.get("backend") == "udp_test";
    auto test_address = address(config.get("test_link_address", "127.0.0.1:19001"));
    auto radio = test ? nullptr : open_transmitter(config);
    std::cout << (test ? "BACKEND=UDP_TEST (NO RADIO)" : "BACKEND=USRP (VIDEO GOES THROUGH RF)")
              << std::endl;
    RunControl control(config.number("duration_seconds", 0), stop_file);
    Counters stats;
    std::random_device random;
    uint32_t session = random();
    uint64_t sequence = 0;
    std::array<uint8_t, 65536> data{};
    std::vector<phy::C> silence(phy::radio_samples);
    std::cout << "TX_READY: waiting for encoded camera video on localhost UDP "
              << config.get("video_port", "5600") << std::endl;
    while (control.running()) {
        int length = camera_input.receive(data.data(), data.size(), test ? 20 : 0);
        if (length > 0 && length <= int(max_payload)) {
            // 1. Wrap one MPEG-TS datagram with session, sequence and CRC.
            Packet packet{session, sequence++, micros(),
                          Bytes(data.begin(), data.begin() + length)};
            auto wire = pack(packet);
            // 2. Production path: OFDM waveform -> UHD -> USRP TX.
            if (radio)
                radio->send(phy::radio_encode(wire));
            else
                test_output.send(wire, test_address);
            stats.bytes += length;
            stats.packets++;
        } else {
            if (length > int(max_payload))
                stats.bad++;
            // Keep the sample stream continuous between video packets.
            if (radio)
                radio->send(silence);
        }
        stats.report(radio ? radio->errors() : 0);
    }
    return 0;
}
