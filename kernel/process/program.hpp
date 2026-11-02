// Simulated user programs: the instruction set, a tiny assembler, generators for the three
// process types, and the locality model that turns COMPUTE ticks into memory references.
#pragma once
#include <sstream>
#include <stdexcept>
#include "../common/rng.hpp"
#include "../common/types.hpp"

namespace owr {

// Every instruction costs exactly one CPU tick. Blocking instructions advance the PC when they
// block, so the process resumes *after* them once the kernel wakes it.
enum class Op { COMPUTE, READ, WRITE, SLEEP, LOCK, UNLOCK, WAIT, SIGNAL, RDLOCK, WRLOCK, RWUNLOCK,
                SEND, RECV, SHMR, SHMW, ALLOC, FREE, FORK, CRASH };
inline const std::array<const char*, 19> OP_NAMES{"COMPUTE", "READ", "WRITE", "SLEEP", "LOCK", "UNLOCK", "WAIT", "SIGNAL",
    "RDLOCK", "WRLOCK", "RWUNLOCK", "SEND", "RECV", "SHMR", "SHMW", "ALLOC", "FREE", "FORK", "CRASH"};

struct Instr {
    Op op = Op::COMPUTE;
    int a = 0;          // COMPUTE: tick count (script form) | I/O duration | sleep ticks | page count | child burst | shm index
    int b = 0;          // SHMW: delta
    std::string s;      // device / resource / queue / segment name
    std::string s2;     // WRITE: document name | SEND: message text | CRASH: reason
    int page = -1;      // COMPUTE (linked form): virtual page referenced
    int offset = 0;     // COMPUTE (linked form): byte offset inside the page

