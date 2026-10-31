#include "project_service.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace penta {
namespace {

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

void clearError(QString* error)
{
    if (error) error->clear();
}

bool validComponent(const QString& name, QString* error)
{
    if (name.isEmpty() || name == "." || name == "..")
        return fail(error, QStringLiteral("A path component is empty, '.' or '..'."));
    if (name.endsWith(' ') || name.endsWith('.'))
        return fail(error, QStringLiteral("Names cannot end in a space or period."));
    for (const QChar c : name) {
        if (c.unicode() < 32 || c.unicode() == 127 || QStringLiteral("<>:\"/\\|?*").contains(c))
            return fail(error, QStringLiteral("The name contains a character that is not portable between Windows and Linux."));
    }
    const QString stem = name.section('.', 0, 0).toUpper();
    const bool numbered = stem.size() == 4 &&
        (stem.startsWith("COM") || stem.startsWith("LPT")) &&
        ((stem[3] >= '1' && stem[3] <= '9') || stem[3] == QChar(0x00b9) ||
         stem[3] == QChar(0x00b2) || stem[3] == QChar(0x00b3));
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" ||
        stem == "CONIN$" || stem == "CONOUT$" || numbered)
        return fail(error, QStringLiteral("The name is reserved on Windows."));
    return true;
}

