#pragma once
#include "duplex.hpp"
#include "modem.hpp"
#include <iostream>
#include <random>
inline void radio_tests() {
    for (bool fail_tx : {false, true}) {
        std::atomic<bool> stop{false};
        std::atomic<int> completed{0};
        auto worker = [&](bool fail) {
            if (fail)
                throw std::runtime_error("expected worker failure");
            auto deadline = sl::Clock::now() + std::chrono::seconds(1);
            while (!stop.load() && sl::Clock::now() < deadline)
                std::this_thread::yield();
            if (stop.load())
                completed++;
        };
        bool caught = false;
        try {
            sl::run_duplex([&] { worker(fail_tx); }, [&] { worker(!fail_tx); }, stop);
        } catch (const std::runtime_error &) {
            caught = true;
        }
        if (!caught || completed != 1)
            throw std::runtime_error("duplex failure cleanup failed");
    }
    std::cout << "PASS: duplex TX/RX worker failures stop and join peer\n";
    using namespace sl;
    using namespace sl::phy;
    std::mt19937 rng(42);
    std::normal_distribution<float> noise(0, .001f);
    for (float cfo : {0.f, .004f, -.009f}) {
        std::vector<C> stream(173);
        std::vector<Bytes> expected;
        for (uint64_t seq = 0; seq < 3; seq++) {
            Packet p{42, seq, 0, Bytes(1316)};
            for (auto &x : p.payload)
                x = uint8_t(rng());
            expected.push_back(pack(p));
            auto wave = radio_encode(expected.back());
            stream.insert(stream.end(), wave.begin(), wave.end());
            stream.insert(stream.end(), 93, C{});
        }
        auto original = stream;
        for (size_t i = 0; i < stream.size(); i++) {
            C x = original[i];
            if (i >= 3)
                x += C(.12f, .05f) * original[i - 3];
            stream[i] = x * std::polar(.7f, cfo * float(i) + .9f) + C(noise(rng), noise(rng));
        }
        RadioDecoder decoder;
        std::vector<Bytes> actual;
        for (size_t i = 0; i < stream.size();) {
            size_t n = std::min(size_t(37 + rng() % 3000), stream.size() - i);
            auto out = decoder.push(stream.data() + i, n);
            actual.insert(actual.end(), out.begin(), out.end());
            i += n;
        }
        if (actual != expected)
            throw std::runtime_error(
                "radio acquisition/CFO/multipath/chunk-boundary test failed: " +
                std::to_string(cfo) + " decoded=" + std::to_string(actual.size()));
    }
    std::cout << "PASS: framed OFDM acquisition, +/- CFO, multipath, AWGN, arbitrary IQ chunk "
                 "boundaries, 3 consecutive packets\n";
}
