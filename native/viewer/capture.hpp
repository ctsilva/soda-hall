// Screenshot mode for soda_viewer: after the window has painted, saves the window and the
// viewport framebuffer to PNG files and exits with a status code. Used to check rendering
// without a person at the screen.
#pragma once

#include <QString>

class MainWindow;
class QApplication;

// Captures `output` (the whole window) and `output + ".viewport.png"` (the GL framebuffer)
// once the window has painted, then quits the application with 0, or 1 if a write fails.
void scheduleCapture(MainWindow&, QApplication&, const QString& output);
