#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "metatileselector.h"
#include "movementpermissionsselector.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJniObject>
#include <QStandardPaths>
#include <QtTest>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
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
    QJsonObject result;
    try {
        const QString path = data + "/" + QString::fromUtf8(request.readAll()).trimmed();
        require(openProject(path), "Project open failed");
        require(userSetMap("PalletTown"), "PalletTown did not load");
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
        QTest::mouseClick(ui->graphicsView_Map->viewport(), Qt::LeftButton, Qt::NoModifier, point);
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
        editor->layout->getBlock(10, 10, &reopened);
        require(reopened == changed, "ZIP round trip lost map edits");
        showAndroidPanel(0);
        ui->graphicsView_Map->QGraphicsView::centerOn(168, 168);
        grab().save(data + "/android-smoke.png");
        result.insert("status", "passed");
        result.insert("checks", "load, canvas paint, undo, redo, collision, save, reopen, JNI ZIP export/import");
    } catch (const std::exception &error) {
        result.insert("status", "failed"); result.insert("error", QString::fromUtf8(error.what()));
    }
    QFile output(data + "/android-smoke.result.json");
    if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(result).toJson());
}
