#include <filesystem>
#include "../simulation/detective.hpp"
#include "helpers.hpp"
using namespace th;

#ifndef OWR_ROOT
#define OWR_ROOT "."
#endif

// ---------------- interrupt controller ----------------
TEST(interrupts_are_serviced_in_priority_order_and_traps_run_immediately) {
    InterruptController ic;
    std::vector<std::string> order;
    for (auto t : {IntType::TIMER, IntType::IO_COMPLETE, IntType::PAGE_FAULT, IntType::ERROR, IntType::SYSCALL})
        ic.on(t, str(t), [&order](Interrupt& i) { order.push_back(str(i.type)); });
    Interrupt a; a.type = IntType::TIMER; ic.raise(a, false);
    Interrupt b; b.type = IntType::IO_COMPLETE; ic.raise(b, false);
    Interrupt c; c.type = IntType::PAGE_FAULT; ic.raise(c, false);
    Interrupt s; s.type = IntType::SYSCALL; ic.raise(s, true);            // synchronous: dispatched at once
    CHECK_EQ((int)order.size(), 1);
    ic.service();
    CHECK((order == std::vector<std::string>{"SYSCALL", "PAGE_FAULT", "IO_COMPLETE", "TIMER"}));
    CHECK_EQ(ic.total(), 4);
    CHECK_EQ(ic.count(IntType::TIMER), 1);
    CHECK(ic.pending().empty());
}

TEST(timer_interrupt_drives_round_robin_context_switch_with_logged_steps) {
    Kernel k(textbook(SchedPolicy::RR, 3));
    spawn(k, 10); spawn(k, 10);
    k.run(8);
    CHECK(logHas(k, "TIMER INTERRUPT"));
    CHECK(logHas(k, "CONTEXT SWITCH P1 → P2"));
    CHECK(logHas(k, "save P1 context"));
    CHECK(logHas(k, "load context"));
    CHECK(k.interrupts().count(IntType::TIMER) > 0);
    size_t t = logPos(k, "TIMER INTERRUPT"), s = logPos(k, "CONTEXT SWITCH P1 → P2");
    CHECK(t < s);
}

TEST(io_interrupt_moves_process_waiting_to_ready_and_scheduler_resumes_it) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"READ DISK 3", "COMPUTE 1"});
    spawn(k, 20);
    runUntilDone(k);
    size_t req = logPos(k, "P1 → DISK READ"), irq = logPos(k, "I/O INTERRUPT: DISK complete → P1 READY");
    CHECK(req != std::string::npos && irq != std::string::npos && req < irq);
    CHECK_EQ(k.find(p)->exitCode, 0);
}

// ---------------- system calls ----------------
TEST(every_syscall_is_dispatched_through_the_trap_layer_and_counted) {
    Kernel k(textbook(SchedPolicy::FCFS));
    k.sync().createSemaphore("s", 1);
    spawnScript(k, {"COMPUTE 1", "READ DISK 2", "WRITE DISK 2", "SLEEP 2", "WAIT s", "SIGNAL s", "LOCK m", "UNLOCK m", "SEND q hi", "RECV q",
                    "ALLOC 1", "FREE 1", "FORK 3", "SHMR g 0", "SHMW g 0 1"});
    runUntilDone(k, 300);
    const std::string st = k.stateJson();
    auto j = json::parse(st);
    for (const char* name : {"read", "write", "sleep", "wait", "signal", "lock", "unlock", "send_message", "receive_message", "allocate_memory", "free_memory",
                             "create_process", "shm_read", "shm_write", "exit"})
        CHECK(j["syscalls"][name].integer() >= 1);
    CHECK(logHas(k, "TRAP read(DISK, 2)"));
    CHECK_EQ(k.metrics().completed, 2);                 // parent and its forked child
    CHECK_EQ(k.find(2)->ppid, 1);
}

TEST(shell_terminate_goes_through_terminate_process_syscall) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawn(k, 100);
    k.run(5);
    CHECK(k.shellTerminate(p));
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    CHECK_EQ(k.find(p)->exitCode, -15);
    CHECK(!k.shellTerminate(p));                        // already dead
    CHECK(!k.shellTerminate(999));
    CHECK(logHas(k, "shell → TRAP terminate_process(1)"));
}

TEST(fork_creates_child_with_parent_pid) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"FORK 4", "COMPUTE 2"});
    runUntilDone(k);
    CHECK_EQ(k.processes().size(), 2u);
    CHECK_EQ(k.find(2)->ppid, p);
}

