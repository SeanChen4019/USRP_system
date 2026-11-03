#pragma once
#include "modem.hpp"
#include "net.hpp"
#include <csignal>
#include <iostream>
#include <random>
#include <thread>
// Simulates continuous IQ framing over a local UDP bridge. The pacing budget is
// one fixed radio frame per packet; computation may be slower than the budget.
inline int iq_channel(const std::string &listen, const std::string &to, double seconds, double rate,
                      double cfo, double snr, const volatile std::sig_atomic_t *active) {
    using namespace sl;
    Socket input, output;
    input.bind(listen);
    auto dst = address(to);
    phy::RadioDecoder decoder;
    std::mt19937 rng(42);
    std::normal_distribution<float> gaussian(0, 1);
    std::array<uint8_t, 65536> b{};
    uint64_t end = micros() + uint64_t(seconds * 1e6), due = micros(), last = due, sample = 0,
             packets = 0, decoded = 0;
    double omega = 2 * phy::pi * cfo / rate;
    while (*active && micros() < end) {
        int n = input.receive(b.data(), b.size(), 20);
        if (!n)
            continue;
        Packet p;
        if (!unpack(b.data(), n, p))
            continue;
        auto wave = phy::radio_encode(Bytes(b.begin(), b.begin() + n));
        double power = 0;
        for (auto x : wave)
            power += std::norm(x);
        float sigma = float(std::sqrt(power / wave.size() / std::pow(10., snr / 10) / 2));
        for (auto &x : wave) {
            x = x * std::polar(1.f, float(std::remainder(omega * double(sample++), 2 * phy::pi))) +
                phy::C(sigma * gaussian(rng), sigma * gaussian(rng));
        }
        auto now = micros();
        if (now < due)
            std::this_thread::sleep_for(std::chrono::microseconds(due - now));
        due = std::max(due, micros()) + uint64_t(wave.size() * 1e6 / rate);
        for (auto &packet : decoder.push(wave.data(), wave.size())) {
            output.send(packet, dst);
            decoded++;
        }
        packets++;
        if (micros() - last >= 1000000) {
            std::cout << "IQ simulation input=" << packets << " decoded=" << decoded
                      << " rejected_candidates=" << decoder.rejected << std::endl;
            last = micros();
        }
    }
    return 0;
}
