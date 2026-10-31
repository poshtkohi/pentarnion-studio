#include "ide/document.hpp"
#include "ide/editor_widget.hpp"
#include "ide/recovery_store.hpp"
#include "ide/syntax_highlighter.hpp"

#include <QDir>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QMimeData>
#include <QPainter>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextLayout>
#include <filesystem>
#include <cmath>

using namespace penta;

namespace {
bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray readBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void append(Document& document, const QString& text) {
    QTextCursor cursor(document.textDocument());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
}
QTextCharFormat formatAt(const QTextBlock& block, int offset) {
    if (!block.isValid() || !block.layout()) return {};
    for (const auto& range : block.layout()->formats())
        if (offset >= range.start && offset < range.start + range.length)
            return range.format;
    return {};
}
int styleAt(const QTextBlock& block, int offset) {
    return formatAt(block, offset).property(SyntaxHighlighter::styleProperty).toInt();
}
int styleValue(SyntaxStyle style) { return static_cast<int>(style); }
class RestoreClipboard final {
public:
    RestoreClipboard() {
        m_data=new QMimeData;
        const auto* current=QApplication::clipboard()->mimeData();
        if(current)for(const auto& format:current->formats())m_data->setData(format,current->data(format));
    }
    ~RestoreClipboard() {QApplication::clipboard()->setMimeData(m_data);}
private:
    QMimeData* m_data;
};
QTextCharFormat importedFormatAt(QTextDocument& document,int position) {
    QTextCursor cursor(&document);cursor.setPosition(position);
    cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
    return cursor.charFormat();
}
void showEditorForClipboard(EditorWidget& editor) {
    editor.resize(900,320);editor.show();editor.activateWindow();editor.setFocus();
    QApplication::processEvents();
}
}

class DocumentTests final : public QObject {
    Q_OBJECT
private slots:
    void bomCrLfUnicodeRoundTrip();
    void untouchedMixedNewlinesPreserveBytes();
    void invalidInputDoesNotReplaceBuffer_data();
    void invalidInputDoesNotReplaceBuffer();
    void failedSaveAsKeepsIdentityAndDirtyText();
    void missingDirectorySavePreservesOriginal();
    void readOnlySavePreservesOriginalAndEdits();
    void externalEditRequiresExplicitResolution();
    void externalDeletionKeepsBuffer();
    void saveAsRequiresOverwriteConfirmation();
    void renameAdoptsIdentityWithoutLosingEdits();
    void revisionAndUndo();
    void canonicalIdentityResolvesRelativePaths();
    void canonicalIdentityResolvesDirectoryAliases();
    void recoveryRoundTripDoesNotWriteOriginal();
    void recoveryCannotSilentlyOverwriteNewerDisk();
    void recoverySurvivesUnsafeOriginal_data();
    void recoverySurvivesUnsafeOriginal();
    void recoveryRejectsCorruptAndForeignRecords();
    void recoveryRejectsTraversalIdentifiers();
    void editorCommentAndIndentUndo();
    void editorTabUsesSpacesAndPreservesExistingTabs();
    void literalFindReplaceIsSingleUndoStep();
    void highlightingFollowsReplacedDocumentWithoutEditingSource();
    void highlightingUsesUtf16PositionsAfterEmoji();
    void highlightingKeepsStringsCommentsAndFutureNamesSeparate();
    void highlightingPropagatesEditedBlockCommentState();
    void highlightingHandlesUnfinishedSource();
    void syntaxHighlightingVisualSample();
    void editorCopyContainsSyntaxHtmlWithoutEditingSource();
    void editorCopyPartialUnicodeAndBlankLines();
    void editorCtrlCAndCutKeepStandardBehavior();
    void editorLargeCopyFallsBackToPlainText();
};

