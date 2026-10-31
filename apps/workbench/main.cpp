#include "main_window.hpp"
#include "studio_branding.hpp"
#include "distribution_paths.hpp"
#include "../distribution_activity.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <memory>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    penta::DistributionActivity activity;
    if(!activity.valid()){
        QMessageBox::warning(nullptr,QObject::tr("Application unavailable"),QObject::tr("Studio could not register its running state for safe installation updates."));
        return 2;
    }
    application.setWindowIcon(penta::studioApplicationIcon());
    QCoreApplication::setApplicationName(QStringLiteral("Pentarnion Studio"));
    QCoreApplication::setOrganizationName(QStringLiteral("Pentarnion"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.2"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Pentarnion Studio: cross-platform mathematical editor, source checker and bounded interpreter. "
        "Choose Run File explicitly for calculations; opening and saving never execute source."));
    parser.addHelpOption(); parser.addVersionOption();
    QCommandLineOption projectOption({QStringLiteral("p"), QStringLiteral("project")},
                                    QStringLiteral("Open a .pprj project."), QStringLiteral("path"));
    QCommandLineOption noSessionOption(QStringLiteral("no-session"),
                                      QStringLiteral("Skip optional previous-session restoration."));
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"),
                                  QStringLiteral("Show the application briefly without changing saved settings."));
    parser.addOption(projectOption); parser.addOption(noSessionOption); parser.addOption(smokeOption);
    parser.process(application);
    const bool smokeTest = parser.isSet(smokeOption);

#ifdef PENTA_DISTRIBUTION_BUILD
    // A direct click on the internal GUI must use the installed launcher before
    // recovery ownership or preferences can touch another development profile.
    if (penta::distributionSettingsPath(QStringLiteral("Workbench.ini")).isEmpty()) {
        const QString launcher = QDir(QCoreApplication::applicationDirPath())
                                     .absoluteFilePath(QStringLiteral("../pentarnion-studio.exe"));
        QStringList forwarded = QCoreApplication::arguments().mid(1);
        for (int index = 0; index < forwarded.size(); ++index) {
            if ((forwarded[index] == QStringLiteral("--project") ||
                 forwarded[index] == QStringLiteral("-p")) && index + 1 < forwarded.size()) {
                ++index;
                forwarded[index] = QFileInfo(forwarded[index]).absoluteFilePath();
            } else if (forwarded[index].startsWith(QStringLiteral("--project="))) {
                forwarded[index] = QStringLiteral("--project=") +
                    QFileInfo(forwarded[index].mid(10)).absoluteFilePath();
            }
        }
        if (QFileInfo(launcher).isFile() && QProcess::startDetached(launcher, forwarded)) return 0;
        QMessageBox::warning(nullptr, QObject::tr("Installed launcher unavailable"),
            QObject::tr("Start Pentarnion Studio using pentarnion-studio.exe in the installation's main folder. "
                        "The internal executable requires that launcher to keep settings and recovery isolated."));
        return 2;
    }
#endif

    // A per-user lock keeps recovery ownership unambiguous. No service is started.
    std::unique_ptr<QLockFile> lock;
    if (!smokeTest) {
        const QString stateDir = penta::RecoveryStore::applicationDataDirectory();
        if (!QDir().mkpath(stateDir)) {
            QMessageBox::warning(nullptr, QObject::tr("Application data unavailable"),
                QObject::tr("The application data directory could not be created. Recovery cannot be safely enabled."));
            return 2;
        }
        lock = std::make_unique<QLockFile>(QDir(stateDir).filePath(QStringLiteral("workbench.lock")));
        if (!lock->tryLock(0)) {
            QMessageBox::information(nullptr, QObject::tr("Pentarnion Studio is already running"),
                QObject::tr("Another Pentarnion Studio instance owns this user's recovery files. Use its window, or close it before starting another instance."));
            return 2;
        }
    }

    penta::MainWindow window(!smokeTest);
    if (parser.isSet(projectOption)) {
        QString error;
        if (!window.openProjectPath(parser.value(projectOption), &error)) {
            if (smokeTest) return 3;
            QMessageBox::warning(&window, QObject::tr("Open project"), error);
        }
    }
    window.show();
    if (smokeTest) {
        QTimer::singleShot(1500, &window, &QWidget::close);
    } else {
        const bool restoreSession = !parser.isSet(noSessionOption) && !parser.isSet(projectOption);
        QTimer::singleShot(0, &window, [&window, restoreSession] { window.restoreStartupState(restoreSession); });
    }
    return application.exec();
}
