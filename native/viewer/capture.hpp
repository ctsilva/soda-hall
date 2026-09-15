// Unattended modes for soda_viewer: a screenshot mode that saves the window and the viewport
// framebuffer to PNG files, and a benchmark mode that times GPU drawing of the visible parts.
// Both exit with a status code once done.
#pragma once

#include <QString>

class MainWindow;
class QApplication;

// Captures `output` (the whole window) and `output + ".viewport.png"` (the GL framebuffer)
// once the window has painted, then quits the application with 0, or 1 if a write fails.
void scheduleCapture(MainWindow&, QApplication&, const QString& output);

// Repaints the viewport continuously for about `seconds`, drawing the visible parts
// `repeats` times per paint, then prints triangles per second to stdout and quits with 0.
void scheduleBenchmark(MainWindow&, QApplication&, int repeats, double seconds);
