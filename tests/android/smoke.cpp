#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "metatileselector.h"
#include "movementpermissionsselector.h"
#include <QApplication>
#include <QDir>
#include <QDialog>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QStandardPaths>
#include <QScreen>
#include <QToolButton>
#include <QTimer>
#include <QTouchEvent>
#include <QScrollBar>
#include <QLineF>
#include <QtTest>
#include <stdexcept>
#include <cmath>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void requirePhoneWindowFits(MainWindow *window) {
    QTest::qWait(100); // Apply pending layout/resize requests after project load.
    const QRect available = window->screen()->availableGeometry();
    require(window->width() <= available.width() && window->height() <= available.height(),
            "Editor window exceeds phone screen bounds");
    auto *navigation = window->findChild<QWidget *>("androidNavigation");
    require(navigation && navigation->isVisible(), "Project navigation is hidden");
    const QRect navRect(navigation->mapToGlobal(QPoint()), navigation->size());
    require(available.contains(navRect), "Project navigation is outside the phone screen");
}
QByteArray read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read fixture file");
    return file.readAll();
}
QJsonObject archive(const char *method, const QString &first, const QString &second) {
    auto context = QNativeInterface::QAndroidApplication::context();
    auto a = QJniObject::fromString(first), b = QJniObject::fromString(second);
    auto reply = QJniObject::callStaticObjectMethod(
        "org/codemrsoy/porymap/AndroidFiles", method,
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        context.object<jobject>(), a.object<jstring>(), b.object<jstring>());
    require(reply.isValid(), "Android archive JNI returned no result");
    auto result = QJsonDocument::fromJson(reply.toString().toUtf8()).object();
    if (result.contains("error")) throw std::runtime_error(result.value("error").toString().toStdString());
    return result;
}
}

