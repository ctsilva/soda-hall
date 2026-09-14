// OFF reader tests: colors in both encodings, polygon fanning, empty meshes, and rejection of
// malformed files. Fixtures are written into the directory given as the first argument.
#include "mesh/off.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

constexpr float kTolerance = 1e-5f;

void check(bool condition, const std::string& what) {
    if (!condition) {
        throw std::runtime_error("check failed: " + what);
    }
}

std::filesystem::path write(const std::filesystem::path& directory, const std::string& name,
                            const std::string& content) {
    const auto path = directory / name;
    std::ofstream(path) << content;
    return path;
}

bool near(float a, float b) {
    return std::abs(a - b) <= kTolerance;
}

void expect_failure(const std::filesystem::path& path, const std::string& fragment) {
    try {
        soda::load_off(path);
    } catch (const std::runtime_error& e) {
        check(std::string(e.what()).find(fragment) != std::string::npos,
              "error for " + path.filename().string() + " should mention '" + fragment +
                  "', got: " + e.what());
        return;
    }
    check(false, path.filename().string() + " should have been rejected");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: soda_off_tests FIXTURE_DIRECTORY\n";
        return 2;
    }
    try {
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);

        // A quad with fractional color and a triangle with byte color, plus a comment line.
        const auto colored = write(directory, "colored.off",
                                   "OFF\n# converted\n5 2 0\n0 0 0\n1 0 0\n1 1 0\n0 1 0\n"
                                   "0 0 1\n4 0 1 2 3 0.25 0.5 1\n3 0 1 4 255 0 0\n");
        const auto mesh = soda::load_off(colored);
        check(mesh.vertices.size() == 5, "five vertices");
        check(mesh.triangles.size() == 3, "quad fans into two triangles plus one");
        check(near(mesh.triangles[0].color.g, 0.5f), "fractional color kept");
        check(mesh.triangles[1].indices[1] == 2 && mesh.triangles[1].indices[2] == 3,
              "fan uses vertices 0, 2, 3 for the second triangle");
        check(near(mesh.triangles[2].color.r, 1.0f) && near(mesh.triangles[2].color.g, 0.0f),
              "byte color scaled to 0..1");
        const auto bounds = mesh.bounds();
        check(bounds && near(bounds->max.z, 1.0f) && near(bounds->min.x, 0.0f), "bounds");

        // Faces without colors get the default, and counts may share the header line.
        const auto plain = write(directory, "plain.off",
                                 "COFF 3 1 0\n0 0 0 1 2 3\n1 0 0 4 5 6\n"
                                 "0 1 0 7 8 9\n3 0 1 2\n");
        const auto plain_mesh = soda::load_off(plain);
        check(plain_mesh.triangles.size() == 1, "one triangle");
        check(near(plain_mesh.triangles[0].color.r, soda::kDefaultFaceColor.r), "default color");

        const auto empty = write(directory, "empty.off", "OFF\n0 0 0\n");
        const auto empty_mesh = soda::load_off(empty);
        check(empty_mesh.vertices.empty() && !empty_mesh.bounds(), "empty mesh has no bounds");

        expect_failure(write(directory, "bad-header.off", "PLY\n1 0 0\n0 0 0\n"), "OFF header");
        expect_failure(
            write(directory, "bad-index.off", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n3 0 1 7\n"),
            "out of range");
        expect_failure(
            write(directory, "short-face.off", "OFF\n3 1 0\n0 0 0\n1 0 0\n0 1 0\n2 0 1\n"),
            "at least three");
        expect_failure(write(directory, "truncated.off", "OFF\n3 1 0\n0 0 0\n1 0 0\n"),
                       "unexpected end");
        expect_failure(write(directory, "trailing.off", "OFF\n1 0 0\n0 0 0\n0 0 0\n"), "trailing");
        expect_failure(directory / "missing.off", "cannot open");

        std::cout << "off tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
