#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "filedialog.h"
#include "log.h"
#include "androidmapgestures.h"

#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QVBoxLayout>
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
#include <QLineEdit>
#include <QLayout>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QScreen>
#include <QStandardPaths>
#include <QStatusBar>
#include <QScrollBar>
#include <QScroller>
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
    if (panel == 3 && (!isProjectOpen() || !editor->map || !ui->mainTabBar->isTabEnabled(MainTab::Connections))) panel = 0;
    if (isProjectOpen()) on_mainTabBar_tabBarClicked(panel == 3 ? MainTab::Connections : MainTab::Map);
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
    setStyleSheet(styleSheet() + QStringLiteral(
        "QMainWindow, QDialog { background: #202a33; color: #e4edf3; }"
        "QToolBar, QFrame { border: 0; }"
        "QTabWidget::pane { border: 0; }"
        "QTabBar::tab { background: #26333f; color: #aebfc9; border: 0; padding: 4px 10px; }"
        "QTabBar::tab:selected { background: #345650; color: #c8f4e5; }"
        "QLineEdit, QComboBox, QSpinBox { background: #18222b; color: #e4edf3; border: 1px solid #465c69; border-radius: 8px; padding: 4px; }"
        "QPushButton { background: #2c3a46; color: #e4edf3; border: 1px solid #465c69; border-radius: 10px; padding: 4px 10px; }"
        "QPushButton:checked, QPushButton:pressed { background: #345650; border-color: #83cdb6; }"
        "QPushButton:disabled { color: #7d919d; }"
        "QGraphicsView { border: 0; background: #17232b; }"
        "QScrollBar { background: #202a33; border: 0; }"
        "QScrollBar::handle { background: #526976; border-radius: 3px; min-height: 24px; min-width: 24px; }"));
    ui->mapListContainer->setMinimumWidth(0);
    ui->mapViewTab->setMinimumWidth(0);
    ui->stackedWidget_MapEvents->setMinimumWidth(0);
    ui->frame_9->setMinimumWidth(0);
    ui->frame_Main->setMinimumWidth(0);
    ui->frame_Tilesets->hide(); // Avoid accidental tileset reassignment while painting.
    for (auto corner : {Qt::TopLeftCorner, Qt::TopRightCorner})
        if (auto *widget = ui->mapListContainer->cornerWidget(corner)) widget->hide();
    ui->mapListContainer->tabBar()->setExpanding(true);
    for (auto *bar : {ui->mapListToolBar_Groups, ui->mapListToolBar_Locations, ui->mapListToolBar_Layouts}) {
        for (auto *button : bar->findChildren<QToolButton *>()) button->hide();
        if (auto *search = bar->findChild<QLineEdit *>()) {
            search->setPlaceholderText(tr("Search maps…"));
            search->setMinimumHeight(44); search->setClearButtonEnabled(true);
        }
    }
    for (auto *tree : {ui->mapList, ui->locationList, ui->layoutList}) {
        tree->setStyleSheet("QTreeView::item { min-height: 48px; }");
        QScroller::grabGesture(tree->viewport(), QScroller::TouchGesture);
    }
    ui->graphicsView_Map->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    ui->graphicsView_Map->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    for (auto *view : QList<QGraphicsView *>{ui->graphicsView_Map, ui->graphicsView_Connections}) {
        view->setFrameShape(QFrame::NoFrame);
        view->setBackgroundBrush(QColor("#17232b"));
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setTransformationAnchor(QGraphicsView::NoAnchor);
        view->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    }
    installAndroidMapGestures(ui->graphicsView_Map, editor, true);
    installAndroidMapGestures(ui->graphicsView_Connections, editor, false);
    ui->frame_ConnectionsTopBar->hide();
    ui->frame_EditConnectionsPanel->hide();
    ui->frame_EditConnectionsPanel->setMinimumWidth(0);
    ui->frame_Connections->layout()->setContentsMargins(0, 0, 0, 0);
    auto *connectionBar = new QWidget(ui->frame_Connections);
    auto *connectionLayout = new QHBoxLayout(connectionBar);
    connectionLayout->setContentsMargins(8, 4, 8, 4);
    auto *connectionSettings = new QPushButton(tr("Connections…"), connectionBar);
    connectionSettings->setObjectName("androidConnectionSettings");
    connect(connectionSettings, &QPushButton::clicked, this, &MainWindow::showAndroidConnections);
    connectionLayout->addWidget(connectionSettings);
    auto *connectionHint = new QLabel(tr("Two fingers to move · pinch to zoom"), connectionBar);
    connectionHint->setWordWrap(true); connectionLayout->addWidget(connectionHint, 1);
    qobject_cast<QVBoxLayout *>(ui->frame_Connections->layout())->insertWidget(0, connectionBar);

    // Three stable header controls; the title yields space on narrow phones.
    auto *navigation = addToolBar(tr("Project"));
    navigation->setObjectName("androidNavigation");
    navigation->setMovable(false);
    navigation->setFloatable(false);
    navigation->setStyleSheet(QStringLiteral(
        "QToolBar { border: 0; background: #202a33; spacing: 4px; padding: 4px; }"
        "QToolButton { color: #e4edf3; background: #2c3a46; border-radius: 10px; min-height: 44px; padding: 2px 6px; }"
        "QToolButton:disabled { color: #7d919d; }"
        "QMenu { background: #26333f; color: #e4edf3; }"
        "QMenu::item { min-height: 40px; padding: 4px 16px; }"
        "QMenu::item:selected { background: #83cdb6; color: #132e28; }"));
    auto *open = new QToolButton(navigation);
    open->setObjectName("androidOpenFolderButton");
    open->setText(tr("Open")); open->setAccessibleName(tr("Open project folder"));
    connect(open, &QToolButton::clicked, this, &MainWindow::openAndroidFolder);
    navigation->addWidget(open);
    auto *title = new QLabel(tr("Porymap"), navigation);
    title->setObjectName("androidMapTitle");
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
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
    projectMenu->addAction(tr("Map tools…"), this, &MainWindow::showAndroidTools)->setObjectName("androidToolsAction");
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
    const QStringList labels{tr("Map"), tr("Maps"), tr("Tiles"), tr("Edges")};
    for (int i = 0; i < labels.size(); ++i) {
        auto *button = new QToolButton(row);
        button->setObjectName(QString("androidWorkspace%1").arg(i));
        button->setText(labels[i]); button->setAccessibleName(labels[i]);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        button->setMinimumSize(48, 48);
        button->setStyleSheet("QToolButton { color: #e4edf3; border: 0; border-radius: 10px; padding: 4px; }"
                             "QToolButton:checked { background: #345650; color: #c8f4e5; font-weight: bold; }");
        rowLayout->addWidget(button, 1);
        if (i < 3) {
            auto *action = new QAction(labels[i], panels);
            action->setCheckable(true); action->setChecked(i == 0);
            button->setDefaultAction(action);
            connect(action, &QAction::triggered, this, [this, i] { showAndroidPanel(i); });
        } else {
            auto *action = new QAction(tr("Edges"), panels);
            action->setCheckable(true); button->setDefaultAction(action);
            connect(action, &QAction::triggered, this, [this] { showAndroidPanel(3); });
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

void MainWindow::showAndroidTools() {
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
}

void MainWindow::showAndroidConnections() {
    if (!isProjectOpen() || !editor->map) return;
    QDialog dialog(this); dialog.setWindowTitle(tr("Map connections"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *mirror = new QCheckBox(tr("Mirror opposite connections"), &dialog);
    mirror->setChecked(ui->checkBox_MirrorConnections->isChecked());
    connect(mirror, &QCheckBox::toggled, ui->checkBox_MirrorConnections, &QCheckBox::setChecked);
    layout->addWidget(mirror);
    auto *panel = ui->frame_EditConnectionsPanel;
    panel->setParent(&dialog); layout->addWidget(panel, 1); panel->show();
    ui->pushButton_AddConnection->setText(tr("Add connection"));
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(close);
    const QSize available = screen()->availableGeometry().size();
    dialog.resize(qMin(420, available.width() - 24), qMin(600, available.height() - 24));
    dialog.exec();
    layout->removeWidget(panel);
    panel->setParent(ui->splitter_Connections); ui->splitter_Connections->insertWidget(1, panel); panel->hide();
}

void MainWindow::chooseAndroidProject() {
    QStringList roots; collectProjects(projectLibrary(), roots);
    if (roots.isEmpty()) { openAndroidFolder(); return; }
    QStringList labels;
    for (const QString &root : roots) {
        const QString relative = QDir(projectLibrary()).relativeFilePath(root);
        labels.append(QFileInfo(root).fileName() + " — " + relative.section('/', 0, 0).left(16));
    }
    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Open project"), tr("Projects saved on this device"), labels, 0, false, &ok);
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
