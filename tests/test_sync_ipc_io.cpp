#include "helpers.hpp"
using namespace th;

// ---------------- synchronisation primitives ----------------
TEST(mutex_hands_ownership_to_first_waiter) {
    SyncManager s;
    CHECK(s.lock("m", 1));
    CHECK(!s.lock("m", 2));
    CHECK(!s.lock("m", 3));
    bool ok = false;
    auto w = s.unlock("m", 1, ok);
    CHECK(ok); CHECK_EQ((int)w.size(), 1); CHECK_EQ(w[0], 2);
    CHECK_EQ(s.mutexes["m"].owner, 2);
    s.unlock("m", 9, ok);                              // non-owner
    CHECK(!ok);
}

TEST(counting_semaphore_blocks_at_zero_and_wakes_in_fifo_order) {
    SyncManager s;
    s.createSemaphore("s", 2);
    CHECK(s.semWait("s", 1)); CHECK(s.semWait("s", 2));
    CHECK(!s.semWait("s", 3)); CHECK(!s.semWait("s", 4));
    CHECK_EQ(s.semSignal("s")[0], 3);
    CHECK_EQ(s.semSignal("s")[0], 4);
    CHECK(s.semSignal("s").empty());
    CHECK_EQ(s.semaphores["s"].value, 1);
}

TEST(rwlock_allows_many_readers_one_writer_and_queues_readers_behind_writers) {
    SyncManager s;
    bool ok;
    CHECK(s.rdlock("l", 1)); CHECK(s.rdlock("l", 2));
    CHECK(!s.wrlock("l", 3));
    CHECK(!s.rdlock("l", 4));                          // fair: waits behind the queued writer
    s.rwunlock("l", 1, ok);
    auto w = s.rwunlock("l", 2, ok);
    CHECK_EQ((int)w.size(), 1); CHECK_EQ(w[0], 3);     // writer granted
    CHECK_EQ(s.rwlocks["l"].writer, 3);
    w = s.rwunlock("l", 3, ok);
    CHECK_EQ(w[0], 4);                                 // then the reader
}

TEST(wait_for_graph_finds_three_process_cycle_only) {
    SyncManager s;
    s.lock("A", 1); s.lock("B", 2); s.lock("C", 3);
    s.lock("B", 1); s.lock("C", 2); s.lock("A", 3);    // 1->2, 2->3, 3->1
    s.lock("A", 4);                                    // 4 waits behind the cycle but is not part of it
    auto d = s.findDeadlocks();
    REQUIRE(d.size() == 1);
    CHECK_EQ((int)d[0].size(), 3);
    CHECK((d[0] == std::vector<int>{1, 2, 3}));
}

TEST(banker_safe_state_and_request_admission) {
    Banker b;
    b.available = {3, 3, 2};
    b.maxDemand = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
    b.allocation = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
    REQUIRE(b.wellFormed());
    std::vector<int> seq;
    CHECK(b.isSafe(seq));
    CHECK_EQ((int)seq.size(), 5);
    std::string why;
    CHECK(b.request(1, {1, 0, 2}, why));               // textbook: granted
    CHECK(!b.request(4, {3, 3, 0}, why));              // not enough available
    Banker c = b;
    CHECK(!c.request(0, {0, 2, 0}, why));              // textbook: would be unsafe
    CHECK(why.find("unsafe") != std::string::npos);
}

// ---------------- deadlock lab ----------------
TEST(deadlock_is_detected_and_system_stays_deadlocked_without_recovery) {
    Config c; c.autoDeadlockRecovery = false;
    Kernel k(c);
    auto r = failure::createDeadlock(k, 2);
    REQUIRE(r.pids.size() == 2);
    k.run(60);
    REQUIRE(k.deadlockEvents().size() == 1);
    CHECK(!k.deadlockEvents()[0].resolved);
    CHECK_EQ((int)k.find(r.pids[0])->wait, (int)WaitReason::LOCK);
    CHECK_EQ((int)k.find(r.pids[1])->wait, (int)WaitReason::LOCK);
    CHECK(logHas(k, "DEADLOCK DETECTED"));
    bool red = false;
    for (auto& h : k.health()) red = red || (h.name == "DEADLOCK RISK" && h.level == "RED");
    CHECK(red);
}

