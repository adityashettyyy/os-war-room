// OS Detective: generated "case files". Each case runs a hidden fault scenario on a fresh kernel,
// shows the symptoms and raw logs, and lets the evaluator name the cause.
#pragma once
#include "experiment_lab.hpp"
#include "simulation_engine.hpp"

namespace owr {

struct DetectiveCase {
    int id = 0;
    std::string title, difficulty, answer, explanation;
    std::vector<std::string> hints;
    std::string symptomsJson;                       // metrics snapshot shown to the player
    std::map<std::string, std::vector<std::string>> logs;   // process | memory | io | interrupt
};

const std::vector<std::string>& detectiveChoices();
int detectiveCaseCount();
const DetectiveCase& detectiveCase(int id);          // built lazily, deterministic, cached
std::string detectiveListJson();
std::string detectiveCaseJson(int id);               // never includes the answer
std::string detectiveDiagnose(int id, const std::string& guess);

}  // namespace owr
