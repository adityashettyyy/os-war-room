// Physical frames, per-process page tables, demand paging and address translation.
#pragma once
#include "replacement.hpp"

namespace owr {

struct PTE {
    int frame = -1;
    bool present = false;
};

struct Translation {
    int page = 0, offset = 0, frame = -1;
    long physical = -1;
    bool present = false, valid = true;   // valid=false: address outside the process's virtual space
};

class MemoryManager {
public:
    struct FaultResult {
        bool ok = false;        // false: no evictable frame (all pinned)
        int frame = -1;
        bool evicted = false;
        int victimPid = -1, victimPage = -1;
    };

    MemoryManager(int frames, int pageSizeKB, ReplPolicy p)
        : frames_((size_t)std::max(1, frames)), pageKB_(std::max(1, pageSizeKB)), policy_(makeReplacement(p)), kind_(p) {}

    void setPolicy(ReplPolicy p) { policy_ = makeReplacement(p); kind_ = p; }
    ReplPolicy policy() const { return kind_; }
    int totalFrames() const { return (int)frames_.size(); }
    int pageSizeKB() const { return pageKB_; }
    int usedFrames() const { int n = 0; for (auto& f : frames_) n += f.used; return n; }
    const std::vector<Frame>& frames() const { return frames_; }
    const std::vector<PTE>* table(int pid) const { auto it = tables_.find(pid); return it == tables_.end() ? nullptr : &it->second; }

    void createSpace(int pid, int vpages) { tables_[pid].assign((size_t)std::max(1, vpages), PTE{}); }
    void resize(int pid, int vpages) {
        auto& t = tables_[pid];
        vpages = std::max(1, vpages);
        for (size_t i = (size_t)vpages; i < t.size(); ++i) if (t[i].present) { frames_[(size_t)t[i].frame] = Frame{}; }
        t.resize((size_t)vpages);
    }
    // Free every frame of the process; the page table survives (swap-out) or is dropped (exit).
    int evictAll(int pid) {
        int n = 0;
        auto it = tables_.find(pid);
        if (it == tables_.end()) return 0;
        for (auto& e : it->second) if (e.present) { frames_[(size_t)e.frame] = Frame{}; e = PTE{}; ++n; }
        return n;
    }
    int release(int pid) { int n = evictAll(pid); tables_.erase(pid); return n; }

    bool present(int pid, int page) const {
        auto* t = table(pid);
        return t && page >= 0 && page < (int)t->size() && (*t)[(size_t)page].present;
    }
    int frameOf(int pid, int page) const { return present(pid, page) ? (*table(pid))[(size_t)page].frame : -1; }
    void touch(int pid, int page, int now) {
        int f = frameOf(pid, page);
        if (f >= 0) { frames_[(size_t)f].lastUsed = now; frames_[(size_t)f].ref = true; }
    }
    void unpin(int pid, int page) { int f = frameOf(pid, page); if (f >= 0) frames_[(size_t)f].pinned = false; }

    // Bring (pid, page) into memory, evicting a victim if needed. The new frame is pinned until
    // the faulting process re-executes its reference, so it cannot be stolen in the meantime.
    FaultResult load(int pid, int page, int now, const Oracle& oracle) {
        FaultResult r;
        int slot = -1;
        for (int i = 0; i < (int)frames_.size(); ++i) if (!frames_[(size_t)i].used) { slot = i; break; }
        if (slot < 0) {
            slot = policy_->pickVictim(frames_, now, oracle);
            if (slot < 0) return r;
            Frame& v = frames_[(size_t)slot];
            r.evicted = true; r.victimPid = v.pid; r.victimPage = v.page;
            auto it = tables_.find(v.pid);
            if (it != tables_.end() && v.page >= 0 && v.page < (int)it->second.size()) it->second[(size_t)v.page] = PTE{};
            ++replacements_;
        }
        frames_[(size_t)slot] = Frame{true, pid, page, now, now, true, true};
        auto& t = tables_[pid];
        if (page >= (int)t.size()) t.resize((size_t)page + 1);
        t[(size_t)page] = PTE{slot, true};
        r.ok = true; r.frame = slot;
        ++loads_;
        return r;
    }

    // Virtual address -> (page, offset) -> frame -> physical address.
    Translation translate(int pid, long vaddr) const {
        Translation t;
        long ps = (long)pageKB_ * 1024;
        t.page = (int)(vaddr / ps);
        t.offset = (int)(vaddr % ps);
        auto* pt = table(pid);
        t.valid = pt && vaddr >= 0 && t.page < (int)pt->size();
        if (!t.valid) return t;
        const PTE& e = (*pt)[(size_t)t.page];
        t.present = e.present;
        if (e.present) { t.frame = e.frame; t.physical = (long)e.frame * ps + t.offset; }
        return t;
    }

    long replacements() const { return replacements_; }
    long loads() const { return loads_; }

private:
    std::vector<Frame> frames_;
    int pageKB_;
    std::map<int, std::vector<PTE>> tables_;
    std::unique_ptr<ReplacementPolicy> policy_;
    ReplPolicy kind_;
    long replacements_ = 0, loads_ = 0;
};

}  // namespace owr
