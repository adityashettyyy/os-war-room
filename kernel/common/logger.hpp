// Kernel event log: a bounded in-memory ring of categorised, tick-stamped events.
#pragma once
#include <cstdio>
#include "types.hpp"

namespace owr {

struct LogEvent {
    long id = 0;
    int tick = 0;
    Category cat = Category::SYSTEM;
    int pid = -1;
    std::string msg;
};

class Logger {
    std::deque<LogEvent> ev_;
    long next_ = 1;
    size_t cap_;
    bool echo_ = false;
public:
    explicit Logger(size_t cap = 8000) : cap_(cap) {}
    void setEcho(bool e) { echo_ = e; }
    static std::string stamp(int tick) { char b[16]; snprintf(b, sizeof b, "T+%04d", tick); return b; }
    void log(int tick, Category c, int pid, std::string msg) {
        if (echo_) std::printf("[%s] %-9s %s\n", stamp(tick).c_str(), str(c), msg.c_str());
        ev_.push_back({next_++, tick, c, pid, std::move(msg)});
        if (ev_.size() > cap_) ev_.pop_front();
    }
    const std::deque<LogEvent>& events() const { return ev_; }
    long lastId() const { return next_ - 1; }
    // Events with id > since, optionally limited to categories (empty = all) and a pid (-1 = all).
    std::vector<LogEvent> query(long since, const std::set<Category>& cats = {}, int pid = -1, size_t limit = 500) const {
        std::vector<LogEvent> out;
        for (const auto& e : ev_) {
            if (e.id <= since) continue;
            if (!cats.empty() && !cats.count(e.cat)) continue;
            if (pid >= 0 && e.pid != pid) continue;
            out.push_back(e);
        }
        if (out.size() > limit) out.erase(out.begin(), out.end() - (long)limit);
        return out;
    }
    size_t count(Category c) const { size_t n = 0; for (auto& e : ev_) if (e.cat == c) ++n; return n; }
    bool contains(const std::string& needle) const { for (auto& e : ev_) if (e.msg.find(needle) != std::string::npos) return true; return false; }
};

}  // namespace owr
