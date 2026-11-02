// Simulated devices with request queues, write-behind buffering (DISK, NETWORK),
// printer spooling (PRINTER) and failure/repair handling.
#pragma once
#include "../common/config.hpp"

namespace owr {

struct IORequest {
    int id = 0, pid = -1;
    bool write = false;
    int duration = 1, remaining = 1;
    int enq = 0, start = -1;
    std::string doc;
    bool sync = true;     // the issuing process is blocked until this request completes
};

class Device {
public:
    enum class Submit { ASYNC_ACCEPTED, QUEUED_BLOCKING, PARKED_FULL, REJECTED };
    struct Event {
        enum Kind { DONE, STARTED, PRODUCER_UNBLOCKED, RECOVERED } kind;
        IORequest req;
    };

    std::string name;
    bool buffered = false, spooled = false;
    int capacity = 0;                     // buffer slots / spool jobs
    std::deque<IORequest> queue;          // waiting for the device
    std::optional<IORequest> cur;         // in service
    std::deque<IORequest> parked;         // producers blocked because the buffer/spool is full
    int occupancy = 0;                    // buffered/spooled writes accepted but not yet completed
    bool failed = false;
    int failedUntil = -1;
    long completed = 0, busyTicks = 0, errors = 0, fullEvents = 0, producerBlocks = 0, consumerIdleTicks = 0, totalQueueWait = 0, started = 0;
    long lostWrites = 0;
    int maxQueue = 0;
    long queueLenSum = 0;                 // for the time-averaged queue length

    Device(std::string n, bool buf, bool spool, int cap) : name(std::move(n)), buffered(buf), spooled(spool), capacity(cap) {}

    int queueLength() const { return (int)queue.size() + (cur ? 1 : 0) + (int)parked.size(); }
    bool bufferedWrites() const { return buffered || spooled; }

    Submit submit(IORequest r, int now) {
        if (failed) { ++errors; return Submit::REJECTED; }
        r.enq = now; r.remaining = r.duration;
        Submit res;
        if (r.write && bufferedWrites()) {
            if (occupancy < capacity) { ++occupancy; r.sync = false; queue.push_back(r); res = Submit::ASYNC_ACCEPTED; }
            else { ++fullEvents; ++producerBlocks; r.sync = true; parked.push_back(r); res = Submit::PARKED_FULL; }
        } else { r.sync = true; queue.push_back(r); res = Submit::QUEUED_BLOCKING; }
        maxQueue = std::max(maxQueue, queueLength());
        return res;
    }

    // One tick of device time. Completion is reported one tick after the last service tick.
    void advance(int now, std::vector<Event>& ev) {
        if (failed) {
            if (now >= failedUntil) { failed = false; ev.push_back({Event::RECOVERED, {}}); }
            else return;
        }
        if (cur && cur->remaining <= 0) {
            ++completed;
            if (cur->write && bufferedWrites()) --occupancy;
            ev.push_back({Event::DONE, *cur});
            cur.reset();
            if (!parked.empty() && occupancy < capacity) {
                IORequest r = parked.front(); parked.pop_front();
                r.sync = false; ++occupancy; queue.push_back(r);
                ev.push_back({Event::PRODUCER_UNBLOCKED, r});
            }
        }
        if (!cur && !queue.empty()) {
            cur = queue.front(); queue.pop_front();
            cur->start = now; totalQueueWait += now - cur->enq; ++started;
            ev.push_back({Event::STARTED, *cur});
        }
        if (cur) { ++busyTicks; --cur->remaining; }
        else if (bufferedWrites() && occupancy == 0) ++consumerIdleTicks;
        queueLenSum += queueLength();
    }

    // Take the device offline; returns every request that was in the system (callers wake the blocked ones).
    std::vector<IORequest> fail(int now, int duration) {
        std::vector<IORequest> aborted;
        if (cur) aborted.push_back(*cur);
        for (auto& r : queue) aborted.push_back(r);
        for (auto& r : parked) aborted.push_back(r);
        for (auto& r : aborted) if (!r.sync) ++lostWrites;
        cur.reset(); queue.clear(); parked.clear(); occupancy = 0;
        failed = true; failedUntil = now + std::max(1, duration);
        return aborted;
    }
    // A process died: forget its blocking requests (data already accepted into a buffer stays).
    void removePid(int pid) {
        auto drop = [&](std::deque<IORequest>& q) { q.erase(std::remove_if(q.begin(), q.end(), [&](const IORequest& r) { return r.pid == pid && r.sync; }), q.end()); };
        drop(queue); drop(parked);
        if (cur && cur->pid == pid && cur->sync) cur->pid = -1;   // orphaned; completes silently
    }
};

class IOManager {
public:
    explicit IOManager(const Config& c) {
        devices_.emplace_back("DISK", true, false, c.diskBuffer);
        devices_.emplace_back("PRINTER", false, true, c.spoolCapacity);
        devices_.emplace_back("KEYBOARD", false, false, 0);
        devices_.emplace_back("NETWORK", true, false, c.diskBuffer);
    }
    Device* get(const std::string& n) { for (auto& d : devices_) if (d.name == n) return &d; return nullptr; }
    const Device* get(const std::string& n) const { for (auto& d : devices_) if (d.name == n) return &d; return nullptr; }
    std::vector<Device>& devices() { return devices_; }
    const std::vector<Device>& devices() const { return devices_; }
    void removePid(int pid) { for (auto& d : devices_) d.removePid(pid); }
    int totalQueue() const { int n = 0; for (auto& d : devices_) n += d.queueLength(); return n; }
    int maxQueueNow() const { int n = 0; for (auto& d : devices_) n = std::max(n, d.queueLength()); return n; }
private:
    std::vector<Device> devices_;
};

}  // namespace owr