void MainWindow::runAndroidSmokeTest() {
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QFile request(data + "/android-smoke.request");
    if (!request.open(QIODevice::ReadOnly)) return; // Normal app launch.
    const QString requested = QString::fromUtf8(request.readAll()).trimmed();
    request.close();
    QJsonObject result;
    try {
        require(isVisible(), "Main window did not become visible on launch");
        requirePhoneWindowFits(this);
        const bool fromPicker = property("androidImportedFromPicker").toBool();
        require(fromPicker || !isProjectOpen(), "Fresh-launch regression check requires no open project");
        // These slots used to dereference palette items before project load,
        // causing the app to close immediately during initAndroidUi().
        on_horizontalSlider_MetatileZoom_valueChanged(60);
        on_horizontalSlider_CollisionZoom_valueChanged(60);
        if (!fromPicker) grab().save(data + "/android-startup.png");
        if (requested == "picker" || requested == "folder") {
            auto *button = findChild<QToolButton *>("androidOpenFolderButton");
            require(button && button->isVisible() && button->isEnabled(), "Open folder button is unavailable before project load");
            const QPalette colors = button->palette();
            require(std::abs(qGray(colors.color(QPalette::ButtonText).rgb()) -
                             qGray(colors.color(QPalette::Button).rgb())) > 80,
                    "Import ZIP button has unreadable foreground/background contrast");
            connect(this, &MainWindow::androidProjectImported, this, [this, data](const QString &root) {
                QFile nextRequest(data + "/android-smoke.request");
                if (!nextRequest.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
                nextRequest.write(QDir(data).relativeFilePath(root).toUtf8());
                nextRequest.close();
                setProperty("androidImportedFromPicker", true);
                QTimer::singleShot(100, this, &MainWindow::runAndroidSmokeTest);
            }, Qt::SingleShotConnection);
            const QPoint target = button->mapToGlobal(button->rect().center());
            const qreal ratio = screen()->devicePixelRatio();
            QJsonObject targets{{"import_x", qRound(target.x() * ratio)},
                                {"import_y", qRound(target.y() * ratio)}};
            QFile targetFile(data + "/android-ui-targets.json");
            require(targetFile.open(QIODevice::WriteOnly), "Cannot write import touch target");
            targetFile.write(QJsonDocument(targets).toJson());
            return; // adb taps the actual button and chooses a ZIP in DocumentsUI.
        }
        const QString path = data + "/" + requested;
        if (!fromPicker) require(openProject(path), "Project open failed");
        else require(editor->project->root == path, "System picker import opened the wrong project");
        require(userSetMap("PalletTown"), "PalletTown did not load");
        requirePhoneWindowFits(this);
        showAndroidPanel(0);
        on_mainTabBar_tabBarClicked(MainTab::Map);
        on_mapViewTab_tabBarClicked(MapViewTab::Metatiles);
        editor->setEditAction(Editor::EditAction::Paint);
        Layout *layout = editor->layout;
        const QString blockPath = QDir(path).filePath(layout->blockdata_path);
        const QByteArray originalFile = read(blockPath);
        Block original; require(layout->getBlock(10, 10, &original), "Missing fixture block");
        const uint16_t tile = original.metatileId() == 0x10 ? 0x115 : 0x10;
        require(editor->metatile_selector_item->select(tile), "Tile selection failed");
        ui->graphicsView_Map->QGraphicsView::centerOn(10 * 16 + 8, 10 * 16 + 8);
        QTest::qWait(250);
        QPoint point = ui->graphicsView_Map->mapFromScene(QPointF(168, 168));
        require(ui->graphicsView_Map->viewport()->rect().contains(point), "Paint location is off screen");
        if (fromPicker) {
            const QPoint global = ui->graphicsView_Map->viewport()->mapToGlobal(point);
            require(screen()->availableGeometry().contains(global), "Paint touch target is outside the phone screen");
            const qreal ratio = screen()->devicePixelRatio();
            QFile targetFile(data + "/android-paint-target.json");
            require(targetFile.open(QIODevice::WriteOnly), "Cannot write canvas touch target");
            targetFile.write(QJsonDocument(QJsonObject{
                {"x", qRound(global.x() * ratio)}, {"y", qRound(global.y() * ratio)}}).toJson());
            targetFile.close();
            for (int attempt = 0; attempt < 150 && !QFile::exists(data + "/android-touch.done"); ++attempt)
                QTest::qWait(200);
            require(QFile::exists(data + "/android-touch.done"), "Android canvas touch was not delivered");
            QTest::qWait(150);
        } else {
            QTest::mouseClick(ui->graphicsView_Map->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        }
        Block painted; layout->getBlock(10, 10, &painted);
        require(painted.metatileId() == tile, "Canvas mouse/touch painting did not change tile");
        // Exercise the actual touch-event path, including multi-pointer sequences.
        auto *device = QTest::createTouchDevice();
        auto *view = ui->graphicsView_Map;
        auto *viewport = view->viewport();
        auto *gesture = view->findChild<QObject *>("androidGestures");
        require(gesture, "Map gesture controller is missing");
        auto holdChoose = [&](int option, const QString &snapshot = QString()) {
            const QPoint start = viewport->rect().center();
            auto sequence = QTest::touchEvent(viewport, device, false);
            sequence.press(0, start, viewport).commit(); QTest::qWait(560);
            auto *menu = viewport->findChild<QWidget *>("androidQuickMenu");
            require(menu && menu->isVisible(), "Press and hold did not open the quick menu");
            for (int i = 0; i < 5; ++i) {
                const QRectF target = menu->property(qPrintable(QString("target%1").arg(i))).toRectF();
                require(QRectF(viewport->rect()).contains(target), "Quick-menu option is outside the phone canvas");
            }
            if (option < 0) {
                sequence.release(0, start, viewport).commit();
                require(gesture->property("lastAction").toInt() == -1, "Releasing a hold selected an option without sliding");
                return;
            }
            const int before = editor->editGroup.activeStack()->index();
            const auto mode = editor->getEditAction();
            const QPoint target = menu->property(qPrintable(QString("target%1").arg(option))).toRectF().center().toPoint();
            sequence.move(0, target, viewport).commit(); QTest::qWait(60);
            require(editor->editGroup.activeStack()->index() == before && editor->getEditAction() == mode,
                    "Quick-menu action executed before releasing the finger");
            if (!snapshot.isEmpty()) grab().save(data + "/android-quick-" + snapshot + ".png");
            sequence.release(0, target, viewport).commit();
            require(!menu->isVisible(), "Quick menu stayed open after release");
            require(gesture->property("lastAction").toInt() == option, "Slide and release selected the wrong option");
        };
        const auto heldBlocks = layout->blockdata;
        const int heldIndex = layout->editHistory.index();
        holdChoose(-1);
        require(layout->blockdata == heldBlocks && layout->editHistory.index() == heldIndex, "Opening the hold menu painted a tile");
        holdChoose(1, "portrait"); require(editor->getEditAction() == Editor::EditAction::Fill, "Hold menu did not select Fill");
        holdChoose(2); require(editor->getEditAction() == Editor::EditAction::Pick, "Hold menu did not select Pick tile");
        editor->metatile_selector_item->select(original.metatileId());
        const QPoint pickPoint = view->mapFromScene(QPointF(168, 168));
        auto pickTouch = QTest::touchEvent(viewport, device, false);
        pickTouch.press(0, pickPoint, viewport).commit(); pickTouch.release(0, pickPoint, viewport).commit();
        require(editor->metatile_selector_item->getMetatileSelection().metatileItems.first().metatileId == tile,
                "Pick tile did not sample the map tile");
        require(editor->getEditAction() == Editor::EditAction::Paint && layout->blockdata == heldBlocks,
                "Picking a tile painted the map or failed to return to Paint");
        holdChoose(0); require(editor->getEditAction() == Editor::EditAction::Paint, "Hold menu did not select Paint");
        holdChoose(3); require(layout->editHistory.index() == heldIndex - 1, "Hold-menu Undo failed");
        holdChoose(4); require(layout->editHistory.index() == heldIndex, "Hold-menu Redo failed");
        // A finger can start moving before the second arrives. Neither the
        // pending stroke nor its history may survive conversion to navigation.
        const QPoint center = viewport->rect().center();
        const auto beforeNavigation = layout->blockdata;
        const int beforeIndex = layout->editHistory.index(), beforeCount = layout->editHistory.count();
        const QPointF sceneBefore = view->mapToScene(center);
        const qreal scaleBefore = view->transform().m11();
        auto navigation = QTest::touchEvent(viewport, device, false);
        navigation.press(0, center - QPoint(40, 0), viewport).commit();
        navigation.move(0, center - QPoint(20, 0), viewport).commit();
        navigation.stationary(0).press(1, center + QPoint(40, 0), viewport).commit();
        navigation.move(0, center + QPoint(10, 20), viewport).move(1, center + QPoint(70, 20), viewport).commit();
        require(QLineF(sceneBefore, view->mapToScene(center)).length() > 5, "Two-finger pan did not move the canvas");
        navigation.move(0, center - QPoint(20, 20), viewport).move(1, center + QPoint(100, 20), viewport).commit();
        require(view->transform().m11() > scaleBefore, "Pinch-out did not zoom in");
        navigation.move(0, center + QPoint(10, 20), viewport).move(1, center + QPoint(70, 20), viewport).commit();
        require(qAbs(view->transform().m11() - scaleBefore) < 0.05, "Pinch-in did not zoom back out");
        navigation.stationary(0).release(1, center + QPoint(70, 20), viewport).commit();
        navigation.move(0, center, viewport).commit(); QTest::qWait(530);
        navigation.release(0, center, viewport).commit();
        require(layout->blockdata == beforeNavigation && layout->editHistory.index() == beforeIndex && layout->editHistory.count() == beforeCount,
                "Two-finger navigation changed tiles or undo/redo history");
        auto canceled = QTest::touchEvent(viewport, device, false);
        canceled.press(0, center, viewport).commit(); canceled.move(0, center + QPoint(25, 0), viewport).commit();
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QCoreApplication::sendEvent(viewport, &cancel);
        canceled.release(0, center + QPoint(25, 0), viewport).commit();
        require(layout->blockdata == beforeNavigation && layout->editHistory.index() == beforeIndex && layout->editHistory.count() == beforeCount,
                "A canceled stroke changed tiles or history");
        editor->setAndroidMapScale(scaleBefore);
        view->QGraphicsView::centerOn(168, 168);
        layout->editHistory.undo();
        Block undone; layout->getBlock(10, 10, &undone);
        require(undone == original, "Undo did not restore original block");
        layout->editHistory.redo();
        on_mapViewTab_tabBarClicked(MapViewTab::Collision);
        editor->movement_permissions_selector_item->select(original.collision() == 0 ? 1 : 0, 3);
        point = ui->graphicsView_Map->mapFromScene(QPointF(168, 168));
        QTest::mouseClick(ui->graphicsView_Map->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        Block changed; layout->getBlock(10, 10, &changed);
        require(changed.collision() != original.collision(), "Canvas collision editing failed");
        require(changed.metatileId() == tile, "Collision edit lost tile ID");
        require(save(false), "Save failed");
        const QByteArray saved = read(blockPath);
        result.insert("saved_block_path", QDir(path).relativeFilePath(blockPath));
        result.insert("saved_block_sha256", QString::fromLatin1(QCryptographicHash::hash(saved, QCryptographicHash::Sha256).toHex()));
        require(saved.size() == originalFile.size() && saved != originalFile, "Saved block file is invalid");
        int differences = 0;
        for (int i = 0; i < saved.size(); ++i) differences += saved.at(i) != originalFile.at(i);
        require(differences <= 2, "Editing one block changed unrelated map bytes");
        require(openProject(path) && userSetMap("PalletTown"), "Reopen failed");
        Block reopened; editor->layout->getBlock(10, 10, &reopened);
        require(reopened == changed, "Edits did not survive reopening");
        const QString zip = data + "/smoke-export.zip";
        require(archive("exportProject", path, zip).value("ok").toBool(), "Project export failed");
        auto imported = archive("importProject", zip, data + "/smoke-imported");
        require(openProject(imported.value("root").toString()) && userSetMap("PalletTown"), "ZIP reimport did not open");
        requirePhoneWindowFits(this);
        editor->layout->getBlock(10, 10, &reopened);
        require(reopened == changed, "ZIP round trip lost map edits");
        auto checkTools = [this, data](const QString &name) {
            bool fits = false;
            auto *tools = findChild<QAction *>("androidToolsAction");
            require(tools && tools->isEnabled(), "Map tools action is unavailable");
            QTimer timer; timer.setSingleShot(true);
            connect(&timer, &QTimer::timeout, this, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog) {
                    const QRect rect(dialog->mapToGlobal(QPoint()), dialog->size());
                    fits = screen()->availableGeometry().contains(rect);
                    dialog->grab().save(data + "/android-tools-" + name + ".png");
                    dialog->accept();
                }
            });
            timer.start(300);
            tools->trigger();
            timer.stop();
            require(fits, "Tools dialog exceeds phone bounds");
        };
        checkTools("portrait");
        for (int panel = 0; panel < 4; ++panel) {
            showAndroidPanel(panel); requirePhoneWindowFits(this);
            auto *button = findChild<QToolButton *>(QString("androidWorkspace%1").arg(panel));
            require(button && button->width() >= 48 && button->height() >= 48, "Bottom navigation touch target is too small");
            require(button->isVisible(), "Bottom navigation is hidden");
            grab().save(data + QString("/android-panel-%1.png").arg(panel));
        }
        require(editor->getEditMode() == Editor::EditMode::Connections && ui->graphicsView_Connections->isVisible(), "Edges did not open the native connection editor");
        bool connectionFits = false;
        QTimer closeConnections; closeConnections.setSingleShot(true);
        connect(&closeConnections, &QTimer::timeout, this, [&] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                connectionFits = screen()->availableGeometry().contains(QRect(dialog->mapToGlobal(QPoint()), dialog->size()));
                dialog->grab().save(data + "/android-connections.png"); dialog->accept();
            }
        });
        closeConnections.start(300); showAndroidConnections(); closeConnections.stop();
        require(connectionFits, "Connection settings exceed phone bounds");
        showAndroidPanel(0); on_mapViewTab_tabBarClicked(MapViewTab::Metatiles);
        if (fromPicker) {
            QFile rotate(data + "/android-rotate.request");
            require(rotate.open(QIODevice::WriteOnly), "Cannot request landscape test"); rotate.close();
            for (int i = 0; i < 150 && width() <= height(); ++i) QTest::qWait(200);
            require(width() > height(), "Android did not rotate to landscape");
            for (int panel = 0; panel < 4; ++panel) {
                showAndroidPanel(panel); requirePhoneWindowFits(this);
                grab().save(data + QString("/android-landscape-%1.png").arg(panel));
            }
            checkTools("landscape");
            showAndroidPanel(0); holdChoose(0, "landscape");
        }
        showAndroidPanel(0);
        ui->graphicsView_Map->QGraphicsView::centerOn(168, 168);
        grab().save(data + "/android-smoke.png");
        result.insert("status", "passed");
        result.insert("checks", fromPicker
            ? "Android touch, folder save-back, hold-slide-release Fill/Pick/Paint/Undo/Redo, two-finger pan/pinch without edits, connections, portrait/landscape, ZIP round trip"
            : "load, canvas paint, undo, redo, collision, save, reopen, JNI ZIP export/import");
    } catch (const std::exception &error) {
        result.insert("status", "failed"); result.insert("error", QString::fromUtf8(error.what()));
    }
    QFile output(data + "/android-smoke.result.json");
    if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(result).toJson());
}
