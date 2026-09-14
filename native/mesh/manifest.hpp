// The dataset manifest: which floors and rooms exist, where their meshes live, and their
// bounds and sizes as recorded by the converter. Reading the manifest loads no meshes.
#pragma once

#include "mesh/off.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace soda {

// One mesh file as described by the manifest. Counts and bounds come from the manifest, so a
// consumer can budget or frame before loading; `verify_manifest` checks them against the file.
struct Part {
    std::filesystem::path path;  // Absolute, resolved against the manifest's directory.
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::optional<Bounds> bounds;  // None for an empty mesh.
};

struct Room {
    std::string id;    // Dataset identifier, e.g. "room-319" or "stairs-3-1".
    std::string name;  // Display name, e.g. "Room 319".
    int floor = 0;
    Part shell;
    Part furniture;
    std::vector<std::pair<std::string, int>> objects;  // Furniture DEF name and placements.

    // Shell bounds merged with furniture bounds; none when both meshes are empty.
    std::optional<Bounds> bounds() const;
};

struct Floor {
    int number = 0;
    std::string name;
    Part shell;      // Kofler's whole-floor walls without furniture.
    Part furniture;  // Every room's furniture on this floor, merged.
    std::optional<Bounds> bounds;
    std::optional<std::filesystem::path> floorplan;
    std::vector<Room> rooms;
};

// A standalone model outside the floor/room structure: the 1994 WALKTHRU UniGrafix files,
// converted to the same frame. Shell only; these carry no furniture.
struct Model {
    std::string id;                // e.g. "walkthru-building".
    std::string name;              // Display name.
    std::filesystem::path source;  // The file it was converted from, absolute.
    Part shell;
};

struct Manifest {
    std::filesystem::path directory;  // The dataset root; part paths are already resolved.
    std::string name;
    std::string description;
    std::string units;
    std::string up;  // Axis name, "z" for this dataset.
    std::optional<Bounds> bounds;
    std::vector<Floor> floors;
    std::vector<Model> walkthru;  // Empty when the dataset was built without the WALKTHRU files.

    const Room* find_room(const std::string& id) const;
    const Floor* find_floor(int number) const;
    const Model* find_model(const std::string& id) const;
};

// Loads manifest.json. Throws runtime_error for unreadable files, malformed JSON, or missing
// required members (floors, rooms, part paths). Mesh files are not opened.
Manifest load_manifest(const std::filesystem::path& path);

// Opens every mesh the manifest names and compares its counts and bounds with the manifest.
// Returns one line per discrepancy; empty when the dataset is consistent.
std::vector<std::string> verify_manifest(const Manifest& manifest);

}  // namespace soda