void DocumentTests::editorCopyContainsSyntaxHtmlWithoutEditingSource() {
    RestoreClipboard restore;
    Document document;
    const QString source=QString::fromUtf8("% Ω 🙂 <tag> & \"quoted\"\n\nreal value = 1.25;\npentarnion p = i;\n");
    document.initializeNew(source);
    EditorWidget editor;editor.setDocument(document.textDocument());
    QPalette colors=editor.palette();colors.setColor(QPalette::Text,QColor("#202020"));colors.setColor(QPalette::Base,Qt::white);editor.setPalette(colors);
    showEditorForClipboard(editor);
    QTextCursor cursor(editor.document());cursor.select(QTextCursor::Document);editor.setTextCursor(cursor);
    const auto revision=document.revision();const bool modified=document.isModified();
    editor.copy();
    QTRY_VERIFY(QApplication::clipboard()->mimeData()->hasHtml());
    const auto* mime=QApplication::clipboard()->mimeData();
    QCOMPARE(mime->text(),source);
    const QString html=mime->html();
    QVERIFY(html.contains("<pre"));QVERIFY(html.contains("monospace"));
    QVERIFY(html.contains("&lt;tag&gt;"));QVERIFY(html.contains("&amp;"));QVERIFY(!html.contains("<tag>"));
    QVERIFY(html.contains("#0000cc"));QVERIFY(html.contains("#008000"));QVERIFY(html.contains("#7f007f"));
    QVERIFY(html.contains("font-weight:700"));
    QTextDocument imported;imported.setHtml(html);
    QCOMPARE(imported.toPlainText(),source);
    QCOMPARE(importedFormatAt(imported,source.indexOf("real")).foreground().color(),QColor("#0000cc"));
    QCOMPARE(importedFormatAt(imported,source.indexOf("Ω")).foreground().color(),QColor("#008000"));
    QCOMPARE(importedFormatAt(imported,source.indexOf("value")).foreground().color(),QColor("#202020"));
    QCOMPARE(importedFormatAt(imported,source.indexOf("real")).fontWeight(),QFont::Bold);
    QCOMPARE(document.text(),source);QCOMPARE(document.revision(),revision);QCOMPARE(document.isModified(),modified);
    QVERIFY(!document.textDocument()->isUndoAvailable());
    const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if(!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));QVERIFY(writeBytes(QDir(artifacts).filePath("highlighted-copy.html"),html.toUtf8()));
        imported.setTextWidth(900);
        QImage image(QSize(900,std::max(1,static_cast<int>(std::ceil(imported.size().height())))),QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);QPainter painter(&image);imported.drawContents(&painter);painter.end();
        QVERIFY(image.save(QDir(artifacts).filePath("rich-copy-rendered.png")));
    }
}

void DocumentTests::editorCopyPartialUnicodeAndBlankLines() {
    RestoreClipboard restore;
    EditorWidget editor;
    const QString source=QString::fromUtf8("outside\n\npentarnion p = i;\n\n% Ω🙂 <&>\nlast line");
    editor.setPlainText(source);
    QFont font=editor.font();font.setItalic(true);editor.setFont(font);
    showEditorForClipboard(editor);
    const int first=source.indexOf("\n\n")+1,last=source.indexOf("last line");
    QTextCursor cursor(editor.document());cursor.setPosition(first);cursor.setPosition(last,QTextCursor::KeepAnchor);editor.setTextCursor(cursor);
    editor.copy();
    QTRY_VERIFY(QApplication::clipboard()->mimeData()->hasHtml());
    const QString expected=source.mid(first,last-first),html=QApplication::clipboard()->mimeData()->html();
    QCOMPARE(QApplication::clipboard()->text(),expected);
    QVERIFY(expected.startsWith('\n'));QVERIFY(expected.endsWith('\n'));QVERIFY(expected.contains("\n\n"));
    QVERIFY(!html.contains("outside"));QVERIFY(!html.contains("last line"));QVERIFY(html.contains("font-style:italic"));
    QTextDocument imported;imported.setHtml(html);QCOMPARE(imported.toPlainText(),expected);
    const int partialStart=source.indexOf("pentarnion")+4;
    cursor.setPosition(partialStart);cursor.setPosition(partialStart+5,QTextCursor::KeepAnchor);editor.setTextCursor(cursor);editor.copy();
    QCOMPARE(QApplication::clipboard()->text(),QStringLiteral("arnio"));
    imported.setHtml(QApplication::clipboard()->mimeData()->html());QCOMPARE(imported.toPlainText(),QStringLiteral("arnio"));
    QCOMPARE(importedFormatAt(imported,0).foreground().color(),QColor("#0000cc"));
}

void DocumentTests::editorCtrlCAndCutKeepStandardBehavior() {
    RestoreClipboard restore;
    EditorWidget editor;editor.resize(700,200);editor.setPlainText(QStringLiteral("real x=1;\nreal y=2;"));
    editor.show();editor.activateWindow();editor.setFocus();QApplication::processEvents();
    QTextCursor cursor(editor.document());cursor.setPosition(0);cursor.setPosition(9,QTextCursor::KeepAnchor);editor.setTextCursor(cursor);
    QTest::keyClick(&editor,Qt::Key_C,Qt::ControlModifier);
    QTRY_COMPARE(QApplication::clipboard()->text(),QStringLiteral("real x=1;"));
    QVERIFY(QApplication::clipboard()->mimeData()->hasHtml());
    QCOMPARE(editor.toPlainText(),QStringLiteral("real x=1;\nreal y=2;"));
    QTest::keyClick(&editor,Qt::Key_X,Qt::ControlModifier);
    QTRY_COMPARE(editor.toPlainText(),QStringLiteral("\nreal y=2;"));
    QCOMPARE(QApplication::clipboard()->text(),QStringLiteral("real x=1;"));
    QVERIFY(QApplication::clipboard()->mimeData()->hasHtml());
    editor.undo();QCOMPARE(editor.toPlainText(),QStringLiteral("real x=1;\nreal y=2;"));
}

