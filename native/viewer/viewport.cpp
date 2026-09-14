// Draws the visible parts with flat shading, then the grid and the selection box. Meshes are
// read on the CPU when first shown and uploaded during the next paint, when the context is
// current; this file owns every GL resource and the camera, nothing about the dataset.
#include "viewport.hpp"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QPainter>
#include <QWheelEvent>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {

constexpr glm::vec4 kBackgroundColor{0.93f, 0.94f, 0.96f, 1};
constexpr glm::vec4 kGridColor{0.55f, 0.62f, 0.72f, 1};
constexpr glm::vec4 kSelectionColor{0.95f, 0.5f, 0.05f, 1};
// The grid lies on the ground plane under the building and extends a little past it.
constexpr int kGridDivisions = 24;
constexpr float kGridExtentFactor = 1.4f;
// One directional light fixed to the camera, from the upper left, two-sided so back faces
// of the single-sided room shells read the same as front faces.
constexpr glm::vec3 kLightDirectionView{-0.35f, 0.5f, 1.0f};
constexpr float kAmbientLight = 0.4f;
constexpr int kMinimumViewportWidth = 500;
constexpr int kMinimumViewportHeight = 400;
constexpr int kPositionComponents = 3;
constexpr int kColorComponents = 4;
constexpr double kMinimumSceneRadius = 1e-3;
constexpr double kFrameDistanceFactor = 2.4;
constexpr double kVerticalFieldOfViewDegrees = 45.0;
constexpr double kNearClipRadiusFactor = 0.002;
constexpr int kFarClipRadiusFactor = 12;
constexpr double kOrbitRadiansPerPixel = 0.008;
constexpr double kMaximumPitch = 1.55;  // Just short of straight down, to keep the up vector.
constexpr int kPanHeightFloor = 1;
constexpr double kWheelZoomSensitivity = 0.001;
constexpr double kMinimumZoomRadiusFactor = 0.01;
constexpr int kMaximumZoomRadiusFactor = 40;
constexpr float kColorByteScale = 255.0f;
constexpr std::uint8_t kOpaqueAlpha = 255;

std::uint8_t colorByte(float component) {
    return static_cast<std::uint8_t>(
        std::lround(std::clamp(component, 0.0f, 1.0f) * kColorByteScale));
}

}  // namespace

Viewport::Viewport(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(kMinimumViewportWidth, kMinimumViewportHeight);
}

Viewport::~Viewport() {
    if (context()) {
        disconnect(context(), nullptr, this, nullptr);
    }
    cleanup();
}

void Viewport::cleanup() {
    if (!context()) {
        return;
    }
    makeCurrent();
    program_.reset();
    for (auto& [key, part] : parts_) {
        releasePart(part);
    }
    if (lineVbo_) {
        glDeleteBuffers(1, &lineVbo_);
    }
    if (lineVao_) {
        glDeleteVertexArrays(1, &lineVao_);
    }
    lineVbo_ = lineVao_ = 0;
    doneCurrent();
}

void Viewport::releasePart(Part& part) {
    // Only valid with a current context; callers hold one.
    if (part.vbo) {
        glDeleteBuffers(1, &part.vbo);
    }
    if (part.vao) {
        glDeleteVertexArrays(1, &part.vao);
    }
    part.vbo = part.vao = 0;
    part.count = 0;
    part.loaded = false;
    part.pending.clear();
}

void Viewport::initializeGL() {
    if (!initializeOpenGLFunctions()) {
        error_ = "OpenGL 3.3 core is unavailable";
        return;
    }
    connect(
        context(), &QOpenGLContext::aboutToBeDestroyed, this,
        [this] {
            cleanup();
        },
        Qt::DirectConnection);

    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    program_ = std::make_unique<QOpenGLShaderProgram>();
    // Flat shading without stored normals: the fragment shader takes the cross product of the
    // view-space position derivatives, which is constant across a triangle.
    const char* vertex_shader = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec4 color;
uniform mat4 projection;
uniform mat4 view;
out vec3 viewPosition;
out vec4 tint;
void main() {
    vec4 eye = view * vec4(position, 1);
    viewPosition = eye.xyz;
    gl_Position = projection * eye;
    tint = color;
}
)";
    const char* fragment_shader = R"(#version 330 core