bool validRelative(const QString& path, bool source, QString* error)
{
    if (path.isEmpty() || path.contains('\\') || QDir::isAbsolutePath(path))
        return fail(error, QStringLiteral("Use a nonempty project-relative path with '/' separators."));
    const auto parts = path.split('/', Qt::KeepEmptyParts);
    for (const QString& part : parts)
        if (!validComponent(part, error)) return false;
    if (source && !path.endsWith(".p", Qt::CaseInsensitive))
        return fail(error, QStringLiteral("Source files must have the .p extension."));
    return true;
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString physicalPath(const QString& path, int depth = 0)
{
    // Resolve the parent first, then the link itself. QFileInfo's canonical path
    // and this MinGW's std::filesystem::canonical both leave NTFS junctions in
    // place, so junctionTarget must be followed explicitly at every ancestor.
    // The bound also rejects cycles and hostile chains without unbounded work.
    if (depth > 256) return {};
    const QString absolute = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
    const QFileInfo info(absolute);
    if (!info.exists()) return {};
    const QString parent = QDir::cleanPath(QDir::fromNativeSeparators(info.absolutePath()));
    if (info.isRoot() || parent == absolute) return info.canonicalFilePath();
    const QString physicalParent = physicalPath(parent, depth + 1);
    if (physicalParent.isEmpty()) return {};
    const QFileInfo actual(QDir(physicalParent).filePath(info.fileName()));
    QString target;
    if (actual.isJunction()) target = actual.junctionTarget();
    else if (actual.isSymbolicLink()) target = actual.symLinkTarget();
    if (!target.isEmpty()) {
        if (QDir::isRelativePath(target)) target = QDir(physicalParent).filePath(target);
        return physicalPath(target, depth + 1);
    }
    return actual.canonicalFilePath();
}

QString identity(const QString& canonical)
{
    const QString path = QDir::fromNativeSeparators(canonical);
    return pathCaseSensitivity() == Qt::CaseInsensitive ? path.toCaseFolded() : path;
}

bool withinRoot(const QString& root, const QString& target)
{
    const QString rootPath = QDir::cleanPath(QDir::fromNativeSeparators(root));
    const QString targetPath = QDir::cleanPath(QDir::fromNativeSeparators(target));
    const QString prefix = rootPath.endsWith('/') ? rootPath : rootPath + '/';
    return targetPath.compare(rootPath, pathCaseSensitivity()) == 0 ||
           targetPath.startsWith(prefix, pathCaseSensitivity());
}

bool checkedPath(const Project& project, const QString& relative, QString& absolute,
                 bool source, bool mustExist, QString* error)
{
    if (!validRelative(relative, source, error)) return false;
    const QFileInfo rootInfo(project.rootPath);
    const QString root = physicalPath(project.rootPath);
    if (!rootInfo.isDir() || root.isEmpty())
        return fail(error, QStringLiteral("The project root directory no longer exists."));
    QString current = root;
    const auto parts = relative.split('/');
    for (qsizetype index = 0; index < parts.size(); ++index) {
        const QString& part = parts[index];
        const QDir parent(current);
        if (parent.exists()) {
            const auto names = parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
            for (const QString& name : names) {
                if (name != part && name.compare(part, Qt::CaseInsensitive) == 0)
                    return fail(error, QStringLiteral("The path collides with '%1' when filename case is ignored.").arg(name));
            }
        }
        current = QDir(current).filePath(part);
        const QFileInfo info(current);
        if (info.exists()) {
            const QString canonical = physicalPath(current);
            if (canonical.isEmpty() || !withinRoot(root, canonical))
                return fail(error, QStringLiteral("The path leaves the project root through a symlink or junction."));
            if (index + 1 < parts.size() && !info.isDir())
                return fail(error, QStringLiteral("An intermediate path component is not a directory."));
        } else if (info.isSymLink()) {
            return fail(error, QStringLiteral("The path contains a broken symlink."));
        }
    }
    const QFileInfo finalInfo(current);
    if (mustExist && (!finalInfo.exists() || (source && !finalInfo.isFile())))
        return fail(error, QStringLiteral("The source file is missing or is not a regular file: %1").arg(relative));
    if (source && finalInfo.exists() && !finalInfo.isFile())
        return fail(error, QStringLiteral("The source path is a directory: %1").arg(relative));
    absolute = QDir::cleanPath(current);
    return true;
}

bool readBytes(const QString& path, QByteArray& bytes, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot finish reading %1: %2").arg(path, file.errorString()));
    return true;
}

QByteArray digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QByteArray serialized(const Project& project, const QByteArray& previous)
{
    QJsonArray sources;
    for (const auto& source : project.sources) sources.append(source);
    // Preserve unrecognised, nonexecuting metadata instead of dropping it on save.
    QJsonObject object = previous.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(previous).object();
    QJsonObject algebra = object.value("algebra").toObject();
    algebra["id"] = project.algebraId;
    algebra["gamma"] = project.gamma;
    object["schema_version"] = 1;
    object["name"] = project.name;
    object["language_version"] = project.languageVersion;
    object["algebra"] = algebra;
    object["sources"] = sources;
    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

bool saveManifest(Project& project, QString* error)
{
    const QFileInfo manifestInfo(project.manifestPath);
    if (!withinRoot(project.rootPath, manifestInfo.absoluteFilePath()) ||
        manifestInfo.absolutePath() != QDir(project.rootPath).absolutePath())
        return fail(error, QStringLiteral("The manifest must remain directly inside the project root."));
    if (manifestInfo.isSymLink())
        return fail(error, QStringLiteral("A symlinked project manifest cannot be replaced."));
    QByteArray existing;
    if (project.manifestDigest.isEmpty()) {
        if (manifestInfo.exists())
            return fail(error, QStringLiteral("The project manifest already exists; it will not be overwritten."));
    } else {
        if (!readBytes(project.manifestPath, existing, error)) return false;
        if (digest(existing) != project.manifestDigest)
            return fail(error, QStringLiteral("The project manifest changed outside the IDE. Reopen the project before changing its file list."));
    }
    const QByteArray bytes = serialized(project, existing);
    QSaveFile file(project.manifestPath);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("Cannot safely save the project manifest: %1").arg(file.errorString()));
    project.manifestDigest = digest(bytes);
    return true;
}

bool sourceUnique(const Project& project, const QString& relative, const QString& absolute,
                  const QString& except, QString* error)
{
    const QString canonical = physicalPath(absolute);
    for (const QString& existing : project.sources) {
        if (existing == except) continue;
        if (existing.compare(relative, Qt::CaseInsensitive) == 0)
            return fail(error, QStringLiteral("The source file is already in the project or has a case-colliding name."));
        if (!canonical.isEmpty()) {
            const QString existingCanonical = physicalPath(QDir(project.rootPath).filePath(existing));
            if (!existingCanonical.isEmpty() && identity(canonical) == identity(existingCanonical))
                return fail(error, QStringLiteral("Two project entries refer to the same canonical file."));
        }
    }
    return true;
}

void removeEmptyDirectories(const QStringList& created)
{
    for (auto it = created.crbegin(); it != created.crend(); ++it)
        QDir().rmdir(*it); // Only directories created by this operation, and only if empty.
}

bool ensureDirectories(const Project& project, const QString& relative,
                       QStringList& created, QString* error)
{
    if (relative.isEmpty()) return true;
    QString checked;
    if (!checkedPath(project, relative, checked, false, false, error)) return false;
    QString current = project.rootPath;
    for (const auto& part : relative.split('/')) {
        const QString next = QDir(current).filePath(part);
        const QFileInfo info(next);
        if (info.exists()) {
            if (!info.isDir()) return fail(error, QStringLiteral("A folder path names an existing file."));
        } else {
            if (!QDir(current).mkdir(part))
                return fail(error, QStringLiteral("Cannot create folder: %1").arg(next));
            created.append(next);
        }
        current = next;
    }
    return checkedPath(project, relative, checked, false, true, error);
}

bool writeNewFile(const QString& absolute, const QByteArray& bytes, QString* error)
{
    QFile file(absolute);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(error, QStringLiteral("Cannot create a new source file without overwriting existing content: %1 (%2)").arg(absolute, file.errorString()));
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        const QString message = file.errorString();
        file.close();
        QFile::remove(absolute); // This call created the previously absent file.
        return fail(error, QStringLiteral("Cannot write new source file: %1").arg(message));
    }
    file.close();
    return true;
}

} // namespace

