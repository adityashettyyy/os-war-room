// Diagnostic record saved by the kernel error handler before a failed process is torn down.
#pragma once
#include "../common/types.hpp"

namespace owr {

struct CrashDump {
    int tick = 0, pid = -1;
    std::string name, reason;
    State stateBefore = State::READY;
    int pc = 0, regs[4] = {0, 0, 0, 0};
    std::string lastInstr;
    std::vector<std::string> heldResources;
    int residentPages = 0, pendingIo = 0;
};

}  // namespace owr
