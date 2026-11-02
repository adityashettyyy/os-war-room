// Mutexes, counting semaphores and reader-writer locks, plus wait-for-graph deadlock detection.
// Every blocking primitive hands ownership directly to the next waiter on release, so a woken
// process never has to retry: it resumes *after* the instruction that blocked it.
#pragma once
#include "../common/types.hpp"

namespace owr {

struct MutexRes {
    std::string name;
    int owner = -1;
    std::deque<int> waiters;
    long acquisitions = 0, contentions = 0;
};
struct SemaphoreRes {
    std::string name;
    int value = 1, initial = 1;
    std::deque<int> waiters;
    long waits = 0, blocks = 0;
};
struct RwLockRes {
    std::string name;
    std::set<int> readers;
    int writer = -1;
    std::deque<std::pair<int, bool>> waiters;   // (pid, wantsWrite)
    long acquisitions = 0, contentions = 0;
};

class SyncManager {
public:
    std::map<std::string, MutexRes> mutexes;
    std::map<std::string, SemaphoreRes> semaphores;
    std::map<std::string, RwLockRes> rwlocks;

    void createSemaphore(const std::string& n, int init) {
        auto& s = semaphores[n];
        s.name = n; s.value = s.initial = init;
    }
    void createMutex(const std::string& n) { mutexes[n].name = n; }

    // ---- mutex ----
    bool lock(const std::string& n, int pid) {            // true: acquired, false: caller must block
        auto& m = mutexes[n]; m.name = n;
        if (m.owner < 0) { m.owner = pid; ++m.acquisitions; return true; }
        m.waiters.push_back(pid); ++m.contentions; ++blocks_;
        return false;
    }
    // Returns pids woken (granted the lock). ok=false if pid does not own the mutex.
    std::vector<int> unlock(const std::string& n, int pid, bool& ok) {
        std::vector<int> woke;
        auto it = mutexes.find(n);
        ok = it != mutexes.end() && it->second.owner == pid;
        if (!ok) return woke;
        auto& m = it->second;
        if (m.waiters.empty()) m.owner = -1;
        else { m.owner = m.waiters.front(); m.waiters.pop_front(); ++m.acquisitions; woke.push_back(m.owner); }
        return woke;
    }

    // ---- semaphore ----
    bool semWait(const std::string& n, int pid) {
        auto& s = semaphores[n];
        if (s.name.empty()) { s.name = n; s.value = s.initial = 1; }
        ++s.waits;
        if (s.value > 0) { --s.value; return true; }
        s.waiters.push_back(pid); ++s.blocks; ++blocks_;
        return false;
    }
    std::vector<int> semSignal(const std::string& n) {
        auto& s = semaphores[n];
        if (s.name.empty()) { s.name = n; s.value = s.initial = 1; s.value = 0; }
        if (!s.waiters.empty()) { int p = s.waiters.front(); s.waiters.pop_front(); return {p}; }
        ++s.value;
        return {};
    }

    // ---- reader-writer lock (FIFO fair: readers queue behind a waiting writer) ----
    bool rdlock(const std::string& n, int pid) {
        auto& l = rwlocks[n]; l.name = n;
        if (l.writer < 0 && l.waiters.empty()) { l.readers.insert(pid); ++l.acquisitions; return true; }
        l.waiters.emplace_back(pid, false); ++l.contentions; ++blocks_;
        return false;
    }
    bool wrlock(const std::string& n, int pid) {
        auto& l = rwlocks[n]; l.name = n;
        if (l.writer < 0 && l.readers.empty() && l.waiters.empty()) { l.writer = pid; ++l.acquisitions; return true; }
        l.waiters.emplace_back(pid, true); ++l.contentions; ++blocks_;
        return false;
    }
    std::vector<int> rwunlock(const std::string& n, int pid, bool& ok) {
        std::vector<int> woke;
        auto it = rwlocks.find(n);
        ok = false;
        if (it == rwlocks.end()) return woke;
        auto& l = it->second;
        if (l.writer == pid) { l.writer = -1; ok = true; }
        else if (l.readers.erase(pid)) ok = true;
        if (ok) grant(l, woke);
        return woke;
    }

