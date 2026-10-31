#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTextDocument>

namespace penta {

// One source buffer. No parsing, evaluation or implicit formatting occurs here.
class Document final : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);
    bool load(const QString& path, QString* error = nullptr);
    bool reload(QString* error = nullptr);
    bool save(QString* error = nullptr, bool overwriteExternal = false);
    bool saveAs(const QString& path, QString* error = nullptr,
                bool overwriteExternal = false);
    void initializeNew(const QString& text = {});
    bool restoreUnsaved(const QString& path, const QString& text, bool bom,
                        const QString& newline, QString* error = nullptr);
    void adoptRenamedPath(const QString& path);

    QString path() const { return m_path; }
    QString identity() const;
    static QString canonicalIdentity(const QString& path);
    QString text() const;
    QTextDocument* textDocument() { return &m_document; }
    const QTextDocument* textDocument() const { return &m_document; }
    bool isModified() const { return m_document.isModified(); }
    bool hasExternalChange() const;
    quint64 revision() const { return m_revision; }
    bool hasBom() const { return m_bom; }
    QString newline() const { return m_newline; }
    QString newlineDescription() const;
    QString encodingDescription() const;

signals:
    void stateChanged();
    void pathChanged(const QString& oldPath, const QString& newPath);

private:
    bool writeTo(const QString& path, bool overwriteExternal, QString* error);
    void setPath(const QString& path);
    QTextDocument m_document;
    QString m_path;
    QString m_newline = QStringLiteral("\n");
    QByteArray m_diskHash;
    QByteArray m_originalBytes;
    bool m_diskExisted = false;
    bool m_bom = false;
    bool m_loading = false;
    bool m_pendingTextChange = false;
    // A snapshot has no trustworthy crash-time disk baseline. Replacing its
    // named original therefore always requires an explicit user decision.
    bool m_recoveryNeedsConfirmation = false;
    quint64 m_revision = 0;
};

} // namespace penta
