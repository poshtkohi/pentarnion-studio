#include "document.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPlainTextDocumentLayout>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>

namespace penta {
namespace {
constexpr qint64 maximumSourceBytes = 64 * 1024 * 1024;

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

QByteArray hash(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

QString absolutePath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString physicalPath(const QString& path, int depth = 0) {
    // Resolve each ancestor explicitly. Both QFileInfo::canonicalFilePath and
    // this MinGW runtime's std::filesystem::canonical leave NTFS junctions in
    // place. Following junctionTarget is needed to identify the actual file.
    // Unlike project-source validation, this also supports a missing Save As
    // leaf while resolving its existing ancestors. The bound rejects cycles.
    if (path.isEmpty() || depth > 256) return {};
    const QString absolute = QDir::cleanPath(QDir::fromNativeSeparators(
        QFileInfo(path).absoluteFilePath()));
    const QFileInfo info(absolute);
    const QString parent = QDir::cleanPath(QDir::fromNativeSeparators(info.absolutePath()));
    if (info.isRoot() || parent == absolute) {
        const QString canonical = info.canonicalFilePath();
        return canonical.isEmpty() ? absolute : canonical;
    }
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
    const QString canonical = actual.canonicalFilePath();
    return canonical.isEmpty() ? actual.absoluteFilePath() : canonical;
}
}

Document::Document(QObject* parent) : QObject(parent) {
    // A document must use the plain-text layout before an editor adopts it.
    m_document.setDocumentLayout(new QPlainTextDocumentLayout(&m_document));
    m_document.setUndoRedoEnabled(true);
    connect(&m_document, &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (!m_loading && (removed != 0 || added != 0)) m_pendingTextChange = true;
    });
    connect(&m_document, &QTextDocument::contentsChanged, this, [this] {
        // QSyntaxHighlighter also emits contentsChanged for layout-only
        // formatting. Only character changes advance the source revision.
        if (m_loading || !m_pendingTextChange) return;
        m_pendingTextChange = false;
        ++m_revision;
        emit stateChanged();
    });
    connect(&m_document, &QTextDocument::modificationChanged, this, [this] {
        if (!m_loading) emit stateChanged();
    });
}

QString Document::canonicalIdentity(const QString& path) {
    if (path.isEmpty()) return {};
    QString canonical = QDir::cleanPath(QDir::fromNativeSeparators(physicalPath(path)));
#ifdef Q_OS_WIN
    canonical = canonical.toCaseFolded();
#endif
    return canonical;
}

QString Document::identity() const { return canonicalIdentity(m_path); }

QString Document::text() const {
    // toPlainText() also converts NBSP to spaces. Preserve source characters.
    QString value = m_document.toRawText();
    value.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return value;
}

void Document::setPath(const QString& path) {
    const QString next = path.isEmpty() ? QString{} : absolutePath(path);
    if (m_path == next) return;
    const QString previous = m_path;
    m_path = next;
    m_document.setMetaInformation(QTextDocument::DocumentUrl, m_path);
    emit pathChanged(previous, m_path);
}

bool Document::load(const QString& path, QString* error) {
    if (error) error->clear();
    QFile file(path);
    if (!QFileInfo(path).isFile())
        return fail(error, tr("The source file does not exist or is not a regular file: %1").arg(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, tr("Cannot open %1: %2").arg(path, file.errorString()));
    if (file.size() > maximumSourceBytes)
        return fail(error, tr("Source files larger than 64 MiB cannot be opened in this version."));
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, tr("Cannot read %1: %2").arg(path, file.errorString()));
    const bool bom = bytes.startsWith("\xEF\xBB\xBF");
    const QByteArray payload = bom ? bytes.mid(3) : bytes;
    for (const char byte : payload) {
        const auto value = static_cast<unsigned char>(byte);
        if (value < 32 && value != 9 && value != 10 && value != 13)
            return fail(error, tr("The file contains binary control bytes; the original was left unchanged."));
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString contents = decoder.decode(payload);
    if (decoder.hasError())
        return fail(error, tr("The file is not valid UTF-8; the original was left unchanged."));

    // Preserve the existing newline convention. Mixed files use their first
    // newline convention after an edit; an unedited save retains exact bytes.
    const qsizetype firstLf = contents.indexOf(QLatin1Char('\n'));
    const QString newline = firstLf > 0 && contents.at(firstLf - 1) == QLatin1Char('\r')
            ? QStringLiteral("\r\n") : QStringLiteral("\n");
    contents.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    m_loading = true;
    m_document.setPlainText(contents);
    m_document.setModified(false);
    m_loading = false;
    m_bom = bom;
    m_newline = newline;
    m_diskHash = hash(bytes);
    m_originalBytes = bytes;
    m_diskExisted = true;
    m_recoveryNeedsConfirmation = false;
    ++m_revision;
    setPath(path);
    emit stateChanged();
    return true;
}

void Document::initializeNew(const QString& contents) {
    m_loading = true;
    m_document.setPlainText(contents);
    m_document.setModified(!contents.isEmpty());
    m_loading = false;
    setPath({});
    m_bom = false;
    m_newline = QStringLiteral("\n");
    m_diskExisted = false;
    m_recoveryNeedsConfirmation = false;
    m_diskHash.clear();
    m_originalBytes.clear();
    ++m_revision;
    emit stateChanged();
}

bool Document::restoreUnsaved(const QString& path, const QString& contents,
                             bool bom, const QString& newline, QString* error) {
    if (error) error->clear();
    if (newline != QStringLiteral("\n") && newline != QStringLiteral("\r\n"))
        return fail(error, tr("Invalid newline convention in recovery data."));
    // Snapshot text remains recoverable even if its old original has become
    // binary, invalid UTF-8, unreadable, oversized or missing. Such a buffer is
    // unnamed so an ordinary Save can never replace the unsafe original.
    bool loadedBaseline = false;
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QString loadError;
        loadedBaseline = load(path, &loadError);
    }
    if (!loadedBaseline) {
        initializeNew();
    }
    m_bom = bom;
    m_newline = newline;
    m_recoveryNeedsConfirmation = loadedBaseline;
    m_loading = true;
    m_document.setPlainText(contents);
    m_document.setModified(true);
    m_loading = false;
    ++m_revision;
    emit stateChanged();
    return true;
}

bool Document::reload(QString* error) {
    if (m_path.isEmpty()) return fail(error, tr("This document has no file to reload."));
    return load(m_path, error);
}

bool Document::hasExternalChange() const {
    if (m_path.isEmpty()) return false;
    if (m_recoveryNeedsConfirmation) return true;
    const QFileInfo info(m_path);
    if (!info.exists()) return m_diskExisted;
    if (!m_diskExisted || !info.isFile()) return true;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maximumSourceBytes) return true;
    const QByteArray bytes = file.readAll();
    return file.error() != QFileDevice::NoError || hash(bytes) != m_diskHash;
}

