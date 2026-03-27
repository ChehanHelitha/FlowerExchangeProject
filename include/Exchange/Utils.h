// Shared utility functions (timestamp, ID generation).
#pragma once
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>

namespace Utils {

    // Thread-safe counter for generating unique order IDs.
    // Multiple threads may generate IDs simultaneously, so we use
    // std::atomic to ensure each thread gets a different number.
    inline std::atomic<int>& getOrderCounter() {
        static std::atomic<int> counter{1};
        return counter;
    }

    // WHY inline: Defined in a header included by multiple TUs.
    // inline prevents ODR (One Definition Rule) violations.
    inline std::string generateOrderId() {
        int id = getOrderCounter().fetch_add(1, std::memory_order_relaxed);
        return "ord" + std::to_string(id);
    }

    // Returns current wall-clock time formatted as YYYYMMDD-HHMMSS.sss
    inline std::string getCurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch()) % 1000;

        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm     tm_buf;

#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);   // Thread-safe on POSIX
#endif

        std::ostringstream oss;
        oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S")
            << '.'
            << std::setfill('0') << std::setw(3) << ms.count();
        return oss.str();
    }

} // namespace Utils
