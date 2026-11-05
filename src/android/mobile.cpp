#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "filedialog.h"
#include "log.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QEventLoop>
#include <QUrl>
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
    if (auto *button = findChild<QToolButton *>(QString("androidWorkspace%1").arg(panel)))
        if (button->defaultAction()) button->defaultAction()->setChecked(true);
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

    // Three stable header controls; the title yields space on narrow phones.
    auto *navigation = addToolBar(tr("Project"));
    navigation->setObjectName("androidNavigation");
    navigation->setMovable(false);
    navigation->setFloatable(false);
    navigation->setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; background: #f4f6f8; spacing: 4px; padding: 4px; }"
        "QToolButton { color: #17212b; background: #e7ecf2; border-radius: 10px; min-height: 44px; padding: 2px 6px; }"
        "QToolButton:disabled { color: #6b7280; }"
        "QMenu { background: white; color: #17212b; }"
        "QMenu::item { min-height: 40px; padding: 4px 16px; }"
        "QMenu::item:selected { background: #1565c0; color: white; }"));
    auto *open = new QToolButton(navigation);
    open->setObjectName("androidOpenFolderButton");
    open->setText(tr("Open")); open->setAccessibleName(tr("Open project folder"));
    connect(open, &QToolButton::clicked, this, &MainWindow::openAndroidFolder);
    navigation->addWidget(open);
    auto *title = new QLabel(tr("Porymap"), navigation);
    title->setObjectName("androidMapTitle");
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setStyleSheet("font-weight: bold; padding: 4px;");
    navigation->addWidget(title);
    ui->action_Save_Project->setText(tr("Save"));
    navigation->addAction(ui->action_Save_Project);
    auto *projectButton = new QToolButton(navigation);
    projectButton->setObjectName("androidProjectButton");
    projectButton->setText(tr("More"));
    projectButton->setAccessibleName(tr("Project menu"));
    projectButton->setPopupMode(QToolButton::InstantPopup);
    auto *projectMenu = new QMenu(projectButton);
    projectMenu->addAction(tr("Open project folder…"), this, &MainWindow::openAndroidFolder);
    projectMenu->addAction(tr("Recent working copies…"), this, &MainWindow::chooseAndroidProject);
    projectMenu->addAction(tr("Import ZIP…"), this, &MainWindow::importAndroidProject)->setObjectName("androidImportAction");
    projectMenu->addAction(tr("Export ZIP…"), this, &MainWindow::exportAndroidProject);
    projectMenu->addSeparator();
    projectMenu->addAction(undoAction); projectMenu->addAction(redoAction);
    projectMenu->addSeparator();
    projectMenu->addMenu(ui->menuFile);
    projectMenu->addMenu(ui->menuEdit);
    projectMenu->addMenu(ui->menuOptions);
    projectMenu->addMenu(ui->menuHelp);
    projectButton->setMenu(projectMenu);
    navigation->addWidget(projectButton);

    // A full-width bottom navigation row never overflows into Qt's tiny extension menu.
    auto *bottom = new QToolBar(tr("Workspace"), this);
    bottom->setObjectName("androidWorkspace");
    bottom->setMovable(false); bottom->setFloatable(false);
    addToolBar(Qt::BottomToolBarArea, bottom);
    auto *row = new QWidget(bottom);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(4, 4, 4, 4); rowLayout->setSpacing(4);
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *panels = new QActionGroup(this); panels->setExclusive(true);
    const QStringList labels{tr("Map"), tr("Maps"), tr("Tiles"), tr("Tools")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QToolButton(row);
        button->setObjectName(QString("androidWorkspace%1").arg(i));
        button->setText(labels[i]); button->setAccessibleName(labels[i]);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        button->setMinimumSize(48, 48);
        button->setStyleSheet("QToolButton { color: #17212b; border: 0; border-radius: 10px; padding: 4px; }"
                             "QToolButton:checked { background: #d8e9ff; color: #084b9b; font-weight: bold; }");
        rowLayout->addWidget(button, 1);
        if (i < 3) {
            auto *action = new QAction(labels[i], panels);
            action->setCheckable(true); action->setChecked(i == 0);
            button->setDefaultAction(action);
            connect(action, &QAction::triggered, this, [this, i] { showAndroidPanel(i); });
        } else {
            connect(button, &QToolButton::clicked, this, [this] {
                if (!isProjectOpen()) return;
                QDialog dialog(this); dialog.setWindowTitle(tr("Map tools"));
                auto *grid = new QGridLayout(&dialog);
                grid->setContentsMargins(16, 16, 16, 16); grid->setSpacing(10);
                const QRect available = screen()->availableGeometry();
                const int columns = available.width() > available.height() ? 4 : 2;
                const QList<QPair<QString, Editor::EditAction>> modes{
                    {tr("Paint"), Editor::EditAction::Paint}, {tr("Pan / pinch"), Editor::EditAction::Move},
                    {tr("Fill"), Editor::EditAction::Fill}, {tr("Pick tile"), Editor::EditAction::Pick}};
                for (int j = 0; j < modes.size(); ++j) {
                    auto *tool = new QPushButton(modes[j].first, &dialog);
                    tool->setMinimumSize(112, 52); tool->setCheckable(true);
                    tool->setChecked(editor->getEditAction() == modes[j].second);
                    grid->addWidget(tool, j / columns, j % columns);
                    connect(tool, &QPushButton::clicked, &dialog, [&, mode = modes[j].second] {
                        editor->setEditAction(mode); showAndroidPanel(0); dialog.accept();
                    });
                }
                auto *collision = new QPushButton(tr("Collision"), &dialog);
                collision->setMinimumHeight(48); collision->setCheckable(true);
                collision->setChecked(ui->mapViewTab->currentIndex() == MapViewTab::Collision);
                const int collisionRow = (modes.size() + columns - 1) / columns;
                grid->addWidget(collision, collisionRow, 0, 1, columns);
                connect(collision, &QPushButton::clicked, &dialog, [&](bool checked) {
                    ui->mainTabBar->setCurrentIndex(MainTab::Map);
                    on_mainTabBar_tabBarClicked(MainTab::Map);
                    ui->mapViewTab->setCurrentIndex(checked ? MapViewTab::Collision : MapViewTab::Metatiles);
                    on_mapViewTab_tabBarClicked(ui->mapViewTab->currentIndex());
                    showAndroidPanel(2); dialog.accept();
                });
                const QList<QAction *> commands{undoAction, redoAction, ui->actionZoom_Out, ui->actionZoom_In};
                const QStringList names{tr("Undo"), tr("Redo"), tr("Zoom −"), tr("Zoom +")};
                for (int j = 0; j < commands.size(); ++j) {
                    auto *button = new QPushButton(names[j], &dialog);
                    button->setMinimumHeight(48); button->setEnabled(commands[j]->isEnabled());
                    grid->addWidget(button, collisionRow + 1 + j / columns, j % columns);
                    connect(button, &QPushButton::clicked, &dialog, [&, action = commands[j]] { action->trigger(); dialog.accept(); });
                }
                auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
                connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                grid->addWidget(close, collisionRow + 1 + (commands.size() + columns - 1) / columns, 0, 1, columns);
                dialog.exec();
            });
        }
    }
    bottom->addWidget(row);
    connect(this, &MainWindow::mapOpened, title, [this, title] {
        title->setText(editor->map ? editor->map->name() : tr("Porymap"));
    });
    // Hide desktop tab strips, status bar and nested layout margins around the canvas.
    ui->mapViewTab->tabBar()->hide();
    ui->horizontalSpacer_20->changeSize(0, 0, QSizePolicy::Ignored, QSizePolicy::Ignored);
    statusBar()->hide();
    for (auto *frame : {ui->frame_9, ui->frame_Main})
        if (frame->layout()) { frame->layout()->setContentsMargins(0, 0, 0, 0); frame->layout()->setSpacing(0); }
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
    if (roots.isEmpty()) { openAndroidFolder(); return; }
    QStringList labels;
    for (const QString &root : roots) {
        const QString relative = QDir(projectLibrary()).relativeFilePath(root);
        labels.append(QFileInfo(root).fileName() + " — " + relative.section('/', 0, 0));
    }
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Open project"), tr("Working copies (Save writes linked copies back to their folder)"), labels, 0, false, &ok);
    if (ok && !selected.isEmpty()) openProject(roots.at(labels.indexOf(selected)));
}

