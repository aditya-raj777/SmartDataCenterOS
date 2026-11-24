#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace dc {

namespace {
const Json& nullJson() {
    static const Json n;
    return n;
}
} // namespace

Json::Json() : type_(Type::Null), bool_(false), num_(0) {}
Json::Json(std::nullptr_t) : Json() {}
Json::Json(bool v) : type_(Type::Bool), bool_(v), num_(0) {}
Json::Json(int v) : type_(Type::Number), bool_(false), num_(v) {}
Json::Json(long v) : type_(Type::Number), bool_(false), num_(static_cast<double>(v)) {}
Json::Json(long long v) : type_(Type::Number), bool_(false), num_(static_cast<double>(v)) {}
Json::Json(unsigned v) : type_(Type::Number), bool_(false), num_(v) {}
Json::Json(unsigned long v) : type_(Type::Number), bool_(false), num_(static_cast<double>(v)) {}
Json::Json(double v) : type_(Type::Number), bool_(false), num_(v) {}
Json::Json(const char* v) : type_(Type::String), bool_(false), num_(0), str_(v ? v : "") {}
Json::Json(const std::string& v) : type_(Type::String), bool_(false), num_(0), str_(v) {}

Json Json::array() { Json j; j.type_ = Type::Array; return j; }
Json Json::object() { Json j; j.type_ = Type::Object; return j; }

bool Json::asBool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Number) return num_ != 0;
    if (type_ == Type::String) return str_ == "true" || str_ == "1" || str_ == "yes";
    return def;
}

double Json::asNumber(double def) const {
    if (type_ == Type::Number) return num_;
    if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
    if (type_ == Type::String && !str_.empty()) {
        char* end = nullptr;
        double v = std::strtod(str_.c_str(), &end);
        if (end && *end == '\0') return v;
    }
    return def;
}

int Json::asInt(int def) const {
    if (type_ == Type::Null) return def;
    double d = asNumber(static_cast<double>(def));
    return static_cast<int>(std::lround(d));
}

std::string Json::asString(const std::string& def) const {
    if (type_ == Type::String) return str_;
    if (type_ == Type::Number || type_ == Type::Bool) return dump();
    return def;
}

void Json::push(const Json& v) {
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) throw JsonError("push() on non-array JSON value");
    arr_.push_back(v);
}

std::size_t Json::size() const {
    if (type_ == Type::Array) return arr_.size();
    if (type_ == Type::Object) return obj_.size();
    return 0;
}

const Json& Json::at(std::size_t i) const {
    if (type_ != Type::Array || i >= arr_.size()) return nullJson();
    return arr_[i];
}

const std::vector<Json>& Json::items() const {
    static const std::vector<Json> empty;
    return type_ == Type::Array ? arr_ : empty;
}

Json& Json::operator[](const std::string& key) {
    if (type_ == Type::Null) type_ = Type::Object;
    if (type_ != Type::Object) throw JsonError("operator[] on non-object JSON value (key '" + key + "')");
    for (auto& m : obj_)
        if (m.first == key) return m.second;
    obj_.emplace_back(key, Json());
    return obj_.back().second;
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ != Type::Object) return nullJson();
    for (const auto& m : obj_)
        if (m.first == key) return m.second;
    return nullJson();
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& m : obj_)
        if (m.first == key) return true;
    return false;
}

const std::vector<Json::Member>& Json::members() const {
    static const std::vector<Member> empty;
    return type_ == Type::Object ? obj_ : empty;
}

int Json::getInt(const std::string& key, int def) const { return (*this)[key].asInt(def); }
double Json::getNum(const std::string& key, double def) const { return (*this)[key].asNumber(def); }
std::string Json::getStr(const std::string& key, const std::string& def) const { return (*this)[key].asString(def); }
bool Json::getBool(const std::string& key, bool def) const { return (*this)[key].asBool(def); }

