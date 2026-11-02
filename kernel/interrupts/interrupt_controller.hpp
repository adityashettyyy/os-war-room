// Interrupt controller. Synchronous exceptions (traps, page faults, errors) are dispatched
// immediately; asynchronous device/timer interrupts are queued and serviced by priority.
#pragma once
#include <array>
#include "../common/types.hpp"

namespace owr {

struct Interrupt {
    int id = 0;
    IntType type = IntType::TIMER;
    int pid = -1;
    int tick = 0;
    int page = -1;
    std::string device, cause, handler, action;
    int a = 0, b = 0, c = 0;    // type-specific payload (SYSCALL: a=number, b/c=arguments; IO: a=blocking flag)
    std::string s, s2;          // type-specific string payload
    bool quiet = false;         // routine housekeeping interrupts hidden from the event log
    bool sync = false;
};

class InterruptController {
public:
    using Handler = std::function<void(Interrupt&)>;

    void on(IntType t, const std::string& name, Handler h) { handlers_[(int)t] = {name, std::move(h)}; }

    void raise(Interrupt i, bool synchronous) {
        i.id = nextId_++;
        i.sync = synchronous;
        i.handler = handlers_[(int)i.type].first;
        if (synchronous) dispatch(i);
        else pending_.push_back(std::move(i));
    }
    // Service every pending asynchronous interrupt, highest priority (lowest enum value) first.
    void service() {
        while (!pending_.empty()) {
            size_t best = 0;
            for (size_t k = 1; k < pending_.size(); ++k)
                if ((int)pending_[k].type < (int)pending_[best].type) best = k;
            Interrupt i = pending_[best];
            pending_.erase(pending_.begin() + (long)best);
            dispatch(i);
        }
    }
    const std::vector<Interrupt>& pending() const { return pending_; }
    const std::deque<Interrupt>& history() const { return history_; }
    long count(IntType t) const { return counts_[(size_t)t]; }
    long total() const { long n = 0; for (long c : counts_) n += c; return n; }
    void dropPending(int pid) {
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const Interrupt& i) { return i.pid == pid && i.type != IntType::ERROR; }), pending_.end());
    }

private:
    void dispatch(Interrupt& i) {
        ++counts_[(size_t)i.type];
        auto& h = handlers_[(int)i.type];
        if (h.second) h.second(i);
        history_.push_back(i);
        if (history_.size() > 300) history_.pop_front();
    }
    std::array<std::pair<std::string, Handler>, 5> handlers_;
    std::vector<Interrupt> pending_;
    std::deque<Interrupt> history_;
    std::array<long, 5> counts_{};
    int nextId_ = 1;
};

}  // namespace owr
