#include "json.hpp"

#include <cctype>
#include <stdexcept>
#include <iomanip>

namespace cas {
namespace json {

Value::Value() : m_type(Type::Null) {}
Value::Value(std::nullptr_t) : m_type(Type::Null) {}
Value::Value(bool b) : m_type(Type::Boolean), m_boolVal(b) {}
Value::Value(int i) : m_type(Type::Number), m_numVal(static_cast<double>(i)) {}
Value::Value(int64_t i) : m_type(Type::Number), m_numVal(static_cast<double>(i)) {}
Value::Value(uint32_t u) : m_type(Type::Number), m_numVal(static_cast<double>(u)) {}
Value::Value(uint64_t u) : m_type(Type::Number), m_numVal(static_cast<double>(u)) {}
Value::Value(double d) : m_type(Type::Number), m_numVal(d) {}
Value::Value(const char* s) : m_type(Type::String), m_strVal(s ? s : "") {}
Value::Value(const std::string& s) : m_type(Type::String), m_strVal(s) {}
Value::Value(Type type) : m_type(type) {}

bool Value::asBool(bool def) const {
    if (m_type == Type::Boolean) return m_boolVal;
    if (m_type == Type::Number) return m_numVal != 0.0;
    if (m_type == Type::String) return m_strVal == "true" || m_strVal == "1";
    return def;
}

int64_t Value::asInt64(int64_t def) const {
    if (m_type == Type::Number) return static_cast<int64_t>(m_numVal);
    if (m_type == Type::String) {
        try { return std::stoll(m_strVal); } catch (...) {}
    }
    return def;
}

uint64_t Value::asUInt64(uint64_t def) const {
    if (m_type == Type::Number) return static_cast<uint64_t>(m_numVal);
    if (m_type == Type::String) {
        try { return std::stoull(m_strVal); } catch (...) {}
    }
    return def;
}

double Value::asDouble(double def) const {
    if (m_type == Type::Number) return m_numVal;
    if (m_type == Type::String) {
        try { return std::stod(m_strVal); } catch (...) {}
    }
    return def;
}

std::string Value::asString(const std::string& def) const {
    if (m_type == Type::String) return m_strVal;
    if (m_type == Type::Number) {
        if (m_numVal == static_cast<int64_t>(m_numVal)) {
            return std::to_string(static_cast<int64_t>(m_numVal));
        }
        return std::to_string(m_numVal);
    }
    if (m_type == Type::Boolean) return m_boolVal ? "true" : "false";
    return def;
}

size_t Value::size() const {
    if (m_type == Type::Array) return m_arrVal.size();
    if (m_type == Type::Object) return m_objVal.size();
    return 0;
}

void Value::push_back(const Value& v) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_arrVal.clear();
    }
    m_arrVal.push_back(v);
}

const Value& Value::operator[](size_t index) const {
    static const Value nullVal;
    if (m_type == Type::Array && index < m_arrVal.size()) {
        return m_arrVal[index];
    }
    return nullVal;
}

Value& Value::operator[](size_t index) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_arrVal.clear();
    }
    if (index >= m_arrVal.size()) {
        m_arrVal.resize(index + 1);
    }
    return m_arrVal[index];
}

bool Value::has(const std::string& key) const {
    if (m_type != Type::Object) return false;
    return m_objVal.find(key) != m_objVal.end();
}

const Value& Value::operator[](const std::string& key) const {
    static const Value nullVal;
    if (m_type == Type::Object) {
        auto it = m_objVal.find(key);
        if (it != m_objVal.end()) return it->second;
    }
    return nullVal;
}

Value& Value::operator[](const std::string& key) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
        m_objVal.clear();
    }
    return m_objVal[key];
}

const std::map<std::string, Value>& Value::getObject() const {
    return m_objVal;
}

const std::vector<Value>& Value::getArray() const {
    return m_arrVal;
}

static std::string escapeString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += "\"";
    return out;
}

std::string Value::stringify(bool pretty, int indent) const {
    std::string ind(pretty ? indent * 2 : 0, ' ');
    std::string nextInd(pretty ? (indent + 1) * 2 : 0, ' ');
    std::string nl = pretty ? "\n" : "";
    std::string sp = pretty ? " " : "";

    switch (m_type) {
        case Type::Null: return "null";
        case Type::Boolean: return m_boolVal ? "true" : "false";
        case Type::Number: {
            if (m_numVal == static_cast<int64_t>(m_numVal)) {
                return std::to_string(static_cast<int64_t>(m_numVal));
            }
            std::ostringstream ss;
            ss << m_numVal;
            return ss.str();
        }
        case Type::String: return escapeString(m_strVal);
        case Type::Array: {
            if (m_arrVal.empty()) return "[]";
            std::string res = "[" + nl;
            for (size_t i = 0; i < m_arrVal.size(); ++i) {
                res += nextInd + m_arrVal[i].stringify(pretty, indent + 1);
                if (i + 1 < m_arrVal.size()) res += ",";
                res += nl;
            }
            res += ind + "]";
            return res;
        }
        case Type::Object: {
            if (m_objVal.empty()) return "{}";
            std::string res = "{" + nl;
            size_t idx = 0;
            for (const auto& pair : m_objVal) {
                res += nextInd + escapeString(pair.first) + ":" + sp + pair.second.stringify(pretty, indent + 1);
                if (++idx < m_objVal.size()) res += ",";
                res += nl;
            }
            res += ind + "}";
            return res;
        }
    }
    return "null";
}