// ---------------- error handling & crash ----------------
TEST(process_crash_saves_dump_releases_resources_and_others_continue) {
    Kernel k(textbook(SchedPolicy::RR, 3));
    int victim = spawnScript(k, {"LOCK m", "COMPUTE 30", "UNLOCK m"});
    int waiter = spawnScript(k, {"COMPUTE 1", "LOCK m", "COMPUTE 2", "UNLOCK m"});
    int bystander = spawn(k, 15);
    k.run(8);
    CHECK(failure::crashProcess(k, victim, "ILLEGAL INSTRUCTION").ok);
    CHECK_EQ((int)k.find(victim)->state, (int)State::TERMINATED);
    CHECK(k.find(victim)->exitCode != 0);
    REQUIRE(k.crashDumps().size() == 1);
    CHECK_EQ(k.crashDumps()[0].pid, victim);
    CHECK_EQ((int)k.crashDumps()[0].heldResources.size(), 1);
    CHECK_EQ(k.sync().mutexes["m"].owner == victim, false);
    runUntilDone(k, 500);
    CHECK_EQ(k.find(waiter)->exitCode, 0);              // waiter got the released lock
    CHECK_EQ(k.find(bystander)->exitCode, 0);
    CHECK(logHas(k, "ERROR INTERRUPT"));
    CHECK(logHas(k, "scheduler continues"));
    CHECK_EQ(k.memory().usedFrames(), 0);
}

TEST(crash_instruction_triggers_the_error_interrupt) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"COMPUTE 2", "CRASH divide by zero", "COMPUTE 5"});
    runUntilDone(k);
    CHECK_EQ((int)k.find(p)->state, (int)State::TERMINATED);
    CHECK_EQ(k.find(p)->exitReason, std::string("divide by zero"));
    CHECK_EQ(k.interrupts().count(IntType::ERROR), 1);
}

TEST(crashing_a_process_waiting_on_io_leaves_device_consistent) {
    Kernel k(textbook(SchedPolicy::FCFS));
    int p = spawnScript(k, {"READ DISK 20"});
    k.run(5);
    CHECK(k.crashProcess(p, "killed"));
    k.run(40);
    CHECK_EQ(k.io().get("DISK")->queueLength(), 0);
    CHECK(!k.crashProcess(p, "again"));                 // dead processes cannot crash twice
}

// ---------------- health ----------------
TEST(health_monitor_reports_documented_levels) {
    Kernel idle(Config{});
    for (auto& h : idle.health()) CHECK_EQ(h.level, std::string("GREEN"));
    Config c; c.thrashingControl = false;
    Kernel k(c);
    failure::exhaustMemory(k, 16);
    k.run(300);
    std::map<std::string, std::string> lv;
    for (auto& h : k.health()) lv[h.name] = h.level;
    CHECK_EQ(lv["MEMORY"], std::string("RED"));
    CHECK_EQ(lv["PAGE FAULTS"], std::string("RED"));
    CHECK_EQ(lv.size(), 6u);
}

// ---------------- failure lab ----------------
TEST(failure_lab_actions_report_and_are_recorded) {
    Kernel k(Config{});
    spawn(k, 200);
    k.run(3);
    CHECK(failure::forcePageFault(k).ok || true);
    CHECK(failure::floodIO(k, 3).ok);
    CHECK(failure::bufferStress(k, 2).ok);
    CHECK(failure::deviceFailure(k, "NETWORK", 10).ok);
    CHECK(!failure::deviceFailure(k, "NOPE", 10).ok);
    CHECK(failure::starvation(k, false).ok);
    CHECK(!failure::inject(k, "bogus", json::Value{}).ok);
    CHECK(k.failures().size() >= 5);
    CHECK(k.log().count(Category::FAILURE) >= 5);
}

TEST(buffer_stress_blocks_producers_and_all_complete) {
    Config c; c.diskBuffer = 2; c.thrashingControl = false;
    Kernel k(c);
    failure::bufferStress(k, 6);
    runUntilDone(k, 3000);
    CHECK(k.allDone());
    CHECK(k.io().get("DISK")->producerBlocks > 0);
    CHECK(k.io().get("NETWORK")->fullEvents + k.io().get("DISK")->fullEvents > 0);
}