void DocumentTests::editorLargeCopyFallsBackToPlainText() {
    RestoreClipboard restore;
    EditorWidget editor;
    const QString source(4*1024*1024+1,QLatin1Char('x'));
    editor.setPlainText(source);
    showEditorForClipboard(editor);
    QTextCursor cursor(editor.document());cursor.select(QTextCursor::Document);editor.setTextCursor(cursor);
    editor.copy();
    QTRY_COMPARE(QApplication::clipboard()->text().size(),source.size());
    QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
    QCOMPARE(QApplication::clipboard()->text(),source);
}

void DocumentTests::bomCrLfUnicodeRoundTrip() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QString::fromUtf8("source λ.p"));
    const QString content = QString::fromUtf8("% λ 🙂") + QChar(0x00a0)
            + QStringLiteral("\r\npentarnion p = i;\r\n");
    QByteArray bytes("\xEF\xBB\xBF");
    bytes += content.toUtf8();
    QVERIFY(writeBytes(path, bytes));
    Document document;
    QString error;
    QVERIFY2(document.load(path, &error), qPrintable(error));
    QVERIFY(document.hasBom());
    QCOMPARE(document.newlineDescription(), QStringLiteral("CRLF"));
    QVERIFY(document.text().contains(QChar(0x00a0)));
    QVERIFY(!document.isModified());
    QVERIFY2(document.save(&error), qPrintable(error));
    QCOMPARE(readBytes(path), bytes);
    append(document, QString::fromUtf8("% Ω\n"));
    QVERIFY(document.isModified());
    QVERIFY2(document.save(&error), qPrintable(error));
    QCOMPARE(readBytes(path), bytes + QString::fromUtf8("% Ω\r\n").toUtf8());
    QVERIFY(!document.isModified());
}

void DocumentTests::untouchedMixedNewlinesPreserveBytes() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("mixed.p"));
    const QByteArray bytes("% first\r\n% second\n% third\r\n");
    QVERIFY(writeBytes(path, bytes));
    Document document;
    QString error;
    QVERIFY2(document.load(path, &error), qPrintable(error));
    QVERIFY2(document.save(&error), qPrintable(error));
    QCOMPARE(readBytes(path), bytes);
}

void DocumentTests::invalidInputDoesNotReplaceBuffer_data() {
    QTest::addColumn<QByteArray>("bytes");
    QTest::newRow("invalid-utf8") << QByteArray("\xC3\x28", 2);
    QTest::newRow("truncated-utf8") << QByteArray("% \xE2\x82", 4);
    QTest::newRow("nul-binary") << QByteArray("a\0b", 3);
    QTest::newRow("binary-control") << QByteArray("a\x01" "b", 3);
    QTest::newRow("utf16") << QByteArray("\xFF\xFE" "a\0", 4);
}

void DocumentTests::invalidInputDoesNotReplaceBuffer() {
    QFETCH(QByteArray, bytes);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("invalid.p"));
    QVERIFY(writeBytes(path, bytes));
    Document document;
    document.initializeNew(QStringLiteral("keep my edits"));
    const quint64 revision = document.revision();
    QString error;
    QVERIFY(!document.load(path, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.text(), QStringLiteral("keep my edits"));
    QCOMPARE(document.revision(), revision);
    QCOMPARE(document.path(), QString{});
    QVERIFY(document.isModified());
    QCOMPARE(readBytes(path), bytes);
}

void DocumentTests::failedSaveAsKeepsIdentityAndDirtyText() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("original.p"));
    QVERIFY(writeBytes(path, "real x = 1;\n"));
    Document document;
    QString error;
    QVERIFY(document.load(path, &error));
    append(document, QStringLiteral("% unsaved\n"));
    const QString identity = document.identity();
    QVERIFY(!document.saveAs(directory.filePath(QStringLiteral("missing/new.p")), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(document.identity(), identity);
    QCOMPARE(document.path(), QFileInfo(path).absoluteFilePath());
    QVERIFY(document.isModified());
    QVERIFY(document.text().endsWith(QStringLiteral("% unsaved\n")));
    QCOMPARE(readBytes(path), QByteArray("real x = 1;\n"));
}

void DocumentTests::missingDirectorySavePreservesOriginal() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("original.p"));
    QVERIFY(writeBytes(path, "% disk\n"));
    Document document;
    QString error;
    QVERIFY(document.load(path, &error));
    append(document, QStringLiteral("% edited\n"));
    const QString invalidPath = directory.filePath(QStringLiteral("parent-does-not-exist/original.p"));
    QVERIFY(!document.saveAs(invalidPath, &error));
    QCOMPARE(readBytes(path), QByteArray("% disk\n"));
    QVERIFY(document.isModified());
}

void DocumentTests::readOnlySavePreservesOriginalAndEdits() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("read-only.p"));
    QVERIFY(writeBytes(path, "% original\n"));
    Document document;
    QString error;
    QVERIFY(document.load(path, &error));
    append(document, QStringLiteral("% keep these edits\n"));
    const QFile::Permissions originalPermissions = QFile::permissions(path);
    struct PermissionRestore {
        QString path;
        QFile::Permissions permissions;
        ~PermissionRestore() { QFile::setPermissions(path, permissions); }
    } restore{path, originalPermissions};
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::ReadUser | QFile::ReadGroup | QFile::ReadOther)
            || QFileInfo(path).isWritable())
        QSKIP("The test filesystem does not enforce a read-only source file for this user.");
    QVERIFY(!document.save(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(path), QByteArray("% original\n"));
    QVERIFY(document.isModified());
    QVERIFY(document.text().endsWith(QStringLiteral("% keep these edits\n")));
}

