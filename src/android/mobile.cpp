#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "filedialog.h"
#include "log.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QJniEnvironment>
#include <QJniObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollBar>
#include <QTouchEvent>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QtConcurrent>
#include <functional>
#include <cmath>

namespace {
QString projectLibrary() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/projects";
    QDir().mkpath(path);
    return path;
}

void collectProjects(const QString &path, QStringList &roots, int depth = 0) {
    QDir dir(path);
    if (dir.exists("data/layouts/layouts.json") && dir.exists("data/maps/map_groups.json")) {
        roots.append(path);
        return;
    }
    if (depth >= 3) return;
    for (const QString &child : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        collectProjects(dir.filePath(child), roots, depth + 1);
}

QString invokeArchive(const char *method, const char *signature, const QString &first, const QString &second) {
    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    const QJniObject a = QJniObject::fromString(first);
    const QJniObject b = QJniObject::fromString(second);
    const QJniObject result = QJniObject::callStaticObjectMethod(
        "org/codemrsoy/porymap/AndroidFiles", method, signature,
        context.object<jobject>(), a.object<jstring>(), b.object<jstring>());
    QJniEnvironment env;
    if (env.checkAndClearExceptions() || !result.isValid())
        return QStringLiteral("{\"error\":\"Android file service failed; see the application log.\"}");
    return result.toString();
}

void archiveOperation(QWidget *parent, const QString &title, std::function<QString()> work,
                      std::function<void(const QJsonObject &)> done) {
    auto *progress = new QProgressDialog(title, QString(), 0, 0, parent);
    progress->setWindowModality(Qt::ApplicationModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->show();
    auto *watcher = new QFutureWatcher<QString>(parent);
    QObject::connect(watcher, &QFutureWatcher<QString>::finished, parent, [=] {
        const QJsonDocument result = QJsonDocument::fromJson(watcher->result().toUtf8());
        progress->close(); progress->deleteLater(); watcher->deleteLater();
        if (!result.isObject() || result.object().contains("error")) {
            QMessageBox::critical(parent, title, result.isObject()
                ? result.object().value("error").toString() : QStringLiteral("Invalid Android file-service response."));
            return;
        }
        done(result.object());
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}
}

void MainWindow::showAndroidPanel(int panel) {
    // One full-width workspace at a time: a desktop three-column layout does
    // not fit a phone. The native models and editor widgets remain authoritative.
    ui->mapListContainer->setVisible(panel == 1);
    ui->frame_Main->setVisible(panel != 1);
    ui->stackedWidget_MapEvents->setVisible(panel == 2);
    ui->frame_9->setVisible(panel == 0);
    setProperty("androidPanel", panel);
}

void MainWindow::initAndroidUi() {
    setMinimumSize(0, 0);
    // Desktop tabs contain wide hidden pages. Their minimum size hints must
    // not grow the Android window beyond the actual phone surface.
    layout()->setSizeConstraint(QLayout::SetNoConstraint);
    ui->centralWidget->layout()->setSizeConstraint(QLayout::SetNoConstraint);
    for (QWidget *panel : QList<QWidget *>{ui->mapListContainer, ui->mapViewTab,
             ui->stackedWidget_MapEvents, ui->frame_9, ui->frame_Main}) {
        panel->setMinimumSize(0, 0);
        auto policy = panel->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        panel->setSizePolicy(policy);
    }
    ui->centralWidget->layout()->setContentsMargins(0, 0, 0, 0);
    ui->mainTabBar->hide();
    ui->frame_mapTools->hide();
    menuBar()->hide();
    setStyleSheet(styleSheet() + QStringLiteral(
        "QToolButton { min-width: 40px; min-height: 42px; padding: 3px; }"
        "QPushButton { min-height: 38px; }"
        "QLineEdit, QComboBox, QSpinBox { min-height: 34px; }"
        "QTabBar::tab { min-height: 36px; }"
        "QTreeView::item { min-height: 36px; }"));
    ui->mapListContainer->setMinimumWidth(0);
    ui->mapViewTab->setMinimumWidth(0);
    ui->stackedWidget_MapEvents->setMinimumWidth(0);
    ui->frame_9->setMinimumWidth(0);
    ui->frame_Main->setMinimumWidth(0);
    ui->frame_Tilesets->hide(); // Avoid accidental tileset reassignment while painting.
    ui->graphicsView_Map->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    ui->graphicsView_Map->setResizeAnchor(QGraphicsView::AnchorViewCenter);

    auto *navigation = addToolBar(QStringLiteral("Phone project tools"));
    navigation->setObjectName("androidNavigation");
    navigation->setMovable(false);
    navigation->setToolButtonStyle(Qt::ToolButtonTextOnly);
    navigation->setStyleSheet(QStringLiteral(
        "QToolBar { background: #f4f6f8; color: #17212b; }"
        "QToolButton { color: #17212b; min-height: 42px; padding: 3px; }"
        "QToolButton:disabled { color: #6b7280; }"
        "QMenu { background: white; color: #17212b; }"
        "QMenu::item:selected { background: #1565c0; color: white; }"));
    auto *importButton = new QToolButton(navigation);
    importButton->setObjectName("androidImportButton");
    importButton->setText(tr("Import ZIP"));
    importButton->setAccessibleName(tr("Import project ZIP"));
    connect(importButton, &QToolButton::clicked, this, &MainWindow::importAndroidProject);
    navigation->addWidget(importButton);
    auto *projectButton = new QToolButton(navigation);
    projectButton->setObjectName("androidProjectButton");
    projectButton->setText(tr("Project"));
    projectButton->setAccessibleName(tr("Project menu"));
    projectButton->setPopupMode(QToolButton::InstantPopup);
    auto *projectMenu = new QMenu(projectButton);
    projectMenu->addAction(tr("Import project ZIP…"), this, &MainWindow::importAndroidProject);
    projectMenu->addAction(tr("Open imported project…"), this, &MainWindow::chooseAndroidProject);
    projectMenu->addAction(tr("Export project ZIP…"), this, &MainWindow::exportAndroidProject);
    projectMenu->addSeparator();
    projectMenu->addMenu(ui->menuFile);
    projectMenu->addMenu(ui->menuEdit);
    projectMenu->addMenu(ui->menuOptions);
    projectMenu->addMenu(ui->menuHelp);
    projectButton->setMenu(projectMenu);
    navigation->addWidget(projectButton);
    auto *maps = navigation->addAction(tr("Maps"));
    connect(maps, &QAction::triggered, this, [this] {
        showAndroidPanel(property("androidPanel").toInt() == 1 ? 0 : 1);
    });
    auto *tiles = navigation->addAction(tr("Palette"));
    connect(tiles, &QAction::triggered, this, [this] {
        showAndroidPanel(property("androidPanel").toInt() == 2 ? 0 : 2);
    });
    ui->action_Save_Project->setText(tr("Save"));
    navigation->addAction(ui->action_Save_Project);
    navigation->addAction(undoAction);
    navigation->addAction(redoAction);
    addToolBarBreak();
    auto *tools = addToolBar(QStringLiteral("Phone map tools"));
    tools->setObjectName("androidMapTools");
    tools->setMovable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *actions = new QActionGroup(this);
    actions->setExclusive(true);
    for (const auto &entry : QList<QPair<QString, Editor::EditAction>>{
             {tr("Paint"), Editor::EditAction::Paint}, {tr("Pan"), Editor::EditAction::Move},
             {tr("Fill"), Editor::EditAction::Fill}, {tr("Pick"), Editor::EditAction::Pick}}) {
        auto *action = tools->addAction(entry.first);
        action->setCheckable(true); actions->addAction(action);
        action->setChecked(entry.second == Editor::EditAction::Paint);
        connect(action, &QAction::triggered, this, [this, mode = entry.second] {
            editor->setEditAction(mode); showAndroidPanel(0);
        });
    }
    auto *collision = tools->addAction(tr("Collision"));
    collision->setCheckable(true);
    connect(collision, &QAction::toggled, this, [this](bool checked) {
        ui->mainTabBar->setCurrentIndex(MainTab::Map);
        on_mainTabBar_tabBarClicked(MainTab::Map);
        ui->mapViewTab->setCurrentIndex(checked ? MapViewTab::Collision : MapViewTab::Metatiles);
        on_mapViewTab_tabBarClicked(ui->mapViewTab->currentIndex());
        showAndroidPanel(2);
    });
    tools->addAction(ui->actionZoom_Out);
    tools->addAction(ui->actionZoom_In);
    if (!isProjectOpen()) {
        tools->setEnabled(false);
        objectsDisabled.insert(tools);
    }
    ui->horizontalSlider_MetatileZoom->setValue(60);
    ui->horizontalSlider_CollisionZoom->setValue(60);
    connect(this, &MainWindow::mapOpened, this, [this] { showAndroidPanel(0); });
    connect(this, &MainWindow::layoutOpened, this, [this] { showAndroidPanel(0); });
    for (QTreeView *tree : {ui->mapList, ui->locationList, ui->layoutList})
        connect(tree, &QTreeView::clicked, this, &MainWindow::openMapListItem);
    // A backgrounded Android process may be reclaimed without closeEvent().
    // Persist only this app's imported copy, retaining the native undo stack.
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && isProjectOpen() && editor->project->hasUnsavedChanges()) {
            if (!editor->saveAll()) logError("Android background save failed");
            updateWindowTitle();
        }
    });
    porymapConfig.shownInGameReloadMessage = true;
    showAndroidPanel(isProjectOpen() ? 0 : 1);
    resize(QGuiApplication::primaryScreen()->availableGeometry().size());
    move(QGuiApplication::primaryScreen()->availableGeometry().topLeft());
#ifdef PORYMAP_ANDROID_TESTS
    QTimer::singleShot(1500, this, &MainWindow::runAndroidSmokeTest);
#endif
}

