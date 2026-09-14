// Recursive-descent JSON parsing for the manifest. Only what the manifest needs: no \u
// escapes beyond ASCII, no duplicate-key policy beyond last-wins, no depth limit.
#include "mesh/json.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace soda {

namespace {

class Parser {
    const std::string& text_;
    std::size_t position_ = 0;

  public:
    explicit Parser(const std::string& text) : text_(text) {}

    Json document() {
        auto value = parse_value();
        skip_whitespace();
        if (position_ != text_.size()) {
            fail("trailing content after the document");
        }
        return value;
    }

  private:
    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("json offset " + std::to_string(position_) + ": " + message);
    }

    void skip_whitespace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_]))) {
            ++position_;
        }
    }

    char peek() {
        skip_whitespace();
        if (position_ >= text_.size()) {
            fail("unexpected end of document");
        }
        return text_[position_];
    }

    void expect(char c) {
        if (peek() != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++position_;
    }

    bool consume_literal(const char* literal) {
        const std::string_view view(literal);
        if (text_.compare(position_, view.size(), view) == 0) {
            position_ += view.size();
            return true;
        }
        return false;
    }

    Json parse_value() {
        const char c = peek();
        if (c == '{') {
            return parse_object();
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '"') {
            return Json(parse_string());
        }
        if (consume_literal("true")) {
            return Json(true);
        }
        if (consume_literal("false")) {
            return Json(false);
        }
        if (consume_literal("null")) {
            return Json(nullptr);
        }
        return parse_number();
    }

    Json parse_number() {
        const auto start = position_;
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' ||
                c == 'e' || c == 'E') {
                ++position_;
            } else {
                break;
            }
        }
        double value{};
        const auto* begin = text_.data() + start;
        const auto* end = text_.data() + position_;
        const auto [parsed, error] = std::from_chars(begin, end, value);
        if (start == position_ || error != std::errc{} || parsed != end) {
            position_ = start;
            fail("invalid number");
        }
        return Json(value);
    }

    std::string parse_string() {
        expect('"');
        std::string result;
        while (true) {
            if (position_ >= text_.size()) {
                fail("unterminated string");
            }
            const char c = text_[position_++];
            if (c == '"') {
                return result;
            }
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            if (position_ >= text_.size()) {
                fail("unterminated escape");
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                result.push_back(escaped);
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'u': {
                if (position_ + 4 > text_.size()) {
                    fail("truncated \\u escape");
                }
                unsigned code{};
                const auto* begin = text_.data() + position_;
                const auto [parsed, error] = std::from_chars(begin, begin + 4, code, 16);
                if (error != std::errc{} || parsed != begin + 4 || code > 0x7f) {
                    fail("unsupported \\u escape (ASCII only)");
                }
                result.push_back(static_cast<char>(code));
                position_ += 4;
                break;
            }
            default:
                fail("unknown escape");
            }
        }
    }

    Json parse_array() {
        expect('[');
        Json::Array items;
        if (peek() == ']') {
            ++position_;
            return Json(std::move(items));
        }
        while (true) {
            items.push_back(parse_value());
            const char c = peek();
            ++position_;
            if (c == ']') {
                return Json(std::move(items));
            }
            if (c != ',') {
                fail("expected ',' or ']' in array");
            }
        }
    }

    Json parse_object() {
        expect('{');
        Json::Object members;
        if (peek() == '}') {
            ++position_;
            return Json(std::move(members));
        }
        while (true) {
            auto key = parse_string();
            expect(':');
            members[std::move(key)] = parse_value();
            const char c = peek();
            ++position_;
            if (c == '}') {
                return Json(std::move(members));
            }
            if (c != ',') {
                fail("expected ',' or '}' in object");
            }
        }
    }
};

const Json& null_json() {
    static const Json null;
    return null;
}

}  // namespace

bool Json::is_null() const {
    return std::holds_alternative<std::nullptr_t>(value_);
}

bool Json::is_bool() const {
    return std::holds_alternative<bool>(value_);
}

bool Json::is_number() const {
    return std::holds_alternative<double>(value_);
}

bool Json::is_string() const {
    return std::holds_alternative<std::string>(value_);
}

bool Json::is_array() const {
    return std::holds_alternative<Array>(value_);
}

bool Json::is_object() const {
    return std::holds_alternative<Object>(value_);
}

bool Json::as_bool() const {
    if (!is_bool()) {
        throw std::runtime_error("json value is not a boolean");
    }
    return std::get<bool>(value_);
}

double Json::as_number() const {
    if (!is_number()) {
        throw std::runtime_error("json value is not a number");
    }
    return std::get<double>(value_);
}

const std::string& Json::as_string() const {
    if (!is_string()) {
        throw std::runtime_error("json value is not a string");
    }
    return std::get<std::string>(value_);
}

const Json::Array& Json::as_array() const {
    if (!is_array()) {
        throw std::runtime_error("json value is not an array");
    }
    return std::get<Array>(value_);
}

const Json::Object& Json::as_object() const {
    if (!is_object()) {
        throw std::runtime_error("json value is not an object");
    }
    return std::get<Object>(value_);
}

const Json& Json::at(const std::string& key) const {
    const auto& members = as_object();
    const auto found = members.find(key);
    if (found == members.end()) {
        throw std::runtime_error("json object has no member \"" + key + "\"");
    }
    return found->second;
}

const Json& Json::get(const std::string& key) const {
    if (!is_object()) {
        return null_json();
    }
    const auto& members = std::get<Object>(value_);
    const auto found = members.find(key);
    return found == members.end() ? null_json() : found->second;
}

bool Json::has(const std::string& key) const {
    return is_object() && std::get<Object>(value_).count(key) > 0;
}

Json parse_json(const std::string& text) {
    return Parser(text).document();
}

Json load_json(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error(path.string() + ": cannot open file");
    }
    std::stringstream buffer;
    buffer << stream.rdbuf();
    try {
        return parse_json(buffer.str());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
}

}  // namespace soda
