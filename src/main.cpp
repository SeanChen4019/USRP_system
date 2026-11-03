#include "core.hpp"
#include "iq_channel.hpp"
#include "modem.hpp"
#include "net.hpp"
#include "radio_tests.hpp"
#include <atomic>
#include <csignal>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <random>
#include <thread>
using namespace sl;
static volatile std::sig_atomic_t running = 1;
static void stop(int) {
    running = 0;
}
struct Args {
    std::map<std::string, std::string> m;
    Args(int argc, char **argv) {
        for (int i = 2; i < argc; i++) {
            std::string k = argv[i];
            if (k.rfind("--", 0) || i + 1 >= argc)
                throw std::runtime_error("options require --name value");
            if (!m.emplace(k, argv[++i]).second)
                throw std::runtime_error("duplicate option: " + k);
        }
    }
    std::string s(std::string k, std::string d) {
        auto it = m.find("--" + k);
        return it == m.end() ? d : it->second;
    }
    double n(std::string k, double d) {
        return std::stod(s(k, std::to_string(d)));
    }
    void check(std::initializer_list<const char *> keys) {
        for (auto &kv : m) {
            bool found = false;
            for (auto key : keys)
                if (kv.first == std::string("--") + key)
                    found = true;
            if (!found)
                throw std::runtime_error("unknown option: " + kv.first);
        }
    }
};
static void require(bool b, const char *msg) {
    if (!b)
        throw std::runtime_error(msg);
}
struct Stats {
    uint64_t packets = 0, bytes = 0, bad = 0, duplicates = 0;
    uint64_t first = micros(), last = first, prevbytes = 0;
    std::ofstream csv;
    explicit Stats(const std::string &path) {
        if (!path.empty()) {
            csv.open(path);
            if (!csv)
                throw std::runtime_error("cannot create CSV");
            csv << "elapsed_s,goodput_mbps,average_mbps,unique_packets,unique_bytes,invalid,"
                   "duplicates\n";
        }
    }
    void report(bool force = false) {
        auto now = micros();
        if (!force && now - last < 1000000)
            return;
        double t = double(now - first) / 1e6, dt = double(now - last) / 1e6;
        if (dt <= 0)
            return;
        double mbps = (bytes - prevbytes) * 8.0 / (dt * 1e6),
               avg = bytes * 8.0 / (std::max(t, 1e-9) * 1e6);
        std::cout << std::fixed << std::setprecision(3) << "elapsed=" << t << "s goodput=" << mbps
                  << "Mbps average=" << avg << "Mbps packets=" << packets << " bytes=" << bytes
                  << " invalid=" << bad << " duplicates=" << duplicates << std::endl;
        if (csv) {
            csv << t << ',' << mbps << ',' << avg << ',' << packets << ',' << bytes << ',' << bad
                << ',' << duplicates << '\n';
            csv.flush();
        }
        last = now;
        prevbytes = bytes;
    }
};
static uint32_t session_id() {
    std::random_device r;
    return uint32_t(r());
}
static int selftest() {
    radio_tests();
    std::mt19937 gen(42);
    for (size_t len : {size_t(0), size_t(1), size_t(1200), size_t(1400)}) {
        Packet p{123, 456, 789, Bytes(len)};
        for (auto &x : p.payload)
            x = uint8_t(gen());
        auto b = pack(p);
        Packet q;
        require(unpack(b.data(), b.size(), q) && q.payload == p.payload && q.seq == p.seq,
                "packet roundtrip");
        for (size_t i = 0; i < b.size(); i++) {
            b[i] ^= 1;
            require(!unpack(b.data(), b.size(), q), "corruption accepted");
            b[i] ^= 1;
        }
        for (size_t n = 0; n < b.size(); n++)
            require(!unpack(b.data(), n, q), "truncation accepted");
        auto iq = phy::modulate(p.payload);
        require(phy::demodulate(iq, p.payload.size()) == p.payload, "OFDM roundtrip");
    }
    const std::string check = "123456789";
    require(crc(reinterpret_cast<const uint8_t *>(check.data()), check.size()) == 0xcbf43926,
            "CRC reference");
    Dedup d;
    require(d.accept(10) && d.accept(12) && d.accept(11) && !d.accept(10) && d.accept(9000) &&
                !d.accept(12),
            "dedup reorder/window");
    Bytes b(1200);
    for (auto &x : b)
        x = uint8_t(gen());
    auto iq = phy::modulate(b);
    std::normal_distribution<float> noise(0, .002f);
    for (auto &x : iq)
        x += phy::C(noise(gen), noise(gen));
    require(phy::demodulate(iq, b.size()) == b, "OFDM small-noise recovery");
    auto start = micros();
    size_t count = 0;
    while (micros() - start < 500000) {
        auto wave = phy::modulate(b);
        auto out = phy::demodulate(wave, b.size());
        require(out == b, "OFDM benchmark integrity");
        ++count;
    }
    double seconds = (micros() - start) / 1e6;
    std::cout << "PASS: CRC reference, all single-byte corruptions, all truncations, "
                 "reorder/dedup, OFDM clean/noisy recovery\nOFDM kernel CPU roundtrip payload rate="
              << count * b.size() * 8.0 / seconds / 1e6
              << " Mbps (NOT radio throughput; no FEC/synchronization)\n";
    return 0;
}
static int tx(Args &a, bool ingress) {
    a.check({"to", "seconds", "mbps", "payload", "listen", "csv"});
    Socket out, in;
    auto dst = address(a.s("to", "127.0.0.1:9001"));
    double duration = a.n("seconds", 10), mbps = a.n("mbps", 12);
    int payload = int(a.n("payload", 1200));
    require(duration > 0 && mbps > 0 && payload > 0 && payload <= 1400,
            "invalid duration/rate/payload");
    if (ingress)
        in.bind(a.s("listen", "127.0.0.1:5600"));
    Stats st(a.s("csv", ""));
    Packet p{session_id(), 0, 0, Bytes(size_t(payload))};
    for (size_t i = 0; i < p.payload.size(); i++)
        p.payload[i] = uint8_t(i);
    uint64_t end = micros() + uint64_t(duration * 1e6), due = micros();
    std::array<uint8_t, 65536> buf{};
    std::cout << (ingress ? "UDP ingress" : "Synthetic traffic") << " -> "
              << a.s("to", "127.0.0.1:9001") << " session=" << p.session
              << " (UDP backend, not RF)\n";
    while (running && micros() < end) {
        if (ingress) {
            int n = in.receive(buf.data(), buf.size(), 20);
            if (!n) {
                st.report();
                continue;
            }
            if (n > int(max_payload)) {
                st.bad++;
                continue;
            }
            p.payload.assign(buf.begin(), buf.begin() + n);
        } else {
            auto now = micros();
            if (now < due) {
                std::this_thread::sleep_for(std::chrono::microseconds(due - now));
            }
            if (micros() > due + 20000)
                due = micros();
            due += uint64_t(p.payload.size() * 8.0 / mbps);
        }
        p.stamp = micros();
        out.send(pack(p), dst);
        p.seq++;
        st.packets++;
        st.bytes += p.payload.size();
        st.report();
    }
    st.report(true);
    return 0;
}
static int rx(Args &a) {
    a.check({"listen", "seconds", "out", "csv", "session"});
    Socket in, out;
    in.bind(a.s("listen", "0.0.0.0:9002"));
    std::string forward = a.s("out", "");
    sockaddr_in dst{};
    if (!forward.empty())
        dst = address(forward);
    double duration = a.n("seconds", 15);
    require(duration > 0, "invalid duration");
    Stats st(a.s("csv", ""));
    Dedup dedup;
    bool locked = false;
    uint32_t session = 0;
    std::string expected = a.s("session", "");
    if (!expected.empty()) {
        session = uint32_t(std::stoul(expected));
        locked = true;
    }
    uint64_t end = micros() + uint64_t(duration * 1e6);
    std::array<uint8_t, 65536> b{};
    std::cout << "Receiver (UDP backend). Locks first session; restart for a new sender.\n";
    while (running && micros() < end) {
        int n = in.receive(b.data(), b.size(), 20);
        if (n) {
            Packet p;
            if (!unpack(b.data(), size_t(n), p)) {
                st.bad++;
            } else {
                if (!locked) {
                    locked = true;
                    session = p.session;
                    std::cout << "session=" << session << std::endl;
                }
                if (p.session != session)
                    st.bad++;
                else if (!dedup.accept(p.seq))
                    st.duplicates++;
                else {
                    st.packets++;
                    st.bytes += p.payload.size();
                    if (!forward.empty())
                        out.send(p.payload, dst);
                }
            }
        }
        st.report();
    }
    st.report(true);
    return 0;
}
static int channel(Args &a) {
    a.check(
        {"listen", "to", "seconds", "mbps", "delay-ms", "loss", "queue-ms", "seed", "duplicate"});
    Socket in, out;
    in.bind(a.s("listen", "0.0.0.0:9001"));
    auto dst = address(a.s("to", "127.0.0.1:9002"));
    double duration = a.n("seconds", 15), rate = a.n("mbps", 20), delay = a.n("delay-ms", 5),
           loss = a.n("loss", 0), budget = a.n("queue-ms", 100), duplicate = a.n("duplicate", 0);
    require(duration > 0 && rate > 0 && delay >= 0 && budget > 0 && loss >= 0 && loss <= 1 &&
                duplicate >= 0 && duplicate <= 1,
            "invalid channel parameters");
    std::mt19937 gen(uint32_t(a.n("seed", 42)));
    std::uniform_real_distribution<double> random(0, 1);
    struct Pending {
        uint64_t due;
        Bytes b;
    };
    std::deque<Pending> queue;
    uint64_t end = micros() + uint64_t(duration * 1e6), tail = micros(), last = tail, sent = 0,
             dropped = 0, overflow = 0;
    std::array<uint8_t, 65536> b{};
    std::cout << "Simulated channel; rate includes StreamLink headers, NOT IQ or physical radio.\n";
    while (running && micros() < end) {
        auto now = micros();
        while (!queue.empty() && queue.front().due <= now) {
            out.send(queue.front().b, dst);
            if (random(gen) < duplicate)
                out.send(queue.front().b, dst);
            queue.pop_front();
            sent++;
        }
        int n = in.receive(b.data(), b.size(), queue.empty() ? 10 : 1);
        now = micros();
        if (n) {
            if (random(gen) < loss)
                dropped++;
            else {
                uint64_t finish = std::max(tail, now) + uint64_t(n * 8.0 / rate);
                if (finish - now > uint64_t(budget * 1000) || queue.size() >= 65536)
                    overflow++;
                else {
                    tail = finish;
                    queue.push_back(
                        {finish + uint64_t(delay * 1000), Bytes(b.begin(), b.begin() + n)});
                }
            }
        }
        if (now - last >= 1000000) {
            std::cout << "sent=" << sent << " random_drop=" << dropped << " queue_drop=" << overflow
                      << " queue_packets=" << queue.size() << std::endl;
            last = now;
        }
    }
    std::cout << "sent=" << sent << " random_drop=" << dropped << " queue_drop=" << overflow
              << " pending_at_shutdown=" << queue.size() << std::endl;
    return 0;
}
static void help() {
    std::cout
        << "StreamLink C++17 v0.1 - PC transport and experimental OFDM\n"
           "  selftest\n  tx --to 127.0.0.1:9001 --mbps 12 --seconds 10 --payload 1200\n"
           "  channel --listen 0.0.0.0:9001 --to 127.0.0.1:9002 --mbps 20 --loss 0 --delay-ms 5 "
           "--queue-ms 100 --seconds 15\n"
           "  rx --listen 0.0.0.0:9002 --seconds 15 --csv rx.csv [--out 127.0.0.1:5601]\n"
           "  ingress --listen 127.0.0.1:5600 --to 127.0.0.1:9001 --seconds 3600\n"
           "  iq-channel --rate 10000000 --cfo 3000 --snr 30 --seconds 15\n"
           "All transport commands use UDP/IP, not USRP. ingress expects datagrams <=1400 bytes.\n";
}
int main(int argc, char **argv) {
    try {
        Network net;
        std::signal(SIGINT, stop);
        if (argc < 2) {
            help();
            return 0;
        }
        Args a(argc, argv);
        std::string cmd = argv[1];
        if (cmd == "selftest") {
            a.check({});
            return selftest();
        }
        if (cmd == "tx")
            return tx(a, false);
        if (cmd == "ingress")
            return tx(a, true);
        if (cmd == "rx")
            return rx(a);
        if (cmd == "channel")
            return channel(a);
        if (cmd == "iq-channel") {
            a.check({"listen", "to", "seconds", "rate", "cfo", "snr"});
            double seconds = a.n("seconds", 15), rate = a.n("rate", 10e6), cfo = a.n("cfo", 1000),
                   snr = a.n("snr", 30);
            require(seconds > 0 && rate > 0 && std::isfinite(cfo) && std::isfinite(snr),
                    "invalid IQ channel parameters");
            return iq_channel(a.s("listen", "127.0.0.1:9001"), a.s("to", "127.0.0.1:9002"), seconds,
                              rate, cfo, snr, &running);
        }
        if (cmd == "help") {
            help();
            return 0;
        }
        throw std::runtime_error("unknown command");
    } catch (const std::exception &e) {
        std::cerr << "ERROR: " << e.what() << '\n';
        return 1;
    }
}