TEST(deadlock_recovery_terminates_lowest_importance_victim) {
    Config c; c.autoDeadlockRecovery = true; c.deadlockRecoveryDelay = 5;
    Kernel k(c);
    auto r = failure::createDeadlock(k, 2);
    runUntilDone(k, 400);
    CHECK(k.allDone());
    CHECK(k.deadlockEvents()[0].resolved);
    CHECK_EQ(k.deadlockEvents()[0].victim, r.pids[1]);  // priority 4 is less important than priority 3
    CHECK_EQ(k.find(r.pids[0])->exitCode, 0);           // survivor completes
    CHECK(k.find(r.pids[1])->exitCode != 0);
    CHECK_EQ((int)k.sync().findDeadlocks().size(), 0);
}

TEST(deadlock_recovery_by_rollback_lets_both_processes_finish) {
    Config c; c.autoDeadlockRecovery = true; c.deadlockRecoveryDelay = 3; c.recovery = RecoveryPolicy::ROLLBACK;
    Kernel k(c);
    auto r = failure::createDeadlock(k, 2);
    runUntilDone(k, 600);
    CHECK(k.allDone());
    CHECK_EQ(k.find(r.pids[0])->exitCode, 0);
    CHECK_EQ(k.find(r.pids[1])->exitCode, 0);
    CHECK_EQ(k.find(r.pids[1])->rollbacks, 1);
}

TEST(three_way_circular_wait_is_detected) {
    Config c; c.autoDeadlockRecovery = false;
    Kernel k(c);
    auto r = failure::createDeadlock(k, 3);
    k.run(80);
    REQUIRE(k.deadlockEvents().size() == 1);
    CHECK_EQ((int)k.deadlockEvents()[0].pids.size(), 3);
}

TEST(ordered_lock_acquisition_avoids_deadlock) {
    Kernel k(Config{});
    for (int i = 0; i < 2; ++i)
        spawnScript(k, {"COMPUTE 1", "LOCK R1", "COMPUTE 1", "READ DISK 3", "LOCK R2", "COMPUTE 2", "UNLOCK R2", "UNLOCK R1"}, 3 + i);
    runUntilDone(k, 500);
    CHECK(k.allDone());
    CHECK(k.deadlockEvents().empty());
    CHECK_EQ(k.metrics().completed, 2);
}

TEST(producer_consumer_with_semaphores_completes_and_restores_counts) {
    Kernel k(textbook(SchedPolicy::FCFS));
    k.sync().createSemaphore("empty", 2); k.sync().createSemaphore("full", 0); k.sync().createSemaphore("mutex", 1);
    std::vector<std::string> prod, cons;
    for (int i = 0; i < 6; ++i) {
        for (auto l : {"WAIT empty", "WAIT mutex", "COMPUTE 1", "SIGNAL mutex", "SIGNAL full"}) prod.push_back(l);
        for (auto l : {"WAIT full", "WAIT mutex", "COMPUTE 1", "SIGNAL mutex", "SIGNAL empty"}) cons.push_back(l);
    }
    spawnScript(k, prod); spawnScript(k, cons);
    runUntilDone(k, 2000);
    CHECK(k.allDone());
    CHECK_EQ(k.metrics().completed, 2);
    CHECK_EQ(k.sync().semaphores["empty"].value, 2);
    CHECK_EQ(k.sync().semaphores["full"].value, 0);
    CHECK_EQ(k.sync().semaphores["mutex"].value, 1);
    CHECK(k.sync().semaphores["empty"].blocks > 0);     // the producer really did block on the bounded buffer
}

TEST(readers_writers_all_finish_and_lock_ends_free) {
    Kernel k(textbook(SchedPolicy::RR, 2));
    for (int i = 0; i < 3; ++i) spawnScript(k, {"RDLOCK db", "COMPUTE 3", "RWUNLOCK db", "RDLOCK db", "COMPUTE 2", "RWUNLOCK db"});
    for (int i = 0; i < 2; ++i) spawnScript(k, {"WRLOCK db", "COMPUTE 3", "RWUNLOCK db"});
    runUntilDone(k, 2000);
    CHECK(k.allDone());
    CHECK_EQ(k.metrics().completed, 5);
    CHECK_EQ(k.sync().rwlocks["db"].writer, -1);
    CHECK(k.sync().rwlocks["db"].readers.empty());
    CHECK(k.deadlockEvents().empty());
}

