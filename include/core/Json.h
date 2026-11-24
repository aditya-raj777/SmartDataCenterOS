// Smart Data Center OS Simulator
// Minimal, dependency-free JSON value type used by the API/integration layer.
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dc {

class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, Json>;

    Json();
    Json(std::nullptr_t);
    Json(bool v);
    Json(int v);
    Json(long v);
    Json(long long v);
    Json(unsigned v);
    Json(unsigned long v);
    Json(double v);
    Json(const char* v);
    Json(const std::string& v);

    static Json array();
    static Json object();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool def = false) const;
    double asNumber(double def = 0.0) const;
    int asInt(int def = 0) const;
    std::string asString(const std::string& def = "") const;

    // Array access
    void push(const Json& v);
    std::size_t size() const;
    const Json& at(std::size_t i) const;
    const std::vector<Json>& items() const;

    // Object access
    Json& operator[](const std::string& key);             // creates member
    const Json& operator[](const std::string& key) const; // null if missing
    bool has(const std::string& key) const;
    const std::vector<Member>& members() const;

    // Typed getters with defaults (object only)
    int getInt(const std::string& key, int def = 0) const;
    double getNum(const std::string& key, double def = 0.0) const;
    std::string getStr(const std::string& key, const std::string& def = "") const;
    bool getBool(const std::string& key, bool def = false) const;

    std::string dump() const;
    static Json parse(const std::string& text);

private:
    void dumpTo(std::string& out) const;

    Type type_;
    bool bool_;
    double num_;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<Member> obj_;
};

} // namespace dc