namespace {
class Parser {
public:
    Parser(const std::string& str) : m_str(str), m_pos(0) {}

    Value parse() {
        skipWhitespace();
        Value v = parseValue();
        skipWhitespace();
        return v;
    }

private:
    void skipWhitespace() {
        while (m_pos < m_str.size()) {
            char c = m_str[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                m_pos++;
            } else {
                break;
            }
        }
    }

    char peek() {
        skipWhitespace();
        if (m_pos >= m_str.size()) return '\0';
        return m_str[m_pos];
    }

    char get() {
        skipWhitespace();
        if (m_pos >= m_str.size()) return '\0';
        return m_str[m_pos++];
    }

    Value parseValue() {
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
        throw std::runtime_error("Unexpected character in JSON: " + std::string(1, c));
    }

    Value parseObject() {
        get(); // consume '{'
        Value obj(Type::Object);
        skipWhitespace();
        if (peek() == '}') {
            get();
            return obj;
        }

        while (m_pos < m_str.size()) {
            if (peek() != '"') {
                throw std::runtime_error("Expected string key in object at pos " + std::to_string(m_pos));
            }
            std::string key = parseString().asString();
            skipWhitespace();
            if (get() != ':') {
                throw std::runtime_error("Expected ':' after key in object");
            }
            Value val = parseValue();
            obj[key] = val;

            skipWhitespace();
            char c = get();
            if (c == '}') return obj;
            if (c == ',') continue;
            throw std::runtime_error("Expected ',' or '}' in object");
        }
        throw std::runtime_error("Unterminated object");
    }

    Value parseArray() {
        get(); // consume '['
        Value arr(Type::Array);
        skipWhitespace();
        if (peek() == ']') {
            get();
            return arr;
        }

        while (m_pos < m_str.size()) {
            arr.push_back(parseValue());
            skipWhitespace();
            char c = get();
            if (c == ']') return arr;
            if (c == ',') continue;
            throw std::runtime_error("Expected ',' or ']' in array");
        }
        throw std::runtime_error("Unterminated array");
    }

    Value parseString() {
        get(); // consume '"'
        std::string s;
        while (m_pos < m_str.size()) {
            char c = m_str[m_pos++];
            if (c == '"') {
                return Value(s);
            }
            if (c == '\\') {
                if (m_pos >= m_str.size()) break;
                char esc = m_str[m_pos++];
                switch (esc) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/': s += '/'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'n': s += '\n'; break;
                    case 'r': s += '\r'; break;
                    case 't': s += '\t'; break;
                    case 'u': {
                        if (m_pos + 4 <= m_str.size()) {
                            std::string hexStr = m_str.substr(m_pos, 4);
                            m_pos += 4;
                            try {
                                uint32_t code = std::stoul(hexStr, nullptr, 16);
                                if (code < 0x80) {
                                    s += static_cast<char>(code);
                                } else if (code < 0x800) {
                                    s += static_cast<char>(0xC0 | (code >> 6));
                                    s += static_cast<char>(0x80 | (code & 0x3F));
                                } else {
                                    s += static_cast<char>(0xE0 | (code >> 12));
                                    s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                    s += static_cast<char>(0x80 | (code & 0x3F));
                                }
                            } catch (...) {}
                        }
                        break;
                    }
                    default: s += esc; break;
                }
            } else {
                s += c;
            }
        }
        throw std::runtime_error("Unterminated string");
    }

    Value parseBool() {
        if (m_str.compare(m_pos, 4, "true") == 0) {
            m_pos += 4;
            return Value(true);
        }
        if (m_str.compare(m_pos, 5, "false") == 0) {
            m_pos += 5;
            return Value(false);
        }
        throw std::runtime_error("Invalid boolean value");
    }

    Value parseNull() {
        if (m_str.compare(m_pos, 4, "null") == 0) {
            m_pos += 4;
            return Value(nullptr);
        }
        throw std::runtime_error("Invalid null value");
    }

    Value parseNumber() {
        size_t start = m_pos;
        if (m_str[m_pos] == '-') m_pos++;
        while (m_pos < m_str.size() && std::isdigit(m_str[m_pos])) m_pos++;
        if (m_pos < m_str.size() && m_str[m_pos] == '.') {
            m_pos++;
            while (m_pos < m_str.size() && std::isdigit(m_str[m_pos])) m_pos++;
        }
        if (m_pos < m_str.size() && (m_str[m_pos] == 'e' || m_str[m_pos] == 'E')) {
            m_pos++;
            if (m_pos < m_str.size() && (m_str[m_pos] == '+' || m_str[m_pos] == '-')) m_pos++;
            while (m_pos < m_str.size() && std::isdigit(m_str[m_pos])) m_pos++;
        }

        std::string numStr = m_str.substr(start, m_pos - start);
        double d = std::stod(numStr);
        return Value(d);
    }

    const std::string& m_str;
    size_t m_pos;
};
} // anonymous namespace

Value Value::parse(const std::string& str) {
    Parser p(str);
    return p.parse();
}

} // namespace json
} // namespace cas
