// Optional hardware bridge. Hardware operation requires lab validation.
#include "duplex.hpp"
#include "modem.hpp"
#include "net.hpp"
#include <csignal>
#include <iostream>
#include <map>
#include <thread>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
using namespace sl;
static volatile std::sig_atomic_t active = 1;
static void stop_radio(int) {
    active = 0;
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) {
            std::cout
                << "streamlink_usrp probe|tx|rx|duplex --args addr=192.168.10.2 --rate 5e6 --freq "
                   "<Hz> --gain 0 --seconds 60 [--antenna name] [--listen 127.0.0.1:9001] "
                   "[--to 127.0.0.1:9002]\nTX/RX require an explicit frequency. QPSK SISO "
                   "no-FEC experimental waveform.\n";
            return 0;
        }
        std::string mode = argv[1];
        if (mode != "probe" && mode != "tx" && mode != "rx" && mode != "duplex")
            throw std::runtime_error("unknown command");
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i++) {
            std::string k = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("option missing value");
            if (k != "--args" && k != "--rate" && k != "--freq" && k != "--gain" &&
                k != "--seconds" && k != "--antenna" && k != "--listen" && k != "--to" &&
                k != "--tx-antenna" && k != "--rx-antenna" && k != "--tx-gain" && k != "--rx-gain")
                throw std::runtime_error("unknown option " + k);
            options[k] = argv[++i];
        }
        auto option = [&](std::string k, std::string fallback) {
            auto it = options.find(k);
            return it == options.end() ? fallback : it->second;
        };
        Network network;
        std::signal(SIGINT, stop_radio);
        auto radio = uhd::usrp::multi_usrp::make(option("--args", "addr=192.168.10.2"));
        std::cout << radio->get_pp_string() << std::endl;
        if (mode == "probe")
            return 0;
        if (!options.count("--freq"))
            throw std::runtime_error(
                "set --freq explicitly after checking daughterboard and test setup");
        double rate = std::stod(option("--rate", "5e6")), freq = std::stod(options["--freq"]),
               gain = std::stod(option("--gain", "0")),
               seconds = std::stod(option("--seconds", "60"));
        if (rate <= 0 || freq <= 0 || seconds <= 0)
            throw std::runtime_error("rate/frequency/duration must be positive");
        uhd::stream_args_t stream_args("fc32", "sc16");
        stream_args.channels = {0};
        std::string antenna = option("--antenna", "");
        const bool use_tx = mode == "tx" || mode == "duplex";
        const bool use_rx = mode == "rx" || mode == "duplex";
        uhd::tx_streamer::sptr tx_stream;
        uhd::rx_streamer::sptr rx_stream;
        std::atomic<bool> stop{false};
        // All device configuration is completed before workers start.
        if (use_tx) {
            radio->set_tx_rate(rate, 0);
            radio->set_tx_freq(uhd::tune_request_t(freq), 0);
            radio->set_tx_gain(std::stod(option("--tx-gain", std::to_string(gain))), 0);
            auto tx_antenna = option("--tx-antenna", antenna);
            if (!tx_antenna.empty())
                radio->set_tx_antenna(tx_antenna, 0);
            std::cout << "actual_rate=" << radio->get_tx_rate(0)
                      << " actual_freq=" << radio->get_tx_freq(0)
                      << " frontend=" << radio->get_tx_subdev_name(0) << std::endl;

            tx_stream = radio->get_tx_stream(stream_args);
        }
        if (use_rx) {
            radio->set_rx_rate(rate, 0);
            radio->set_rx_freq(uhd::tune_request_t(freq), 0);
            radio->set_rx_gain(std::stod(option("--rx-gain", std::to_string(gain))), 0);
            auto rx_antenna = option("--rx-antenna", antenna);
            if (!rx_antenna.empty())
                radio->set_rx_antenna(rx_antenna, 0);
            std::cout << "actual_rate=" << radio->get_rx_rate(0)
                      << " actual_freq=" << radio->get_rx_freq(0)
                      << " frontend=" << radio->get_rx_subdev_name(0) << std::endl;

            rx_stream = radio->get_rx_stream(stream_args);
        }
        if (use_tx && use_rx && std::abs(radio->get_tx_rate(0) - radio->get_rx_rate(0)) > 1)
            throw std::runtime_error("TX/RX actual sample rates differ");
        auto tx_work = [&] {
            auto stream = tx_stream;
            Socket input;
            input.bind(option("--listen", "127.0.0.1:9001"));
            std::array<uint8_t, 65536> bytes{};
            std::vector<phy::C> zeros(phy::radio_samples);
            uhd::tx_metadata_t md;
            md.start_of_burst = true;
            md.end_of_burst = false;
            md.has_time_spec = false;
            uint64_t end = micros() + uint64_t(seconds * 1e6), last = micros(), packets = 0,
                     errors = 0;
            while (active && !stop.load() && micros() < end) {
                int n = input.receive(bytes.data(), bytes.size(), 0);
                std::vector<phy::C> wave;
                Packet p;
                if (n && unpack(bytes.data(), n, p)) {
                    wave = phy::radio_encode(Bytes(bytes.begin(), bytes.begin() + n));
                    packets++;
                } else if (n)
                    errors++;
                const auto &iq = wave.empty() ? zeros : wave;
                size_t offset = 0;
                while (offset < iq.size()) {
                    size_t sent = stream->send(iq.data() + offset, iq.size() - offset, md, 1.0);
                    if (!sent)
                        throw std::runtime_error("UHD TX timeout");
                    offset += sent;
                    md.start_of_burst = false;
                }
                uhd::async_metadata_t event;
                while (stream->recv_async_msg(event, 0.0)) {
                    if (event.event_code != uhd::async_metadata_t::EVENT_CODE_BURST_ACK)
                        errors++;
                }
                if (micros() - last > 1000000) {
                    std::cout << "tx_packets=" << packets << " errors_or_async_events=" << errors
                              << std::endl;
                    last = micros();
                }
            }
            md.end_of_burst = true;
            stream->send(zeros.data(), 0, md, 1.0);
        };
        auto rx_work = [&] {
            auto stream = rx_stream;
            Socket output;
            auto dst = address(option("--to", "127.0.0.1:9002"));
            std::vector<phy::C> iq(32768);
            phy::RadioDecoder decoder;

            uint64_t end = micros() + uint64_t(seconds * 1e6), last = micros(), overflows = 0,
                     timeouts = 0;
            try {
                while (active && !stop.load() && micros() < end) {
                    uhd::rx_metadata_t md;
                    size_t n = stream->recv(iq.data(), iq.size(), md, 0.2);
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                        timeouts++;
                        continue;
                    }
                    if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                        overflows++;
                        decoder.reset();
                        continue;
                    }
                    if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
                        throw std::runtime_error(md.strerror());
                    for (auto &packet : decoder.push(iq.data(), n))
                        output.send(packet, dst);
                    if (micros() - last > 1000000) {
                        std::cout << "decoded=" << decoder.decoded
                                  << " rejected=" << decoder.rejected << " overflow=" << overflows
                                  << " timeout=" << timeouts << std::endl;
                        last = micros();
                    }
                }
            } catch (...) {
                stream->issue_stream_cmd(
                    uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
                throw;
            }
            stream->issue_stream_cmd(
                uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
        };
        // Start receiver first; ONE multi_usrp owns the device in duplex mode.
        if (use_rx) {
            uhd::stream_cmd_t start(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
            start.stream_now = true;
            rx_stream->issue_stream_cmd(start);
            std::cout << "RX_READY" << std::endl;
        }
        try {
            if (use_tx && use_rx)
                run_duplex(tx_work, rx_work, stop);
            else if (use_tx)
                tx_work();
            else
                rx_work();
        } catch (...) {
            if (use_rx) {
                try {
                    rx_stream->issue_stream_cmd(
                        uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
                } catch (...) {
                }
            }
            throw;
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
