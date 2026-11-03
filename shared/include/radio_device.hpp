#pragma once
#include "app_config.hpp"
#include "modem.hpp"
#include <memory>
namespace video_link {
    class RadioTx {
      public:
        virtual ~RadioTx() = default;
        virtual void send(const std::vector<sl::phy::C> &iq) = 0;
        virtual uint64_t errors() const = 0;
    };
    class RadioRx {
      public:
        virtual ~RadioRx() = default;
        // A discontinuity means the modem must discard its buffered samples.
        virtual size_t receive(std::vector<sl::phy::C> &iq, bool &discontinuity) = 0;
        virtual uint64_t errors() const = 0;
    };
    std::unique_ptr<RadioTx> open_transmitter(const Config &config);
    std::unique_ptr<RadioRx> open_receiver(const Config &config);
    bool has_uhd();
} // namespace video_link
