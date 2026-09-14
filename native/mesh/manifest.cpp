// Manifest decoding from JSON and the consistency check that opens every mesh file.
#include "mesh/json.hpp"
#include "mesh/manifest.hpp"

#include <glm/common.hpp>

#include <cmath>
#include <stdexcept>

namespace soda {

namespace {

// Bounds in the manifest are rounded to three decimals by the converter.
constexpr float kBoundsTolerance = 0.01f;

glm::vec3 read_vec3(const Json& json) {
    const auto& items = json.as_array();
    if (items.size() != 3) {
        throw std::runtime_error("expected three coordinates");
    }
    return {static_cast<float>(items[0].as_number()), static_cast<float>(items[1].as_number()),
            static_cast<float>(items[2].as_number())};
}

std::optional<Bounds> read_bounds(const Json& json) {
    if (json.is_null()) {
        return std::nullopt;
    }
    return Bounds{read_vec3(json.at("min")), read_vec3(json.at("max"))};
}

Part read_part(const Json& json, const std::filesystem::path& directory) {
    Part part;
    part.path = directory / json.at("path").as_string();
    part.vertices = static_cast<std::size_t>(json.at("vertices").as_number());
    part.triangles = static_cast<std::size_t>(json.at("triangles").as_number());
    part.bounds = read_bounds(json.get("bounds"));
    return part;
}

Room read_room(const Json& json, const std::filesystem::path& directory) {
    Room room;
    room.id = json.at("id").as_string();
    room.name = json.at("name").as_string();
    room.floor = static_cast<int>(json.at("floor").as_number());
    room.shell = read_part(json.at("shell"), directory);
    room.furniture = read_part(json.at("furniture"), directory);
    for (const auto& [name, count] : json.get("objects").as_object()) {
        room.objects.emplace_back(name, static_cast<int>(count.as_number()));
    }
    return room;
}

Floor read_floor(const Json& json, const std::filesystem::path& directory) {
    Floor floor;
    floor.number = static_cast<int>(json.at("number").as_number());
    floor.name = json.at("name").as_string();
    floor.shell = read_part(json.at("shell"), directory);
    floor.furniture = read_part(json.at("furniture"), directory);
    floor.bounds = read_bounds(json.get("bounds"));
    if (json.get("floorplan").is_string()) {
        floor.floorplan = directory / json.get("floorplan").as_string();
    }
    for (const auto& room : json.at("rooms").as_array()) {
        floor.rooms.push_back(read_room(room, directory));
    }
    return floor;
}

Model read_model(const Json& json, const std::filesystem::path& directory) {
    Model model;
    model.id = json.at("id").as_string();
    model.name = json.at("name").as_string();
    if (json.get("source").is_string()) {
        model.source = directory / json.get("source").as_string();
    }
    model.shell = read_part(json.at("shell"), directory);
    return model;
}

bool close(const glm::vec3& a, const glm::vec3& b) {
    const auto difference = glm::abs(a - b);
    return difference.x <= kBoundsTolerance && difference.y <= kBoundsTolerance &&
           difference.z <= kBoundsTolerance;
}

void verify_part(const Part& part, const std::string& label, std::vector<std::string>& problems) {
    Mesh mesh;
    try {
        mesh = load_off(part.path);
    } catch (const std::exception& e) {
        problems.push_back(label + ": " + e.what());
        return;
    }
    if (mesh.vertices.size() != part.vertices) {
        problems.push_back(label + ": manifest lists " + std::to_string(part.vertices) +
                           " vertices, file has " + std::to_string(mesh.vertices.size()));
    }
    if (mesh.triangles.size() != part.triangles) {
        problems.push_back(label + ": manifest lists " + std::to_string(part.triangles) +
                           " triangles, file has " + std::to_string(mesh.triangles.size()));
    }
    const auto actual = mesh.bounds();
    if (actual.has_value() != part.bounds.has_value()) {
        problems.push_back(label + ": manifest and file disagree on whether the mesh is empty");
    } else if (actual &&
               !(close(actual->min, part.bounds->min) && close(actual->max, part.bounds->max))) {
        problems.push_back(label + ": bounds differ from the manifest");
    }
}

}  // namespace

std::optional<Bounds> Room::bounds() const {
    if (shell.bounds && furniture.bounds) {
        return shell.bounds->merged(*furniture.bounds);
    }
    return shell.bounds ? shell.bounds : furniture.bounds;
}

const Room* Manifest::find_room(const std::string& id) const {
    for (const auto& floor : floors) {
        for (const auto& room : floor.rooms) {
            if (room.id == id) {
                return &room;
            }
        }
    }
    return nullptr;
}

const Floor* Manifest::find_floor(int number) const {
    for (const auto& floor : floors) {
        if (floor.number == number) {
            return &floor;
        }
    }
    return nullptr;
}

const Model* Manifest::find_model(const std::string& id) const {
    for (const auto& model : walkthru) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

Manifest load_manifest(const std::filesystem::path& path) {
    const auto json = load_json(path);
    Manifest manifest;
    manifest.directory = std::filesystem::absolute(path).parent_path();
    try {
        manifest.name = json.get("name").is_string() ? json.get("name").as_string() : "";
        manifest.description =
            json.get("description").is_string() ? json.get("description").as_string() : "";
        manifest.units = json.get("units").is_string() ? json.get("units").as_string() : "";
        manifest.up = json.get("up").is_string() ? json.get("up").as_string() : "z";
        manifest.bounds = read_bounds(json.get("bounds"));
        for (const auto& floor : json.at("floors").as_array()) {
            manifest.floors.push_back(read_floor(floor, manifest.directory));
        }
        if (json.get("walkthru").is_array()) {
            for (const auto& model : json.get("walkthru").as_array()) {
                manifest.walkthru.push_back(read_model(model, manifest.directory));
            }
        }
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path.string() + ": " + e.what());
    }
    return manifest;
}

std::vector<std::string> verify_manifest(const Manifest& manifest) {
    std::vector<std::string> problems;
    for (const auto& floor : manifest.floors) {
        verify_part(floor.shell, floor.name + " shell", problems);
        verify_part(floor.furniture, floor.name + " furniture", problems);
        for (const auto& room : floor.rooms) {
            verify_part(room.shell, room.name + " shell", problems);
            verify_part(room.furniture, room.name + " furniture", problems);
        }
    }
    for (const auto& model : manifest.walkthru) {
        verify_part(model.shell, model.name, problems);
    }
    return problems;
}

}  // namespace soda