bool ProjectService::create(const QString& parent, const QString& name, Project& out, QString* error)
{
    clearError(error);
    if (!validComponent(name, error)) return false;
    const QFileInfo parentInfo(parent);
    if (!parentInfo.isDir()) return fail(error, QStringLiteral("Select an existing parent directory."));
    QDir parentDir(physicalPath(parent));
    for (const auto& existing : parentDir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
        if (existing.compare(name, Qt::CaseInsensitive) == 0)
            return fail(error, QStringLiteral("A file or directory with that project name already exists."));
    if (!parentDir.mkdir(name)) return fail(error, QStringLiteral("Cannot create the project directory."));
    Project candidate;
    candidate.name = name;
    candidate.rootPath = physicalPath(parentDir.filePath(name));
    candidate.manifestPath = QDir(candidate.rootPath).filePath("project.pprj");
    candidate.sources = {QStringLiteral("main.p")};
    const QString mainPath = QDir(candidate.rootPath).filePath("main.p");
    const QByteArray starter("% Pentarnion Studio calculations. Run File or use the Command Window.\n"
                             "use_algebra(\"A_gamma\", gamma=1);\n"
                             "pentarnion p = i + j;\n"
                             "display(\"p =\", p);\n"
                             "display(\"p squared =\", p^2);\n");
    if (!writeNewFile(mainPath, starter, error)) {
        QDir().rmdir(candidate.rootPath);
        return false;
    }
    if (!saveManifest(candidate, error)) {
        QFile::remove(mainPath);
        QDir().rmdir(candidate.rootPath);
        return false;
    }
    out = candidate;
    return true;
}

bool ProjectService::load(const QString& manifest, Project& out, QString* error)
{
    clearError(error);
    const QFileInfo info(manifest);
    if (!info.isFile() || !manifest.endsWith(".pprj", Qt::CaseInsensitive))
        return fail(error, QStringLiteral("Select an existing .pprj project manifest."));
    if (info.isSymLink()) return fail(error, QStringLiteral("Open the actual project manifest rather than a symlink."));
    QByteArray bytes;
    if (!readBytes(info.absoluteFilePath(), bytes, error)) return false;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject())
        return fail(error, QStringLiteral("Malformed project JSON: %1").arg(parseError.errorString()));
    const auto object = json.object();
    if (!object.value("schema_version").isDouble() || object.value("schema_version").toDouble() != 1)
        return fail(error, QStringLiteral("Unsupported project schema version; expected 1."));
    const QString languageVersion = object.value("language_version").toString();
    if (languageVersion != "0.1" && languageVersion != "0.2")
        return fail(error, QStringLiteral("Unsupported language version; expected 0.1 or 0.2."));
    if (!object.value("name").isString() || object.value("name").toString().trimmed().isEmpty())
        return fail(error, QStringLiteral("The project name must be a nonempty string."));
    if (!object.value("algebra").isObject())
        return fail(error, QStringLiteral("The project algebra metadata is missing or invalid."));
    const auto algebra = object.value("algebra").toObject();
    if (algebra.value("id").toString() != "A_gamma_v1")
        return fail(error, QStringLiteral("Unsupported algebra identifier; expected A_gamma_v1."));
    const auto gammaValue = algebra.value("gamma");
    const double gamma = gammaValue.toDouble(-1);
    if (!gammaValue.isDouble() || !std::isfinite(gamma) || gamma < 0 || gamma > 1)
        return fail(error, QStringLiteral("Project gamma must be a finite number in [0,1]."));
    if (!object.value("sources").isArray())
        return fail(error, QStringLiteral("Project sources must be an array of relative .p paths."));
    Project candidate;
    candidate.manifestPath = physicalPath(info.absoluteFilePath());
    candidate.rootPath = QFileInfo(candidate.manifestPath).absolutePath();
    candidate.name = object.value("name").toString();
    candidate.languageVersion = languageVersion;
    candidate.gamma = gamma;
    for (const auto& source : object.value("sources").toArray()) {
        if (!source.isString()) return fail(error, QStringLiteral("Every source entry must be a string."));
        const QString relative = source.toString();
        QString absolute;
        if (!resolveSource(candidate, relative, absolute, error) ||
            !sourceUnique(candidate, relative, absolute, QString(), error)) return false;
        candidate.sources.append(relative);
    }
    candidate.manifestDigest = digest(bytes);
    out = candidate;
    return true;
}

