// Waits for the first paints, then grabs the window and the framebuffer. The delay gives Qt
// time to show the window and the viewport time to upload the meshes shown at startup.
#include "capture.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QImage>
#include <QTimer>

#include <iostream>

namespace {

constexpr int kCaptureDelayMilliseconds = 700;

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
