// Shared enums, name tables and small helpers used by every kernel module.
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace owr {

enum class State { NEW, READY, RUNNING, WAITING, SUSPENDED, TERMINATED };
enum class WaitReason { NONE, IO, PAGE_FAULT, LOCK, SEMAPHORE, MESSAGE, SLEEP, BUFFER_FULL };
enum class ProcType { CPU_BOUND, IO_BOUND, INTERACTIVE, CUSTOM };
enum class Category { SYSTEM, PROCESS, SCHED, INTERRUPT, SYSCALL, MEMORY, IO, SYNC, IPC, ERROR, FAILURE };
enum class SchedPolicy { FCFS, SJF, SRTF, RR, PRIORITY, MLFQ, ADAPTIVE };
enum class ReplPolicy { FIFO, LRU, OPTIMAL, CLOCK };
enum class RecoveryPolicy { TERMINATE, ROLLBACK };
enum class IntType { ERROR, PAGE_FAULT, IO_COMPLETE, TIMER, SYSCALL };  // ordered by service priority

inline const std::array<const char*, 6> STATE_NAMES{"NEW", "READY", "RUNNING", "WAITING", "SUSPENDED", "TERMINATED"};
inline const std::array<const char*, 8> WAIT_NAMES{"NONE", "IO", "PAGE_FAULT", "LOCK", "SEMAPHORE", "MESSAGE", "SLEEP", "BUFFER_FULL"};
inline const std::array<const char*, 4> PTYPE_NAMES{"CPU_BOUND", "IO_BOUND", "INTERACTIVE", "CUSTOM"};
inline const std::array<const char*, 11> CAT_NAMES{"SYSTEM", "PROCESS", "SCHED", "INTERRUPT", "SYSCALL", "MEMORY", "IO", "SYNC", "IPC", "ERROR", "FAILURE"};
inline const std::array<const char*, 7> SCHED_NAMES{"FCFS", "SJF", "SRTF", "RR", "PRIORITY", "MLFQ", "ADAPTIVE"};
inline const std::array<const char*, 4> REPL_NAMES{"FIFO", "LRU", "OPTIMAL", "CLOCK"};
inline const std::array<const char*, 2> RECOVERY_NAMES{"TERMINATE", "ROLLBACK"};
inline const std::array<const char*, 5> INT_NAMES{"ERROR", "PAGE_FAULT", "IO_COMPLETE", "TIMER", "SYSCALL"};

inline const char* str(State v) { return STATE_NAMES[(int)v]; }
inline const char* str(WaitReason v) { return WAIT_NAMES[(int)v]; }
inline const char* str(ProcType v) { return PTYPE_NAMES[(int)v]; }
inline const char* str(Category v) { return CAT_NAMES[(int)v]; }
inline const char* str(SchedPolicy v) { return SCHED_NAMES[(int)v]; }
inline const char* str(ReplPolicy v) { return REPL_NAMES[(int)v]; }
inline const char* str(RecoveryPolicy v) { return RECOVERY_NAMES[(int)v]; }
inline const char* str(IntType v) { return INT_NAMES[(int)v]; }

inline std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// Case-insensitive lookup of an enum value from its printed name.
template <class E, size_t N>
std::optional<E> parseEnum(const std::array<const char*, N>& names, const std::string& text) {
    std::string u = upper(text);
    for (size_t i = 0; i < N; ++i)
        if (u == names[i]) return static_cast<E>(i);
    return std::nullopt;
}
inline std::optional<SchedPolicy> parseSched(const std::string& s) {
    if (upper(s) == "ROUND ROBIN" || upper(s) == "ROUND_ROBIN") return SchedPolicy::RR;
    return parseEnum<SchedPolicy>(SCHED_NAMES, s);
}
inline std::optional<ReplPolicy> parseRepl(const std::string& s) { return parseEnum<ReplPolicy>(REPL_NAMES, s); }
inline std::optional<ProcType> parsePType(const std::string& s) {
    std::string u = upper(s);
    if (u == "CPU") return ProcType::CPU_BOUND;
    if (u == "IO") return ProcType::IO_BOUND;
    return parseEnum<ProcType>(PTYPE_NAMES, s);
}

}  // namespace owr