in vec3 viewPosition;
in vec4 tint;
uniform int lit;
uniform vec3 lightDirection;
uniform float ambient;
uniform vec4 overrideColor;
uniform int useOverride;
out vec4 outputColor;
void main() {
    float shade = 1.0;
    if (lit == 1) {
        vec3 n = normalize(cross(dFdx(viewPosition), dFdy(viewPosition)));
        shade = ambient + (1.0 - ambient) * abs(dot(n, normalize(lightDirection)));
    }
    vec4 base = useOverride == 1 ? overrideColor : tint;
    outputColor = vec4(base.rgb * shade, base.a);
}
)";
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader) ||
        !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_shader) ||
        !program_->link()) {
        error_ = program_->log();
        program_.reset();
    }
}

void Viewport::setWorld(std::optional<soda::Bounds> bounds) {
    world_ = bounds;
    grid_.vertices.clear();
    if (world_) {
        const auto center = world_->center();
        const auto half = world_->extent() * (kGridExtentFactor / 2);
        const float z = world_->min.z;
        for (int i = 0; i <= kGridDivisions; ++i) {
            const float t = -1.0f + 2.0f * static_cast<float>(i) / kGridDivisions;
            const float x = center.x + t * half.x;
            const float y = center.y + t * half.y;
            addQuadEdge(grid_, {x, center.y - half.y, z}, {x, center.y + half.y, z});
            addQuadEdge(grid_, {center.x - half.x, y, z}, {center.x + half.x, y, z});
        }
    }
    update();
}

void Viewport::addQuadEdge(Lines& lines, glm::vec3 a, glm::vec3 b) {
    lines.vertices.push_back({a, {}});
    lines.vertices.push_back({b, {}});
}

void Viewport::addBox(Lines& lines, const soda::Bounds& bounds) {
    const auto& lo = bounds.min;
    const auto& hi = bounds.max;
    const std::array<glm::vec3, 8> corners{
        glm::vec3{lo.x, lo.y, lo.z}, glm::vec3{hi.x, lo.y, lo.z}, glm::vec3{hi.x, hi.y, lo.z},
        glm::vec3{lo.x, hi.y, lo.z}, glm::vec3{lo.x, lo.y, hi.z}, glm::vec3{hi.x, lo.y, hi.z},
        glm::vec3{hi.x, hi.y, hi.z}, glm::vec3{lo.x, hi.y, hi.z}};
    constexpr std::array<std::array<int, 2>, 12> edges{{{0, 1},
                                                        {1, 2},
                                                        {2, 3},
                                                        {3, 0},
                                                        {4, 5},
                                                        {5, 6},
                                                        {6, 7},
                                                        {7, 4},
                                                        {0, 4},
                                                        {1, 5},
                                                        {2, 6},
                                                        {3, 7}}};
    for (const auto& edge : edges) {
        addQuadEdge(lines, corners[static_cast<std::size_t>(edge[0])],
                    corners[static_cast<std::size_t>(edge[1])]);
    }
}

void Viewport::addPart(const std::string& key, PartKind kind, std::filesystem::path file,
                       std::optional<soda::Bounds> bounds) {
    auto& part = parts_[key];
    if (part.vao || part.vbo) {
        makeCurrent();
        releasePart(part);
        doneCurrent();
    }
    part = Part{};
    part.kind = kind;
    part.file = std::move(file);
    part.bounds = bounds;
}

void Viewport::clearParts() {
    if (context()) {
        makeCurrent();
        for (auto& [key, part] : parts_) {
            releasePart(part);
        }
        doneCurrent();
    }
    parts_.clear();
    update();
}

