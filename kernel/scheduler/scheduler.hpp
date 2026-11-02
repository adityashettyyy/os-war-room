// CPU scheduling policies behind one strategy interface.
// The kernel owns dispatching; a policy only decides ordering, time slices and preemption.
#pragma once
#include <cmath>
#include "../common/config.hpp"
#include "../process/pcb.hpp"

namespace owr {

class Scheduler {
public:
    explicit Scheduler(const Config& c) : cfg_(c) {}
    virtual ~Scheduler() = default;
    virtual SchedPolicy policy() const = 0;

    virtual void enqueue(PCB* p) { ready_.push_back(p); }
    PCB* pickNext() {
        if (ready_.empty()) return nullptr;
        auto it = select();
        PCB* p = *it;
        ready_.erase(it);
        return p;
    }
    // Should `running` give up the CPU right now because of what is in the ready queue?
    virtual bool shouldPreempt(const PCB&) const { return false; }
    virtual int timeSlice(const PCB&) const { return 0; }    // 0 => run until block/exit
    virtual void onQuantumExpired(PCB&) {}
    virtual void onBurstEnd(PCB&) {}                         // process blocked or exited
    // Called once per tick: waiting-time bookkeeping, priority aging, MLFQ boost.
    virtual void tick(int now) {
        for (PCB* p : ready_) {
            ++p->readyWait;
            if (cfg_.aging && cfg_.agingInterval > 0 && p->readyWait % cfg_.agingInterval == 0 && p->effPriority > 0) --p->effPriority;
        }
        (void)now;
    }

