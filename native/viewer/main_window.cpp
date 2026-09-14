// Builds the viewer window and maps tree checkboxes onto viewport parts. Floor rows toggle
// the floor's shell; room rows toggle the room's shell and furniture together, with the
// display toggles filtering by kind on top.
#include "main_window.hpp"

#include <QAction>
#include <QCheckBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <iostream>
#include <stdexcept>

namespace {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 840;
constexpr int kMinimumTreeHeight = 260;
constexpr int kMinimumPanelWidth = 300;
constexpr int kInitialPanelWidth = 320;
constexpr int kInitialViewportWidth = 960;
constexpr int kIdRole = Qt::UserRole;
constexpr int kFloorRole = Qt::UserRole + 1;

QString partKey(const std::string& id, const char* part) {
    return QString::fromStdString(id) + "/" + part;
}

QString roomDescription(const soda::Room& room) {
    auto text = QString("%1 · floor %2\nShell: %3 triangles · Furniture: %4 triangles")
                    .arg(QString::fromStdString(room.name))
                    .arg(room.floor)
                    .arg(room.shell.triangles)
                    .arg(room.furniture.triangles);
    if (!room.objects.empty()) {
        QStringList objects;
        for (const auto& [name, count] : room.objects) {
            objects << (count > 1 ? QString("%1 ×%2").arg(QString::fromStdString(name)).arg(count)
                                  : QString::fromStdString(name));
        }
        text += "\nObjects: " + objects.join(", ");
    }
    return text;
}

QString floorDescription(const soda::Floor& floor) {
    std::size_t furniture = 0;
    for (const auto& room : floor.rooms) {
        furniture += room.furniture.triangles;
    }
    return QString("%1 · %2 rooms\nShell: %3 triangles · Room furniture: %4 triangles")
        .arg(QString::fromStdString(floor.name))
        .arg(floor.rooms.size())
        .arg(floor.shell.triangles)
        .arg(furniture);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Soda Hall Viewer");
    resize(kInitialWindowWidth, kInitialWindowHeight);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* viewMenu = menuBar()->addMenu("&View");
    auto* showMenu = menuBar()->addMenu("&Show");

    auto* splitter = new QSplitter;
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    controls_.open = new QPushButton("Open manifest…");
    layout->addWidget(controls_.open);
    command(fileMenu, "Open manifest…", QKeySequence::Open, controls_.open);

    controls_.tree = new QTreeWidget;
    controls_.tree->setHeaderLabels({"Floor / room", "Triangles"});
    controls_.tree->header()->setStretchLastSection(false);
    controls_.tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    controls_.tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    controls_.tree->setMinimumHeight(kMinimumTreeHeight);
    controls_.tree->setToolTip("Check a floor to show its walls; check a room to show its "
                               "walls and furniture. Double-click to frame.");
    layout->addWidget(controls_.tree, 1);

    controls_.info = new QLabel("Open manifest.json from the dataset directory.");
    controls_.info->setWordWrap(true);
    layout->addWidget(controls_.info);

    viewport_ = new Viewport;

    auto* showRow = new QHBoxLayout;
    controls_.showFloorRooms = new QPushButton("Floor's rooms");
    controls_.showBuilding = new QPushButton("Building");
    controls_.hideAll = new QPushButton("Hide all");
    controls_.showFloorRooms->setToolTip("Show every room on the selected floor");
    controls_.showBuilding->setToolTip("Show every floor's walls and no rooms");
    showRow->addWidget(controls_.showFloorRooms);
    showRow->addWidget(controls_.showBuilding);
    showRow->addWidget(controls_.hideAll);
    layout->addWidget(new QLabel("Show"));
    layout->addLayout(showRow);
    command(showMenu, "Selected floor's rooms", QKeySequence("Ctrl+Shift+R"),
            controls_.showFloorRooms);
    command(showMenu, "Building shells", QKeySequence("Ctrl+Shift+B"), controls_.showBuilding);
    command(showMenu, "Hide all", QKeySequence("Ctrl+Shift+H"), controls_.hideAll);
    connect(controls_.showFloorRooms, &QPushButton::clicked, this, [this] {
        if (const auto floor = currentFloor()) {
            showFloorRooms(*floor, true);
            viewport_->frameVisible();
        }
    });
    connect(controls_.showBuilding, &QPushButton::clicked, this, [this] {
        showBuilding();
        viewport_->frameVisible();
    });
    connect(controls_.hideAll, &QPushButton::clicked, this, &MainWindow::hideAll);

    auto* frameRow = new QHBoxLayout;
    controls_.frameVisible = new QPushButton("Frame visible");
    controls_.frameSelection = new QPushButton("Frame selection");
    frameRow->addWidget(controls_.frameVisible);
    frameRow->addWidget(controls_.frameSelection);
    layout->addLayout(frameRow);
    command(viewMenu, "Frame visible", QKeySequence("Ctrl+Shift+F"), controls_.frameVisible);
    command(viewMenu, "Frame selection", QKeySequence("Ctrl+Shift+E"), controls_.frameSelection);
    connect(controls_.frameVisible, &QPushButton::clicked, viewport_, &Viewport::frameVisible);
    connect(controls_.frameSelection, &QPushButton::clicked, this, [this] {
        selectItem(controls_.tree->currentItem());
        if (auto* item = controls_.tree->currentItem()) {
            const auto id = item->data(0, kIdRole).toString().toStdString();
            if (const auto* room = manifest_ ? manifest_->find_room(id) : nullptr) {
                if (const auto bounds = room->bounds()) {
                    viewport_->frame(*bounds);
                }
            } else if (const auto floor =
                           manifest_ ? manifest_->find_floor(item->data(0, kFloorRole).toInt())
                                     : nullptr) {
                if (floor->bounds) {
                    viewport_->frame(*floor->bounds);
                }
            }
        }
    });
    viewMenu->addSeparator();

    controls_.floorShells = toggle(viewMenu, layout, "Floor shells", viewport_->showFloorShells,
                                   QKeySequence("Ctrl+Shift+L"));
    controls_.roomShells = toggle(viewMenu, layout, "Room shells", viewport_->showRoomShells,
                                  QKeySequence("Ctrl+Shift+W"));
    controls_.furniture = toggle(viewMenu, layout, "Furniture", viewport_->showFurniture,
                                 QKeySequence("Ctrl+Shift+U"));
    controls_.grid = toggle(viewMenu, layout, "Reference grid", viewport_->showGrid,
                            QKeySequence("Ctrl+Shift+G"));
    controls_.selection = toggle(viewMenu, layout, "Selection box", viewport_->showSelection,
                                 QKeySequence("Ctrl+Shift+X"));

    auto* help = new QLabel("Drag: orbit · Right drag: pan · Scroll: zoom\n"
                            "Double-click the view: frame visible\n"
                            "Shortcuts: see the View and Show menus");
    help->setWordWrap(true);
    layout->addWidget(help);

    controls_.scroll = new QScrollArea;
    controls_.scroll->setWidgetResizable(true);
    controls_.scroll->setWidget(panel);
    controls_.scroll->setMinimumWidth(kMinimumPanelWidth);
    splitter->addWidget(controls_.scroll);
    splitter->addWidget(viewport_);
    splitter->setSizes({kInitialPanelWidth, kInitialViewportWidth});
    setCentralWidget(splitter);

    connect(controls_.tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int) {
        applyItem(item, true);
        refreshStatus();
    });
    connect(controls_.tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        selectItem(item);
    });
    connect(controls_.tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem*) {
        controls_.frameSelection->click();
    });
    connect(controls_.open, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, "Open dataset manifest", {},
                                                       "Manifest (manifest.json);;All files (*)");
        if (!path.isEmpty() && load(path)) {
            showBuilding();
            viewport_->frameVisible();
        }
    });
}

