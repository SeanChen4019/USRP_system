#include "receiver_app.hpp"
#include "app_runtime.hpp"
#include "net.hpp"
#include "radio_device.hpp"

int run_receiver(const video_link::Config &config, const std::string &stop_file) {
    using namespace sl;
    using namespace video_link;
    Network network;
    Socket player_output, test_input;
    bool test = config.get("backend") == "udp_test";
    if (test)
        test_input.bind(config.get("test_link_address", "127.0.0.1:19001"));
    auto player = address("127.0.0.1:" + config.get("video_port", "5601"));
    auto radio = test ? nullptr : open_receiver(config);
    std::cout << (test ? "BACKEND=UDP_TEST (NO RADIO)" : "BACKEND=USRP (VIDEO COMES FROM RF)")
              << std::endl;
    RunControl control(config.number("duration_seconds", 0), stop_file);
    Counters stats;
    phy::RadioDecoder modem;
    Dedup dedup;
    uint32_t session = 0;
    bool session_locked = false;
    std::vector<phy::C> iq(32768);
    std::array<uint8_t, 65536> datagram{};
    auto deliver = [&](const uint8_t *data, size_t length) {
        // CRC and duplicate rejection happen before forwarding to the video player.
        Packet packet;
        if (!unpack(data, length, packet)) {
            stats.bad++;
            return;
        }
        if (!session_locked) {
            session = packet.session;
            session_locked = true;
        }
        if (packet.session != session) {
            stats.bad++;
            return;
        }
        if (!dedup.accept(packet.seq)) {
            stats.duplicates++;
            return;
        }
        player_output.send(packet.payload, player);
        stats.bytes += packet.payload.size();
        stats.packets++;
    };
    std::cout << "RX_READY: video output on localhost UDP " << config.get("video_port", "5601")
              << std::endl;
    while (control.running()) {
        if (radio) {
            // 1. USRP IQ -> acquisition/CFO/equalization -> decoded wire packets.
            bool discontinuity = false;
            auto count = radio->receive(iq, discontinuity);
            if (discontinuity)
                modem.reset();
            if (count)
                for (const auto &packet : modem.push(iq.data(), count))
                    deliver(packet.data(), packet.size());
        } else {
            int count = test_input.receive(datagram.data(), datagram.size(), 20);
            if (count)
                deliver(datagram.data(), size_t(count));
        }
        stats.report(radio ? radio->errors() : 0);
    }
    return 0;
}
