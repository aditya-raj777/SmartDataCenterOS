// Shared enums and small utilities used across all modules.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace dc {

/// Raised for invalid user input / configuration.
class SimError : public std::runtime_error {
public:
    explicit SimError(const std::string& msg) : std::runtime_error(msg) {}
};

/// The five-state process model.
enum class ProcessState { New, Ready, Running, Waiting, Terminated };

/// Why a process sits outside the ready queue.
enum class WaitReason { None, Memory, IO, Resource };

const char* toString(ProcessState s);
const char* toString(WaitReason r);

/// Ordering convention for numeric priorities.
enum class PriorityOrder { LowerIsHigher, HigherIsHigher };

namespace util {
std::string trim(const std::string& s);
std::string toUpper(std::string s);
std::vector<std::string> split(const std::string& s, char delim);
std::string fmt(double v, int decimals = 2);
double round2(double v);
} // namespace util

} // namespace dc
