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
    // The Android activity uses a light surface. An inherited system dark
    // palette otherwise makes enabled toolbar labels white on that surface.
    QPalette phonePalette(QColor("#e7ecf2"));
    phonePalette.setColor(QPalette::Window, QColor("#f4f6f8"));
    phonePalette.setColor(QPalette::Base, Qt::white);
    phonePalette.setColor(QPalette::AlternateBase, QColor("#edf1f5"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                      QPalette::ToolTipText})
        phonePalette.setColor(role, QColor("#17212b"));
    phonePalette.setColor(QPalette::ToolTipBase, Qt::white);
    phonePalette.setColor(QPalette::Highlight, QColor("#1565c0"));
    phonePalette.setColor(QPalette::HighlightedText, Qt::white);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        phonePalette.setColor(QPalette::Disabled, role, QColor("#6b7280"));
    a.setPalette(phonePalette);
#endif

    porysplash = new PorymapLoadingScreen;

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [=]() { delete porysplash; });

    MainWindow w(nullptr);
    w.initialize();

    return a.exec();
}
