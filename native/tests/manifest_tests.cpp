// Manifest tests: JSON decoding, path resolution, floor/room/model lookups, and the verify
// pass against a fixture dataset written into the directory given as the first argument.
#include "mesh/json.hpp"
#include "mesh/manifest.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error("check failed: " + what);
    }
}

void write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << content;
}

const char* kManifest = R"({
 "name": "Fixture Hall", "units": "inches", "up": "z",
 "bounds": {"min": [0, 0, 0], "max": [2, 2, 1]},
 "floors": [{
  "number": 3, "name": "Floor 3", "floorplan": "floorplans/floor-3.gif",
  "shell": {"path": "floors/floor-3.shell.off", "vertices": 3, "triangles": 1,
            "bounds": {"min": [0, 0, 0], "max": [2, 2, 0]}},
  "furniture": {"path": "floors/floor-3.furniture.off", "vertices": 0, "triangles": 0,
                "bounds": null},
  "bounds": {"min": [0, 0, 0], "max": [2, 2, 1]},
  "rooms": [{
   "id": "room-301", "name": "Room 301", "floor": 3,
   "shell": {"path": "rooms/floor-3/room-301.shell.off", "vertices": 3, "triangles": 1,
             "bounds": {"min": [0, 0, 0], "max": [2, 2, 0]}},
   "furniture": {"path": "rooms/floor-3/room-301.furniture.off", "vertices": 3,
                 "triangles": 1, "bounds": {"min": [0, 0, 1], "max": [1, 1, 1]}},
   "objects": {"CHAIR1": 2, "DESK": 1}
  }]
 }],
 "walkthru": [{
  "id": "walkthru-building", "name": "WALKTHRU model", "source": "source/walkthru/csb3r.macro.ug",
  "shell": {"path": "walkthru/building.shell.off", "vertices": 3, "triangles": 1,
            "bounds": {"min": [0, 0, 0], "max": [2, 2, 0]}}
 }]
})";

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: soda_manifest_tests FIXTURE_DIRECTORY\n";
        return 2;
    }
    try {
        const std::filesystem::path directory = argv[1];
        std::filesystem::remove_all(directory);

        // JSON basics, including escapes and the tolerant `get` accessor.
        const auto json = soda::parse_json(R"({"a": [1, 2.5, -3e2], "s": "x\nyA", "t": true,
                                               "n": null, "o": {}})");
        check(json.at("a").as_array().size() == 3, "array of three");
        check(json.at("a").as_array()[2].as_number() == -300, "exponent number");
        check(json.at("s").as_string() == "x\nyA", "escapes");
        check(json.at("t").as_bool(), "true");
        check(json.get("missing").is_null(), "missing member reads as null");
        check(json.at("o").as_object().empty(), "empty object");
        bool rejected = false;
        try {
            soda::parse_json("[1, 2");
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        check(rejected, "unterminated array rejected");

        write(directory / "manifest.json", kManifest);
        write(directory / "floors/floor-3.shell.off", "OFF\n3 1 0\n0 0 0\n2 0 0\n2 2 0\n3 0 1 2\n");
        write(directory / "floors/floor-3.furniture.off", "OFF\n0 0 0\n");
        write(directory / "rooms/floor-3/room-301.shell.off",
              "OFF\n3 1 0\n0 0 0\n2 0 0\n2 2 0\n3 0 1 2\n");
        write(directory / "rooms/floor-3/room-301.furniture.off",
              "OFF\n3 1 0\n0 0 1\n1 0 1\n1 1 1\n3 0 1 2 0.5 0.5 0.5\n");

        const auto manifest = soda::load_manifest(directory / "manifest.json");
        check(manifest.name == "Fixture Hall", "name");
        check(manifest.floors.size() == 1 && manifest.floors[0].rooms.size() == 1, "one room");
        const auto* room = manifest.find_room("room-301");
        check(room != nullptr && room->floor == 3, "find_room");
        check(manifest.find_room("room-999") == nullptr, "unknown room");
        check(manifest.find_floor(3) != nullptr && manifest.find_floor(4) == nullptr, "find_floor");
        check(room->shell.path == directory / "rooms/floor-3/room-301.shell.off" ||
                  std::filesystem::equivalent(room->shell.path,
                                              directory / "rooms/floor-3/room-301.shell.off"),
              "paths resolve against the manifest directory");
        check(room->objects.size() == 2 && room->objects[0].first == "CHAIR1" &&
                  room->objects[0].second == 2,
              "objects decoded in name order");
        const auto bounds = room->bounds();
        check(bounds && bounds->max.z == 1 && bounds->max.x == 2, "room bounds merge parts");
        check(!manifest.floors[0].furniture.bounds, "empty part has no bounds");
        check(manifest.floors[0].floorplan.has_value(), "floorplan path");
        const auto* model = manifest.find_model("walkthru-building");
        check(model != nullptr && model->shell.triangles == 1 &&
                  model->source.filename() == "csb3r.macro.ug",
              "walkthru model decoded");
        check(manifest.find_model("walkthru-roof") == nullptr, "unknown model");

        write(directory / "walkthru/building.shell.off",
              "OFF\n3 1 0\n0 0 0\n2 0 0\n2 2 0\n3 0 1 2\n");
        check(soda::verify_manifest(manifest).empty(), "fixture verifies cleanly");

        // A wrong triangle count (same vertices and bounds) and a missing file are reported.
        write(directory / "rooms/floor-3/room-301.furniture.off",
              "OFF\n3 2 0\n0 0 1\n1 0 1\n1 1 1\n3 0 1 2\n3 2 1 0\n");
        std::filesystem::remove(directory / "floors/floor-3.shell.off");
        const auto problems = soda::verify_manifest(manifest);
        check(problems.size() == 2, "two problems, got " + std::to_string(problems.size()));

        rejected = false;
        try {
            write(directory / "broken.json", R"({"floors": [{"number": 1}]})");
            soda::load_manifest(directory / "broken.json");
        } catch (const std::runtime_error& e) {
            rejected = std::string(e.what()).find("broken.json") != std::string::npos;
        }
        check(rejected, "manifest missing members rejected with the file name");

        std::cout << "manifest tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
