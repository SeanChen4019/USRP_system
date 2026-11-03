#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace sl {
    using Bytes = std::vector<uint8_t>;
    using Clock = std::chrono::steady_clock;
    inline uint64_t micros() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   Clock::now().time_since_epoch())
            .count();
    }
    inline uint32_t crc(const uint8_t *p, size_t n) {
        uint32_t c = ~0u;
        for (size_t i = 0; i < n; ++i) {
            c ^= p[i];
            for (int j = 0; j < 8; ++j)
                c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
        }
        return ~c;
    }
    inline void put(Bytes &b, uint64_t x, int n) {
        for (int i = n - 1; i >= 0; --i)
            b.push_back(uint8_t(x >> (i * 8)));
    }
    inline uint64_t get(const uint8_t *p, int n) {
        uint64_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 8) | p[i];
        return v;
    }
    constexpr size_t header = 36, max_payload = 1400;
    struct Packet {
        uint32_t session;
        uint64_t seq, stamp;
        Bytes payload;
    };
    inline Bytes pack(const Packet &p) {
        if (p.payload.size() > max_payload)
            throw std::runtime_error("payload exceeds 1400 bytes");
        Bytes b;
        put(b, 0x534c4e4b, 4);
        put(b, 1, 1);
        put(b, 0, 1);
        put(b, p.payload.size(), 2);
        put(b, p.session, 4);
        put(b, p.seq, 8);
        put(b, p.stamp, 8);
        put(b, 0, 4);
        b.insert(b.end(), p.payload.begin(), p.payload.end());
        put(b, crc(b.data(), b.size()), 4);
        return b;
    }
    inline bool unpack(const uint8_t *b, size_t n, Packet &p) {
        if (n < header || get(b, 4) != 0x534c4e4b || b[4] != 1 || b[5] != 0 || get(b + 28, 4) != 0)
            return false;
        size_t len = get(b + 6, 2);
        if (len > max_payload || n != header + len || crc(b, n - 4) != get(b + n - 4, 4))
            return false;
        p.session = uint32_t(get(b + 8, 4));
        p.seq = get(b + 12, 8);
        p.stamp = get(b + 20, 8);
        p.payload.assign(b + 32, b + 32 + len);
        return true;
    }
    // Sliding sequence window: accepts reordered packets, rejects duplicates and very old packets.
    class Dedup {
        std::array<uint64_t, 8192> slots{};
        bool initialized = false;
        uint64_t high = 0;

      public:
        Dedup() {
            slots.fill(UINT64_MAX);
        }
        bool accept(uint64_t seq) {
            if (seq == UINT64_MAX)
                return false;
            if (initialized && seq < high && high - seq >= slots.size())
                return false;
            if (slots[seq % slots.size()] == seq)
                return false;
            slots[seq % slots.size()] = seq;
            high = initialized ? std::max(high, seq) : seq;
            initialized = true;
            return true;
        }
    };
    namespace phy {
        using C = std::complex<float>;
        constexpr float pi = 3.14159265358979323846f;
        inline void fft(std::vector<C> &a, bool inverse) {
            size_t n = a.size();
            if (!n || (n & (n - 1)))
                throw std::runtime_error("FFT size must be power of two");
            for (size_t i = 1, j = 0; i < n; i++) {
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
                if (i < j)
                    std::swap(a[i], a[j]);
            }
            for (size_t len = 2; len <= n; len <<= 1) {
                C wlen = std::polar(1.f, (inverse ? 2.f : -2.f) * pi / float(len));
                for (size_t i = 0; i < n; i += len) {
                    C w = 1;
                    for (size_t j = 0; j < len / 2; j++) {
                        C u = a[i + j], v = a[i + j + len / 2] * w;
                        a[i + j] = u + v;
                        a[i + j + len / 2] = u - v;
                        w *= wlen;
                    }
                }
            }
            if (inverse)
                for (auto &x : a)
                    x /= float(n);
        }
        inline std::vector<int> carriers() {
            std::vector<int> c;
            for (int k = -26; k <= 26; k++)
                if (k && k != -21 && k != -7 && k != 7 && k != 21)
                    c.push_back((k + 64) % 64);
            return c;
        }
        // Experimental OFDM payload kernel only: 64 FFT, 16 CP, 48 QPSK data carriers.
        // Receiver requires exact symbol boundaries; no OTA synchronization/FEC yet.
        inline std::vector<C> modulate(const Bytes &b) {
            auto cs = carriers();
            std::vector<C> out;
            size_t bits = b.size() * 8;
            for (size_t start = 0; start < bits; start += 96) {
                std::vector<C> a(64);
                for (size_t j = 0; j < cs.size(); j++) {
                    size_t k = start + 2 * j;
                    auto bit = [&](size_t q) {
                        return q < bits ? ((b[q / 8] >> (7 - q % 8)) & 1) : 0;
                    };
                    a[cs[j]] = C(bit(k) ? -1.f : 1.f, bit(k + 1) ? -1.f : 1.f) / std::sqrt(2.f);
                }
                for (int k : {-21, -7, 7, 21})
                    a[(k + 64) % 64] = 1;
                fft(a, true);
                out.insert(out.end(), a.end() - 16, a.end());
                out.insert(out.end(), a.begin(), a.end());
            }
            return out;
        }
        inline Bytes demodulate(const std::vector<C> &iq, size_t bytes) {
            if (iq.size() != ((bytes * 8 + 95) / 96) * 80)
                throw std::runtime_error("OFDM length mismatch");
            Bytes out(bytes);
            auto cs = carriers();
            size_t bit = 0;
            for (size_t s = 0; s < iq.size(); s += 80) {
                std::vector<C> a(iq.begin() + s + 16, iq.begin() + s + 80);
                fft(a, false);
                for (int k : cs) {
                    for (bool v : {a[k].real() < 0, a[k].imag() < 0}) {
                        if (bit < bytes * 8)
                            out[bit / 8] |= uint8_t(v) << (7 - bit % 8);
                        ++bit;
                    }
                }
            }
            return out;
        }
    } // namespace phy
} // namespace sl
