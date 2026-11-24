#include "core/Types.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace dc {

const char* toString(ProcessState s) {
    switch (s) {
    case ProcessState::New: return "NEW";
    case ProcessState::Ready: return "READY";
    case ProcessState::Running: return "RUNNING";
    case ProcessState::Waiting: return "WAITING";
    case ProcessState::Terminated: return "TERMINATED";
    }
    return "?";
}

const char* toString(WaitReason r) {
    switch (r) {
    case WaitReason::None: return "";
    case WaitReason::Memory: return "MEMORY";
    case WaitReason::IO: return "IO";
    case WaitReason::Resource: return "RESOURCE";
    }
    return "";
}

namespace util {

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string toUpper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

std::string fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }

} // namespace util
} // namespace dc
