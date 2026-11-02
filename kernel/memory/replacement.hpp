// Page-replacement policies (FIFO, LRU, Optimal, Clock) plus a standalone reference-string lab.
#pragma once
#include <climits>
#include "../common/types.hpp"

namespace owr {

struct Frame {
    bool used = false;
    int pid = -1, page = -1;
    int loadedAt = 0, lastUsed = 0;
    bool ref = false;     // Clock reference bit
    bool pinned = false;  // being used by an in-flight page fault; cannot be evicted
};

// Distance (in future references) until a frame's page is used again; INT_MAX if never.
using Oracle = std::function<long(const Frame&)>;

class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;
    virtual ReplPolicy kind() const = 0;
    // Returns the index of the frame to evict, or -1 when every frame is pinned.
    virtual int pickVictim(std::vector<Frame>& f, int now, const Oracle& oracle) = 0;
protected:
    template <class Better>
    static int best(const std::vector<Frame>& f, Better better) {
        int b = -1;
        for (int i = 0; i < (int)f.size(); ++i) {
            if (!f[i].used || f[i].pinned) continue;
            if (b < 0 || better(f[i], f[b])) b = i;
        }
        return b;
    }
};

struct FifoPolicy : ReplacementPolicy {
    ReplPolicy kind() const override { return ReplPolicy::FIFO; }
    int pickVictim(std::vector<Frame>& f, int, const Oracle&) override {
        return best(f, [](const Frame& a, const Frame& b) { return a.loadedAt < b.loadedAt; });
    }
};
struct LruPolicy : ReplacementPolicy {
    ReplPolicy kind() const override { return ReplPolicy::LRU; }
    int pickVictim(std::vector<Frame>& f, int, const Oracle&) override {
        return best(f, [](const Frame& a, const Frame& b) { return a.lastUsed < b.lastUsed; });
    }
};
struct OptimalPolicy : ReplacementPolicy {
    ReplPolicy kind() const override { return ReplPolicy::OPTIMAL; }
    int pickVictim(std::vector<Frame>& f, int, const Oracle& o) override {
        if (!o) return best(f, [](const Frame& a, const Frame& b) { return a.lastUsed < b.lastUsed; });
        int b = -1; long bd = -1;
        for (int i = 0; i < (int)f.size(); ++i) {
            if (!f[i].used || f[i].pinned) continue;
            long d = o(f[i]);
            if (d > bd) { bd = d; b = i; }
        }
        return b;
    }
};
// Second-chance (clock): sweep a hand over the frames, clearing reference bits until one is clear.
struct ClockPolicy : ReplacementPolicy {
    int hand = 0;
    ReplPolicy kind() const override { return ReplPolicy::CLOCK; }
    int pickVictim(std::vector<Frame>& f, int, const Oracle&) override {
        int n = (int)f.size();
        for (int step = 0; step < 2 * n + 1; ++step) {
            int i = hand;
            hand = (hand + 1) % n;
            if (!f[i].used || f[i].pinned) continue;
            if (f[i].ref) f[i].ref = false;
            else return i;
        }
        return -1;
    }
};

inline std::unique_ptr<ReplacementPolicy> makeReplacement(ReplPolicy p) {
    switch (p) {
        case ReplPolicy::FIFO: return std::make_unique<FifoPolicy>();
        case ReplPolicy::LRU: return std::make_unique<LruPolicy>();
        case ReplPolicy::OPTIMAL: return std::make_unique<OptimalPolicy>();
        case ReplPolicy::CLOCK: return std::make_unique<ClockPolicy>();
    }
    return std::make_unique<LruPolicy>();
}

// ---- standalone reference-string lab (exact Optimal, since the whole string is known) --------
struct LabStep {
    int ref = 0;
    bool fault = false;
    int victim = -1;                 // page evicted, -1 if none
    std::vector<int> frames;         // page in each frame after the step, -1 = empty
};
struct LabResult {
    ReplPolicy policy = ReplPolicy::FIFO;
    std::vector<LabStep> steps;
    int faults = 0, hits = 0;
};

inline LabResult simulateReplacement(const std::vector<int>& refs, int nFrames, ReplPolicy pol) {
    LabResult r;
    r.policy = pol;
    nFrames = std::max(1, nFrames);
    std::vector<Frame> fr(nFrames);
    auto policy = makeReplacement(pol);
    size_t pos = 0;
    Oracle oracle = [&](const Frame& f) -> long {
        for (size_t j = pos + 1; j < refs.size(); ++j) if (refs[j] == f.page) return (long)(j - pos);
        return INT_MAX;
    };
    for (pos = 0; pos < refs.size(); ++pos) {
        int now = (int)pos + 1, page = refs[pos];
        LabStep st;
        st.ref = page;
        int hit = -1;
        for (int i = 0; i < nFrames; ++i) if (fr[i].used && fr[i].page == page) hit = i;
        if (hit >= 0) { fr[hit].lastUsed = now; fr[hit].ref = true; ++r.hits; }
        else {
            st.fault = true;
            ++r.faults;
            int slot = -1;
            for (int i = 0; i < nFrames; ++i) if (!fr[i].used) { slot = i; break; }
            if (slot < 0) { slot = policy->pickVictim(fr, now, oracle); st.victim = fr[slot].page; }
            fr[slot] = Frame{true, 0, page, now, now, true, false};
        }
        for (auto& f : fr) st.frames.push_back(f.used ? f.page : -1);
        r.steps.push_back(st);
    }
    return r;
}

}  // namespace owr