bool ProjectService::upgradeLanguageVersion(Project& project, const QString& version, QString* error)
{
    clearError(error);
    if (version != QStringLiteral("0.2"))
        return fail(error, QStringLiteral("Only an explicit upgrade to language 0.2 is supported."));
    if (project.languageVersion == version) return true;
    if (project.languageVersion != QStringLiteral("0.1"))
        return fail(error, QStringLiteral("This project cannot be upgraded from its language version."));
    Project candidate = project;
    candidate.languageVersion = version;
    if (!saveManifest(candidate, error)) return false;
    project = candidate;
    return true;
}

bool ProjectService::resolveSource(const Project& project, const QString& relative,
                                   QString& absolute, QString* error, bool mustExist)
{
    clearError(error);
    QString result;
    if (!checkedPath(project, relative, result, true, mustExist, error)) return false;
    absolute = result;
    return true;
}

bool ProjectService::addSource(Project& project, const QString& relative,
                               const QString& initialText, QString* error)
{
    clearError(error);
    QString absolute;
    if (!resolveSource(project, relative, absolute, error, false) ||
        !sourceUnique(project, relative, absolute, QString(), error)) return false;
    if (QFileInfo::exists(absolute))
        return fail(error, QStringLiteral("The source already exists. Open/include it instead of overwriting it."));
    QStringList created;
    const QString folder = relative.contains('/') ? relative.left(relative.lastIndexOf('/')) : QString();
    if (!ensureDirectories(project, folder, created, error)) {
        removeEmptyDirectories(created);
        return false;
    }
    if (!writeNewFile(absolute, initialText.toUtf8(), error)) {
        removeEmptyDirectories(created);
        return false;
    }
    Project candidate = project;
    candidate.sources.append(relative);
    if (!saveManifest(candidate, error)) {
        QFile::remove(absolute);
        removeEmptyDirectories(created);
        return false;
    }
    project = candidate;
    return true;
}