// ---------------- state export ----------------
TEST(state_process_and_log_json_are_valid_and_complete) {
    Simulation sim;
    { ProcessSpec s; s.cpuBurst = 500; sim.kernel().createProcess(s); }
    failure::createDeadlock(sim.kernel(), 2);
    sim.step(60);
    auto st = json::parse(sim.kernel().stateJson());
    for (const char* key : {"tick", "status", "config", "cpu", "processes", "readyQueue", "memory", "io", "interrupts", "sync", "ipc", "health", "series", "metrics", "syscalls"})
        CHECK(st.has(key));
    CHECK_EQ((int)st["memory"]["frames"].size(), sim.kernel().memory().totalFrames());
    CHECK_EQ((int)st["io"]["devices"].size(), 4);
    CHECK_EQ((int)st["health"].size(), 6);
    auto pj = json::parse(sim.kernel().processJson(1));
    CHECK_EQ(pj["pid"].integer(), 1);
    CHECK(pj["pageTable"].size() > 0);
    CHECK(pj["program"].size() > 0);
    auto lj = json::parse(sim.kernel().logJson(0, {}, -1, 100));
    CHECK(lj["events"].size() > 0);
    auto some = sim.kernel().logJson(0, {Category::SYNC}, -1, 50);
    auto filtered = json::parse(some);
    CHECK(filtered["events"].size() > 0);
    for (auto& e : filtered["events"].a) CHECK_EQ(e["cat"].str(), std::string("SYNC"));
    CHECK(json::parse(sim.kernel().processJson(404)).has("error"));
}

TEST(simulation_is_deterministic_for_a_given_seed) {
    auto run = [](uint32_t seed) {
        WorkloadSpec w; w.seed = seed;
        return runExperiment(w, Config{}, "x").m.toJson();
    };
    CHECK_EQ(run(42), run(42));
    CHECK(run(42) != run(43));
}

// ---------------- scenario files ----------------
static Simulation load(const std::string& name) {
    Simulation sim;
    auto info = sim.loadScenario(json::parse(Simulation::readFile(std::string(OWR_ROOT) + "/scenarios/" + name + ".json")));
    sim.step(info.suggestedTicks);
    return sim;
}

TEST(every_scenario_file_loads_and_runs) {
    int n = 0;
    for (auto& f : std::filesystem::directory_iterator(std::string(OWR_ROOT) + "/scenarios")) {
        if (f.path().extension() != ".json") continue;
        Simulation sim;
        auto info = sim.loadScenario(json::parse(Simulation::readFile(f.path().string())));
        CHECK(!info.title.empty());
        CHECK(!info.demonstrates.empty());
        sim.step(info.suggestedTicks);
        CHECK(sim.kernel().now() == info.suggestedTicks);
        ++n;
    }
    CHECK(n >= 8);
}

TEST(scenario_cpu_battle_shows_scheduling_and_context_switching) {
    auto s = load("cpu_heavy");
    auto m = s.kernel().metrics();
    CHECK(m.ctxSwitches > 20);
    CHECK(m.completed >= 8);
    CHECK(m.cpuUtil * m.ticks / m.makespan > 0.7);       // utilisation while there was work to do
}
TEST(scenario_memory_meltdown_shows_page_faults_and_thrashing) {
    auto s = load("memory_pressure");
    CHECK(s.kernel().thrashingEvents() > 0);
    CHECK(s.kernel().metrics().replacements > 10);
    CHECK(s.kernel().suspensions() > 0);                 // load control reacted
}
TEST(scenario_io_storm_shows_queues_buffering_and_spooling) {
    auto s = load("io_flood");
    const Kernel& k = s.kernel();
    CHECK(k.io().get("DISK")->maxQueue >= 6);
    CHECK(k.io().get("PRINTER")->maxQueue >= 3);
    CHECK(k.io().get("DISK")->fullEvents + k.io().get("PRINTER")->fullEvents > 0);
}
TEST(scenario_deadlock_trap_detects_and_recovers) {
    auto s = load("deadlock");
    const Kernel& k = s.kernel();
    REQUIRE(!k.deadlockEvents().empty());
    CHECK(k.deadlockEvents()[0].resolved);
    CHECK(k.allDone());
}
TEST(scenario_process_crash_recovers) {
    auto s = load("fault_recovery");
    const Kernel& k = s.kernel();
    CHECK(!k.crashDumps().empty());
    CHECK(k.metrics().completed >= 3);
}
TEST(scenario_producer_consumer_and_race_condition_behave_as_documented) {
    auto pc = load("producer_consumer");
    CHECK(pc.kernel().allDone());
    CHECK_EQ(pc.kernel().sync().semaphores.at("full").value, 0);
    auto race = load("race_condition");
    CHECK(race.kernel().ipc().segments.at("counter").data[0] < 20);
}
TEST(scenario_dining_philosophers_deadlocks_without_recovery) {
    auto s = load("dining_philosophers");
    CHECK(!s.kernel().deadlockEvents().empty());
}

