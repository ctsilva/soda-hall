// Reads the dataset's OFF meshes: triangles with one RGB color per face, in building
// coordinates. Polygons with more than three vertices are accepted and fanned, but the
// converter already triangulates, so a well-formed dataset file never needs it.
#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace soda {

struct Bounds {
    glm::vec3 min{};
    glm::vec3 max{};

    glm::vec3 center() const {
        return (min + max) * 0.5f;
    }

    glm::vec3 extent() const {
        return max - min;
    }

    // The smallest box holding both; `other` alone when this box is empty and vice versa.
    Bounds merged(const Bounds& other) const;
};

struct Triangle {
    std::array<std::uint32_t, 3> indices{};
    glm::vec3 color{};  // Components in 0..1.
};

struct Mesh {
    std::vector<glm::vec3> vertices;
    std::vector<Triangle> triangles;

    // No value for an empty mesh.
    std::optional<Bounds> bounds() const;
};

// Reads an OFF file. The header may be OFF or a prefixed variant (COFF, NOFF, ...); extra
// per-vertex fields are ignored. Each face line is "k i1 ... ik [r g b [a]]"; colors may be
// 0..1 fractions or 0..255 integers, and faces without a color get kDefaultFaceColor. Lines
// starting with '#' are comments. Throws runtime_error naming the file and line for syntax
// errors, out-of-range indices, and faces with fewer than three vertices. An empty mesh
// (zero vertices and faces) is valid and returns empty vectors.
Mesh load_off(const std::filesystem::path& path);

constexpr glm::vec3 kDefaultFaceColor{0.8f, 0.8f, 0.8f};

}  // namespace soda
