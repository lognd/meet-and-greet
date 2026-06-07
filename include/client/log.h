#pragma once
#include <fstream>
#include <mutex>
#include <string>
#include <chrono>
#include <ctime>

// Single-file logger that writes to mag_client.log in the working directory.
// FTXUI owns the terminal, so stdout/stderr are invisible during TUI operation.
// Use LOG("message") or LOG("key", value) anywhere in client code.
//
// Thread-safe; call mag::log::init() once before any LOG use.

namespace mag::log {

inline std::ofstream& stream() {
    static std::ofstream f;
    return f;
}
inline std::mutex& mtx() {
    static std::mutex m;
    return m;
}

inline void init(const std::string& path = "mag_client.log") {
    stream().open(path, std::ios::app);
    // separator so repeated runs are distinguishable in the log
    std::lock_guard<std::mutex> lk(mtx());
    stream() << "\n=== session start ===\n";
    stream().flush();
}

inline void write(const std::string& msg) {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    char buf[20];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
    std::lock_guard<std::mutex> lk(mtx());
    stream() << "[" << buf << "] " << msg << "\n";
    stream().flush();
}

} // namespace mag::log

#define LOG(...)  mag::log::write(mag::log::_fmt(__VA_ARGS__))

namespace mag::log {
inline std::string _fmt(const std::string& msg) { return msg; }
template<typename V>
inline std::string _fmt(const std::string& key, const V& val) {
    return key + "=" + std::to_string(val);
}
inline std::string _fmt(const std::string& key, const std::string& val) {
    return key + "=" + val;
}
inline std::string _fmt(const std::string& key, const char* val) {
    return key + "=" + std::string(val);
}
inline std::string _fmt(const std::string& key, bool val) {
    return key + "=" + (val ? "true" : "false");
}
} // namespace mag::log