void DocumentTests::externalEditRequiresExplicitResolution() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("source.p"));
    QVERIFY(writeBytes(path, "% original\n"));
    Document document;
    QString error;
    QVERIFY(document.load(path, &error));
    append(document, QStringLiteral("% mine\n"));
    QVERIFY(writeBytes(path, "% external\n"));
    QVERIFY(document.hasExternalChange());
    QVERIFY(!document.save(&error));
    QCOMPARE(readBytes(path), QByteArray("% external\n"));
    QVERIFY(document.isModified());
    const QString copy = directory.filePath(QStringLiteral("mine.p"));
    QVERIFY2(document.saveAs(copy, &error), qPrintable(error));
    QCOMPARE(readBytes(copy), QByteArray("% original\n% mine\n"));
    QCOMPARE(readBytes(path), QByteArray("% external\n"));
    QVERIFY(!document.hasExternalChange());
}

void DocumentTests::externalDeletionKeepsBuffer() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("source.p"));
    QVERIFY(writeBytes(path, "% original\n"));
    Document document;
    QString error;
    QVERIFY(document.load(path, &error));
    append(document, QStringLiteral("% mine\n"));
    QVERIFY(QFile::remove(path));
    QVERIFY(document.hasExternalChange());
    QVERIFY(!document.save(&error));
    QVERIFY(document.isModified());
    QVERIFY(!document.reload(&error));
    QVERIFY(document.text().endsWith(QStringLiteral("% mine\n")));
    QVERIFY(document.save(&error, true));
    QVERIFY(QFile::exists(path));
}

void DocumentTests::saveAsRequiresOverwriteConfirmation() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("exists.p"));
    QVERIFY(writeBytes(path, "% existing\n"));
    Document document;
    document.initializeNew(QStringLiteral("% replacement\n"));
    QString error;
    QVERIFY(!document.saveAs(path, &error));
    QCOMPARE(readBytes(path), QByteArray("% existing\n"));
    QVERIFY(document.isModified());
    QVERIFY(document.path().isEmpty());
    QVERIFY2(document.saveAs(path, &error, true), qPrintable(error));
    QCOMPARE(readBytes(path), QByteArray("% replacement\n"));
    QVERIFY(!document.isModified());
}

void DocumentTests::renameAdoptsIdentityWithoutLosingEdits() {
    QTemporaryDir directory;
    const QString oldPath = directory.filePath(QStringLiteral("old.p"));
    const QString newPath = directory.filePath(QStringLiteral("new.p"));
    QVERIFY(writeBytes(oldPath, "% original\n"));
    Document document;
    QString error;
    QVERIFY(document.load(oldPath, &error));
    append(document, QStringLiteral("% dirty\n"));
    QSignalSpy spy(&document, &Document::pathChanged);
    QVERIFY(QFile::rename(oldPath, newPath));
    document.adoptRenamedPath(newPath);
    QCOMPARE(spy.size(), 1);
    QVERIFY(document.isModified());
    QVERIFY(!document.hasExternalChange());
    QVERIFY(document.save(&error));
    QCOMPARE(readBytes(newPath), QByteArray("% original\n% dirty\n"));
}

void DocumentTests::revisionAndUndo() {
    Document document;
    document.initializeNew(QStringLiteral("real x = 1;"));
    document.textDocument()->setModified(false);
    const quint64 before = document.revision();
    append(document, QStringLiteral("\n"));
    QVERIFY(document.revision() > before);
    QVERIFY(document.isModified());
    document.textDocument()->undo();
    QCOMPARE(document.text(), QStringLiteral("real x = 1;"));
    QVERIFY(!document.isModified());
}

void DocumentTests::canonicalIdentityResolvesRelativePaths() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("source.p"));
    QVERIFY(writeBytes(path, "% text\n"));
    QCOMPARE(Document::canonicalIdentity(path),
             Document::canonicalIdentity(directory.path() + QStringLiteral("/./source.p")));
}

void DocumentTests::canonicalIdentityResolvesDirectoryAliases() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString physical = directory.filePath(QString::fromUtf8("physical λ"));
    const QString alias = directory.filePath(QStringLiteral("alias directory"));
    QVERIFY(QDir().mkpath(physical));
    const QString source = QDir(physical).filePath(QStringLiteral("source.p"));
    QVERIFY(writeBytes(source, "% original\n"));
#ifdef Q_OS_WIN
    QProcess command;
    command.setProgram(QStringLiteral("cmd.exe"));
    command.setNativeArguments(QStringLiteral("/d /c mklink /J \"%1\" \"%2\"")
            .arg(QDir::toNativeSeparators(alias), QDir::toNativeSeparators(physical)));
    command.start();
    if (!command.waitForFinished(10000) || command.exitCode() != 0)
        QSKIP("This filesystem cannot create an NTFS junction for the alias regression.");
