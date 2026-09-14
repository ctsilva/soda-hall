// Line-oriented OFF reader. Whole-floor furniture files run to a few hundred thousand faces,
// so parsing uses from_chars on a reused token buffer rather than stream extraction.
#include "mesh/off.hpp"

#include <glm/common.hpp>

#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace soda {

namespace {

constexpr std::size_t kMaximumCount = 100000000;

class LineReader {
    std::filesystem::path path_;
    std::ifstream stream_;
    std::size_t line_number_ = 0;
    std::string line_;
    std::vector<std::string_view> tokens_;

  public:
    explicit LineReader(const std::filesystem::path& path) : path_(path), stream_(path) {
        if (!stream_) {
            fail("cannot open file");
        }
    }

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error(path_.string() + ":" + std::to_string(line_number_) + ": " +
                                 message);
    }

    // Tokens of the next non-blank, non-comment line; false at end of file. The views point
    // into an internal buffer that the next call overwrites.
    bool next() {
        while (std::getline(stream_, line_)) {
            ++line_number_;
            if (const auto hash = line_.find('#'); hash != std::string::npos) {
                line_.erase(hash);
            }
            tokens_.clear();
            std::size_t i = 0;
            while (i < line_.size()) {
                while (i < line_.size() && std::isspace(static_cast<unsigned char>(line_[i]))) {
                    ++i;
                }
                const auto start = i;
                while (i < line_.size() && !std::isspace(static_cast<unsigned char>(line_[i]))) {
                    ++i;
                }
                if (i > start) {
                    tokens_.emplace_back(line_.data() + start, i - start);
                }
            }
            if (!tokens_.empty()) {
                return true;
            }
        }
        return false;
    }

    const std::vector<std::string_view>& line(const char* what) {
        if (!next()) {
            fail(std::string("unexpected end of file, expected ") + what);
        }
        return tokens_;
    }

    std::size_t count(std::string_view token) const {
        std::size_t value{};
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc{} || end != token.data() + token.size() || value > kMaximumCount) {
            fail("invalid count or index: " + std::string(token));
        }
        return value;
    }

    float number(std::string_view token) const {
        float value{};
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc{} || end != token.data() + token.size() || !std::isfinite(value)) {
            fail("invalid finite number: " + std::string(token));
        }
        return value;
    }
};

bool is_off_header(std::string_view token) {
    if (token.size() < 3 || token.substr(token.size() - 3) != "OFF") {
        return false;
    }
    for (char c : token.substr(0, token.size() - 3)) {
        if (c != 'C' && c != 'N' && c != 'S' && c != 'T') {
            return false;
        }
    }
    return true;
}

}  // namespace

Bounds Bounds::merged(const Bounds& other) const {
    return {glm::min(min, other.min), glm::max(max, other.max)};
}

std::optional<Bounds> Mesh::bounds() const {
    if (vertices.empty()) {
        return std::nullopt;
    }
    Bounds box{vertices.front(), vertices.front()};
    for (const auto& vertex : vertices) {
        box.min = glm::min(box.min, vertex);
        box.max = glm::max(box.max, vertex);
    }
    return box;
}

Mesh load_off(const std::filesystem::path& path) {
    LineReader reader(path);
    auto header = reader.line("OFF header");
    if (!is_off_header(header[0])) {
        reader.fail("expected OFF header, got " + std::string(header[0]));
    }
    // Counts may share the header line or follow on their own.
    std::vector<std::string_view> counts(header.begin() + 1, header.end());
    if (counts.empty()) {
        counts = reader.line("vertex/face/edge counts");
    }
    if (counts.size() < 2) {
        reader.fail("expected vertex and face counts");
    }
    const auto vertex_count = reader.count(counts[0]);
    const auto face_count = reader.count(counts[1]);

    Mesh mesh;
    mesh.vertices.reserve(vertex_count);
    mesh.triangles.reserve(face_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        const auto& tokens = reader.line("vertex");
        if (tokens.size() < 3) {
            reader.fail("vertex needs three coordinates");
        }
        mesh.vertices.push_back(
            {reader.number(tokens[0]), reader.number(tokens[1]), reader.number(tokens[2])});
    }
    std::vector<std::uint32_t> indices;
    for (std::size_t i = 0; i < face_count; ++i) {
        const auto& tokens = reader.line("face");
        const auto sides = reader.count(tokens[0]);
        if (sides < 3) {
            reader.fail("face needs at least three vertices");
        }
        if (tokens.size() < sides + 1) {
            reader.fail("face lists fewer vertices than declared");
        }
        indices.clear();
        for (std::size_t j = 0; j < sides; ++j) {
            const auto index = reader.count(tokens[j + 1]);
            if (index >= mesh.vertices.size()) {
                reader.fail("face vertex index out of range: " + std::string(tokens[j + 1]));
            }
            indices.push_back(static_cast<std::uint32_t>(index));
        }
        glm::vec3 color = kDefaultFaceColor;
        if (tokens.size() >= sides + 4) {
            // Any component above one means the color is written in 0..255 bytes.
            color = {reader.number(tokens[sides + 1]), reader.number(tokens[sides + 2]),
                     reader.number(tokens[sides + 3])};
            if (color.r > 1 || color.g > 1 || color.b > 1) {
                color /= 255.0f;
            }
        }
        for (std::size_t j = 1; j + 1 < indices.size(); ++j) {
            mesh.triangles.push_back({{indices[0], indices[j], indices[j + 1]}, color});
        }
    }
    if (reader.next()) {
        reader.fail("unexpected trailing content");
    }
    return mesh;
}

}  // namespace soda
