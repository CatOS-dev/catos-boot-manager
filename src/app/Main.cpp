#include "BootManagerWindow.h"
#include "services/BootSession.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QMessageBox>
#include <QTranslator>
#include <memory>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CatOS Boot Manager"));
    QTranslator translator;
    if (QLocale::system().language() == QLocale::Chinese) {
        const QString bin = QCoreApplication::applicationDirPath();
        const QStringList locations{bin, bin + QStringLiteral("/translations"),
            QDir::cleanPath(bin + QStringLiteral("/../share/catos-boot-manager/translations"))};
        for (const auto &location : locations) {
            if (translator.load(QStringLiteral("catos-boot-manager_zh_CN"), location)) {
                app.installTranslator(&translator);
                break;
            }
        }
    }
    BootSession session;
    std::unique_ptr<BootManagerWindow> window;
    bool privileged = false;
    const auto openReadOnly = [&] {
        window = std::make_unique<BootManagerWindow>(&session);
        window->setReadOnly(true);
        window->show();
    };
    QObject::connect(&session, &BootSession::authorized, &app, [&] {
        privileged = true;
        if (window) return;  // An open read-only window switches itself over.
        window = std::make_unique<BootManagerWindow>(&session);
        window->show();
    });
    // Without administrator access, still show what can be read.
    QObject::connect(&session, &BootSession::authorizationCancelled, &app, [&] { if (!window) openReadOnly(); });
    QObject::connect(&session, &BootSession::failed, &app, [&](const QString &message) {
        QMessageBox::critical(window.get(), QObject::tr("Administrator access unavailable"), message);
        if (!window) { openReadOnly(); return; }
        if (privileged) app.exit(1);  // The administrator connection was lost mid-session.
    });
    QMetaObject::invokeMethod(&session, &BootSession::start, Qt::QueuedConnection);
    return app.exec();
}
