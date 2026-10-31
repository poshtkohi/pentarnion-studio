#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace penta {

struct Project {
    QString manifestPath;
    QString rootPath;
    QString name;
    QString languageVersion = QStringLiteral("0.2");
    QString algebraId = QStringLiteral("A_gamma_v1");
    double gamma = 1.0;
    QStringList sources;
    QByteArray manifestDigest; // Detect external metadata edits before writing.
};

// Project operations never execute source or change process-global settings.
// On failure, the supplied Project/out value remains unchanged.
class ProjectService {
public:
    static bool create(const QString& parent, const QString& name, Project& out,
                       QString* error = nullptr);
    static bool load(const QString& manifest, Project& out, QString* error = nullptr);
    static bool addSource(Project& project, const QString& relative,
                          const QString& initialText = QString(), QString* error = nullptr);
    static bool includeSource(Project& project, const QString& relative,
                              QString* error = nullptr);
    static bool renameSource(Project& project, const QString& oldRelative,
                             const QString& newRelative, QString* error = nullptr);
    static bool removeSource(Project& project, const QString& relative,
                             bool deleteFile = false, QString* error = nullptr);
    static bool createFolder(Project& project, const QString& relative,
                             QString* error = nullptr);
    static bool upgradeLanguageVersion(Project& project, const QString& version,
                                       QString* error = nullptr);
    static bool resolveSource(const Project& project, const QString& relative,
                              QString& absolute, QString* error = nullptr,
                              bool mustExist = true);
};

} // namespace penta