void MainWindow::chooseAndroidProject() {
    QStringList roots; collectProjects(projectLibrary(), roots);
    if (roots.isEmpty()) { importAndroidProject(); return; }
    QStringList labels;
    for (const QString &root : roots) {
        const QString relative = QDir(projectLibrary()).relativeFilePath(root);
        labels.append(QFileInfo(root).fileName() + " — " + relative.section('/', 0, 0));
    }
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Open project"), tr("Imported projects"), labels, 0, false, &ok);
    if (ok && !selected.isEmpty()) openProject(roots.at(labels.indexOf(selected)));
}

void MainWindow::importAndroidProject() {
    const QString uri = QFileDialog::getOpenFileName(this, tr("Import project ZIP"), QString(), tr("ZIP archives (*.zip)"));
    if (uri.isEmpty()) return;
    const QString folder = projectLibrary() + "/" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss")
        + "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    archiveOperation(this, tr("Importing project…"), [=] {
        return invokeArchive("importProject", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", uri, folder);
    }, [this](const QJsonObject &result) {
        const QString root = result.value("root").toString();
        if (openProject(root)) emit androidProjectImported(root);
    });
}

void MainWindow::exportAndroidProject() {
    if (!isProjectOpen()) {
        QMessageBox::information(this, tr("Export project"), tr("Open an imported project first."));
        return;
    }
    if (!save(false)) return;
    const QString root = editor->project->root;
    const QString uri = QFileDialog::getSaveFileName(this, tr("Export project ZIP"), "porymap-project.zip", tr("ZIP archives (*.zip)"));
    if (uri.isEmpty()) return;
    archiveOperation(this, tr("Exporting project…"), [=] {
        return invokeArchive("exportProject", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", root, uri);
    }, [this](const QJsonObject &) {
        QMessageBox::information(this, tr("Project exported"), tr("Your saved project ZIP is ready to copy back to desktop Porymap."));
    });
}

bool MapView::viewportEvent(QEvent *event) {
    // Pinch belongs to explicit Pan mode, so placing a second finger can never
    // start a paint stroke. Paint/Pick/Fill retain Qt's single-touch mouse path.
    const auto type = event->type();
    if (editor && editor->getEditAction() == Editor::EditAction::Move &&
        (type == QEvent::TouchBegin || type == QEvent::TouchUpdate ||
         type == QEvent::TouchEnd || type == QEvent::TouchCancel)) {
        auto *touch = static_cast<QTouchEvent *>(event);
        const auto points = touch->points();
        if (type == QEvent::TouchEnd || type == QEvent::TouchCancel || points.isEmpty()) {
            androidPinchDistance = 0; event->accept(); return true;
        }
        QPointF center = points.first().position();
        qreal distance = 0;
        if (points.size() > 1) {
            center = (center + points.at(1).position()) / 2;
            distance = QLineF(points.first().position(), points.at(1).position()).length();
        }
        if (type == QEvent::TouchUpdate) {
            const QPoint delta = (center - androidPanPosition).toPoint();
            // Do not jump when a second finger first changes the gesture center.
            if ((distance > 0) == (androidPinchDistance > 0)) {
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
                verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            }
            if (distance > 0 && androidPinchDistance > 0) {
                const qreal ratio = distance / androidPinchDistance;
                if (ratio > 1.22 || ratio < 0.82) {
                    editor->scaleMapView(ratio > 1 ? 1 : -1);
                } else distance = androidPinchDistance; // Accumulate small movements.
            }
        }
        androidPanPosition = center; androidPinchDistance = distance;
        event->accept(); return true;
    }
    return GraphicsView::viewportEvent(event);
}