TEST(dining_philosophers_naive_deadlocks_but_resource_ordering_does_not) {
    auto run = [](bool ordered) {
        Config c = textbook(SchedPolicy::RR, 2);
        Kernel k(c);
        for (int i = 0; i < 5; ++i) {
            int a = i + 1, b = (i + 1) % 5 + 1;
            if (ordered && a > b) std::swap(a, b);
            spawnScript(k, {"COMPUTE 1", "LOCK F" + std::to_string(a), "COMPUTE 1", "SLEEP 2", "LOCK F" + std::to_string(b), "COMPUTE 3", "UNLOCK F" + std::to_string(b), "UNLOCK F" + std::to_string(a)});
        }
        k.run(400);
        return std::make_pair(k.allDone(), k.deadlockEvents().size());
    };
    auto naive = run(false), fixed = run(true);
    CHECK(!naive.first); CHECK(naive.second >= 1);
    CHECK(fixed.first); CHECK_EQ(fixed.second, 0u);
}

TEST(unsynchronised_shared_counter_loses_updates_but_mutex_fixes_it) {
    auto run = [](bool locked) {
        Kernel k(textbook(SchedPolicy::RR, 1));
        std::vector<std::string> body;
        for (int i = 0; i < 10; ++i) {
            if (locked) body.push_back("LOCK m");
            body.push_back("SHMR ctr 0"); body.push_back("COMPUTE 1"); body.push_back("SHMW ctr 0 1");
            if (locked) body.push_back("UNLOCK m");
        }
        spawnScript(k, body); spawnScript(k, body);
        runUntilDone(k, 2000);
        return k.ipc().segments["ctr"].data[0];
    };
    CHECK(run(false) < 20);
    CHECK_EQ(run(true), 20);
}

// ---------------- IPC ----------------
TEST(message_queue_blocks_receiver_until_a_message_arrives) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int rx = spawnScript(k, {"RECV q", "COMPUTE 1"});
    int tx = spawnScript(k, {"COMPUTE 5", "SEND q HELLO world"}, 5, 0);
    k.run(4);
    CHECK_EQ((int)k.find(rx)->state, (int)State::WAITING);
    CHECK_EQ((int)k.find(rx)->wait, (int)WaitReason::MESSAGE);
    runUntilDone(k);
    CHECK_EQ(k.find(rx)->inbox, std::string("HELLO world"));
    CHECK_EQ(k.find(rx)->msgRecv, 1);
    CHECK_EQ(k.find(tx)->msgSent, 1);
    CHECK(logHas(k, "HELLO world"));
}

TEST(bounded_message_queue_blocks_sender_when_full_then_drains) {
    Kernel k(textbook(SchedPolicy::FCFS));
    k.ipc().createQueue("q", 1);
    int tx = spawnScript(k, {"SEND q a", "SEND q b", "SEND q c"});
    k.run(4);
    CHECK_EQ((int)k.find(tx)->wait, (int)WaitReason::MESSAGE);   // second send blocked: capacity 1
    int rx = spawnScript(k, {"RECV q", "RECV q", "RECV q"});
    runUntilDone(k, 200);
    CHECK_EQ(k.metrics().completed, 2);
    CHECK_EQ(k.find(rx)->msgRecv, 3);
}

TEST(shared_memory_is_visible_between_processes) {
    Kernel k(textbook(SchedPolicy::FCFS));
    spawnScript(k, {"SHMR seg 2", "SHMW seg 2 7"});
    int rd = spawnScript(k, {"COMPUTE 2", "SHMR seg 2"});
    runUntilDone(k);
    CHECK_EQ(k.ipc().segments["seg"].data[2], 7);
    CHECK_EQ(k.find(rd)->regs[0], 7);
}

