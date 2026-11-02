// Simulation controller: owns the kernel, runs timed scenario actions, and loads scenario files.
#pragma once
#include <fstream>
#include <sstream>
#include "../kernel/kernel.hpp"
#include "config_io.hpp"
#include "failure_lab.hpp"

namespace owr {

struct ScenarioInfo {
    std::string name, title, description;
    std::vector<std::string> demonstrates;
    int suggestedTicks = 200;
    std::string toJson() const {
        json::Writer w;
        w.obj().k("name").v(name).k("title").v(title).k("description").v(description).k("demonstrates").strs(demonstrates).k("suggestedTicks").v(suggestedTicks).end();
        return w.str();
    }
};

class Simulation {
public:
    explicit Simulation(Config c = {}) : k_(std::make_unique<Kernel>(c)) {}
    Kernel& kernel() { return *k_; }
    const Kernel& kernel() const { return *k_; }

    void reset(Config c) { actions_.clear(); k_ = std::make_unique<Kernel>(c); }
    void schedule(int at, std::function<void(Kernel&)> fn) { actions_.emplace(at, std::move(fn)); }
    void step(int n = 1) {
        for (int i = 0; i < n; ++i) {
            while (!actions_.empty() && actions_.begin()->first <= k_->now()) {
                auto fn = actions_.begin()->second;
                actions_.erase(actions_.begin());
                fn(*k_);
            }
            k_->tick();
        }
    }
    size_t pendingActions() const { return actions_.size(); }

    // Replace the kernel with one configured by the scenario, then create its processes and arm its actions.
    ScenarioInfo loadScenario(const json::Value& doc) {
        Config c;
        applyConfig(c, doc["config"]);
        reset(c);
        Kernel& k = *k_;
        const auto& setup = doc["setup"];
        for (auto& m : setup["mutexes"].a) k.sync().createMutex(m.str());
        for (auto& s : setup["semaphores"].a) k.sync().createSemaphore(s["name"].str(), s["value"].integer(1));
        for (auto& q : setup["queues"].a) k.ipc().createQueue(q["name"].str(), q["capacity"].integer(c.msgQueueCapacity));
        for (auto& g : setup["segments"].a) k.ipc().segment(g["name"].str(), g["words"].integer(8));
        for (auto& pj : doc["processes"].a) {
            int count = std::clamp(pj["count"].integer(1), 1, 100);
            int step = pj["arrivalStep"].integer(0);
            for (int i = 0; i < count; ++i) {
                ProcessSpec s = specFromJson(pj);
                if (!pj["name"].str().empty() && count > 1) s.name = pj["name"].str() + "-" + std::to_string(i + 1);
                if (s.arrival >= 0 || step > 0) s.arrival = std::max(0, s.arrival) + i * step;
                k.createProcess(s);
            }
        }
        for (auto& a : doc["actions"].a) {
            std::string what = a["do"].str();
            json::Value params = a;
            schedule(a["at"].integer(0), [what, params](Kernel& kk) { failure::inject(kk, what, params); });
        }
        ScenarioInfo info;
        info.name = doc["name"].str();
        info.title = doc["title"].str(info.name);
        info.description = doc["description"].str();
        for (auto& d : doc["demonstrates"].a) info.demonstrates.push_back(d.str());
        info.suggestedTicks = doc["suggestedTicks"].integer(200);
        k.log().log(k.now(), Category::SYSTEM, -1, "SCENARIO LOADED: " + info.title);
        return info;
    }

    static std::string readFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) throw std::runtime_error("cannot open " + path);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

private:
    std::unique_ptr<Kernel> k_;
    std::multimap<int, std::function<void(Kernel&)>> actions_;
};

}  // namespace owr