    // Process died or was rolled back: drop its holdings and queue positions; returns pids woken.
    std::vector<int> releaseAll(int pid) {
        std::vector<int> woke;
        for (auto& [n, m] : mutexes) {
            m.waiters.erase(std::remove(m.waiters.begin(), m.waiters.end(), pid), m.waiters.end());
            if (m.owner == pid) {
                if (m.waiters.empty()) m.owner = -1;
                else { m.owner = m.waiters.front(); m.waiters.pop_front(); ++m.acquisitions; woke.push_back(m.owner); }
            }
        }
        for (auto& [n, s] : semaphores) s.waiters.erase(std::remove(s.waiters.begin(), s.waiters.end(), pid), s.waiters.end());
        for (auto& [n, l] : rwlocks) {
            l.waiters.erase(std::remove_if(l.waiters.begin(), l.waiters.end(), [&](auto& w) { return w.first == pid; }), l.waiters.end());
            bool changed = l.readers.erase(pid) > 0;
            if (l.writer == pid) { l.writer = -1; changed = true; }
            if (changed || !l.waiters.empty()) grant(l, woke);
        }
        return woke;
    }

    std::vector<std::string> heldBy(int pid) const {
        std::vector<std::string> r;
        for (auto& [n, m] : mutexes) if (m.owner == pid) r.push_back(n);
        for (auto& [n, l] : rwlocks) {
            if (l.writer == pid) r.push_back(n + "(w)");
            else if (l.readers.count(pid)) r.push_back(n + "(r)");
        }
        return r;
    }
    std::string waitingOn(int pid) const {
        for (auto& [n, m] : mutexes) for (int w : m.waiters) if (w == pid) return n;
        for (auto& [n, s] : semaphores) for (int w : s.waiters) if (w == pid) return n;
        for (auto& [n, l] : rwlocks) for (auto& w : l.waiters) if (w.first == pid) return n;
        return "";
    }
    long blockEvents() const { return blocks_; }

    // ---- deadlock detection ----
    // Wait-for graph over blocked processes: p -> q when p waits for a resource q currently holds.
    std::map<int, std::set<int>> waitForGraph() const {
        std::map<int, std::set<int>> g;
        for (auto& [n, m] : mutexes) for (int w : m.waiters) if (m.owner >= 0) g[w].insert(m.owner);
        for (auto& [n, l] : rwlocks) {
            for (size_t i = 0; i < l.waiters.size(); ++i) {
                int w = l.waiters[i].first;
                bool wantsWrite = l.waiters[i].second;
                if (l.writer >= 0) g[w].insert(l.writer);
                if (wantsWrite) for (int r : l.readers) g[w].insert(r);
                if (!wantsWrite && l.writer < 0) for (size_t j = 0; j < i; ++j) if (l.waiters[j].second) g[w].insert(l.waiters[j].first);
            }
        }
        return g;
    }
    // Strongly connected components with more than one process (or a self-wait) are deadlocks.
    std::vector<std::vector<int>> findDeadlocks() const {
        auto g = waitForGraph();
        std::map<int, int> idx, low;
        std::vector<int> stack;
        std::set<int> on;
        std::vector<std::vector<int>> out;
        int counter = 0;
        std::function<void(int)> dfs = [&](int v) {
            idx[v] = low[v] = counter++;
            stack.push_back(v); on.insert(v);
            auto it = g.find(v);
            if (it != g.end())
                for (int w : it->second) {
                    if (!idx.count(w)) { dfs(w); low[v] = std::min(low[v], low[w]); }
                    else if (on.count(w)) low[v] = std::min(low[v], idx[w]);
                }
            if (low[v] == idx[v]) {
                std::vector<int> comp;
                int w;
                do { w = stack.back(); stack.pop_back(); on.erase(w); comp.push_back(w); } while (w != v);
                bool self = g.count(v) && g.at(v).count(v);
                if (comp.size() > 1 || self) { std::sort(comp.begin(), comp.end()); out.push_back(comp); }
            }
        };
        for (auto& [v, _] : g) if (!idx.count(v)) dfs(v);
        std::sort(out.begin(), out.end());
        return out;
    }
    // Resources a deadlocked group is stuck on: name -> owner, for the report.
    std::vector<std::pair<std::string, int>> blockedResources(const std::vector<int>& group) const {
        std::vector<std::pair<std::string, int>> r;
        std::set<int> gs(group.begin(), group.end());
        for (auto& [n, m] : mutexes) for (int w : m.waiters) if (gs.count(w) && gs.count(m.owner)) r.emplace_back(n, m.owner);
        return r;
    }

private:
    void grant(RwLockRes& l, std::vector<int>& woke) {
        while (!l.waiters.empty()) {
            auto [pid, write] = l.waiters.front();
            if (write) {
                if (l.writer < 0 && l.readers.empty()) { l.writer = pid; l.waiters.pop_front(); ++l.acquisitions; woke.push_back(pid); }
                break;
            }
            if (l.writer >= 0) break;
            l.readers.insert(pid); l.waiters.pop_front(); ++l.acquisitions; woke.push_back(pid);
        }
    }
    long blocks_ = 0;
};

}  // namespace owr
