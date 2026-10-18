#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <memory>
#include <cstdint>

namespace cas {
namespace json {

enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class Value {
public:
    Value();
    Value(std::nullptr_t);
    Value(bool b);
    Value(int i);
    Value(int64_t i);
    Value(uint32_t u);
    Value(uint64_t u);
    Value(double d);
    Value(const char* s);
    Value(const std::string& s);
    Value(Type type);

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Boolean; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool asBool(bool def = false) const;
    int64_t asInt64(int64_t def = 0) const;
    uint64_t asUInt64(uint64_t def = 0) const;
    double asDouble(double def = 0.0) const;
    std::string asString(const std::string& def = "") const;

    // Array operations
    size_t size() const;
    void push_back(const Value& v);
    const Value& operator[](size_t index) const;
    Value& operator[](size_t index);

    // Object operations
    bool has(const std::string& key) const;
    const Value& operator[](const std::string& key) const;
    Value& operator[](const std::string& key);
    const std::map<std::string, Value>& getObject() const;

    // Array access
    const std::vector<Value>& getArray() const;

    // Serialization & Parsing
    std::string stringify(bool pretty = false, int indent = 0) const;
    static Value parse(const std::string& str);

private:
    Type m_type;
    bool m_boolVal{false};
    double m_numVal{0.0};
    std::string m_strVal;
    std::vector<Value> m_arrVal;
    std::map<std::string, Value> m_objVal;
};

} // namespace json
} // namespace cas
