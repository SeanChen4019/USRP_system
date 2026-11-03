#include "radio_device.hpp"
#include <iostream>
#ifdef STREAMLINK_HAS_UHD
#include <uhd/types/stream_cmd.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/usrp/multi_usrp.hpp>
namespace video_link {
    namespace {
        uhd::usrp::multi_usrp::sptr configure(const Config &c, bool tx) {
            auto radio = uhd::usrp::multi_usrp::make(c.get("device_args"));
            radio->set_clock_source("internal");
            radio->set_time_source("internal");
            size_t ch = size_t(c.number("channel", 0));
            double fs = c.number("sample_rate", 10e6), f = c.number("frequency_hz", 0),
                   gain = c.number("gain_db", 0);
            if (ch >= (tx ? radio->get_tx_num_channels() : radio->get_rx_num_channels()))
                throw std::runtime_error("Selected channel unavailable");
            if (tx) {
                radio->set_tx_rate(fs, ch);
                radio->set_tx_freq(uhd::tune_request_t(f), ch);
                radio->set_tx_gain(gain, ch);
                if (!c.get("antenna").empty())
                    radio->set_tx_antenna(c.get("antenna"), ch);
            } else {
                radio->set_rx_rate(fs, ch);
                radio->set_rx_freq(uhd::tune_request_t(f), ch);
                radio->set_rx_gain(gain, ch);
                if (!c.get("antenna").empty())
                    radio->set_rx_antenna(c.get("antenna"), ch);
            }
            double actual = tx ? radio->get_tx_rate(ch) : radio->get_rx_rate(ch);
            if (std::abs(actual - fs) > 1)
                throw std::runtime_error(
                    "UHD coerced sample rate; choose a supported sample_rate on BOTH ends");
            std::cout << "USRP " << (tx ? "TX" : "RX") << " actual_rate=" << actual
                      << " actual_freq=" << (tx ? radio->get_tx_freq(ch) : radio->get_rx_freq(ch))
                      << " clock=internal\n";
            return radio;
        }
        class Transmitter final : public RadioTx {
            uhd::usrp::multi_usrp::sptr radio;
            uhd::tx_streamer::sptr stream;
            uhd::tx_metadata_t metadata;
            uint64_t failures = 0;

          public:
            explicit Transmitter(const Config &c) : radio(configure(c, true)) {
                uhd::stream_args_t args("fc32", "sc16");
                args.channels = {size_t(c.number("channel", 0))};
                stream = radio->get_tx_stream(args);
                metadata.start_of_burst = true;
                metadata.end_of_burst = false;
                metadata.has_time_spec = false;
            }
            ~Transmitter() override {
                try {
                    sl::phy::C zero{};
                    metadata.end_of_burst = true;
                    stream->send(&zero, 0, metadata, .2);
                } catch (...) {
                }
            }
            void send(const std::vector<sl::phy::C> &iq) override {
                for (size_t offset = 0; offset < iq.size();) {
                    auto n = stream->send(iq.data() + offset, iq.size() - offset, metadata, .5);
                    if (!n)
                        throw std::runtime_error("UHD TX timeout");
                    offset += n;
                    metadata.start_of_burst = false;
                }
                uhd::async_metadata_t event;
                while (stream->recv_async_msg(event, 0))
                    if (event.event_code != uhd::async_metadata_t::EVENT_CODE_BURST_ACK)
                        ++failures;
            }
            uint64_t errors() const override {
                return failures;
            }
        };
        class Receiver final : public RadioRx {
            uhd::usrp::multi_usrp::sptr radio;
            uhd::rx_streamer::sptr stream;
            uint64_t failures = 0;

          public:
            explicit Receiver(const Config &c) : radio(configure(c, false)) {
                uhd::stream_args_t args("fc32", "sc16");
                args.channels = {size_t(c.number("channel", 0))};
                stream = radio->get_rx_stream(args);
                uhd::stream_cmd_t start(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
                start.stream_now = true;
                stream->issue_stream_cmd(start);
            }
            ~Receiver() override {
                try {
                    stream->issue_stream_cmd(
                        uhd::stream_cmd_t(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS));
                } catch (...) {
                }
            }
            size_t receive(std::vector<sl::phy::C> &iq, bool &discontinuity) override {
                uhd::rx_metadata_t md;
                auto n = stream->recv(iq.data(), iq.size(), md, .1);
                discontinuity = false;
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT)
                    return 0;
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    ++failures;
                    discontinuity = true;
                    return 0;
                }
                if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
                    throw std::runtime_error(md.strerror());
                return n;
            }
            uint64_t errors() const override {
                return failures;
            }
        };
    } // namespace
    bool has_uhd() {
        return true;
    }
    std::unique_ptr<RadioTx> open_transmitter(const Config &c) {
        return std::make_unique<Transmitter>(c);
    }
    std::unique_ptr<RadioRx> open_receiver(const Config &c) {
        return std::make_unique<Receiver>(c);
    }
} // namespace video_link
#else
namespace video_link {
    bool has_uhd() {
        return false;
    }
    std::unique_ptr<RadioTx> open_transmitter(const Config &) {
        throw std::runtime_error(
            "This executable has no UHD. Rebuild with -WithUhd; no UDP fallback is allowed.");
    }
    std::unique_ptr<RadioRx> open_receiver(const Config &) {
        throw std::runtime_error(
            "This executable has no UHD. Rebuild with -WithUhd; no UDP fallback is allowed.");
    }
} // namespace video_link
#endif
