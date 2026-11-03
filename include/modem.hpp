#pragma once
#include "core.hpp"
namespace sl::phy {
    constexpr size_t radio_bytes = 1440;
    constexpr size_t radio_samples = 256 + ((radio_bytes * 8 + 95) / 96) * 80;
    inline std::vector<C> training() {
        std::vector<C> t(64);
        uint32_t v = 0x1234abcd;
        for (int k = -26; k <= 26; k++)
            if (k) {
                v ^= v << 13;
                v ^= v >> 17;
                v ^= v << 5;
                t[(k + 64) % 64] = (v & 1) ? 1.f : -1.f;
            }
        fft(t, true);
        return t;
    }
    inline std::vector<C> radio_encode(const Bytes &packet) {
        if (packet.size() > max_payload + header)
            throw std::runtime_error("radio packet too large");
        Bytes padded;
        put(padded, packet.size(), 2);
        padded.insert(padded.end(), packet.begin(), packet.end());
        padded.resize(radio_bytes);
        auto t = training();
        std::vector<C> wave;
        wave.reserve(radio_samples);
        for (int i = 0; i < 4; i++)
            wave.insert(wave.end(), t.begin(), t.end());
        auto data = modulate(padded);
        wave.insert(wave.end(), data.begin(), data.end());
        for (auto &x : wave)
            x *= 2.f;
        return wave;
    }
    // SISO bring-up waveform: repeated-training acquisition, coarse CFO, channel
    // equalization and pilot phase tracking. Fixed QPSK, no FEC; not Wi-Fi compatible.
    class RadioDecoder {
        std::vector<C> buffer;
        std::vector<C> train = training();
        size_t cursor = 0;

      public:
        uint64_t decoded = 0, rejected = 0, discarded_samples = 0;
        void reset() {
            buffer.clear();
            cursor = 0;
        }
        std::vector<Bytes> push(const C *data, size_t count) {
            buffer.insert(buffer.end(), data, data + count);
            std::vector<Bytes> result;
            while (cursor + radio_samples <= buffer.size()) {
                C r = 0;
                float e0 = 0, e1 = 0;
                for (size_t i = 0; i < 64; i++) {
                    C x = buffer[cursor + i], y = buffer[cursor + 64 + i];
                    r += std::conj(x) * y;
                    e0 += std::norm(x);
                    e1 += std::norm(y);
                }
                bool found = false;
                while (cursor + radio_samples <= buffer.size()) {
                    float repeat = std::norm(r) / (e0 * e1 + 1e-20f);
                    if (e0 > 1e-9f && repeat > .80f) {
                        float omega = std::arg(r) / 64;
                        C dot = 0;
                        float et = 0;
                        for (size_t i = 0; i < 64; i++) {
                            dot += std::conj(train[i]) * buffer[cursor + i] *
                                   std::polar(1.f, -omega * float(i));
                            et += std::norm(train[i]);
                        }
                        if (std::norm(dot) / (et * e0 + 1e-20f) > .80f) {
                            std::vector<C> h(64);
                            for (size_t i = 0; i < 64; i++)
                                for (size_t j = 0; j < 4; j++)
                                    h[i] += buffer[cursor + 64 * j + i] *
                                            std::polar(1.f, -omega * float(64 * j + i)) * .25f;
                            fft(h, false);
                            auto known = train;
                            fft(known, false);
                            for (size_t k = 0; k < 64; k++)
                                if (std::norm(known[k]) > .1f)
                                    h[k] /= known[k];
                            Bytes bytes(radio_bytes);
                            size_t bit = 0;
                            auto cs = carriers();
                            for (size_t s = 256; s < radio_samples; s += 80) {
                                std::vector<C> a(64);
                                for (size_t k = 0; k < 64; k++)
                                    a[k] = buffer[cursor + s + 16 + k] *
                                           std::polar(1.f, -omega * float(s + 16 + k));
                                fft(a, false);
                                for (size_t k = 0; k < 64; k++)
                                    if (std::norm(h[k]) > 1e-10f)
                                        a[k] /= h[k];
                                C pilots = 0;
                                for (int k : {-21, -7, 7, 21})
                                    pilots += a[(k + 64) % 64];
                                C correction = std::polar(1.f, -std::arg(pilots));
                                for (int k : cs) {
                                    C z = a[k] * correction;
                                    for (bool v : {z.real() < 0, z.imag() < 0}) {
                                        if (bit < radio_bytes * 8)
                                            bytes[bit / 8] |= uint8_t(v) << (7 - bit % 8);
                                        bit++;
                                    }
                                }
                            }
                            size_t len = get(bytes.data(), 2);
                            Packet p;
                            if (len <= max_payload + header && len >= header &&
                                unpack(bytes.data() + 2, len, p)) {
                                result.emplace_back(bytes.begin() + 2, bytes.begin() + 2 + len);
                                decoded++;
                                cursor += radio_samples;
                                found = true;
                                break;
                            }
                            rejected++;
                        }
                    }
                    C x = buffer[cursor], y = buffer[cursor + 64], z = buffer[cursor + 128];
                    r += std::conj(y) * z - std::conj(x) * y;
                    e0 += std::norm(y) - std::norm(x);
                    e1 += std::norm(z) - std::norm(y);
                    e0 = std::max(e0, 0.f);
                    e1 = std::max(e1, 0.f);
                    cursor++;
                }
                if (!found)
                    break;
            }
            if (cursor) {
                discarded_samples += cursor;
                buffer.erase(buffer.begin(), buffer.begin() + cursor);
                cursor = 0;
            }
            return result;
        }
    };
} // namespace sl::phy
