// Declares the viewer's OpenGL viewport: a set of registered mesh parts (floor shells, room
// shells, furniture) that are loaded on first display, a Z-up orbit camera, a reference grid,
// and a selection box. Dataset structure and file lookup live in the window, not here.
#pragma once

#include "mesh/off.hpp"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QPoint>
#include <glm/mat4x4.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Which display toggle governs a part, in addition to its own visibility flag.
enum class PartKind { floor_shell, room_shell, furniture };

class Viewport final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
  public:
    explicit Viewport(QWidget* parent = nullptr);
    ~Viewport() override;

    // The building's overall bounds: places the reference grid and frames the initial view.
    void setWorld(std::optional<soda::Bounds> bounds);

    // Registers a mesh file under a key. Nothing is read until the part first becomes
    // visible. Re-registering a key replaces the part and releases its GPU buffers.
    void addPart(const std::string& key, PartKind kind, std::filesystem::path file,
                 std::optional<soda::Bounds> bounds);
    void clearParts();

    // Shows or hides a part. The first show reads the file, which throws runtime_error on a
    // bad file and leaves the part hidden. Hiding keeps the loaded mesh for a later show.
    void setPartVisible(const std::string& key, bool visible);
    bool partVisible(const std::string& key) const;

    // Bounds of every part that is currently drawn, or none when nothing is visible.
    std::optional<soda::Bounds> visibleBounds() const;
    std::size_t visibleTriangles() const;

    void setSelection(std::optional<soda::Bounds> bounds);
    void frame(const soda::Bounds& bounds);
    // Frames the visible parts, or the world when nothing is visible.
    void frameVisible();

    bool showFloorShells = true;
    bool showRoomShells = true;
    bool showFurniture = true;
    bool showGrid = true;
    bool showSelection = true;
    // Capture runs clear this so a stray mouse or wheel event cannot move the camera.
    bool acceptInput = true;

    // Benchmark mode: draw the visible parts this many times per paint, wait for the GPU to
    // finish, and add the elapsed time and triangles drawn to the two counters. Zero means
    // normal painting with no measurement.
    int benchmarkRepeats = 0;
    double benchmarkSeconds = 0;
    std::size_t benchmarkTriangles = 0;

    QString rendererError() const {
        return error_;
    }

  protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

  private:
    static constexpr double kDefaultYaw = -0.9;
    static constexpr double kDefaultPitch = 0.55;

    // Sixteen bytes per vertex: whole-floor furniture runs to a million vertices and the
    // fragment shader derives flat normals from screen-space derivatives, so no normal is
    // stored.
    struct Vertex {
        glm::vec3 position;
        std::array<std::uint8_t, 4> color;
    };

    struct Part {
        PartKind kind = PartKind::room_shell;
        std::filesystem::path file;
        std::optional<soda::Bounds> bounds;
        bool visible = false;
        bool loaded = false;
        std::vector<Vertex> pending;  // Read from disk, waiting for a current GL context.
        GLuint vao = 0;
        GLuint vbo = 0;
        GLsizei count = 0;
        std::size_t triangles = 0;
    };

    // Small line batches (grid, selection box) rebuilt on the CPU and streamed each frame.
    struct Lines {
        std::vector<Vertex> vertices;
    };

    bool kindVisible(PartKind kind) const;
    void loadPart(Part& part) const;
    void uploadPending();
    void releasePart(Part& part);
    void cleanup();
    void drawLines(const Lines& lines, const glm::mat4& projection, const glm::mat4& view,
                   glm::vec4 color);
    void setUniforms(const glm::mat4& projection, const glm::mat4& view, bool lit);
    static void addBox(Lines& lines, const soda::Bounds& bounds);
    static void addQuadEdge(Lines& lines, glm::vec3 a, glm::vec3 b);

    std::map<std::string, Part> parts_;
    std::optional<soda::Bounds> world_;
    std::optional<soda::Bounds> selection_;
    Lines grid_;
    Lines selectionBox_;

    // The camera orbits target_ at distance_; yaw_ turns around Z and pitch_ tilts toward
    // it. radius_ is the framed scene's half-diagonal and scales clipping and zoom limits.
    glm::dvec3 target_{};
    double radius_ = 1;
    double distance_ = 1;
    double yaw_ = kDefaultYaw;
    double pitch_ = kDefaultPitch;
    QPoint last_;

    GLuint lineVao_ = 0;
    GLuint lineVbo_ = 0;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    QString error_;
};