    std::string text() const {
        std::string r = OP_NAMES[(int)op];
        switch (op) {
            case Op::COMPUTE: return r + " page " + std::to_string(page);
            case Op::READ: case Op::WRITE: return r + " " + s + " " + std::to_string(a) + (s2.empty() ? "" : " " + s2);
            case Op::SLEEP: case Op::ALLOC: case Op::FREE: case Op::FORK: return r + " " + std::to_string(a);
            case Op::SEND: return r + " " + s + " \"" + s2 + "\"";
            case Op::SHMR: return r + " " + s + "[" + std::to_string(a) + "]";
            case Op::SHMW: return r + " " + s + "[" + std::to_string(a) + "] += " + std::to_string(b);
            case Op::CRASH: return r + (s2.empty() ? "" : " " + s2);
            default: return r + " " + s;
        }
    }
};

inline bool validDevice(const std::string& d) { return d == "DISK" || d == "PRINTER" || d == "KEYBOARD" || d == "NETWORK"; }

// ---- assembler ------------------------------------------------------------------------------
// One instruction per line; ';' or '#' start a comment. Throws std::runtime_error("line N: ...").
inline std::vector<Instr> assemble(const std::vector<std::string>& lines) {
    std::vector<Instr> out;
    int ln = 0;
    for (std::string line : lines) {
        ++ln;
        auto cut = line.find_first_of(";#");
        if (cut != std::string::npos) line.erase(cut);
        std::istringstream in(line);
        std::string opn;
        if (!(in >> opn)) continue;
        auto fail = [&](const std::string& m) { throw std::runtime_error("line " + std::to_string(ln) + ": " + m); };
        auto op = parseEnum<Op>(OP_NAMES, opn);
        if (!op) fail("unknown instruction '" + opn + "'");
        Instr i;
        i.op = *op;
        auto rest = [&]() { std::string r; std::getline(in, r); size_t p = r.find_first_not_of(" \t"), e = r.find_last_not_of(" \t\r"); return p == std::string::npos ? std::string() : r.substr(p, e - p + 1); };
        auto needInt = [&](int& dst, int min, const char* what) {
            if (!(in >> dst)) fail(std::string("expected ") + what);
            if (dst < min) fail(std::string(what) + " must be >= " + std::to_string(min));
        };
        auto needName = [&](std::string& dst, const char* what) { if (!(in >> dst)) fail(std::string("expected ") + what); };
        switch (i.op) {
            case Op::COMPUTE: case Op::SLEEP: case Op::ALLOC: case Op::FREE: case Op::FORK: needInt(i.a, 1, "count"); break;
            case Op::READ: case Op::WRITE:
                needName(i.s, "device"); i.s = upper(i.s);
                if (!validDevice(i.s)) fail("unknown device '" + i.s + "'");
                needInt(i.a, 1, "duration");
                i.s2 = rest();
                break;
            case Op::LOCK: case Op::UNLOCK: case Op::WAIT: case Op::SIGNAL: case Op::RDLOCK: case Op::WRLOCK: case Op::RWUNLOCK:
            case Op::RECV: needName(i.s, "name"); break;
            case Op::SEND: needName(i.s, "queue"); i.s2 = rest(); if (i.s2.empty()) i.s2 = "msg"; break;
            case Op::SHMR: needName(i.s, "segment"); needInt(i.a, 0, "index"); break;
            case Op::SHMW: needName(i.s, "segment"); needInt(i.a, 0, "index"); if (!(in >> i.b)) fail("expected delta"); break;
            case Op::CRASH: i.s2 = rest(); break;
        }
        out.push_back(i);
    }
    return out;
}

// ---- process spec & generators --------------------------------------------------------------
struct ProcessSpec {
    std::string name;                 // empty => "P<pid>"
    ProcType type = ProcType::CPU_BOUND;
    int cpuBurst = 20;                // total CPU ticks (instructions) of a generated program
    int ioBurst = 0;                  // approximate total device time of a generated program
    int priority = 5;                 // 0 = most important
    int memoryKB = 32;                // virtual size
    int arrival = -1;                 // -1 => now
    int ppid = -1;
    int localityPages = -1;           // working-set size in pages (-1 => max(2, pages/4))
    bool memoryRefs = true;           // false: COMPUTE ticks make no memory reference (pure-CPU test workloads)
    std::vector<Instr> script;        // non-empty => custom program (type becomes CUSTOM)
};

// Builds a program of exactly `cpuBurst` instructions: compute chunks separated by I/O requests.
inline std::vector<Instr> generateScript(const ProcessSpec& s, int pid) {
    std::vector<Instr> p;
    int burst = std::max(1, s.cpuBurst);
    auto compute = [&](int n) { if (n > 0) { Instr i; i.op = Op::COMPUTE; i.a = n; p.push_back(i); } };
    auto io = [&](Op op, const char* dev, int d, const std::string& doc = "") {
        Instr i; i.op = op; i.s = dev; i.a = std::max(1, d); i.s2 = doc; p.push_back(i);
    };
    if (s.type == ProcType::CPU_BOUND) {
        if (s.ioBurst > 0 && burst >= 2) { compute(burst - 1); io(Op::WRITE, "DISK", s.ioBurst, "result_" + std::to_string(pid) + ".dat"); }
        else compute(burst);
        return p;
    }
    int ops = s.type == ProcType::INTERACTIVE ? std::max(1, burst / 3) : std::max(1, burst / 4);
    ops = std::min(ops, std::max(1, burst / 2));
    int perIo = std::max(1, (s.ioBurst > 0 ? s.ioBurst : (s.type == ProcType::INTERACTIVE ? 12 : 30)) / ops);
    int computeTotal = std::max(0, burst - ops), base = computeTotal / ops, extra = computeTotal % ops;
    for (int k = 0; k < ops; ++k) {
        compute(base + (k < extra ? 1 : 0));
        if (s.type == ProcType::INTERACTIVE) { io(Op::READ, "KEYBOARD", std::max(2, perIo)); continue; }
        switch ((k + pid) % 7) {                       // deterministic device mix
            case 2: io(Op::WRITE, "DISK", perIo); break;
            case 4: io(Op::WRITE, "PRINTER", perIo, "report_" + std::to_string(pid) + "_" + std::to_string(k) + ".txt"); break;
            case 6: io(Op::WRITE, "NETWORK", perIo); break;
            default: io(Op::READ, "DISK", perIo);
        }
    }
    return p;
}

// Expands COMPUTE n into n single-tick instructions, each referencing a page with temporal
// locality: 92% of references fall in a sliding working-set window, 8% anywhere.
inline std::vector<Instr> linkProgram(const std::vector<Instr>& script, int vpages, int localityPages, int pageSizeKB, uint32_t seed, bool memoryRefs = true) {
    Rng rng(seed);
    std::vector<Instr> out;
    int vp = std::max(1, vpages);
    auto window = [&]() { return localityPages > 0 ? std::min(localityPages, vp) : std::min(vp, std::max(2, vp / 4)); };
    int wb = 0, refs = 0, pageBytes = std::max(1, pageSizeKB) * 1024;
    for (const Instr& in : script) {
        if (in.op == Op::COMPUTE) {
            for (int k = 0; k < std::max(1, in.a); ++k) {
                int w = window();
                if (refs % 25 == 0) wb = rng.range(0, vp - w);
                Instr c;
                c.op = Op::COMPUTE;
                if (!memoryRefs) { out.push_back(c); continue; }
                c.page = rng.unit() < 0.92 ? std::min(vp - 1, wb + rng.range(0, w - 1)) : rng.range(0, vp - 1);
                c.offset = rng.range(0, pageBytes - 1);
                out.push_back(c);
                ++refs;
            }
            continue;
        }
        if (in.op == Op::ALLOC) vp += in.a;
        if (in.op == Op::FREE) vp = std::max(1, vp - in.a);
        out.push_back(in);
    }
    return out;
}

}  // namespace owr
