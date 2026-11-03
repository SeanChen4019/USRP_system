#pragma once
#include <atomic>
#include <exception>
#include <thread>

namespace sl {
    // Each worker owns its stream. Either failure stops the peer; both are joined
    // before the original exception is propagated to the caller.
    template <class Tx, class Rx> void run_duplex(Tx tx, Rx rx, std::atomic<bool> &stop) {
        std::exception_ptr tx_error, rx_error;
        std::thread receiver([&] {
            try {
                rx();
            } catch (...) {
                rx_error = std::current_exception();
                stop = true;
            }
        });
        try {
            std::thread transmitter([&] {
                try {
                    tx();
                } catch (...) {
                    tx_error = std::current_exception();
                    stop = true;
                }
            });
            transmitter.join();
        } catch (...) {
            stop = true;
            receiver.join();
            throw;
        }
        receiver.join();
        if (rx_error)
            std::rethrow_exception(rx_error);
        if (tx_error)
            std::rethrow_exception(tx_error);
    }
} // namespace sl
