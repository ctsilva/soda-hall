// A minimal JSON reader for the dataset manifest: objects, arrays, strings, numbers, booleans,
// and null. No writing, no comments, no streaming; the whole document is parsed into memory.
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace soda {

class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() = default;

    explicit Json(std::nullptr_t) : value_(nullptr) {}

    explicit Json(bool value) : value_(value) {}

    explicit Json(double value) : value_(value) {}

    explicit Json(std::string value) : value_(std::move(value)) {}

    explicit Json(Array value) : value_(std::move(value)) {}

    explicit Json(Object value) : value_(std::move(value)) {}

    bool is_null() const;
    bool is_bool() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    // Typed accessors throw runtime_error when the value has another type.
    bool as_bool() const;
    double as_number() const;
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    // Object member access; `at` throws runtime_error for a missing key or a non-object,
    // `get` returns a null value instead so optional members read naturally.
    const Json& at(const std::string& key) const;
    const Json& get(const std::string& key) const;
    bool has(const std::string& key) const;

  private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

// Parses a complete JSON document. Throws runtime_error with a byte offset on syntax errors,
// on trailing content, and on strings containing unsupported escapes.
Json parse_json(const std::string& text);

// Reads and parses a file; the error names the path.
Json load_json(const std::filesystem::path& path);

}  // namespace soda