#else
    std::error_code errorCode;
    std::filesystem::create_directory_symlink(std::filesystem::path(physical.toUtf8().constData()),
                                            std::filesystem::path(alias.toUtf8().constData()), errorCode);
    if (errorCode) QSKIP("This filesystem cannot create a directory symlink.");
#endif
    struct RemoveAlias {
        QString path;
        ~RemoveAlias() { QDir().rmdir(path); }
    } cleanup{alias};
    QVERIFY(QFileInfo(alias).isDir());
    const QString throughAlias = QDir(alias).filePath(QStringLiteral("source.p"));
    QCOMPARE(Document::canonicalIdentity(throughAlias), Document::canonicalIdentity(source));
    QCOMPARE(Document::canonicalIdentity(QDir(alias).filePath(QStringLiteral("new.p"))),
             Document::canonicalIdentity(QDir(physical).filePath(QStringLiteral("new.p"))));
    Document document;
    QString error;
    QVERIFY2(document.load(throughAlias, &error), qPrintable(error));
    append(document, QStringLiteral("% edited through alias\n"));
    QVERIFY2(document.save(&error), qPrintable(error));
    QCOMPARE(readBytes(source), QByteArray("% original\n% edited through alias\n"));
    QVERIFY(QFileInfo(alias).isDir());
}

void DocumentTests::recoveryRoundTripDoesNotWriteOriginal() {
    QTemporaryDir directory;
    const QString original = directory.filePath(QStringLiteral("original.p"));
    QVERIFY(writeBytes(original, "% original on disk\n"));
    const QString recoveryDirectory = directory.filePath(QStringLiteral("app-data/recovery"));
    RecoveryStore store(recoveryDirectory);
    RecoveryRecord record;
    record.id = RecoveryStore::newId();
    record.path = original;
    record.projectPath = directory.filePath(QStringLiteral("project.pprj"));
    record.text = QString::fromUtf8("% unsaved λ\n");
    record.bom = true;
    record.newline = QStringLiteral("\r\n");
    record.cursorPosition = 5;
    QString error;
    QVERIFY2(store.write(record, &error), qPrintable(error));
    QStringList warnings;
    const auto records = store.records(&warnings);
    QVERIFY(warnings.isEmpty());
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.first().text, record.text);
    QCOMPARE(records.first().path, original);
    QCOMPARE(records.first().cursorPosition, 5);
    Document restored;
    QVERIFY(restored.restoreUnsaved(record.path, record.text, record.bom, record.newline, &error));
    QCOMPARE(restored.text(), record.text);
    QVERIFY(restored.isModified());
    QCOMPARE(readBytes(original), QByteArray("% original on disk\n"));
    QVERIFY(store.remove(record.id, &error));
    QVERIFY(store.records().isEmpty());
    QCOMPARE(readBytes(original), QByteArray("% original on disk\n"));
}

