// Banker's algorithm: safe-state analysis and resource-request admission (optional module).
#pragma once
#include "../common/types.hpp"

namespace owr {

struct Banker {
    std::vector<int> available;                 // free instances per resource type
    std::vector<std::vector<int>> maxDemand;    // [process][resource]
    std::vector<std::vector<int>> allocation;   // [process][resource]

    int processes() const { return (int)allocation.size(); }
    int resources() const { return (int)available.size(); }
    std::vector<int> need(int i) const {
        std::vector<int> n(available.size());
        for (size_t j = 0; j < n.size(); ++j) n[j] = maxDemand[(size_t)i][j] - allocation[(size_t)i][j];
        return n;
    }
    bool wellFormed() const {
        if (maxDemand.size() != allocation.size()) return false;
        for (size_t i = 0; i < allocation.size(); ++i) {
            if (allocation[i].size() != available.size() || maxDemand[i].size() != available.size()) return false;
            for (size_t j = 0; j < available.size(); ++j) if (allocation[i][j] > maxDemand[i][j] || allocation[i][j] < 0) return false;
        }
        return true;
    }
    // Returns true and fills `sequence` with a safe completion order when the state is safe.
    bool isSafe(std::vector<int>& sequence) const {
        sequence.clear();
        std::vector<int> work = available;
        std::vector<bool> done((size_t)processes(), false);
        for (int round = 0; round < processes(); ++round) {
            bool progressed = false;
            for (int i = 0; i < processes(); ++i) {
                if (done[(size_t)i]) continue;
                auto n = need(i);
                bool ok = true;
                for (size_t j = 0; j < n.size(); ++j) if (n[j] > work[j]) { ok = false; break; }
                if (!ok) continue;
                for (size_t j = 0; j < work.size(); ++j) work[j] += allocation[(size_t)i][j];
                done[(size_t)i] = true; sequence.push_back(i); progressed = true;
            }
            if (!progressed) break;
        }
        return (int)sequence.size() == processes();
    }
    // Grants the request only if it stays within need, fits in `available` and leaves a safe state.
    bool request(int i, const std::vector<int>& req, std::string& why) {
        auto n = need(i);
        for (size_t j = 0; j < req.size(); ++j) {
            if (req[j] > n[j]) { why = "request exceeds declared maximum"; return false; }
            if (req[j] > available[j]) { why = "resources not available (process must wait)"; return false; }
        }
        Banker trial = *this;
        for (size_t j = 0; j < req.size(); ++j) { trial.available[j] -= req[j]; trial.allocation[(size_t)i][j] += req[j]; }
        std::vector<int> seq;
        if (!trial.isSafe(seq)) { why = "granting would leave the system in an unsafe state"; return false; }
        *this = trial;
        why = "granted: state remains safe";
        return true;
    }
};

}  // namespace owr
