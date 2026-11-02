// Failure Lab: controlled fault injection against the *simulated* kernel only.
#pragma once
#include "../kernel/kernel.hpp"
#include "config_io.hpp"

namespace owr::failure {

struct Report {
    std::string id, title, summary;
    std::vector<int> pids;
    bool ok = true;
    std::string toJson() const {
        json::Writer w;
        w.obj().k("id").v(id).k("title").v(title).k("summary").v(summary).k("ok").v(ok).k("pids").ints(pids).end();
        return w.str();
    }
};

Report forcePageFault(Kernel& k, int pid = -1);
Report exhaustMemory(Kernel& k, int count = 12);
Report createDeadlock(Kernel& k, int processes = 2);
Report crashProcess(Kernel& k, int pid = -1, const std::string& reason = "INJECTED FAULT");
Report floodIO(Kernel& k, int n = 12, bool memoryRefs = true);
Report deviceFailure(Kernel& k, const std::string& device = "DISK", int ticks = -1);
Report bufferStress(Kernel& k, int n = 6);
Report starvation(Kernel& k, bool configureScheduler = true);
Report inject(Kernel& k, const std::string& action, const json::Value& params);
std::vector<std::pair<std::string, std::string>> catalog();

}  // namespace owr::failure
