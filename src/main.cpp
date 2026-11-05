#include "mainwindow.h"
#include "loadingscreen.h"

#include <QApplication>
#include <QPalette>

int main(int argc, char *argv[])
{
#ifdef Q_OS_ANDROID
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#else
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
#endif
    QCoreApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, true);

    QApplication a(argc, argv);
    a.setStyle("fusion");
#ifdef Q_OS_ANDROID
    // Keep the editor surround quiet without changing the map's pixel colors.
    QPalette phonePalette(QColor("#2c3a46"));
    phonePalette.setColor(QPalette::Window, QColor("#202a33"));
    phonePalette.setColor(QPalette::Base, QColor("#18222b"));
    phonePalette.setColor(QPalette::AlternateBase, QColor("#26333f"));
    phonePalette.setColor(QPalette::Light, QColor("#4b626f"));
    phonePalette.setColor(QPalette::Mid, QColor("#304450"));
    phonePalette.setColor(QPalette::Dark, QColor("#111b22"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                      QPalette::ToolTipText})
        phonePalette.setColor(role, QColor("#e4edf3"));
    phonePalette.setColor(QPalette::ToolTipBase, QColor("#26333f"));
    phonePalette.setColor(QPalette::Highlight, QColor("#83cdb6"));
    phonePalette.setColor(QPalette::HighlightedText, QColor("#132e28"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        phonePalette.setColor(QPalette::Disabled, role, QColor("#7d919d"));
    a.setPalette(phonePalette);
#endif

    porysplash = new PorymapLoadingScreen;

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [=]() { delete porysplash; });

    MainWindow w(nullptr);
    w.initialize();

    return a.exec();
}