MainWindow::~MainWindow() {
    delete takeCentralWidget();
}

QAction* MainWindow::command(QMenu* menu, const QString& label, const QKeySequence& shortcut,
                             QPushButton* button) {
    auto* action = menu->addAction(label);
    action->setShortcut(shortcut);
    connect(action, &QAction::triggered, button, &QPushButton::click);
    button->setToolTip(button->toolTip().isEmpty()
                           ? label + " (" + shortcut.toString(QKeySequence::NativeText) + ")"
                           : button->toolTip() + " (" +
                                 shortcut.toString(QKeySequence::NativeText) + ")");
    return action;
}

QCheckBox* MainWindow::toggle(QMenu* menu, QVBoxLayout* layout, const QString& name, bool& flag,
                              const QKeySequence& shortcut) {
    auto* box = new QCheckBox(name);
    box->setChecked(flag);
    layout->addWidget(box);
    auto* action = menu->addAction(name);
    action->setCheckable(true);
    action->setChecked(flag);
    action->setShortcut(shortcut);
    box->setToolTip(name + " (" + shortcut.toString(QKeySequence::NativeText) + ")");
    connect(action, &QAction::toggled, box, &QCheckBox::setChecked);
    connect(box, &QCheckBox::toggled, action, &QAction::setChecked);
    controls_.toggleActions.push_back({action, &flag});
    connect(box, &QCheckBox::toggled, viewport_, [this, &flag](bool on) {
        flag = on;
        viewport_->update();
        refreshStatus();
    });
    return box;
}