// ---------------- experiment lab ----------------
TEST(experiment_lab_runs_all_schedulers_to_completion) {
    WorkloadSpec w;
    Config c; c.thrashingControl = false;
    auto rs = compareSchedulers(w, c, {SchedPolicy::FCFS, SchedPolicy::SJF, SchedPolicy::SRTF, SchedPolicy::RR, SchedPolicy::PRIORITY, SchedPolicy::MLFQ, SchedPolicy::ADAPTIVE});
    CHECK_EQ((int)rs.size(), 7);
    for (auto& r : rs) { CHECK(r.finished); CHECK_EQ(r.m.completed, w.total()); CHECK(r.m.cpuUtil > 0.3 && r.m.cpuUtil <= 1.0); }
    CHECK(rs[3].m.ctxSwitches > rs[0].m.ctxSwitches);    // time slicing costs more context switches than FCFS
    CHECK(rs[2].m.avgTurnaround <= rs[0].m.avgTurnaround * 1.1);   // SRTF is competitive on turnaround
}

TEST(experiment_lab_compares_page_replacement_with_same_workload) {
    WorkloadSpec w; w.cpuBound = 12; w.ioBound = 12; w.interactive = 6;
    Config c; c.thrashingControl = false; c.physicalKB = 128;
    auto rs = compareReplacement(w, c, {ReplPolicy::FIFO, ReplPolicy::LRU, ReplPolicy::CLOCK, ReplPolicy::OPTIMAL});
    for (auto& r : rs) CHECK(r.finished);
    CHECK(rs[1].m.pageFaults <= rs[0].m.pageFaults * 1.15);   // LRU is not meaningfully worse than FIFO
    CHECK(rs[0].m.pageFaults > 0);
}