bool Document::save(QString* error, bool overwriteExternal) {
    if (m_path.isEmpty()) return fail(error, tr("Choose a file name with Save As first."));
    return writeTo(m_path, overwriteExternal, error);
}

bool Document::saveAs(const QString& path, QString* error, bool overwriteExternal) {
    if (path.trimmed().isEmpty()) return fail(error, tr("A source file name is required."));
    return writeTo(path, overwriteExternal, error);
}

bool Document::writeTo(const QString& path, bool overwriteExternal, QString* error) {
    if (error) error->clear();
    const QString destination = absolutePath(path);
    const bool sameFile = !m_path.isEmpty() && canonicalIdentity(destination) == identity();
    if (!overwriteExternal) {
        if (sameFile && hasExternalChange())
            return fail(error, tr("The file changed outside the editor. Reload it, keep the buffer, or use Save As."));
        if (!sameFile && QFileInfo::exists(destination))
            return fail(error, tr("The Save As destination already exists; explicit overwrite confirmation is required."));
    }
    if (QFileInfo(destination).exists() && !QFileInfo(destination).isFile())
        return fail(error, tr("The destination is not a regular file."));

    QByteArray bytes;
    if (!m_document.isModified() && m_diskExisted) {
        bytes = m_originalBytes;
    } else {
        QString contents = text();
        if (m_newline == QStringLiteral("\r\n"))
            contents.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
        QStringEncoder encoder(QStringEncoder::Utf8, QStringConverter::Flag::Stateless);
        bytes = encoder.encode(contents);
        if (encoder.hasError())
            return fail(error, tr("The buffer contains an invalid Unicode sequence; the original was left unchanged."));
        if (m_bom) bytes.prepend("\xEF\xBB\xBF");
    }
    // Follow an existing file's symlink identity rather than replacing its link.
    const QString canonical = physicalPath(destination);
    if (canonical.isEmpty())
        return fail(error, tr("The destination contains an unresolved or excessively long link chain."));
    QSaveFile output(canonical);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return fail(error, tr("Cannot save %1: %2").arg(destination, output.errorString()));
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return fail(error, tr("Cannot write %1: %2").arg(destination, output.errorString()));
    }
    if (!output.commit())
        return fail(error, tr("Cannot safely replace %1: %2").arg(destination, output.errorString()));

    m_diskHash = hash(bytes);
    m_originalBytes = bytes;
    m_diskExisted = true;
    m_recoveryNeedsConfirmation = false;
    setPath(destination);
    m_document.setModified(false);
    emit stateChanged();
    return true;
}

void Document::adoptRenamedPath(const QString& path) {
    setPath(path);
    emit stateChanged();
}

QString Document::newlineDescription() const {
    return m_newline == QStringLiteral("\r\n") ? QStringLiteral("CRLF") : QStringLiteral("LF");
}

QString Document::encodingDescription() const {
    return m_bom ? QStringLiteral("UTF-8 BOM") : QStringLiteral("UTF-8");
}

} // namespace penta