void MainWindow::reportError(const QString& title, const QString& message, bool reportErrors) {
    if (reportErrors) {
        QMessageBox::warning(this, title, message);
    } else {
        std::cerr << title.toStdString() << ": " << message.toStdString() << '\n';
    }
}

bool MainWindow::load(const QString& filename, bool reportErrors) {
    std::shared_ptr<const soda::Manifest> manifest;
    try {
        manifest = std::make_shared<soda::Manifest>(soda::load_manifest(filename.toStdString()));
    } catch (const std::exception& e) {
        reportError("Could not load the manifest", e.what(), reportErrors);
        return false;
    }

    const QSignalBlocker blocker(controls_.tree);
    controls_.tree->clear();
    items_.clear();
    viewport_->clearParts();
    viewport_->setSelection(std::nullopt);
    viewport_->setWorld(manifest->bounds);
    for (const auto& floor : manifest->floors) {
        const auto floorId = "floor-" + std::to_string(floor.number);
        auto* floorItem = new QTreeWidgetItem(QStringList{QString::fromStdString(floor.name),
                                                          QString::number(floor.shell.triangles)});
        floorItem->setFlags(floorItem->flags() | Qt::ItemIsUserCheckable);
        floorItem->setCheckState(0, Qt::Unchecked);
        floorItem->setData(0, kIdRole, QString::fromStdString(floorId));
        floorItem->setData(0, kFloorRole, floor.number);
        floorItem->setToolTip(0, "Walls of the whole floor, without furniture");
        controls_.tree->addTopLevelItem(floorItem);
        items_.push_back(floorItem);
        viewport_->addPart(partKey(floorId, "shell").toStdString(), PartKind::floor_shell,
                           floor.shell.path, floor.shell.bounds);
        for (const auto& room : floor.rooms) {
            auto* roomItem = new QTreeWidgetItem(
                QStringList{QString::fromStdString(room.name),
                            QString::number(room.shell.triangles + room.furniture.triangles)});
            roomItem->setFlags(roomItem->flags() | Qt::ItemIsUserCheckable);
            roomItem->setCheckState(0, Qt::Unchecked);
            roomItem->setData(0, kIdRole, QString::fromStdString(room.id));
            roomItem->setData(0, kFloorRole, room.floor);
            floorItem->addChild(roomItem);
            items_.push_back(roomItem);
            viewport_->addPart(partKey(room.id, "shell").toStdString(), PartKind::room_shell,
                               room.shell.path, room.shell.bounds);
            viewport_->addPart(partKey(room.id, "furniture").toStdString(), PartKind::furniture,
                               room.furniture.path, room.furniture.bounds);
        }
    }
    manifest_ = std::move(manifest);
    manifestName_ = filename;
    controls_.info->setText(QString("%1: %2 floors\n%3")
                                .arg(QString::fromStdString(manifest_->name))
                                .arg(manifest_->floors.size())
                                .arg(QString::fromStdString(manifest_->description)));
    viewport_->frameVisible();
    refreshStatus();
    return true;
}