void Viewport::loadPart(Part& part) const {
    const auto mesh = soda::load_off(part.file);
    part.pending.clear();
    part.pending.reserve(mesh.triangles.size() * 3);
    for (const auto& triangle : mesh.triangles) {
        const std::array<std::uint8_t, 4> color{colorByte(triangle.color.r),
                                                colorByte(triangle.color.g),
                                                colorByte(triangle.color.b), kOpaqueAlpha};
        for (auto index : triangle.indices) {
            part.pending.push_back({mesh.vertices[index], color});
        }
    }
    part.triangles = mesh.triangles.size();
    part.loaded = true;
    // The file's own extent is the truth once read; the manifest only estimated it.
    if (const auto bounds = mesh.bounds()) {
        part.bounds = bounds;
    }
}

void Viewport::setPartVisible(const std::string& key, bool visible) {
    const auto found = parts_.find(key);
    if (found == parts_.end()) {
        throw std::out_of_range("no part registered as " + key);
    }
    auto& part = found->second;
    if (visible && !part.loaded) {
        loadPart(part);
    }
    part.visible = visible;
    update();
}

bool Viewport::partVisible(const std::string& key) const {
    const auto found = parts_.find(key);
    return found != parts_.end() && found->second.visible;
}

bool Viewport::kindVisible(PartKind kind) const {
    switch (kind) {
    case PartKind::floor_shell:
        return showFloorShells;
    case PartKind::room_shell:
        return showRoomShells;
    case PartKind::furniture:
        return showFurniture;
    }
    return true;
}

std::optional<soda::Bounds> Viewport::visibleBounds() const {
    std::optional<soda::Bounds> result;
    for (const auto& [key, part] : parts_) {
        if (!part.visible || !kindVisible(part.kind) || !part.bounds) {
            continue;
        }
        result = result ? result->merged(*part.bounds) : part.bounds;
    }
    return result;
}

std::size_t Viewport::visibleTriangles() const {
    std::size_t total = 0;
    for (const auto& [key, part] : parts_) {
        if (part.visible && kindVisible(part.kind)) {
            total += part.triangles;
        }
    }
    return total;
}

void Viewport::setSelection(std::optional<soda::Bounds> bounds) {
    selection_ = bounds;
    selectionBox_.vertices.clear();
    if (selection_) {
        addBox(selectionBox_, *selection_);
    }
    update();
}

void Viewport::frame(const soda::Bounds& bounds) {
    target_ = bounds.center();
    radius_ = std::max(static_cast<double>(glm::length(bounds.extent())) / 2, kMinimumSceneRadius);
    distance_ = radius_ * kFrameDistanceFactor;
    update();
}

void Viewport::frameVisible() {
    if (const auto bounds = visibleBounds()) {
        frame(*bounds);
    } else if (world_) {
        frame(*world_);
    }
}

void Viewport::uploadPending() {
    for (auto& [key, part] : parts_) {
        if (!part.loaded || part.pending.empty()) {
            continue;
        }
        if (!part.vao) {
            glGenVertexArrays(1, &part.vao);
            glGenBuffers(1, &part.vbo);
        }
        glBindVertexArray(part.vao);
        glBindBuffer(GL_ARRAY_BUFFER, part.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(part.pending.size() * sizeof(Vertex)),
                     part.pending.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(0, kPositionComponents, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(1, kColorComponents, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                              reinterpret_cast<void*>(offsetof(Vertex, color)));
        glBindVertexArray(0);
        part.count = static_cast<GLsizei>(part.pending.size());
        part.pending.clear();
        part.pending.shrink_to_fit();
    }
}

void Viewport::setUniforms(const glm::mat4& projection, const glm::mat4& view, bool lit) {
    glUniformMatrix4fv(program_->uniformLocation("projection"), 1, GL_FALSE,
                       glm::value_ptr(projection));
    glUniformMatrix4fv(program_->uniformLocation("view"), 1, GL_FALSE, glm::value_ptr(view));
    program_->setUniformValue("lit", lit ? 1 : 0);
    program_->setUniformValue("lightDirection", kLightDirectionView.x, kLightDirectionView.y,
                              kLightDirectionView.z);
    program_->setUniformValue("ambient", kAmbientLight);
}

void Viewport::drawLines(const Lines& lines, const glm::mat4& projection, const glm::mat4& view,
                         glm::vec4 color) {
    if (lines.vertices.empty()) {
        return;
    }
    setUniforms(projection, view, false);
    program_->setUniformValue("useOverride", 1);
    program_->setUniformValue("overrideColor", color.r, color.g, color.b, color.a);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.vertices.size() * sizeof(Vertex)),
                 lines.vertices.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, kPositionComponents, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glVertexAttribPointer(1, kColorComponents, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.vertices.size()));
    glBindVertexArray(0);
    program_->setUniformValue("useOverride", 0);
}