// ---------------------------------------------------------------- serialisation
namespace {
void escapeString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

void formatNumber(double v, std::string& out) {
    if (std::isnan(v) || std::isinf(v)) { out += "null"; return; }
    double r = std::round(v);
    if (std::fabs(v - r) < 1e-9 && std::fabs(r) < 9e15) {
        std::ostringstream os;
        os << static_cast<long long>(r);
        out += os.str();
        return;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    // trim trailing zeros
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    out += s;
}
} // namespace

void Json::dumpTo(std::string& out) const {
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: formatNumber(num_, out); break;
    case Type::String: escapeString(str_, out); break;
    case Type::Array:
        out.push_back('[');
        for (std::size_t i = 0; i < arr_.size(); ++i) {
            if (i) out.push_back(',');
            arr_[i].dumpTo(out);
        }
        out.push_back(']');
        break;
    case Type::Object:
        out.push_back('{');
        for (std::size_t i = 0; i < obj_.size(); ++i) {
            if (i) out.push_back(',');
            escapeString(obj_[i].first, out);
            out.push_back(':');
            obj_[i].second.dumpTo(out);
        }
        out.push_back('}');
        break;
    }
}

std::string Json::dump() const {
    std::string out;
    out.reserve(256);
    dumpTo(out);
    return out;
}

// ---------------------------------------------------------------- parsing
namespace {
class Parser {
public:
    explicit Parser(const std::string& t) : s(t), i(0) {}

    Json parseDocument() {
        skipWs();
        Json v = parseValue(0);
        skipWs();
        if (i != s.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    const std::string& s;
    std::size_t i;

    [[noreturn]] void fail(const std::string& msg) {
        throw JsonError("JSON parse error at offset " + std::to_string(i) + ": " + msg);
    }
    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool match(const char* lit) {
        std::size_t n = 0;
        while (lit[n]) ++n;
        if (s.compare(i, n, lit) == 0) { i += n; return true; }
        return false;
    }

    Json parseValue(int depth) {
        if (depth > 200) fail("nesting too deep");
        if (i >= s.size()) fail("unexpected end of input");
        char c = s[i];
        if (c == '{') return parseObject(depth);
        if (c == '[') return parseArray(depth);
        if (c == '"') return Json(parseString());
        if (c == 't') { if (match("true")) return Json(true); fail("invalid literal"); }
        if (c == 'f') { if (match("false")) return Json(false); fail("invalid literal"); }
        if (c == 'n') { if (match("null")) return Json(); fail("invalid literal"); }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        fail(std::string("unexpected character '") + c + "'");
    }

    Json parseNumber() {
        std::size_t start = i;
        if (s[i] == '-') ++i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' || s[i] == '-')) ++i;
        std::string tok = s.substr(start, i - start);
        char* end = nullptr;
        double v = std::strtod(tok.c_str(), &end);
        if (!end || *end != '\0') fail("invalid number '" + tok + "'");
        return Json(v);
    }

    static void appendUtf8(unsigned cp, std::string& out) {
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    unsigned parseHex4() {
        if (i + 4 > s.size()) fail("bad unicode escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad hex digit");
        }
        return v;
    }

    std::string parseString() {
        ++i; // opening quote
        std::string out;
        while (true) {
            if (i >= s.size()) fail("unterminated string");
            char c = s[i++];
            if (c == '"') break;
            if (c == '\\') {
                if (i >= s.size()) fail("bad escape");
                char e = s[i++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(cp, out);
                    break;
                }
                default: fail("bad escape character");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Json parseArray(int depth) {
        ++i;
        Json arr = Json::array();
        skipWs();
        if (i < s.size() && s[i] == ']') { ++i; return arr; }
        while (true) {
            skipWs();
            arr.push(parseValue(depth + 1));
            skipWs();
            if (i >= s.size()) fail("unterminated array");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; break; }
            fail("expected ',' or ']'");
        }
        return arr;
    }

    Json parseObject(int depth) {
        ++i;
        Json obj = Json::object();
        skipWs();
        if (i < s.size() && s[i] == '}') { ++i; return obj; }
        while (true) {
            skipWs();
            if (i >= s.size() || s[i] != '"') fail("expected object key");
            std::string key = parseString();
            skipWs();
            if (i >= s.size() || s[i] != ':') fail("expected ':'");
            ++i;
            skipWs();
            obj[key] = parseValue(depth + 1);
            skipWs();
            if (i >= s.size()) fail("unterminated object");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            fail("expected ',' or '}'");
        }
        return obj;
    }
};
} // namespace

Json Json::parse(const std::string& text) {
    Parser p(text);
    return p.parseDocument();
}

} // namespace dc