QTreeWidgetItem* MainWindow::findItem(const std::string& id) const {
    for (auto* item : items_) {
        if (item->data(0, kIdRole).toString().toStdString() == id) {
            return item;
        }
    }
    return nullptr;
}

std::optional<int> MainWindow::currentFloor() const {
    const auto* item = controls_.tree->currentItem();
    if (!item) {
        return std::nullopt;
    }
    return item->data(0, kFloorRole).toInt();
}

void MainWindow::applyItem(QTreeWidgetItem* item, bool reportErrors) {
    // Floor rows own one part, room rows two. A read failure unchecks the row again.
    const auto id = item->data(0, kIdRole).toString().toStdString();
    const bool visible = item->checkState(0) == Qt::Checked;
    const bool isFloor = item->parent() == nullptr;
    try {
        viewport_->setPartVisible(partKey(id, "shell").toStdString(), visible);
        if (!isFloor) {
            viewport_->setPartVisible(partKey(id, "furniture").toStdString(), visible);
        }
    } catch (const std::exception& e) {
        const QSignalBlocker blocker(controls_.tree);
        item->setCheckState(0, Qt::Unchecked);
        viewport_->setPartVisible(partKey(id, "shell").toStdString(), false);
        if (!isFloor) {
            viewport_->setPartVisible(partKey(id, "furniture").toStdString(), false);
        }
        reportError("Could not read a mesh", e.what(), reportErrors);
    }
}

bool MainWindow::showPart(const std::string& id, bool visible, bool reportErrors) {
    auto* item = findItem(id);
    if (!item) {
        reportError("Unknown floor or room", QString::fromStdString(id), reportErrors);
        return false;
    }
    const QSignalBlocker blocker(controls_.tree);
    item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
    applyItem(item, reportErrors);
    refreshStatus();
    return item->checkState(0) == (visible ? Qt::Checked : Qt::Unchecked);
}

void MainWindow::showBuilding() {
    const QSignalBlocker blocker(controls_.tree);
    for (auto* item : items_) {
        const bool isFloor = item->parent() == nullptr;
        item->setCheckState(0, isFloor ? Qt::Checked : Qt::Unchecked);
        applyItem(item, true);
    }
    refreshStatus();
}

void MainWindow::showFloorRooms(int floor, bool visible) {
    const QSignalBlocker blocker(controls_.tree);
    for (auto* item : items_) {
        if (item->parent() != nullptr && item->data(0, kFloorRole).toInt() == floor) {
            item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
            applyItem(item, true);
        }
    }
    if (auto* floorItem = findItem("floor-" + std::to_string(floor))) {
        floorItem->setExpanded(true);
    }
    refreshStatus();
}

void MainWindow::hideAll() {
    const QSignalBlocker blocker(controls_.tree);
    for (auto* item : items_) {
        item->setCheckState(0, Qt::Unchecked);
        applyItem(item, true);
    }
    refreshStatus();
}

void MainWindow::selectItem(QTreeWidgetItem* item) {
    if (!item || !manifest_) {
        viewport_->setSelection(std::nullopt);
        return;
    }
    const auto id = item->data(0, kIdRole).toString().toStdString();
    if (const auto* room = manifest_->find_room(id)) {
        viewport_->setSelection(room->bounds());
        controls_.info->setText(roomDescription(*room));
    } else if (const auto* floor = manifest_->find_floor(item->data(0, kFloorRole).toInt())) {
        viewport_->setSelection(floor->bounds);
        controls_.info->setText(floorDescription(*floor));
    }
}

void MainWindow::refreshStatus() {
    if (!manifest_) {
        statusBar()->showMessage("No dataset loaded.");
        return;
    }
    statusBar()->showMessage(
        QString("%1 — %2 triangles visible").arg(manifestName_).arg(viewport_->visibleTriangles()));
}
