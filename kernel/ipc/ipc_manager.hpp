// Inter-process communication: bounded message queues and shared-memory segments.
#pragma once
#include "../common/types.hpp"

namespace owr {

struct Message {
    int from = -1;
    std::string text;
    int tick = 0;
};
struct MsgQueue {
    std::string name;
    int capacity = 4;
    std::deque<Message> msgs;
    std::deque<int> recvWaiters;       // blocked receivers
    std::deque<Message> sendWaiters;   // blocked senders (their message is parked here)
    long sent = 0, received = 0, sendBlocks = 0, recvBlocks = 0;
};
struct ShmAccess { int tick = 0, pid = -1, index = 0, value = 0; bool write = false; };
struct SharedSegment {
    std::string name;
    std::vector<int> data = std::vector<int>(8, 0);
    std::deque<ShmAccess> history;
    long reads = 0, writes = 0;
};

class IpcManager {
public:
    std::map<std::string, MsgQueue> queues;
    std::map<std::string, SharedSegment> segments;
    int defaultCapacity = 4;

    MsgQueue& queue(const std::string& n) {
        auto& q = queues[n];
        if (q.name.empty()) { q.name = n; q.capacity = defaultCapacity; }
        return q;
    }
    void createQueue(const std::string& n, int cap) { queue(n).capacity = std::max(1, cap); }
    SharedSegment& segment(const std::string& n, int words = 8) {
        auto& s = segments[n];
        if (s.name.empty()) { s.name = n; s.data.assign((size_t)std::max(1, words), 0); }
        return s;
    }

    struct SendResult { bool blocked = false; int deliverTo = -1; };
    // If a receiver is already waiting the message is handed straight to it (deliverTo >= 0).
    SendResult send(int pid, const std::string& qn, const std::string& text, int tick) {
        auto& q = queue(qn);
        SendResult r;
        Message m{pid, text, tick};
        ++q.sent;
        if (!q.recvWaiters.empty()) { r.deliverTo = q.recvWaiters.front(); q.recvWaiters.pop_front(); ++q.received; return r; }
        if ((int)q.msgs.size() < q.capacity) { q.msgs.push_back(m); return r; }
        q.sendWaiters.push_back(m); ++q.sendBlocks; r.blocked = true;
        return r;
    }
    struct RecvResult { bool blocked = false; Message msg; int wokenSender = -1; };
    RecvResult recv(int pid, const std::string& qn) {
        auto& q = queue(qn);
        RecvResult r;
        if (q.msgs.empty()) { q.recvWaiters.push_back(pid); ++q.recvBlocks; r.blocked = true; return r; }
        r.msg = q.msgs.front(); q.msgs.pop_front(); ++q.received;
        if (!q.sendWaiters.empty()) { q.msgs.push_back(q.sendWaiters.front()); r.wokenSender = q.sendWaiters.front().from; q.sendWaiters.pop_front(); }
        return r;
    }
    void removePid(int pid) {
        for (auto& [n, q] : queues) {
            q.recvWaiters.erase(std::remove(q.recvWaiters.begin(), q.recvWaiters.end(), pid), q.recvWaiters.end());
            q.sendWaiters.erase(std::remove_if(q.sendWaiters.begin(), q.sendWaiters.end(), [&](const Message& m) { return m.from == pid; }), q.sendWaiters.end());
        }
    }
    std::string waitingOn(int pid) const {
        for (auto& [n, q] : queues) {
            for (int w : q.recvWaiters) if (w == pid) return n;
            for (auto& m : q.sendWaiters) if (m.from == pid) return n;
        }
        return "";
    }

    int shmRead(const std::string& sn, int idx, int pid, int tick) {
        auto& s = segment(sn);
        int i = std::clamp(idx, 0, (int)s.data.size() - 1);
        ++s.reads;
        record(s, {tick, pid, i, s.data[(size_t)i], false});
        return s.data[(size_t)i];
    }
    void shmWrite(const std::string& sn, int idx, int value, int pid, int tick) {
        auto& s = segment(sn);
        int i = std::clamp(idx, 0, (int)s.data.size() - 1);
        s.data[(size_t)i] = value; ++s.writes;
        record(s, {tick, pid, i, value, true});
    }
private:
    static void record(SharedSegment& s, ShmAccess a) { s.history.push_back(a); if (s.history.size() > 40) s.history.pop_front(); }
};

}  // namespace owr
