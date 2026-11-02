#include "helpers.hpp"
using namespace th;

TEST(fcfs_textbook_average_waiting_is_17) {
    Kernel k(textbook(SchedPolicy::FCFS));
    spawn(k, 24); spawn(k, 3); spawn(k, 3);
    runUntilDone(k);
    auto m = k.metrics();
    CHECK_EQ(m.completed, 3);
    CHECK_NEAR(m.avgWaiting, 17.0, 1e-9);
    CHECK_NEAR(m.avgTurnaround, (24 + 27 + 30) / 3.0, 1e-9);
    CHECK_EQ(m.makespan, 30);
    CHECK_EQ(m.ctxSwitches, 2);
}

TEST(round_robin_quantum4_textbook) {
    Kernel k(textbook(SchedPolicy::RR, 4));
    spawn(k, 24); spawn(k, 3); spawn(k, 3);
    runUntilDone(k);
    auto m = k.metrics();
    CHECK_NEAR(m.avgWaiting, 17.0 / 3, 1e-9);   // waits 6, 4, 7
    CHECK_EQ(k.find(2)->waitingTime, 4);
    CHECK_EQ(k.find(3)->waitingTime, 7);
    CHECK_EQ(m.ctxSwitches, 3);
    CHECK_NEAR(m.avgResponse, (0 + 4 + 7) / 3.0, 1e-9);
}

TEST(sjf_nonpreemptive_textbook_7_75) {
    Kernel k(textbook(SchedPolicy::SJF));
    spawn(k, 8, 0); spawn(k, 4, 1); spawn(k, 9, 2); spawn(k, 5, 3);
    runUntilDone(k);
    CHECK_NEAR(k.metrics().avgWaiting, 7.75, 1e-9);
}

TEST(srtf_preemptive_textbook_6_5) {
    Kernel k(textbook(SchedPolicy::SRTF));
    spawn(k, 8, 0); spawn(k, 4, 1); spawn(k, 9, 2); spawn(k, 5, 3);
    runUntilDone(k);
    auto m = k.metrics();
    CHECK_NEAR(m.avgWaiting, 6.5, 1e-9);
    CHECK_EQ(k.find(1)->waitingTime, 9);
    CHECK_EQ(k.find(2)->waitingTime, 0);
}

TEST(priority_textbook_8_2) {
    Kernel k(textbook(SchedPolicy::PRIORITY));
    spawn(k, 10, 0, 3); spawn(k, 1, 0, 1); spawn(k, 2, 0, 4); spawn(k, 1, 0, 5); spawn(k, 5, 0, 2);
    runUntilDone(k);
    CHECK_NEAR(k.metrics().avgWaiting, 8.2, 1e-9);
    CHECK_EQ(k.find(2)->waitingTime, 0);   // priority 1 runs first
    CHECK_EQ(k.find(4)->waitingTime, 18);  // priority 5 runs last
}

TEST(preemptive_priority_interrupts_lower_priority_running_process) {
    Kernel k(textbook(SchedPolicy::PRIORITY));
    int low = spawn(k, 10, 0, 7), hi = spawn(k, 3, 2, 1);
    runUntilDone(k);
    CHECK_EQ(k.find(hi)->firstRun, 2);                 // runs the moment it arrives
    CHECK_EQ(k.find(hi)->completion, 5);
    CHECK_EQ(k.find(low)->completion, 13);
}

TEST(context_switch_cost_adds_overhead_ticks) {
    Config c = textbook(SchedPolicy::RR, 2); c.ctxSwitchCost = 1;
    Kernel k(c);
    spawn(k, 10); spawn(k, 10);
    runUntilDone(k);
    auto m = k.metrics();
    CHECK(m.makespan > 20);
    CHECK_EQ(m.makespan, 20 + (int)m.ctxSwitches);
    CHECK(m.cpuUtil < 1.0);
}

