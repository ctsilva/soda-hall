// Entry point for soda_viewer: OpenGL surface setup, argument handling, and the event loop.
// The window lives in main_window.cpp and the screenshot mode in capture.cpp.
#include "capture.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QSurfaceFormat>

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr int kOpenGLMajorVersion = 3;
constexpr int kOpenGLMinorVersion = 3;
constexpr int kDepthBufferBits = 24;
constexpr int kUsageExitCode = 2;
const QString kShowPrefix = "--show=";
const QString kBenchmarkPrefix = "--benchmark";
constexpr int kDefaultBenchmarkRepeats = 20;
constexpr double kBenchmarkSeconds = 5;

int usage() {
    std::cerr << "Usage: soda_viewer [MANIFEST.json] [--show=ID,...] [--capture OUTPUT.png]\n"
                 "                   [--benchmark[=REPEATS]]\n"
                 "  --show=   comma-separated ids to show, e.g. floor-3,room-319 or\n"
                 "            walkthru-building; floor-3/rooms shows every room on floor 3;\n"
                 "            rooms shows every room with its furniture; building shows\n"
                 "            every floor's walls (the default).\n"
                 "  --capture save the window and viewport to PNG after painting, then exit\n"
                 "  --benchmark draw the visible parts REPEATS times per frame (default 20)\n"
                 "            for a few seconds, print triangles per second, then exit\n";
    return kUsageExitCode;
}

}  // namespace

int main(int argc, char** argv) {
    // macOS needs the core-profile default format set before QApplication exists.
    QSurfaceFormat format;
    format.setVersion(kOpenGLMajorVersion, kOpenGLMinorVersion);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(kDepthBufferBits);
    // A benchmark must not wait for the display refresh; that is decided before the
    // application exists, so the flag is looked for ahead of the full argument pass.
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]).startsWith(kBenchmarkPrefix)) {
            format.setSwapInterval(0);
        }
    }
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);

    std::optional<QString> manifest;
    std::optional<QString> capture;
    std::optional<QString> show;
    std::optional<int> benchmark;
    for (int i = 1; i < argc; ++i) {
        const auto argument = QString::fromLocal8Bit(argv[i]);
        if (argument.startsWith(kShowPrefix)) {
            show = argument.mid(kShowPrefix.size());
        } else if (argument == kBenchmarkPrefix) {
            benchmark = kDefaultBenchmarkRepeats;
        } else if (argument.startsWith(kBenchmarkPrefix + "=")) {
            bool ok = false;
            benchmark = argument.mid(kBenchmarkPrefix.size() + 1).toInt(&ok);
            if (!ok || *benchmark < 1) {
                return usage();
            }
        } else if (argument == "--capture") {
            if (i + 1 >= argc || capture) {
                return usage();
            }
            capture = QString::fromLocal8Bit(argv[++i]);
        } else if (argument.startsWith("--") || manifest) {
            return usage();
        } else {
            manifest = argument;
        }
    }
    if ((!manifest && (capture || show || benchmark)) || (capture && benchmark)) {
        return usage();
    }

    MainWindow window;
    window.show();
    if (manifest) {
        // Unattended runs report load failures on stderr, not in a dialog.
        const bool report = !capture && !benchmark;
        if (!window.load(*manifest, report)) {
            return 1;
        }
        if (show) {
            for (const auto& id : show->split(',', Qt::SkipEmptyParts)) {
                const auto name = id.trimmed();
                if (name == "rooms") {
                    for (const auto& floor : window.manifest()->floors) {
                        window.showFloorRooms(floor.number, true);
                    }
                } else if (name == "building") {
                    window.showBuilding();
                } else if (name.startsWith("floor-") && name.endsWith("/rooms")) {
                    const auto number = name.mid(6, name.size() - 6 - 6).toInt();
                    window.showFloorRooms(number, true);
                } else if (!window.showPart(name.toStdString(), true, report)) {
                    return 1;
                }
            }
        } else {
            window.showBuilding();
        }
        window.viewport()->frameVisible();
        if (capture) {
            scheduleCapture(window, app, *capture);
        } else if (benchmark) {
            scheduleBenchmark(window, app, *benchmark, kBenchmarkSeconds);
        }
    }
    return app.exec();
}
