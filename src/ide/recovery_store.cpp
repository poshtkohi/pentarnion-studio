#include "recovery_store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace penta {
namespace {
constexpr qint64 maximumRecoveryBytes = 128 * 1024 * 1024;
const QString owner = QStringLiteral("pentarnion-workbench-recovery");

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

bool ownedObject(const QJsonObject& object, const QString& id) {
    return object.value(QStringLiteral("owner")).toString() == owner
        && object.value(QStringLiteral("schema_version")).toInt(-1) == 1
        && object.value(QStringLiteral("id")).toString() == id;
}
}

RecoveryStore::RecoveryStore(QString directory)
    : m_directory(directory.isEmpty()
        ? QDir(applicationDataDirectory())
              .filePath(QStringLiteral("recovery"))
        : QDir::cleanPath(QFileInfo(directory).absoluteFilePath())) {}

QString RecoveryStore::applicationDataDirectory() {
    const auto installed=qEnvironmentVariable("PENTA_RECOVERY_ROOT");
    if(!qEnvironmentVariableIsEmpty("PENTA_INSTALL_ROOT")&&!installed.isEmpty()&&QFileInfo(installed).isAbsolute())
        return QDir::cleanPath(QFileInfo(installed).absoluteFilePath());
    // Keep the pre-release storage identifier so the Studio display rename
    // neither strands snapshots nor creates a second recovery lock namespace.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("Pentarnion/Pentarnion Workbench"));
}

QString RecoveryStore::newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool RecoveryStore::validId(const QString& id) {
    static const QRegularExpression expression(
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"));
    return expression.match(id).hasMatch();
}

bool RecoveryStore::write(const RecoveryRecord& record, QString* error) const {
    if (error) error->clear();
    if (!validId(record.id)) return fail(error, QStringLiteral("Invalid recovery snapshot identifier."));
    if (record.newline != QStringLiteral("\n") && record.newline != QStringLiteral("\r\n"))
        return fail(error, QStringLiteral("Invalid recovery newline convention."));
    if (record.cursorPosition < 0 || record.cursorPosition > record.text.size())
        return fail(error, QStringLiteral("Invalid recovery cursor position."));
    if (!QDir().mkpath(m_directory))
        return fail(error, QStringLiteral("Cannot create the per-user recovery directory."));
    const QString path = QDir(m_directory).filePath(record.id + QStringLiteral(".json"));
    if (QFileInfo(path).isSymLink())
        return fail(error, QStringLiteral("Refusing to write a recovery snapshot through a symbolic link."));
    if (QFileInfo::exists(path)) {
        QFile previous(path);
        if (!previous.open(QIODevice::ReadOnly) || previous.size() > maximumRecoveryBytes)
            return fail(error, QStringLiteral("Cannot inspect the previous recovery snapshot."));
        QJsonParseError parse;
        const QJsonDocument existing = QJsonDocument::fromJson(previous.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !existing.isObject()
                || !ownedObject(existing.object(), record.id))
            return fail(error, QStringLiteral("Refusing to overwrite an unrecognised recovery file."));
    }
    const QJsonObject object{
        {QStringLiteral("owner"), owner},
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("id"), record.id},
        {QStringLiteral("path"), record.path},
        {QStringLiteral("project_path"), record.projectPath},
        {QStringLiteral("text"), record.text},
        {QStringLiteral("newline"), record.newline},
        {QStringLiteral("bom"), record.bom},
        {QStringLiteral("cursor_position"), record.cursorPosition}
    };
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > maximumRecoveryBytes)
        return fail(error, QStringLiteral("The recovery snapshot exceeds the 128 MiB limit."));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return fail(error, QStringLiteral("Cannot write the recovery snapshot: %1").arg(file.errorString()));
    }
    if (!file.commit())
        return fail(error, QStringLiteral("Cannot safely commit the recovery snapshot: %1").arg(file.errorString()));
    return true;
}

QVector<RecoveryRecord> RecoveryStore::records(QStringList* warnings) const {
    QVector<RecoveryRecord> result;
    if (warnings) warnings->clear();
    const QDir dir(m_directory);
    if (!dir.exists()) return result;
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time)) {
        const QString id = info.completeBaseName();
        auto warn = [&](const QString& why) {
            if (warnings) warnings->append(QStringLiteral("Recovery file %1: %2").arg(info.fileName(), why));
        };
        if (!validId(id) || info.isSymLink()) { warn(QStringLiteral("unrecognised identifier or symbolic link; left untouched.")); continue; }
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly) || file.size() > maximumRecoveryBytes) {
            warn(QStringLiteral("cannot be read safely; left untouched.")); continue;
        }
        QJsonParseError parse;
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parse);
        const QJsonObject object = json.object();
        if (parse.error != QJsonParseError::NoError || !json.isObject() || !ownedObject(object, id)
                || !object.value(QStringLiteral("text")).isString()
                || !object.value(QStringLiteral("path")).isString()
                || !object.value(QStringLiteral("project_path")).isString()
                || !object.value(QStringLiteral("newline")).isString()
                || !object.value(QStringLiteral("bom")).isBool()
                || !object.value(QStringLiteral("cursor_position")).isDouble()) {
            warn(QStringLiteral("invalid or unsupported record; left untouched.")); continue;
        }
        RecoveryRecord record;
        record.id = id;
        record.path = object.value(QStringLiteral("path")).toString();
        record.projectPath = object.value(QStringLiteral("project_path")).toString();
        record.text = object.value(QStringLiteral("text")).toString();
        record.newline = object.value(QStringLiteral("newline")).toString();
        record.bom = object.value(QStringLiteral("bom")).toBool();
        const double cursor = object.value(QStringLiteral("cursor_position")).toDouble(-1);
        if ((record.newline != QStringLiteral("\n") && record.newline != QStringLiteral("\r\n"))
                || cursor < 0 || cursor > record.text.size() || cursor != static_cast<int>(cursor)) {
            warn(QStringLiteral("invalid newline or cursor position; left untouched.")); continue;
        }
        record.cursorPosition = static_cast<int>(cursor);
        result.append(record);
    }
    return result;
}

bool RecoveryStore::remove(const QString& id, QString* error) const {
    if (error) error->clear();
    if (!validId(id)) return fail(error, QStringLiteral("Invalid recovery identifier; no file was removed."));
    const QString path = QDir(m_directory).filePath(id + QStringLiteral(".json"));
    if (!QFileInfo::exists(path)) return true;
    if (QFileInfo(path).isSymLink())
        return fail(error, QStringLiteral("Refusing to remove a symbolic-link recovery file."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumRecoveryBytes)
        return fail(error, QStringLiteral("Cannot validate the recovery snapshot for removal."));
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(file.readAll(), &parse);
    file.close();
    if (parse.error != QJsonParseError::NoError || !json.isObject() || !ownedObject(json.object(), id))
        return fail(error, QStringLiteral("The file is not an acknowledged application recovery snapshot; left untouched."));
    if (!QFile::remove(path)) return fail(error, QStringLiteral("Cannot remove the acknowledged recovery snapshot."));
    return true;
}

} // namespace penta