TEST(mlfq_demotes_cpu_hogs_and_slices_grow) {
    Kernel k(textbook(SchedPolicy::MLFQ, 2));
    int a = spawn(k, 60), b = spawn(k, 60);
    k.run(30);
    CHECK_EQ(k.find(a)->level, 2);
    CHECK_EQ(k.find(b)->level, 2);
    CHECK_EQ(k.scheduler().timeSlice(*k.find(a)), 8);   // q * 2^level
}

TEST(mlfq_interactive_process_keeps_high_priority_level) {
    Kernel k(textbook(SchedPolicy::MLFQ, 4));
    int hog = spawn(k, 80);
    int ui = spawnScript(k, {"COMPUTE 1", "READ KEYBOARD 3", "COMPUTE 1", "READ KEYBOARD 3", "COMPUTE 1", "READ KEYBOARD 3"});
    k.run(40);
    CHECK_EQ(k.find(ui)->level, 0);
    CHECK(k.find(hog)->level >= 1);
}

TEST(adaptive_predicts_short_bursts_for_io_bound_work) {
    Kernel k(textbook(SchedPolicy::ADAPTIVE));
    int p = spawnScript(k, {"COMPUTE 2", "READ DISK 3", "COMPUTE 2", "READ DISK 3", "COMPUTE 2", "READ DISK 3", "COMPUTE 2", "READ DISK 3"});
    runUntilDone(k);
    CHECK(k.find(p)->tau < 4.5);    // started at 6, bursts are 3 ticks long
    CHECK(k.find(p)->tau > 2.9);
}

TEST(aging_prevents_priority_starvation_and_no_aging_starves) {
    auto run = [](bool aging) {
        Config c = textbook(SchedPolicy::PRIORITY); c.aging = aging; c.agingInterval = 5;
        Kernel k(c);
        int victim = spawn(k, 10, 0, 9);
        for (int i = 0; i < 3; ++i) spawn(k, 100, 0, 1);
        k.run(150);
        return std::make_pair(k.find(victim)->cpuTime, k.starvationEvents());
    };
    auto withAging = run(true), without = run(false);
    CHECK(withAging.first > 0);
    CHECK_EQ(without.first, 0);
    CHECK(without.second >= 1);
}

TEST(switching_scheduler_at_runtime_keeps_ready_processes) {
    Kernel k(textbook(SchedPolicy::FCFS));
    spawn(k, 30); spawn(k, 5); spawn(k, 5);
    k.run(3);
    k.setScheduler(SchedPolicy::SJF);
    CHECK_EQ(k.scheduler().size(), 2u);
    runUntilDone(k);
    CHECK_EQ(k.metrics().completed, 3);
}

TEST(sjf_never_worse_than_fcfs_on_simultaneous_arrivals) {
    std::vector<int> bursts{9, 2, 7, 4, 1, 8};
    auto avg = [&](SchedPolicy p) {
        Kernel k(textbook(p));
        for (int b : bursts) spawn(k, b);
        runUntilDone(k);
        return k.metrics().avgWaiting;
    };
    CHECK(avg(SchedPolicy::SJF) <= avg(SchedPolicy::FCFS));
    CHECK(avg(SchedPolicy::SRTF) <= avg(SchedPolicy::FCFS));
}

TEST(process_state_machine_new_ready_running_waiting_terminated) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"COMPUTE 1", "READ DISK 3", "COMPUTE 1"}, 5, 2);
    CHECK_EQ((int)k.find(p)->state, (int)State::NEW);
    k.run(3);                                          // arrives at 2, runs at 2
    CHECK_EQ((int)k.find(p)->state, (int)State::RUNNING);
    k.run(1);                                          // tick 3 executes READ -> blocks
    CHECK_EQ((int)k.find(p)->state, (int)State::WAITING);
    CHECK_EQ((int)k.find(p)->wait, (int)WaitReason::IO);
    runUntilDone(k);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    CHECK_EQ(k.find(p)->exitCode, 0);
}