void MainWindow::openAndroidFolder() {
    const QUrl uri = QFileDialog::getExistingDirectoryUrl(this, tr("Select project root folder"));
    if (uri.isEmpty()) return;
    const QString folder = projectLibrary() + "/folder-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    archiveOperation(this, tr("Opening project folder…"), [=] {
        return invokeArchive("openFolder", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", uri.toString(), folder);
    }, [this](const QJsonObject &result) {
        const QString root = result.value("root").toString();
        if (openProject(root)) {
            statusBar()->showMessage(tr("Folder linked. Save writes changes back to the selected folder."), 8000);
            emit androidProjectImported(root);
        }
    });
}

bool MainWindow::syncAndroidFolder() {
    if (!isProjectOpen()) return true;
    const QString root = editor->project->root;
    if (!QFileInfo::exists(QFileInfo(root).absolutePath() + "/folder-link.properties")) return true;
    // Save/close callers need a truthful success result; keep the GUI responsive
    // while SAF performs disk I/O, and prevent another edit/save during the operation.
    QProgressDialog progress(tr("Saving to project folder…"), QString(), 0, 0, this);
    progress.setWindowModality(Qt::ApplicationModal); progress.setMinimumDuration(0);
    QFutureWatcher<QString> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<QString>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([root] {
        return invokeArchive("saveFolder", "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", root, QString());
    }));
    progress.show();
    if (!watcher.isFinished()) loop.exec();
    progress.close();
    const auto result = QJsonDocument::fromJson(watcher.result().toUtf8()).object();
    if (result.isEmpty() || result.contains("error")) {
        QMessageBox::critical(this, tr("Folder save failed"), result.value("error").toString(tr("Android file service failed. Your working copy is still on this device.")));
        return false;
    }
    return true;
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
    if (!editor->saveAll()) return;
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