// ---------------- I/O ----------------
TEST(blocking_read_takes_exactly_its_duration) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"READ DISK 5", "COMPUTE 1"});
    runUntilDone(k);
    CHECK_EQ(k.find(p)->completion, 7);                 // tick 0 request + 5 ms device + 1 compute
    CHECK_EQ(k.find(p)->blockedTime, 6);               // ticks that end in WAITING: request tick + 5 ms device time
    CHECK_EQ(k.io().get("DISK")->completed, 1);
}

TEST(io_wait_lets_another_process_use_the_cpu) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int io = spawnScript(k, {"READ DISK 8", "COMPUTE 1"});
    int cpu = spawn(k, 6);
    runUntilDone(k);
    CHECK_EQ(k.find(cpu)->firstRun, 1);                 // starts the very next tick
    CHECK_EQ(k.find(io)->completion, 10);
    CHECK(k.metrics().cpuUtil > 0.6);
}

TEST(sleep_wakes_through_a_timer_interrupt) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"SLEEP 4", "COMPUTE 1"});
    runUntilDone(k);
    CHECK_EQ(k.find(p)->completion, 6);
    CHECK(logHas(k, "sleep expired"));
}

TEST(device_requests_are_served_fifo_with_queue_wait) {
    Kernel k(textbook(SchedPolicy::FCFS));
    std::vector<int> pids;
    for (int i = 0; i < 4; ++i) pids.push_back(spawnScript(k, {"READ DISK 4"}));
    k.run(40);
    CHECK_EQ(k.io().get("DISK")->completed, 4);
    CHECK(k.io().get("DISK")->maxQueue >= 3);
    CHECK(k.find(pids[3])->completion > k.find(pids[0])->completion);
    CHECK(k.metrics().avgIoWait > 0);
}

TEST(write_behind_buffer_accepts_writes_then_blocks_producer_when_full) {
    Config c = textbook(SchedPolicy::FCFS); c.diskBuffer = 2;
    Kernel k(c);
    int p = spawnScript(k, {"WRITE DISK 6", "WRITE DISK 6", "WRITE DISK 6", "COMPUTE 1"});
    k.run(4);
    CHECK_EQ((int)k.find(p)->wait, (int)WaitReason::BUFFER_FULL);
    CHECK_EQ(k.io().get("DISK")->occupancy, 2);
    CHECK_EQ(k.io().get("DISK")->fullEvents, 1);
    runUntilDone(k, 200);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    k.run(30);                                          // let the device drain what the process left behind
    CHECK_EQ(k.io().get("DISK")->occupancy, 0);
    CHECK_EQ(k.io().get("DISK")->producerBlocks, 1);
}

TEST(printer_spooler_decouples_process_from_slow_printer) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"WRITE PRINTER 20 report.pdf", "WRITE PRINTER 20 notes.txt", "COMPUTE 1"});
    k.run(6);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);       // finished long before printing completes
    CHECK(k.io().get("PRINTER")->queueLength() > 0);
    k.run(60);
    CHECK_EQ(k.io().get("PRINTER")->completed, 2);
    CHECK_EQ(k.io().get("PRINTER")->occupancy, 0);
}

TEST(device_failure_aborts_queue_and_new_requests_get_eio_until_repair) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int a = spawnScript(k, {"READ DISK 30", "COMPUTE 1"});
    int b = spawnScript(k, {"COMPUTE 25", "READ DISK 3", "COMPUTE 1"});
    k.run(5);
    CHECK(k.failDevice("DISK", 40));
    k.run(2);
    CHECK_EQ(k.find(a)->ioErrors, 1);                   // woken with EIO
    CHECK_EQ(k.find(a)->regs[3], -1);
    k.run(30);
    CHECK_EQ(k.find(b)->ioErrors, 1);                   // request rejected while offline
    CHECK(logHas(k, "OFFLINE"));
    k.run(40);
    CHECK(!k.io().get("DISK")->failed);
    CHECK(logHas(k, "back ONLINE"));
    CHECK(k.allDone());
}

TEST(io_flood_builds_long_device_queues) {
    Config c; c.thrashingControl = false;
    Kernel k(c);
    failure::floodIO(k, 30, false);
    k.run(70);
    CHECK(k.io().maxQueueNow() >= 8);
    bool red = false;
    for (auto& h : k.health()) red = red || (h.name == "I/O" && h.level == "RED");
    CHECK(red);
}