void DocumentTests::recoveryCannotSilentlyOverwriteNewerDisk() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("source.p"));
    const QByteArray newerBytes("% newer disk version after the crash\n");
    QVERIFY(writeBytes(path, newerBytes));
    Document restored;
    QString error;
    QVERIFY(restored.restoreUnsaved(path, QStringLiteral("% recovered unsaved buffer\n"),
                                    false, QStringLiteral("\n"), &error));
    QVERIFY(restored.isModified());
    QVERIFY(restored.hasExternalChange());
    QVERIFY(!restored.save(&error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(readBytes(path), newerBytes);
    QCOMPARE(restored.text(), QStringLiteral("% recovered unsaved buffer\n"));
    QVERIFY(restored.isModified());
    const QString copy = directory.filePath(QStringLiteral("recovered-copy.p"));
    QVERIFY2(restored.saveAs(copy, &error), qPrintable(error));
    QCOMPARE(readBytes(path), newerBytes);
    QCOMPARE(readBytes(copy), QByteArray("% recovered unsaved buffer\n"));
    QVERIFY(!restored.hasExternalChange());

    QVERIFY(restored.restoreUnsaved(path, QStringLiteral("% explicitly accepted replacement\n"),
                                    false, QStringLiteral("\n"), &error));
    QVERIFY(restored.hasExternalChange());
    QVERIFY(restored.save(&error, true));
    QVERIFY(!restored.hasExternalChange());
    QCOMPARE(readBytes(path), QByteArray("% explicitly accepted replacement\n"));

    QVERIFY(restored.restoreUnsaved(path, QStringLiteral("% another snapshot\n"),
                                    false, QStringLiteral("\n"), &error));
    QVERIFY(restored.hasExternalChange());
    QVERIFY(restored.reload(&error));
    QVERIFY(!restored.hasExternalChange());
    QVERIFY(!restored.isModified());
    QCOMPARE(restored.text(), QStringLiteral("% explicitly accepted replacement\n"));
}

void DocumentTests::recoverySurvivesUnsafeOriginal_data() {
    QTest::addColumn<QByteArray>("diskBytes");
    QTest::addColumn<bool>("exists");
    QTest::newRow("invalid-utf8") << QByteArray("\xC3\x28", 2) << true;
    QTest::newRow("binary") << QByteArray("a\0b", 3) << true;
    QTest::newRow("deleted-original") << QByteArray{} << false;
}

void DocumentTests::recoverySurvivesUnsafeOriginal() {
    QFETCH(QByteArray, diskBytes);
    QFETCH(bool, exists);
    QTemporaryDir directory;
    const QString original = directory.filePath(QStringLiteral("source.p"));
    if (exists) QVERIFY(writeBytes(original, diskBytes));
    const QString snapshotText = QString::fromUtf8("% recovered λ\nreal a = 1;\n");
    Document recovered;
    QString error;
    QVERIFY2(recovered.restoreUnsaved(original, snapshotText, true,
                                      QStringLiteral("\r\n"), &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(recovered.path().isEmpty());
    QVERIFY(recovered.identity().isEmpty());
    QVERIFY(recovered.isModified());
    QVERIFY(recovered.hasBom());
    QCOMPARE(recovered.newline(), QStringLiteral("\r\n"));
    QCOMPARE(recovered.text(), snapshotText);
    QVERIFY(!recovered.save(&error));
    QVERIFY(recovered.isModified());
    if (exists) QCOMPARE(readBytes(original), diskBytes);
    else QVERIFY(!QFile::exists(original));
    const QString copy = directory.filePath(QStringLiteral("recovered.p"));
    QVERIFY2(recovered.saveAs(copy, &error), qPrintable(error));
    QByteArray expected("\xEF\xBB\xBF");
    QString diskText = snapshotText;
    diskText.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    expected += diskText.toUtf8();
    QCOMPARE(readBytes(copy), expected);
    if (exists) QCOMPARE(readBytes(original), diskBytes);
    else QVERIFY(!QFile::exists(original));
}

void DocumentTests::recoveryRejectsCorruptAndForeignRecords() {
    QTemporaryDir directory;
    RecoveryStore store(directory.path());
    const QString corruptId = RecoveryStore::newId();
    const QString corruptPath = directory.filePath(corruptId + QStringLiteral(".json"));
    QVERIFY(writeBytes(corruptPath, "{broken"));
    const QString foreignId = RecoveryStore::newId();
    const QString foreignPath = directory.filePath(foreignId + QStringLiteral(".json"));
    QVERIFY(writeBytes(foreignPath, "{\"owner\":\"another-app\",\"schema_version\":1}"));
    QStringList warnings;
    QVERIFY(store.records(&warnings).isEmpty());
    QCOMPARE(warnings.size(), 2);
    QString error;
    QVERIFY(!store.remove(corruptId, &error));
    QVERIFY(!store.remove(foreignId, &error));
    QVERIFY(QFile::exists(corruptPath));
    QVERIFY(QFile::exists(foreignPath));
    RecoveryRecord record;
    record.id = foreignId;
    QVERIFY(!store.write(record, &error));
    QCOMPARE(readBytes(foreignPath), QByteArray("{\"owner\":\"another-app\",\"schema_version\":1}"));
}

void DocumentTests::recoveryRejectsTraversalIdentifiers() {
    QTemporaryDir directory;
    RecoveryStore store(directory.filePath(QStringLiteral("recovery")));
    const QString original = directory.filePath(QStringLiteral("original.p"));
    QVERIFY(writeBytes(original, "% precious\n"));
    QString error;
    QVERIFY(!store.remove(QStringLiteral("../original.p"), &error));
    RecoveryRecord record;
    record.id = QStringLiteral("../original.p");
    record.text = QStringLiteral("overwrite");
    QVERIFY(!store.write(record, &error));
    QCOMPARE(readBytes(original), QByteArray("% precious\n"));
}

void DocumentTests::editorCommentAndIndentUndo() {
    EditorWidget editor;
    const QString original = QStringLiteral("    real a = 1;\nreal b = 2;");
    editor.setPlainText(original);
    editor.selectAll();
    editor.toggleComment();
    QCOMPARE(editor.toPlainText(), QStringLiteral("    % real a = 1;\n% real b = 2;"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);
    editor.selectAll();
    editor.toggleComment();
    editor.selectAll();
    editor.toggleComment();
    QCOMPARE(editor.toPlainText(), original);
    editor.selectAll();
    editor.indentSelection();
    QCOMPARE(editor.toPlainText(), QStringLiteral("        real a = 1;\n    real b = 2;"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);
}

void DocumentTests::editorTabUsesSpacesAndPreservesExistingTabs() {
    EditorWidget editor;
    editor.setPlainText(QStringLiteral("\treal a = 1;\nx"));
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.toPlainText(), QStringLiteral("\treal a = 1;\nx   "));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.toPlainText(), QStringLiteral("\treal a = 1;\nx   \n"));
    QVERIFY(editor.toPlainText().startsWith(QLatin1Char('\t')));
}

void DocumentTests::literalFindReplaceIsSingleUndoStep() {
    EditorWidget editor;
    const QString original = QStringLiteral("i.*j\ni.*j\nI.*J");
    editor.setPlainText(original);
    QCOMPARE(editor.replaceAll(QStringLiteral("i.*j"), QStringLiteral("(i*j)"), true), 2);
    QCOMPARE(editor.toPlainText(), QStringLiteral("(i*j)\n(i*j)\nI.*J"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), original);
    QVERIFY(editor.findLiteral(QStringLiteral("I.*J"), false, true));
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("I.*J"));
    QVERIFY(editor.replaceCurrent(QStringLiteral("I.*J"), QStringLiteral("q"), true));
    QVERIFY(editor.toPlainText().endsWith(QStringLiteral("q")));
}

void DocumentTests::highlightingFollowsReplacedDocumentWithoutEditingSource() {
    Document first;
    Document replacement;
    first.initializeNew(QStringLiteral("real first = 1;\n"));
    replacement.initializeNew(QStringLiteral("pentarnion p = inv(i);\n"));
    replacement.textDocument()->setModified(false);
    const QString before = replacement.text();
    const quint64 revision = replacement.revision();
    EditorWidget editor;
    editor.setDocument(first.textDocument());
    QCOMPARE(editor.syntaxHighlighter()->document(), first.textDocument());
    editor.setDocument(replacement.textDocument());
    QCOMPARE(editor.syntaxHighlighter()->document(), replacement.textDocument());
    QCOMPARE(editor.syntaxHighlighter()->parent(), &editor);
    editor.syntaxHighlighter()->rehighlight();
    QCoreApplication::processEvents();
    const QTextBlock block = replacement.textDocument()->firstBlock();
    QCOMPARE(styleAt(block, 0), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(formatAt(block, 0).foreground().color(), QColor(QStringLiteral("#0000cc")));
    QCOMPARE(styleAt(block, block.text().indexOf(QStringLiteral("inv"))), styleValue(SyntaxStyle::Builtin));
    QCOMPARE(styleAt(block, block.text().indexOf(QStringLiteral("(i")) + 1), styleValue(SyntaxStyle::Basis));
    QCOMPARE(styleAt(block, block.text().indexOf(QStringLiteral(" p ")) + 1), 0);
    QCOMPARE(replacement.text(), before);
    QCOMPARE(replacement.revision(), revision);
    QVERIFY(!replacement.isModified());
    QVERIFY(!replacement.textDocument()->isUndoAvailable());
}

void DocumentTests::highlightingUsesUtf16PositionsAfterEmoji() {
    Document document;
    const QString source = QString::fromUtf8("use_algebra(\"🙂 real i\", gamma=1); real z = 2; % 🙂 integer");
    document.initializeNew(source);
    EditorWidget editor;
    editor.setDocument(document.textDocument());
    editor.syntaxHighlighter()->rehighlight();
    const QTextBlock block = document.textDocument()->firstBlock();
    const int stringReal = source.indexOf(QStringLiteral("real"));
    const int laterReal = source.lastIndexOf(QStringLiteral("real"));
    const int emoji = source.indexOf(QString::fromUtf8("🙂"));
    QCOMPARE(styleAt(block, stringReal), styleValue(SyntaxStyle::String));
    QCOMPARE(styleAt(block, emoji), styleValue(SyntaxStyle::String));
    QCOMPARE(styleAt(block, emoji + 1), styleValue(SyntaxStyle::String));
    QCOMPARE(styleAt(block, laterReal), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(styleAt(block, laterReal + 3), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(styleAt(block, laterReal - 1), 0);
    QCOMPARE(styleAt(block, source.indexOf(QStringLiteral("2;"))), styleValue(SyntaxStyle::Number));
    QCOMPARE(styleAt(block, source.lastIndexOf(QStringLiteral("integer"))), styleValue(SyntaxStyle::Comment));
    QCOMPARE(document.text(), source);
}

void DocumentTests::highlightingKeepsStringsCommentsAndFutureNamesSeparate() {
    Document document;
    const QString source = QStringLiteral("% real i norm2 42\n")
        + QString::fromUtf8("use_algebra(\"real \\\"i\\\" 🙂 norm2\", gamma=1);\n")
        + QStringLiteral("integer count = 10; real gain = .5;\n")
        + QStringLiteral("plot simulate for function search realistic I user_name\n");
    document.initializeNew(source);
    EditorWidget editor;
    editor.setDocument(document.textDocument());
    editor.syntaxHighlighter()->rehighlight();
    const QTextBlock comment = document.textDocument()->firstBlock();
    QCOMPARE(styleAt(comment, comment.text().indexOf(QStringLiteral("real"))), styleValue(SyntaxStyle::Comment));
    const QTextBlock directive = comment.next();
    QCOMPARE(styleAt(directive, 0), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(styleAt(directive, directive.text().indexOf(QStringLiteral("real"))), styleValue(SyntaxStyle::String));
    QCOMPARE(styleAt(directive, directive.text().indexOf(QStringLiteral("norm2"))), styleValue(SyntaxStyle::String));
    const QTextBlock declarations = directive.next();
    QCOMPARE(styleAt(declarations, 0), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(styleAt(declarations, declarations.text().indexOf(QStringLiteral("count"))), 0);
    QCOMPARE(styleAt(declarations, declarations.text().indexOf(QStringLiteral("10"))), styleValue(SyntaxStyle::Number));
    QCOMPARE(styleAt(declarations, declarations.text().indexOf(QStringLiteral(".5"))), styleValue(SyntaxStyle::Number));
    QCOMPARE(styleAt(declarations, declarations.text().indexOf(QLatin1Char('='))), styleValue(SyntaxStyle::Operator));
    const QTextBlock future = declarations.next();
    QCOMPARE(styleAt(future, future.text().indexOf(QStringLiteral("plot"))), styleValue(SyntaxStyle::Builtin));
    for (const QString& name : {QStringLiteral("for"),QStringLiteral("function")})
        QCOMPARE(styleAt(future, future.text().indexOf(name)), styleValue(SyntaxStyle::Keyword));
    for (const QString& name : {QStringLiteral("simulate"),
                               QStringLiteral("search"), QStringLiteral("realistic"),
                               QStringLiteral("I"), QStringLiteral("user_name")})
        QCOMPARE(styleAt(future, future.text().indexOf(name)), 0);
    QCOMPARE(document.text(), source);
}

void DocumentTests::highlightingPropagatesEditedBlockCommentState() {
    Document document;
    document.initializeNew(QStringLiteral("%{\nreal hidden = i;\n%}\nreal visible = 2;\n"));
    EditorWidget editor;
    editor.setDocument(document.textDocument());
    editor.syntaxHighlighter()->rehighlight();
    QCoreApplication::processEvents();
    QCOMPARE(styleAt(document.textDocument()->findBlockByNumber(1), 0), styleValue(SyntaxStyle::Comment));
    QCOMPARE(styleAt(document.textDocument()->findBlockByNumber(3), 0), styleValue(SyntaxStyle::Keyword));
    QTextCursor closing(document.textDocument()->findBlockByNumber(2));
    closing.select(QTextCursor::LineUnderCursor);
    closing.insertText(QStringLiteral("% ordinary text inside the block"));
    QTRY_COMPARE(styleAt(document.textDocument()->findBlockByNumber(3), 0), styleValue(SyntaxStyle::Comment));
    closing = QTextCursor(document.textDocument()->findBlockByNumber(2));
    closing.select(QTextCursor::LineUnderCursor);
    closing.insertText(QStringLiteral("%}"));
    QTRY_COMPARE(styleAt(document.textDocument()->findBlockByNumber(3), 0), styleValue(SyntaxStyle::Keyword));
}

void DocumentTests::highlightingHandlesUnfinishedSource() {
    Document document;
    document.initializeNew(QStringLiteral("pentarnion p=(i*\nuse_algebra(\"unfinished real\n% unfinished comment"));
    EditorWidget editor;
    editor.setDocument(document.textDocument());
    editor.syntaxHighlighter()->rehighlight();
    QCOMPARE(styleAt(document.textDocument()->firstBlock(), 0), styleValue(SyntaxStyle::Keyword));
    QCOMPARE(styleAt(document.textDocument()->findBlockByNumber(1), 13), styleValue(SyntaxStyle::String));
    QCOMPARE(styleAt(document.textDocument()->findBlockByNumber(2), 2), styleValue(SyntaxStyle::Comment));
}

void DocumentTests::syntaxHighlightingVisualSample() {
    EditorWidget editor;
    QPalette palette = editor.palette();
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#202020")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f2f4f7")));
    editor.setPalette(palette);
    editor.setPlainText(QString::fromUtf8(
        "% Pentarnion Studio — shared-lexer syntax highlighting 🙂\n"
        "use_algebra(\"A_gamma\", gamma=1);\n"
        "\n"
        "real strength = 0.25;\n"
        "integer count = 3;\n"
        "pentarnion p = 1 + 2*i - j + ell;\n"
        "pentarnion q = pent([0, 1, -1, 0, 1]);\n"
        "pentarnion reciprocal = inv(q);\n"
        "real magnitude_squared = norm2(p);\n"
        "integer range exponents = 1:1:5;\n"
        "\n"
        "%{\n"
        "  Comments remain comments: real, pentarnion, inv(i)\n"
        "%}\n"));
    editor.resize(980, 500);
    editor.show();
    editor.syntaxHighlighter()->rehighlight();
    QCoreApplication::processEvents();
    QCOMPARE(styleAt(editor.document()->findBlockByNumber(3), 0), styleValue(SyntaxStyle::Keyword));
    const QString directory = qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(editor.grab().save(QDir(directory).filePath(QStringLiteral("pentarnion-syntax-highlighting.png")), "PNG"));
}

QTEST_MAIN(DocumentTests)
#include "document_tests.moc"