    void remove(const PCB* p) { ready_.erase(std::remove(ready_.begin(), ready_.end(), p), ready_.end()); }
    bool contains(const PCB* p) const { return std::find(ready_.begin(), ready_.end(), p) != ready_.end(); }
    size_t size() const { return ready_.size(); }
    std::vector<PCB*> drain() { std::vector<PCB*> v(ready_.begin(), ready_.end()); ready_.clear(); return v; }
    // Ready queue in the order the policy would serve it.
    virtual std::vector<int> order() const { std::vector<int> v; for (auto* p : ready_) v.push_back(p->pid); return v; }
    const std::deque<PCB*>& queue() const { return ready_; }

protected:
    virtual std::deque<PCB*>::iterator select() { return ready_.begin(); }
    std::deque<PCB*> ready_;
    const Config& cfg_;
};

struct FcfsScheduler : Scheduler {
    using Scheduler::Scheduler;
    SchedPolicy policy() const override { return SchedPolicy::FCFS; }
};

// Shortest Job First (non-preemptive). "Burst" is the process's remaining CPU demand.
struct SjfScheduler : Scheduler {
    using Scheduler::Scheduler;
    SchedPolicy policy() const override { return SchedPolicy::SJF; }
    std::vector<int> order() const override {
        std::vector<PCB*> v(ready_.begin(), ready_.end());
        std::stable_sort(v.begin(), v.end(), [](PCB* a, PCB* b) { return a->remaining() < b->remaining(); });
        std::vector<int> r; for (auto* p : v) r.push_back(p->pid); return r;
    }
protected:
    std::deque<PCB*>::iterator select() override {
        return std::min_element(ready_.begin(), ready_.end(), [](PCB* a, PCB* b) { return a->remaining() < b->remaining(); });
    }
};

// Shortest Remaining Time First: SJF plus preemption when a shorter job is ready.
struct SrtfScheduler : SjfScheduler {
    using SjfScheduler::SjfScheduler;
    SchedPolicy policy() const override { return SchedPolicy::SRTF; }
    bool shouldPreempt(const PCB& run) const override {
        for (auto* p : ready_) if (p->remaining() < run.remaining()) return true;
        return false;
    }
};

struct RoundRobinScheduler : Scheduler {
    using Scheduler::Scheduler;
    SchedPolicy policy() const override { return SchedPolicy::RR; }
    int timeSlice(const PCB&) const override { return std::max(1, cfg_.quantum); }
};

// Preemptive priority scheduling (lower number = more important) with optional aging.
struct PriorityScheduler : Scheduler {
    using Scheduler::Scheduler;
    SchedPolicy policy() const override { return SchedPolicy::PRIORITY; }
    bool shouldPreempt(const PCB& run) const override {
        for (auto* p : ready_) if (p->effPriority < run.effPriority) return true;
        return false;
    }
    std::vector<int> order() const override {
        std::vector<PCB*> v(ready_.begin(), ready_.end());
        std::stable_sort(v.begin(), v.end(), [](PCB* a, PCB* b) { return a->effPriority < b->effPriority; });
        std::vector<int> r; for (auto* p : v) r.push_back(p->pid); return r;
    }
protected:
    std::deque<PCB*>::iterator select() override {
        return std::min_element(ready_.begin(), ready_.end(), [](PCB* a, PCB* b) { return a->effPriority < b->effPriority; });
    }
};

// Three-level feedback queue: slices q, 2q, 4q. Using the whole slice demotes a process;
// blocking early keeps its level (rewarding interactive work). All ready processes are
// periodically boosted back to level 0 so nothing starves.
struct MlfqScheduler : Scheduler {
    using Scheduler::Scheduler;
    static constexpr int kLevels = 3;
    SchedPolicy policy() const override { return SchedPolicy::MLFQ; }
    int timeSlice(const PCB& p) const override { return std::max(1, cfg_.quantum) << std::min(p.level, kLevels - 1); }
    void onQuantumExpired(PCB& p) override { p.level = std::min(p.level + 1, kLevels - 1); }
    bool shouldPreempt(const PCB& run) const override {
        for (auto* p : ready_) if (p->level < run.level) return true;
        return false;
    }
    void tick(int now) override {
        Scheduler::tick(now);
        if (cfg_.mlfqBoostInterval > 0 && now > 0 && now % cfg_.mlfqBoostInterval == 0) for (auto* p : ready_) p->level = 0;
    }
    std::vector<int> order() const override {
        std::vector<PCB*> v(ready_.begin(), ready_.end());
        std::stable_sort(v.begin(), v.end(), [](PCB* a, PCB* b) { return a->level < b->level; });
        std::vector<int> r; for (auto* p : v) r.push_back(p->pid); return r;
    }
protected:
    std::deque<PCB*>::iterator select() override {
        return std::min_element(ready_.begin(), ready_.end(), [](PCB* a, PCB* b) { return a->level < b->level; });
    }
};

// Adaptive: shortest-predicted-burst-first where the prediction is an exponential average of the
// process's observed CPU bursts (tau' = 0.5*burst + 0.5*tau), aged so long waiters are not starved.
// The time slice follows the prediction, so I/O-bound work gets short slices and CPU-bound long ones.
struct AdaptiveScheduler : Scheduler {
    using Scheduler::Scheduler;
    SchedPolicy policy() const override { return SchedPolicy::ADAPTIVE; }
    int timeSlice(const PCB& p) const override { return std::clamp((int)std::lround(p.tau), 2, 12); }
    void onBurstEnd(PCB& p) override { if (p.burstRun > 0) p.tau = 0.5 * p.burstRun + 0.5 * p.tau; }
    std::vector<int> order() const override {
        std::vector<PCB*> v(ready_.begin(), ready_.end());
        std::stable_sort(v.begin(), v.end(), [](PCB* a, PCB* b) { return score(a) < score(b); });
        std::vector<int> r; for (auto* p : v) r.push_back(p->pid); return r;
    }
protected:
    static double score(const PCB* p) { return p->tau - p->readyWait / 16.0; }
    std::deque<PCB*>::iterator select() override {
        return std::min_element(ready_.begin(), ready_.end(), [](PCB* a, PCB* b) { return score(a) < score(b); });
    }
};

inline std::unique_ptr<Scheduler> makeScheduler(SchedPolicy p, const Config& c) {
    switch (p) {
        case SchedPolicy::FCFS: return std::make_unique<FcfsScheduler>(c);
        case SchedPolicy::SJF: return std::make_unique<SjfScheduler>(c);
        case SchedPolicy::SRTF: return std::make_unique<SrtfScheduler>(c);
        case SchedPolicy::RR: return std::make_unique<RoundRobinScheduler>(c);
        case SchedPolicy::PRIORITY: return std::make_unique<PriorityScheduler>(c);
        case SchedPolicy::MLFQ: return std::make_unique<MlfqScheduler>(c);
        case SchedPolicy::ADAPTIVE: return std::make_unique<AdaptiveScheduler>(c);
    }
    return std::make_unique<FcfsScheduler>(c);
}

}  // namespace owr
