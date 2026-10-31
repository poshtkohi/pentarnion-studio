#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace penta {

class Document;

struct RecoveryRecord {
    QString id;
    QString path;
    QString text;
    QString projectPath;
    QString newline = QStringLiteral("\n");
    bool bom = false;
    int cursorPosition = 0;
};

class RecoveryStore final {
public:
    // Explicit directory is for isolated tests; default is per-user app data.
    explicit RecoveryStore(QString directory = {});
    // Stable storage identity, independent of the application's display name.
    static QString applicationDataDirectory();
    QString directory() const { return m_directory; }
    bool write(const RecoveryRecord& record, QString* error = nullptr) const;
    QVector<RecoveryRecord> records(QStringList* warnings = nullptr) const;
    bool remove(const QString& id, QString* error = nullptr) const;
    static QString newId();

private:
    static bool validId(const QString& id);
    QString m_directory;
};

} // namespace penta
