#include "BootManagerWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QTranslator>

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
    BootManagerWindow window;
    window.show();
    return app.exec();
}
