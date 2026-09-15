// Capture waits for the first paints, then grabs the window and the framebuffer; the delay
// gives Qt time to show the window and the viewport time to upload the meshes shown at
// startup. Benchmark forces synchronous repaints from a zero-interval timer and reads the
// viewport's accumulated GPU time.
#include "capture.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>

#include <iomanip>
#include <iostream>

namespace {

constexpr int kCaptureDelayMilliseconds = 700;
constexpr int kBenchmarkWarmupFrames = 3;  // Uploads and shader compilation land here.
constexpr double kMillisecondsPerSecond = 1e3;
constexpr double kMillion = 1e6;

}  // namespace

void scheduleCapture(MainWindow& window, QApplication& app, const QString& output) {
    window.viewport()->acceptInput = false;
    QTimer::singleShot(kCaptureDelayMilliseconds, &window, [&window, &app, output] {
        bool ok = true;
        const auto viewportImage = window.viewport()->grabFramebuffer();
        if (viewportImage.isNull() || !viewportImage.save(output + ".viewport.png")) {
            std::cerr << "could not save the viewport image\n";
            ok = false;
        }
        const auto windowImage = window.grab().toImage();
        if (windowImage.isNull() || !windowImage.save(output)) {
            std::cerr << "could not save the window image\n";
            ok = false;
        }
        const auto error = window.viewport()->rendererError();
        if (!error.isEmpty()) {
            std::cerr << "renderer error: " << error.toStdString() << '\n';
            ok = false;
        }
        app.exit(ok ? 0 : 1);
    });
}

void scheduleBenchmark(MainWindow& window, QApplication& app, int repeats, double seconds) {
    auto* viewport = window.viewport();
    viewport->acceptInput = false;
    viewport->benchmarkRepeats = std::max(1, repeats);
    auto* timer = new QTimer(&window);
    auto* clock = new QElapsedTimer;
    auto* frames = new int(0);
    QObject::connect(timer, &QTimer::timeout, &window, [=, &app] {
        viewport->repaint();
        ++*frames;
        if (*frames == kBenchmarkWarmupFrames) {
            viewport->benchmarkSeconds = 0;
            viewport->benchmarkTriangles = 0;
            clock->start();
        }
        if (*frames <= kBenchmarkWarmupFrames ||
            clock->elapsed() < seconds * kMillisecondsPerSecond) {
            return;
        }
        timer->stop();
        const int measured = *frames - kBenchmarkWarmupFrames;
        const double gpuSeconds = viewport->benchmarkSeconds;
        const auto triangles = viewport->benchmarkTriangles;
        const auto perFrame = viewport->visibleTriangles();
        std::cout << std::fixed << std::setprecision(1) << "visible triangles: " << perFrame
                  << "\nrepeats per paint: " << viewport->benchmarkRepeats
                  << "\npaints measured: " << measured << "\nGPU seconds: " << std::setprecision(3)
                  << gpuSeconds << "\ntriangles drawn: " << triangles
                  << "\nmillion triangles per second: " << std::setprecision(1)
                  << (gpuSeconds > 0 ? triangles / gpuSeconds / kMillion : 0)
                  << "\nmilliseconds per full scene: " << std::setprecision(2)
                  << (triangles > 0 ? gpuSeconds * kMillisecondsPerSecond * perFrame / triangles
                                    : 0)
                  << '\n';
        delete clock;
        delete frames;
        app.exit(viewport->rendererError().isEmpty() ? 0 : 1);
    });
    timer->start(0);
}