bool ProjectService::includeSource(Project& project, const QString& relative, QString* error)
{
    clearError(error);
    QString absolute;
    if (!resolveSource(project, relative, absolute, error)) return false;
    if (project.sources.contains(relative)) return true;
    if (!sourceUnique(project, relative, absolute, QString(), error)) return false;
    Project candidate = project;
    candidate.sources.append(relative);
    if (!saveManifest(candidate, error)) return false;
    project = candidate;
    return true;
}

bool ProjectService::renameSource(Project& project, const QString& oldRelative,
                                  const QString& newRelative, QString* error)
{
    clearError(error);
    const auto index = project.sources.indexOf(oldRelative);
    if (index < 0) return fail(error, QStringLiteral("The source is not in this project."));
    QString oldAbsolute, newAbsolute;
    if (!resolveSource(project, oldRelative, oldAbsolute, error)) return false;
    if (oldRelative == newRelative) return true;
    if (!resolveSource(project, newRelative, newAbsolute, error, false) ||
        !sourceUnique(project, newRelative, newAbsolute, oldRelative, error)) return false;
    if (QFileInfo::exists(newAbsolute)) return fail(error, QStringLiteral("The destination already exists."));
    QStringList created;
    const QString folder = newRelative.contains('/') ? newRelative.left(newRelative.lastIndexOf('/')) : QString();
    if (!ensureDirectories(project, folder, created, error)) {
        removeEmptyDirectories(created);
        return false;
    }
    if (!QFile::rename(oldAbsolute, newAbsolute)) {
        removeEmptyDirectories(created);
        return fail(error, QStringLiteral("Cannot rename the source file; the original was retained."));
    }
    Project candidate = project;
    candidate.sources[index] = newRelative;
    if (!saveManifest(candidate, error)) {
        if (!QFile::rename(newAbsolute, oldAbsolute))
            return fail(error, QStringLiteral("Manifest save failed and rename rollback also failed. Your source is preserved at: %1").arg(newAbsolute));
        removeEmptyDirectories(created);
        return false;
    }
    project = candidate;
    return true;
}

bool ProjectService::removeSource(Project& project, const QString& relative,
                                  bool deleteFile, QString* error)
{
    clearError(error);
    if (!project.sources.contains(relative)) return fail(error, QStringLiteral("The source is not in this project."));
    QString absolute;
    if (!resolveSource(project, relative, absolute, error, deleteFile)) return false;
    Project candidate = project;
    candidate.sources.removeAll(relative);
    if (!deleteFile) {
        if (!saveManifest(candidate, error)) return false;
        project = candidate;
        return true;
    }
    // Move just this explicitly selected file aside while committing metadata.
    // Never recursively delete a folder, and never destroy content before commit.
    const QString held = absolute + ".penta-delete-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!QFile::rename(absolute, held))
        return fail(error, QStringLiteral("Cannot prepare the selected source for deletion; it was retained."));
    if (!saveManifest(candidate, error)) {
        if (!QFile::rename(held, absolute))
            return fail(error, QStringLiteral("Manifest save and rollback failed. The file is preserved at: %1").arg(held));
        return false;
    }
    if (!QFile::remove(held)) {
        Project restored = project;
        restored.manifestDigest = candidate.manifestDigest;
        QString rollbackError;
        const bool manifestRestored = saveManifest(restored, &rollbackError);
        const bool fileRestored = QFile::rename(held, absolute);
        if (!manifestRestored || !fileRestored)
            return fail(error, QStringLiteral("Deletion failed; recovery was incomplete. Content remains at %1. %2").arg(fileRestored ? absolute : held, rollbackError));
        project = restored;
        return fail(error, QStringLiteral("The file could not be deleted. The original project and file were restored."));
    }
    project = candidate;
    return true;
}

bool ProjectService::createFolder(Project& project, const QString& relative, QString* error)
{
    clearError(error);
    if (!validRelative(relative, false, error)) return false;
    QStringList created;
    if (!ensureDirectories(project, relative, created, error)) {
        removeEmptyDirectories(created);
        return false;
    }
    return true;
}

} // namespace penta
