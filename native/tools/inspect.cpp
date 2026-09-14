// Headless dataset inspector: prints the manifest's floors, rooms, and standalone models with
// their sizes, and with --verify opens every mesh to confirm the manifest matches the files.
#include "mesh/manifest.hpp"

#include <iostream>
#include <string>

namespace {

constexpr int kUsageExitCode = 2;

int usage() {
    std::cerr << "Usage: soda_inspect MANIFEST.json [--verify] [--rooms]\n"
                 "  --verify  open every mesh and compare counts and bounds with the manifest\n"
                 "  --rooms   list every room with its triangle counts and furniture\n";
    return kUsageExitCode;
}

void print_bounds(const std::optional<soda::Bounds>& bounds) {
    if (!bounds) {
        std::cout << "(empty)";
        return;
    }
    std::cout << "[" << bounds->min.x << ", " << bounds->min.y << ", " << bounds->min.z << "] to ["
              << bounds->max.x << ", " << bounds->max.y << ", " << bounds->max.z << "]";
}

}  // namespace

int main(int argc, char** argv) {
    std::string path;
    bool verify = false;
    bool rooms = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--verify") {
            verify = true;
        } else if (argument == "--rooms") {
            rooms = true;
        } else if (!path.empty() || argument.starts_with("--")) {
            return usage();
        } else {
            path = argument;
        }
    }
    if (path.empty()) {
        return usage();
    }
    try {
        const auto manifest = soda::load_manifest(path);
        std::cout << manifest.name << ": " << manifest.floors.size() << " floors, units "
                  << manifest.units << ", up " << manifest.up << "\nbounds ";
        print_bounds(manifest.bounds);
        std::cout << '\n';
        std::size_t total = 0;
        for (const auto& floor : manifest.floors) {
            std::size_t room_triangles = 0;
            for (const auto& room : floor.rooms) {
                room_triangles += room.shell.triangles + room.furniture.triangles;
            }
            total += floor.shell.triangles + room_triangles;
            std::cout << floor.name << ": " << floor.rooms.size() << " rooms, shell "
                      << floor.shell.triangles << " triangles, rooms " << room_triangles
                      << " triangles\n";
            if (!rooms) {
                continue;
            }
            for (const auto& room : floor.rooms) {
                std::cout << "  " << room.id << " (" << room.name << "): shell "
                          << room.shell.triangles << ", furniture " << room.furniture.triangles;
                if (!room.objects.empty()) {
                    std::cout << " |";
                    for (const auto& [name, count] : room.objects) {
                        std::cout << ' ' << name << 'x' << count;
                    }
                }
                std::cout << '\n';
            }
        }
        std::cout << "total " << total << " triangles across floor shells and rooms\n";
        for (const auto& model : manifest.walkthru) {
            std::cout << model.id << " (" << model.name << "): " << model.shell.triangles
                      << " triangles from " << model.source.filename().string() << '\n';
        }
        if (verify) {
            const auto problems = soda::verify_manifest(manifest);
            for (const auto& problem : problems) {
                std::cerr << problem << '\n';
            }
            std::cout << (problems.empty() ? "verified: every mesh matches the manifest\n"
                                           : "verification failed\n");
            return problems.empty() ? 0 : 1;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