void Viewport::paintGL() {
    // Opaque geometry first with depth, then the grid, then the selection box without depth
    // so a selected room stays outlined even when it sits inside a floor shell.
    if (!error_.isEmpty()) {
        QPainter painter(this);
        painter.drawText(rect(), Qt::AlignCenter, error_);
        return;
    }
    glClearColor(kBackgroundColor.r, kBackgroundColor.g, kBackgroundColor.b, kBackgroundColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!program_) {
        return;
    }
    uploadPending();

    const glm::dvec3 direction{std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_),
                               std::sin(pitch_)};
    const auto eye = target_ + distance_ * direction;
    const glm::mat4 view = glm::lookAt(eye, target_, glm::dvec3(0, 0, 1));
    const glm::mat4 projection = glm::perspective(
        glm::radians(kVerticalFieldOfViewDegrees), double(width()) / std::max(height(), 1),
        radius_ * kNearClipRadiusFactor, distance_ + radius_ * kFarClipRadiusFactor);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    program_->bind();
    setUniforms(projection, view, true);
    program_->setUniformValue("useOverride", 0);
    for (const auto& [key, part] : parts_) {
        if (!part.visible || !kindVisible(part.kind) || part.count == 0) {
            continue;
        }
        glBindVertexArray(part.vao);
        glDrawArrays(GL_TRIANGLES, 0, part.count);
    }
    glBindVertexArray(0);

    if (showGrid) {
        drawLines(grid_, projection, view, kGridColor);
    }
    if (showSelection) {
        glDisable(GL_DEPTH_TEST);
        drawLines(selectionBox_, projection, view, kSelectionColor);
        glEnable(GL_DEPTH_TEST);
    }
    program_->release();
}

void Viewport::mousePressEvent(QMouseEvent* event) {
    if (!acceptInput) {
        return;
    }
    last_ = event->position().toPoint();
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
    if (!acceptInput) {
        return;
    }
    if (event->button() == Qt::LeftButton) {
        frameVisible();
    }
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
    // Left drag orbits: yaw_ turns around the world Z axis, pitch_ tilts toward it. Right or
    // middle drag pans target_ in the camera's right/up basis, one eye distance per viewport
    // height of cursor travel.
    if (!acceptInput) {
        return;
    }
    const auto point = event->position().toPoint();
    const auto delta = point - last_;
    last_ = point;
    if (event->buttons() & Qt::LeftButton) {
        yaw_ -= delta.x() * kOrbitRadiansPerPixel;
        pitch_ =
            std::clamp(pitch_ + delta.y() * kOrbitRadiansPerPixel, -kMaximumPitch, kMaximumPitch);
    }
    if (event->buttons() & (Qt::RightButton | Qt::MiddleButton)) {
        const glm::dvec3 direction{std::cos(pitch_) * std::cos(yaw_),
                                   std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_)};
        const auto right = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), direction));
        const auto up = glm::normalize(glm::cross(direction, right));
        target_ += (-double(delta.x()) * right + double(delta.y()) * up) * distance_ /
                   double(std::max(height(), kPanHeightFloor));
    }
    update();
}

void Viewport::wheelEvent(QWheelEvent* event) {
    if (!acceptInput) {
        return;
    }
    distance_ = std::clamp(distance_ * std::exp(-event->angleDelta().y() * kWheelZoomSensitivity),
                           radius_ * kMinimumZoomRadiusFactor, radius_ * kMaximumZoomRadiusFactor);
    update();
}
