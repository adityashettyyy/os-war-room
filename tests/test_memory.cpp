#include "helpers.hpp"
using namespace th;

static const std::vector<int> kRefs{7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1};

TEST(replacement_lab_matches_textbook_fault_counts) {
    CHECK_EQ(simulateReplacement(kRefs, 3, ReplPolicy::FIFO).faults, 15);
    CHECK_EQ(simulateReplacement(kRefs, 3, ReplPolicy::LRU).faults, 12);
    CHECK_EQ(simulateReplacement(kRefs, 3, ReplPolicy::OPTIMAL).faults, 9);
    int clock = simulateReplacement(kRefs, 3, ReplPolicy::CLOCK).faults;
    CHECK(clock >= 9 && clock <= 15);
}

TEST(replacement_lab_reports_victims_and_frames) {
    auto r = simulateReplacement(kRefs, 3, ReplPolicy::LRU);
    CHECK(r.steps[0].fault);
    CHECK_EQ(r.steps[3].victim, 7);                    // 2 evicts the least recently used page, 7
    CHECK(!r.steps[4].fault);                          // 0 is a hit
    CHECK_EQ((int)r.steps.back().frames.size(), 3);
    CHECK_EQ(r.faults + r.hits, (int)kRefs.size());
}

TEST(belady_anomaly_is_reproduced_for_fifo_not_lru) {
    std::vector<int> s{1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    CHECK_EQ(simulateReplacement(s, 3, ReplPolicy::FIFO).faults, 9);
    CHECK_EQ(simulateReplacement(s, 4, ReplPolicy::FIFO).faults, 10);
    CHECK(simulateReplacement(s, 4, ReplPolicy::LRU).faults <= simulateReplacement(s, 3, ReplPolicy::LRU).faults);
}

TEST(optimal_never_worse_than_other_policies) {
    Rng rng(7);
    std::vector<int> s;
    for (int i = 0; i < 200; ++i) s.push_back(rng.range(0, 9));
    for (int frames = 2; frames <= 6; ++frames) {
        int opt = simulateReplacement(s, frames, ReplPolicy::OPTIMAL).faults;
        for (auto p : {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK}) CHECK(opt <= simulateReplacement(s, frames, p).faults);
    }
}

TEST(address_translation_splits_page_and_offset) {
    MemoryManager m(4, 4, ReplPolicy::LRU);
    m.createSpace(1, 8);
    auto t0 = m.translate(1, 5000);                    // page 1, offset 904
    CHECK_EQ(t0.page, 1); CHECK_EQ(t0.offset, 904); CHECK(!t0.present); CHECK(t0.valid);
    auto r = m.load(1, 1, 10, nullptr);
    REQUIRE(r.ok);
    auto t1 = m.translate(1, 5000);
    CHECK(t1.present);
    CHECK_EQ(t1.physical, (long)r.frame * 4096 + 904);
    CHECK(!m.translate(1, 8 * 4096).valid);            // beyond the address space
}

TEST(pinned_frames_cannot_be_evicted_until_unpinned) {
    MemoryManager m(2, 4, ReplPolicy::LRU);
    m.createSpace(1, 4);
    REQUIRE(m.load(1, 0, 1, nullptr).ok);
    REQUIRE(m.load(1, 1, 2, nullptr).ok);
    CHECK(!m.load(1, 2, 3, nullptr).ok);               // both frames pinned
    m.unpin(1, 0);
    auto r = m.load(1, 2, 4, nullptr);
    REQUIRE(r.ok);
    CHECK(r.evicted); CHECK_EQ(r.victimPage, 0);
    CHECK(!m.present(1, 0));
}

TEST(releasing_a_process_frees_all_its_frames) {
    MemoryManager m(4, 4, ReplPolicy::FIFO);
    m.createSpace(1, 4); m.createSpace(2, 4);
    for (int i = 0; i < 3; ++i) { m.load(1, i, i, nullptr); m.unpin(1, i); }
    m.load(2, 0, 5, nullptr);
    CHECK_EQ(m.usedFrames(), 4);
    CHECK_EQ(m.release(1), 3);
    CHECK_EQ(m.usedFrames(), 1);
}

TEST(fifo_evicts_oldest_and_clock_gives_second_chance) {
    MemoryManager f(2, 4, ReplPolicy::FIFO);
    f.createSpace(1, 4);
    f.load(1, 0, 1, nullptr); f.unpin(1, 0);
    f.load(1, 1, 2, nullptr); f.unpin(1, 1);
    f.touch(1, 0, 3);                                   // recent use must not save it under FIFO
    CHECK_EQ(f.load(1, 2, 4, nullptr).victimPage, 0);
    MemoryManager c(2, 4, ReplPolicy::CLOCK);
    c.createSpace(1, 4);
    c.load(1, 0, 1, nullptr); c.unpin(1, 0);
    c.load(1, 1, 2, nullptr); c.unpin(1, 1);
    auto r = c.load(1, 2, 3, nullptr);                  // all ref bits set: first sweep clears them, hand returns to frame 0
    CHECK(r.evicted);
}

TEST(demand_paging_page_fault_chain_is_logged_in_order) {
    Config c = textbook(SchedPolicy::FCFS); c.physicalKB = 64; c.ctxSwitchCost = 0;
    Kernel k(c);
    ProcessSpec s; s.cpuBurst = 10; s.memoryKB = 16; s.priority = 5;
    int p = k.createProcess(s);
    runUntilDone(k);
    CHECK(k.find(p)->pageFaults >= 1);
    size_t a = logPos(k, "PAGE FAULT"), b = logPos(k, "PAGE LOAD", a), d = logPos(k, "I/O COMPLETE (swap)", b);
    CHECK(a != std::string::npos && b != std::string::npos);
    CHECK(a < b);
    CHECK(d != std::string::npos);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    CHECK_EQ(k.memory().usedFrames(), 0);               // frames released on exit
}

TEST(page_fault_costs_latency_and_resumes_the_same_reference) {
    Config c = textbook(SchedPolicy::FCFS); c.pageFaultLatency = 5;
    Kernel k(c);
    ProcessSpec s; s.script = assemble({"COMPUTE 1"}); s.memoryKB = 4; s.memoryRefs = true;
    int p = k.createProcess(s);
    runUntilDone(k);
    // tick 0 faults; handled in-tick; page-in completes 1+5 ticks later; retry executes then.
    CHECK_EQ(k.find(p)->completion, 7);
    CHECK_EQ(k.find(p)->pageFaults, 1);
    CHECK_EQ(k.find(p)->memRefs, 1);                    // the retry is not counted as a new reference
}

TEST(small_memory_forces_replacement_and_never_exceeds_frames) {
    for (auto pol : {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK, ReplPolicy::OPTIMAL}) {
        Config c = textbook(SchedPolicy::RR, 4); c.physicalKB = 16; c.repl = pol; c.ctxSwitchCost = 1;
        Kernel k(c);
        for (int i = 0; i < 3; ++i) { ProcessSpec s; s.cpuBurst = 60; s.memoryKB = 48; k.createProcess(s); }
        long maxUsed = 0;
        while (!k.allDone() && k.now() < 20000) { k.tick(); maxUsed = std::max<long>(maxUsed, k.memory().usedFrames()); }
        CHECK(k.allDone());
        CHECK(maxUsed <= 4);
        CHECK(k.memory().replacements() > 0);
        CHECK_EQ(k.metrics().completed, 3);
    }
}

TEST(force_page_fault_makes_next_reference_fault_on_a_nonresident_page) {
    Config c = textbook(SchedPolicy::FCFS); c.physicalKB = 256;
    Kernel k(c);
    ProcessSpec s; s.cpuBurst = 60; s.memoryKB = 40; int p = k.createProcess(s);
    k.run(30);
    long before = k.metrics().pageFaults;
    int page = k.forcePageFault(p);
    REQUIRE(page >= 0);
    CHECK(!k.memory().present(p, page));
    k.run(12);
    CHECK(k.metrics().pageFaults > before);
    CHECK(k.memory().present(p, page));
}

TEST(segmentation_fault_when_referencing_freed_memory_terminates_process) {
    Kernel k(textbook(SchedPolicy::FCFS));
    ProcessSpec s; s.script = assemble({"COMPUTE 2"}); s.memoryKB = 16; int p = k.createProcess(s);
    k.find(p)->forcePage = 99;                         // reference outside the 4-page address space
    k.run(5);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    CHECK(k.find(p)->exitCode != 0);
    CHECK(logHas(k, "SEGMENTATION FAULT"));
    CHECK_EQ(k.crashDumps().size(), 1u);
}

TEST(thrashing_is_detected_when_working_sets_exceed_memory) {
    Config c; c.thrashingControl = false;
    Kernel k(c);
    failure::exhaustMemory(k, 16);
    k.run(400);
    CHECK(k.thrashing() || k.thrashingEvents() > 0);
    CHECK(k.metrics().pfRate > 0.5);
    CHECK(k.windowCpuUtil() < 0.5);
    CHECK(logHas(k, "THRASHING DETECTED"));
}

TEST(thrashing_control_suspends_victims_and_the_system_recovers) {
    Config c; c.thrashingControl = true;
    Kernel k(c);
    failure::exhaustMemory(k, 16);
    runUntilDone(k, 12000);
    CHECK(k.suspensions() > 0);
    CHECK(k.allDone());
    CHECK_EQ(k.metrics().completed, 16);
    CHECK(logHas(k, "swapped back in"));
}

TEST(alloc_and_free_syscalls_resize_the_address_space) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"ALLOC 4", "COMPUTE 2", "FREE 2", "COMPUTE 1"}, 5, -1, 8);
    k.run(2);
    CHECK_EQ(k.find(p)->vpages, 2 + 4);
    runUntilDone(k);
    CHECK_EQ(k.find(p)->vpages, 4);
}
