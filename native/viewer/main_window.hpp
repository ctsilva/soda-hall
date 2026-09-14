// Declares the viewer window: the tree of floors, rooms, and standalone models with
// visibility checkboxes, display toggles, the selection summary, and manifest loading.
// Capture mode (capture.cpp) drives the window through these typed control handles rather
// than by label text.
#pragma once

#include "mesh/manifest.hpp"
#include "viewport.hpp"

#include <QKeySequence>
#include <QMainWindow>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class QAction;
class QCheckBox;
class QLabel;
class QMenu;
class QPushButton;
class QScrollArea;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

class MainWindow final : public QMainWindow {
  public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads a manifest and rebuilds the tree, registering every part with the viewport. On
    // failure the current dataset is kept. Errors go to a dialog, or to stderr when
    // reportErrors is false. Nothing is shown until show* is called.
    bool load(const QString& filename, bool reportErrors = true);

    // Shows a floor shell ("floor-3"), a room ("room-319"), or a standalone model
    // ("walkthru-building"); false for an unknown id or a mesh that failed to read (reported
    // the same way as load).
    bool showPart(const std::string& id, bool visible = true, bool reportErrors = true);
    void showBuilding();  // Every floor shell, no rooms.
    void showFloorRooms(int floor, bool visible);
    void hideAll();

    Viewport* viewport() const {
        return viewport_;
    }

    std::shared_ptr<const soda::Manifest> manifest() const {
        return manifest_;
    }

    // Handles to the live controls, for capture runs and tests.
    struct Controls {
        QPushButton* open = nullptr;
        QPushButton* frameVisible = nullptr;
        QPushButton* frameSelection = nullptr;
        QPushButton* showFloorRooms = nullptr;
        QPushButton* showBuilding = nullptr;
        QPushButton* hideAll = nullptr;
        QCheckBox* floorShells = nullptr;
        QCheckBox* roomShells = nullptr;
        QCheckBox* furniture = nullptr;
        QCheckBox* grid = nullptr;
        QCheckBox* selection = nullptr;
        std::vector<std::pair<QAction*, bool*>> toggleActions;
        QScrollArea* scroll = nullptr;
        QLabel* info = nullptr;
        QTreeWidget* tree = nullptr;
    };

    const Controls& controls() const {
        return controls_;
    }

  private:
    QAction* command(QMenu*, const QString& label, const QKeySequence&, QPushButton*);
    QCheckBox* toggle(QMenu*, QVBoxLayout*, const QString& name, bool& flag, const QKeySequence&);
    void applyItem(QTreeWidgetItem* item, bool reportErrors);
    void selectItem(QTreeWidgetItem* item);
    // Manifest bounds of the floor, room, or model a row stands for.
    std::optional<soda::Bounds> itemBounds(QTreeWidgetItem* item) const;
    void refreshStatus();
    void reportError(const QString& title, const QString& message, bool reportErrors);
    QTreeWidgetItem* findItem(const std::string& id) const;
    std::optional<int> currentFloor() const;

    Controls controls_;
    Viewport* viewport_ = nullptr;
    std::shared_ptr<const soda::Manifest> manifest_;
    QString manifestName_;
    std::vector<QTreeWidgetItem*> items_;  // Every floor, room, and model row, in tree order.
};
