#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "metatileselector.h"
#include "movementpermissionsselector.h"
#include <QApplication>
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QStandardPaths>
#include <QScreen>
#include <QToolButton>
#include <QTimer>
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
        for (int panel = 0; panel < 3; ++panel) {
            showAndroidPanel(panel); requirePhoneWindowFits(this);
            auto *button = findChild<QToolButton *>(QString("androidWorkspace%1").arg(panel));
            require(button && button->width() >= 48 && button->height() >= 48, "Bottom navigation touch target is too small");
            require(button->isVisible(), "Bottom navigation is hidden");
            grab().save(data + QString("/android-panel-%1.png").arg(panel));
        }
        showAndroidPanel(0);
        ui->graphicsView_Map->QGraphicsView::centerOn(168, 168);
        grab().save(data + "/android-smoke.png");
        result.insert("status", "passed");
        result.insert("checks", fromPicker
            ? "Android touch, system folder picker, linked folder save, load, canvas paint, undo, redo, collision, reopen, JNI ZIP export/import, mobile panels"
            : "load, canvas paint, undo, redo, collision, save, reopen, JNI ZIP export/import");
    } catch (const std::exception &error) {
        result.insert("status", "failed"); result.insert("error", QString::fromUtf8(error.what()));
    }
    QFile output(data + "/android-smoke.result.json");
    if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(result).toJson());
}
