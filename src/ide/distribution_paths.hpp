#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace penta {
// Only the standalone launcher's explicit, installation-local path changes
// preference storage. Ordinary development builds retain their stable identity.
inline QString distributionSettingsPath(const QString& fileName) {
    const auto requested=qEnvironmentVariable("PENTA_INSTALL_SETTINGS");
    const auto root=qEnvironmentVariable("PENTA_INSTALL_ROOT");
    if(requested.isEmpty()||root.isEmpty())return {};
    const QFileInfo rootInfo(root),settingsInfo(requested);
    if(!rootInfo.isAbsolute()||!settingsInfo.isAbsolute())return {};
    const auto rootPath=QDir::cleanPath(rootInfo.absoluteFilePath());
    const auto expected=QDir(rootPath).filePath(QStringLiteral("settings/Workbench.ini"));
    if(QDir::cleanPath(settingsInfo.absoluteFilePath())!=expected)return {};
    const auto executableRoot=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral(".."));
    if(QDir::cleanPath(executableRoot)!=rootPath)return {};
    const auto directory=QFileInfo(expected).absolutePath();
    // QSettings reports an access error at the isolated path if creation fails;
    // do not fall back to another installation's preferences in that case.
    (void)QDir().mkpath(directory);
    return QDir(directory).filePath(fileName);
}
}