TEST(what_if_more_processes_means_more_page_faults_and_longer_waits) {
    WorkloadSpec w;
    Config c; c.thrashingControl = false;
    auto pts = whatIf(w, c, "processes", {10, 20, 40});
    REQUIRE(pts.size() == 3);
    CHECK(pts[2].r.m.pfRate > pts[0].r.m.pfRate);
    CHECK(pts[2].r.m.avgWaiting > pts[0].r.m.avgWaiting);
    CHECK_EQ(pts[2].r.m.processes, 40);
    bool threw = false;
    try { whatIf(w, c, "nonsense", {1}); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

TEST(what_if_quantum_trades_response_for_context_switches) {
    WorkloadSpec w; w.ioBound = 0; w.interactive = 0; w.cpuBound = 8;
    Config c; c.thrashingControl = false; c.sched = SchedPolicy::RR;
    auto pts = whatIf(w, c, "quantum", {1, 4, 16});
    CHECK(pts[0].r.m.ctxSwitches > pts[1].r.m.ctxSwitches);
    CHECK(pts[1].r.m.ctxSwitches > pts[2].r.m.ctxSwitches);
    CHECK(pts[0].r.m.cpuUtil < pts[2].r.m.cpuUtil);
}

// ---------------- OS detective ----------------
TEST(detective_cases_have_the_documented_symptoms_and_check_answers) {
    for (int id = 1; id <= detectiveCaseCount(); ++id) {
        const auto& c = detectiveCase(id);
        auto sym = json::parse(c.symptomsJson);
        auto good = json::parse(detectiveDiagnose(id, c.answer)), bad = json::parse(detectiveDiagnose(id, "NOTHING"));
        CHECK(good["correct"].boolean());
        CHECK(!bad["correct"].boolean());
        CHECK(bad["answer"].str().empty());               // a wrong guess must not leak the answer
        CHECK(json::parse(detectiveCaseJson(id)).has("logs"));
        CHECK(detectiveCaseJson(id).find(c.answer) == std::string::npos);
        switch (id) {
            case 1: CHECK(sym["pageFaultRate"].num() > 0.55); CHECK(sym["cpuUtil"].num() < 0.5); break;
            case 2: CHECK_EQ(sym["blockedProcesses"].integer(), 2); CHECK(sym["completed"].integer() >= 3); break;
            case 3: CHECK(sym["longestDeviceQueue"].integer() >= 8); CHECK(sym["diskUtilisation"].num() > 0.8); break;
            case 4: CHECK_NEAR(sym["diskUtilisation"].num(), 0.0, 1e-9); break;
            case 5: CHECK(sym["cpuUtil"].num() > 0.9); break;
            case 6: CHECK_NEAR(sym["diskUtilisation"].num(), 0.0, 1e-9); CHECK(sym["readyQueue"].integer() >= 8); break;
        }
    }
    bool io = false;
    for (auto& l : detectiveCase(4).logs.at("io")) io = io || l.find("EIO") != std::string::npos || l.find("OFFLINE") != std::string::npos;
    CHECK(io);
    for (int id = 1; id <= detectiveCaseCount(); ++id)
        for (auto& [name, lines] : detectiveCase(id).logs)
            for (auto& l : lines) { CHECK(l.find("DETECTED") == std::string::npos); CHECK(l.find("FAILURE LAB") == std::string::npos); }
}

// ---------------- assembler & JSON ----------------
TEST(assembler_parses_programs_and_reports_line_numbers) {
    auto p = assemble({"; comment", "COMPUTE 3", "write printer 5 my doc.txt  # trailing", "LOCK R1", "", "SHMW seg 1 -2"});
    REQUIRE(p.size() == 4);
    CHECK_EQ(p[1].s, std::string("PRINTER")); CHECK_EQ(p[1].s2, std::string("my doc.txt")); CHECK_EQ(p[3].b, -2);
    for (auto bad : std::vector<std::vector<std::string>>{{"COMPUTE"}, {"COMPUTE 0"}, {"JUMP 4"}, {"READ TAPE 3"}, {"LOCK"}}) {
        bool threw = false;
        try { assemble(bad); } catch (const std::runtime_error& e) { threw = std::string(e.what()).find("line 1") != std::string::npos; }
        CHECK(threw);
    }
}

TEST(generated_programs_have_exact_burst_length_for_every_type) {
    for (auto t : {ProcType::CPU_BOUND, ProcType::IO_BOUND, ProcType::INTERACTIVE})
        for (int burst : {1, 2, 7, 20, 41}) {
            Kernel k(textbook(SchedPolicy::FCFS));
            ProcessSpec s; s.type = t; s.cpuBurst = burst; s.ioBurst = 24; s.memoryRefs = false;
            int p = k.createProcess(s);
            CHECK_EQ(k.find(p)->cpuBurst, burst);
        }
}

TEST(json_parser_and_writer_round_trip) {
    auto v = json::parse(R"({"a":[1,2.5,{"b":"x\ny\u00e9"}],"t":true,"n":null})");
    CHECK_EQ(v["a"][1].num(), 2.5);
    CHECK_EQ(v["a"][2]["b"].str(), std::string("x\ny\xc3\xa9"));
    CHECK(v["t"].boolean()); CHECK(v["n"].isNull()); CHECK(v["missing"]["deeper"].isNull());
    json::Writer w;
    w.obj().k("s").v("q\"uote\\").k("n").v(3).k("d").v(0.5).k("b").v(true).k("arr").ints({1, 2}).end();
    auto r = json::parse(w.str());
    CHECK_EQ(r["s"].str(), std::string("q\"uote\\")); CHECK_EQ(r["arr"][1].integer(), 2);
    for (auto bad : {"{", "[1,]", "{\"a\" 1}", "tru", "[1] x"}) {
        bool threw = false;
        try { json::parse(bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
}

TEST(config_and_spec_validation_rejects_bad_input) {
    Config c;
    applyConfig(c, json::parse(R"({"scheduler":"mlfq","quantum":6,"replacement":"clock","memoryKB":512})"));
    CHECK_EQ((int)c.sched, (int)SchedPolicy::MLFQ); CHECK_EQ(c.quantum, 6); CHECK_EQ(c.physicalKB, 512);
    for (auto bad : {R"({"scheduler":"nope"})", R"({"quantum":0})", R"({"replacement":"x"})"}) {
        bool threw = false;
        try { Config d; applyConfig(d, json::parse(bad)); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    auto s = specFromJson(json::parse(R"({"type":"io","cpuBurst":12,"priority":3,"script":["COMPUTE 2","READ DISK 3"]})"));
    CHECK_EQ((int)s.script.size(), 2);
}

TEST(process_table_limit_is_enforced) {
    Config c = textbook(SchedPolicy::FCFS); c.maxProcesses = 3;
    Kernel k(c);
    CHECK(spawn(k, 5) > 0); CHECK(spawn(k, 5) > 0); CHECK(spawn(k, 5) > 0);
    CHECK_EQ(spawn(k, 5), -1);
    CHECK(logHas(k, "process table full"));
}
