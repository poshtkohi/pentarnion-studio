#include "offscreen_test_bootstrap.hpp"
#include "document.hpp"
#include "editor_widget.hpp"
#include "main_window.hpp"
#include "command_window.hpp"
#include "figure_manager.hpp"
#include "ide/desktop/studio_workspace_layout.hpp"
#include "project_service.hpp"
#include "source_printing.hpp"
#include "syntax_highlighter.hpp"

#include <QAction>
#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDateTime>
#include <QDialog>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QPushButton>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPageSize>
#include <QPageLayout>
#include <QPageRanges>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <memory>
#include <algorithm>

using namespace penta;

namespace penta {
// Exercise recovery without user settings, user recovery files or global path
// overrides. Production callers continue using the unchanged public interface.
class MainWindowIdleIoTestAccess {
public:
    static void enableRecovery(MainWindow& window, const QString& directory) {
        window.m_recovery = RecoveryStore(directory);
        window.m_persistenceEnabled = true;
        window.configureAutosave(false);
        window.m_recoveryTimer->stop();
    }
    static void snapshot(MainWindow& window) { window.snapshotRecovery(); }
    static QString recoveryPath(const MainWindow& window) {
        return QDir(window.m_recovery.directory()).filePath(window.m_documents.front().recoveryId + QStringLiteral(".json"));
    }
    static int checkpointCount(const MainWindow& window) { return static_cast<int>(window.m_recoveryCheckpoints.size()); }
    static void refreshWatchers(MainWindow& window) { window.refreshWatchers(); }
    static void updateUi(MainWindow& window) { window.updateUi(); }
    static void setProjectContext(MainWindow& window, double gamma, const QString& version) {
        window.m_project->gamma = gamma;
        window.m_project->languageVersion = version;
        window.updateUi();
    }
    static bool setCachedDisplayGamma(MainWindow& window, double gamma) {
        const auto id = window.m_documents[window.m_tabs->currentIndex()].id;
        auto found = window.m_defaultDisplayContexts.find(id);
        if (found == window.m_defaultDisplayContexts.end()) return false;
        found->gamma = gamma;
        return true;
    }
    static double cachedDisplayGamma(const MainWindow& window) {
        return window.m_defaultDisplayContexts.value(window.m_documents[window.m_tabs->currentIndex()].id).gamma;
    }
    static QString cachedDisplayPath(const MainWindow& window) {
        return window.m_defaultDisplayContexts.value(window.m_documents[window.m_tabs->currentIndex()].id).key.path;
    }
    static QString cachedDisplayVersion(const MainWindow& window) {
        return window.m_defaultDisplayContexts.value(window.m_documents[window.m_tabs->currentIndex()].id).version;
    }
    static double validatedSnapshotGamma(const MainWindow& window) {
        return window.snapshotForDocument(window.m_tabs->currentIndex()).gamma;
    }
};
}

namespace {
QByteArray readBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

void replaceEditorText(EditorWidget* editor, const QString& text) {
    editor->selectAll();
    editor->insertPlainText(text);
}

void answerDirtyDialog(int answer) {
    QTimer::singleShot(0, [answer] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* question = qobject_cast<QMessageBox*>(widget);
                    question && question->objectName() == QStringLiteral("dirtyClosePrompt")) {
                question->done(answer);
                return;
            }
    });
}
} // namespace

class GuiTests final : public QObject {
    Q_OBJECT
private slots:
    void createEditSaveReopenTwoFiles();
    void closeHighlightedTabsWithExplorerPresent();
    void invalidProjectKeepsCurrentBuffers();
    void binaryInitialSourceKeepsCurrentProject();
    void cancellingDirtyCloseKeepsTabAndText();
    void cancellingProjectSwitchKeepsAllTabs();
    void cancelledSecondPromptRetainsFirstDiscardedBuffer();
    void canonicalOpenDoesNotDuplicateWritableBuffer();
    void renameOpenDirtySourceKeepsItsEdits();
    void removeFromProjectPreservesDiskFile();
    void failedSaveLeavesDirtyBufferIntact();
    void saveAsRegistersProjectCopyAndKeepsOriginal();
    void saveAsRejectsAnotherOpenBuffer();
    void saveAsRejectsNonportableProjectFilenameBeforeWriting();
    void reopeningSameProjectAfterSaveUsesLatestText();
    void externalChangeKeepProtectsFormerlyCleanBuffer_data();
    void externalChangeKeepProtectsFormerlyCleanBuffer();
    void removeMissingSourceKeepsOtherFilesAndRetainedBuffer();
    void studioBrandingAndAuthorHelp();
    void shellHasInterpreterButNoNativeOrOdeActions();
    void runSavesCurrentBufferBeforeEvaluation();
    void staticErrorPreventsEveryStatement();
    void runtimeErrorRetainsEarlierDisplayOutput();
    void unicodeDiagnosticNavigatesUtf16Editor();
    void projectCheckUsesIndependentUnsavedScopes();
    void legacyProjectRequiresExplicitUpgrade();
    void commandWindowSharesVariablesAndHistory();
    void fileWorkspaceAndGammaAreAvailableToCommands();
    void projectSwitchClearsCommandWorkspace();
    void staleRunCannotPublishOutputOrVariables();
    void importsResolveFromSourceAndProjectConsole();
    void dirtyImportedSourceCannotPublishOlderResults();
    void stopCancelsWork();
    void closingRunningDocumentIsSafe();
    void rejectedContextAndStaticErrorsKeepWorkspaceLabel();
    void blankDisplayOutputIsPreservedWithoutFabricatingQuietOutput();
    void consoleRunAndSelectedFileActionRunTheRequestedSource();
    void consoleRunErrorsStayInlineAndPathsAreLiteralOnly();
    void clcAndClearKeepTheirDistinctEffects();
    void commandWindowScrollPositionSurvivesNewOutput();
    void calculationsStayInsideOneMainWindow();
    void unifiedConsoleProtectsHistoryAndActivePrompt();
    void unifiedConsolePastesMultilineWithoutExecuting();
    void autosaveOnViewportLeaveWhileEditorRetainsFocus();
    void autosavePeriodicallySavesNamedButNeverInventsUnnamedPaths();
    void autosaveSkipsExternalChangesWithoutDialogs();
    void watcherBurstsAreCoalescedWithoutStarvingChanges();
    void replacedSourceWatchIsReinstalled();
    void unchangedRecoveryDoesNotRewriteAndChangesAreCheckpointed();
    void recoveryFailuresRetryAndForeignReplacementsStayUntouched();
    void cursorDisplayContextIsCachedAndExecutionStillValidates();
    void acceptedFiguresStayEmbeddedAndStaleFiguresAreDiscarded();
    void unnamedRunSaveAsCancelPreservesWorkspace();
    void unnamedRunSaveAsSuccessPersistsBeforeExecution();
    void unsafeDiskRunPreflightBlocksAllEntryPoints_data();
    void unsafeDiskRunPreflightBlocksAllEntryPoints();
    void nativeTiledNamedFiguresWhenRequested();
    void nativePaperOdeSubplotsWhenRequested();
    void nativeMatrixAndArkodeExamplesWhenRequested();
    void destructionWithoutCloseSafelyDisposesOpenDocuments();
    void printingSnapshotPreservesSyntaxAndEditorState();
    void printingSnapshotHasNoClipboardHtmlSizeFallback();
    void printingPdfPaginatesAndHonorsPageRange();
    void printingPreviewKeepsCapturedDocumentAndNeverSaves();
    void printingHighDpiTabFixture();
    void printActionAlwaysShowsRenderedPreview();
    void projectFoldersStayCollapsedUntilOpened();
    void consoleDiagnosticsNavigateUnicodeAndRejectStale();
    void consoleDiagnosticsNavigateRuntimeAndMultipleFiles();
    void consoleDiagnosticLinksProtectInputAndKeyboard();
};

void GuiTests::printingSnapshotPreservesSyntaxAndEditorState() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),"Printing",&error),qPrintable(error));window.configureAutosave(false);
    auto* editor=window.currentEditor();auto* document=window.currentDocument();const auto originalDisk=readBytes(document->path());
    const QString source=QString::fromUtf8("% Unicode λ 🙂 and\tindentation\n\tpentarnion p = i + ell;\nreal x = sin(0.5);\ndisplay(\"coloured string\");\n");
    replaceEditorText(editor,source);editor->setFontPointSize(13);
    QTextCursor selection=editor->textCursor();selection.setPosition(2);selection.setPosition(15,QTextCursor::KeepAnchor);editor->setTextCursor(selection);
    const auto clipboardBefore=QApplication::clipboard()->text();
    const quint64 revision=document->revision();const int undo=editor->document()->availableUndoSteps();const bool dirty=document->isModified();
    const auto raw=editor->document()->toRawText();auto snapshot=window.currentSourcePrintSnapshot(&error);QVERIFY2(snapshot,qPrintable(error));
    QVERIFY(snapshot->toRawText()==raw);QCOMPARE(snapshot->defaultFont().family(),editor->font().family());
    QCOMPARE(snapshot->defaultFont().pointSizeF(),editor->font().pointSizeF());QCOMPARE(snapshot->defaultTextOption().tabStopDistance(),editor->tabStopDistance());
    auto check=[&](const QString& token,SyntaxStyle style){const int position=source.indexOf(token);QVERIFY(position>=0);
        QTextCursor at(snapshot.get());at.setPosition(position);at.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
        const auto expected=SyntaxHighlighter::formatForStyle(style);QCOMPARE(at.charFormat().foreground().color(),expected.foreground().color());
        if(expected.hasProperty(QTextFormat::FontWeight))QCOMPARE(at.charFormat().fontWeight(),expected.fontWeight());};
    check("%",SyntaxStyle::Comment);check("pentarnion",SyntaxStyle::Keyword);check("ell",SyntaxStyle::Basis);
    check("sin",SyntaxStyle::Builtin);check("0.5",SyntaxStyle::Number);check("\"coloured",SyntaxStyle::String);
    QCOMPARE(document->revision(),revision);QCOMPARE(document->isModified(),dirty);QCOMPARE(editor->document()->availableUndoSteps(),undo);
    QCOMPARE(editor->textCursor().position(),selection.position());QCOMPARE(editor->textCursor().anchor(),selection.anchor());
    QCOMPARE(QApplication::clipboard()->text(),clipboardBefore);QCOMPARE(readBytes(document->path()),originalDisk);
    replaceEditorText(editor,QStringLiteral("real later = 3;"));QVERIFY(snapshot->toRawText()==raw);
    QVERIFY2(window.saveAllDocuments(&error),qPrintable(error));QVERIFY(window.close());
}

void GuiTests::printingSnapshotHasNoClipboardHtmlSizeFallback() {
    QTextDocument source;const QString large=QString(4*1024*1024+64,QLatin1Char('x'))+QStringLiteral("\nFINAL_SENTINEL");source.setPlainText(large);
    QTextLayout::FormatRange range;range.start=0;range.length=large.size()-15;range.format.setForeground(QColor("#008000"));range.format.setFontItalic(true);
    source.begin().layout()->setFormats({range});QFont font=QFontDatabase::systemFont(QFontDatabase::FixedFont);font.setPointSize(11);
    QString error;auto snapshot=createSourcePrintSnapshot(source,font,"large.p",&error);QVERIFY2(snapshot,qPrintable(error));
    QVERIFY(snapshot->toPlainText()==large);QTextCursor at(snapshot.get());at.setPosition(4*1024*1024+1);at.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
    QCOMPARE(at.charFormat().foreground().color(),QColor("#008000"));QVERIFY(at.charFormat().fontItalic());QVERIFY(snapshot->toPlainText().endsWith("FINAL_SENTINEL"));
}

void GuiTests::printingPdfPaginatesAndHonorsPageRange() {
    EditorWidget editor;QString source=QString::fromUtf8("% Printing λ — full buffer\n\treal first = sin(0.5);\n");
    for(int line=0;line<150;++line)source+=QStringLiteral("real value_%1 = %1.25; %% source line %1\n").arg(line);
    source+=QStringLiteral("% LONG ")+QString(1600,QLatin1Char('x'))+QStringLiteral(" END_LONG\n% FINAL_SENTINEL Ω\n");
    editor.setPlainText(source);editor.syntaxHighlighter()->rehighlight();QString error;
    auto snapshot=createSourcePrintSnapshot(*editor.document(),editor.font(),QString::fromUtf8("colours_λ.p"),&error);QVERIFY2(snapshot,qPrintable(error));
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QPrinter printer(QPrinter::HighResolution);printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setResolution(300);printer.setPageSize(QPageSize(QPageSize::A5));printer.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
    printer.setColorMode(QPrinter::Color);const QString path=temporary.filePath("source.pdf");printer.setOutputFileName(path);
    const int pages=sourcePrintPageCount(*snapshot,printer,&error);QVERIFY2(pages>=3,qPrintable(error));
    const auto before=snapshot->toRawText();QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));QVERIFY(snapshot->toRawText()==before);
    auto pdfPages=[](const QByteArray& pdf){int count=0;auto matches=QRegularExpression(QStringLiteral("/Type\\s*/Page\\b")).globalMatch(QString::fromLatin1(pdf));while(matches.hasNext()){matches.next();++count;}return count;};
    const auto pdf=readBytes(path);QVERIFY(pdf.startsWith("%PDF-"));QCOMPARE(pdfPages(pdf),pages);QCOMPARE(printer.colorMode(),QPrinter::Color);
    printer.setOutputFileName(temporary.filePath("page-two.pdf"));printer.setPrintRange(QPrinter::PageRange);printer.setFromTo(2,2);printer.setColorMode(QPrinter::GrayScale);
    QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));QCOMPARE(pdfPages(readBytes(printer.outputFileName())),1);QCOMPARE(printer.colorMode(),QPrinter::GrayScale);
    QPageRanges ranges;ranges.addPage(1);ranges.addPage(3);printer.setPageRanges(ranges);printer.setPrintRange(QPrinter::PageRange);
    printer.setOutputFileName(temporary.filePath("pages-one-and-three.pdf"));
    QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));QCOMPARE(pdfPages(readBytes(printer.outputFileName())),2);
    const int start=source.indexOf("real first"),end=source.indexOf("real value_1");
    auto selected=createSourcePrintSelectionSnapshot(*snapshot,start,end,&error);QVERIFY2(selected,qPrintable(error));QCOMPARE(selected->toPlainText(),source.mid(start,end-start));
    const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if(!artifacts.isEmpty()){
        QVERIFY(QDir().mkpath(artifacts));printer.setOutputFileName(QDir(artifacts).filePath("syntax-colours-source.pdf"));printer.setPrintRange(QPrinter::AllPages);printer.setColorMode(QPrinter::Color);
        QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));QVERIFY(writeBytes(QDir(artifacts).filePath("syntax-colours-source.p"),source.toUtf8()));
    }
}

void GuiTests::printingHighDpiTabFixture() {
    QFont font(QStringLiteral("Courier New"));font.setStyleHint(QFont::Monospace);font.setPointSize(12);
    QImage metrics(1,1,QImage::Format_ARGB32_Premultiplied);metrics.setDotsPerMeterX(qRound(144/0.0254));metrics.setDotsPerMeterY(qRound(144/0.0254));
    QTextDocument source;source.setDefaultFont(font);source.documentLayout()->setPaintDevice(&metrics);
    source.setPlainText(QStringLiteral("\tB_TAB\n    B_SPACE\n"));
    SourcePrintSettings settings;settings.font=font;settings.title=QStringLiteral("tabs_144.p");settings.logicalDpi=144;
    settings.tabStopDistance=4*QFontMetricsF(font,&metrics).horizontalAdvance(QLatin1Char(' '));
    QString error;auto snapshot=createSourcePrintSnapshot(source,settings,&error);QVERIFY2(snapshot,qPrintable(error));
    QCOMPARE(snapshot->defaultTextOption().tabStopDistance(),settings.tabStopDistance);QVERIFY(snapshot->toRawText()==source.toRawText());
    QTemporaryDir temporary;QVERIFY(temporary.isValid());const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if(!artifacts.isEmpty())QVERIFY(QDir().mkpath(artifacts));
    const QString path=QDir(artifacts.isEmpty()?temporary.path():artifacts).filePath("source-tabs-144dpi.pdf");
    QPrinter printer(QPrinter::HighResolution);printer.setOutputFormat(QPrinter::PdfFormat);printer.setOutputFileName(path);printer.setResolution(300);
    printer.setPageSize(QPageSize(QPageSize::A5));printer.setPageMargins(QMarginsF(12,12,12,12),QPageLayout::Millimeter);
    QCOMPARE(sourcePrintPageCount(*snapshot,printer,&error),1);QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));
    QVERIFY(readBytes(path).startsWith("%PDF-"));
}

void GuiTests::printingPreviewKeepsCapturedDocumentAndNeverSaves() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    auto* print=window.findChild<QAction*>("filePrint");auto* preview=window.findChild<QAction*>("filePrintPreview");auto* setup=window.findChild<QAction*>("filePageSetup");
    QVERIFY(print&&preview&&setup);QCOMPARE(print->shortcut(),QKeySequence(QKeySequence::Print));QVERIFY(!print->isEnabled()&&!preview->isEnabled()&&!setup->isEnabled());
    QVERIFY2(window.createProjectAt(temporary.path(),"Preview",&error),qPrintable(error));const QString firstPath=window.currentDocument()->path();
    QVERIFY(window.addSource("second.p",&error));const QString secondPath=window.currentDocument()->path();
    const auto firstDisk=readBytes(firstPath),secondDisk=readBytes(secondPath);QVERIFY(window.openSourcePath(firstPath,&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000); // Drain the project's queued workspace reset before the printing baseline.
    const QString first=QString::fromUtf8("% original λ\nreal first_unsaved = sin(0.25);\n");replaceEditorText(window.currentEditor(),first);
    const auto raw=window.currentEditor()->document()->toRawText();const auto console=window.resultsText();
    window.configureAutosave(true,60000);QSignalSpy execution(&window,&MainWindow::workFinished);
    bool saw=false,immutable=false,coloured=false,painted=false;QTimer timer;timer.setInterval(30);int attempts=0;
    connect(&timer,&QTimer::timeout,&window,[&]{
        auto* dialog=window.findChild<QDialog*>("sourcePrintPreview");if(!dialog){if(++attempts>100)timer.stop();return;}
        auto* snapshot=dialog->findChild<QTextDocument*>("sourcePrintSnapshot");if(!snapshot)return;saw=true;
        const auto content=snapshot->toRawText();
        QTextCursor keyword(snapshot);keyword.setPosition(first.indexOf("real"));keyword.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
        coloured=keyword.charFormat().foreground().color()==SyntaxHighlighter::formatForStyle(SyntaxStyle::Keyword).foreground().color();
        auto* widget=dialog->findChild<QPrintPreviewWidget*>();painted=widget&&widget->pageCount()>0;
        window.openSourcePath(secondPath,&error);replaceEditorText(window.currentEditor(),QStringLiteral("real second_unsaved = 9;\n"));
        immutable=content==raw&&snapshot->toRawText()==raw;dialog->reject();
    });
    timer.start();preview->trigger();timer.stop();QVERIFY(saw&&immutable&&coloured&&painted);
    QTest::qWait(650);QCOMPARE(readBytes(firstPath),firstDisk);QCOMPARE(readBytes(secondPath),secondDisk);
    QVERIFY(std::none_of(execution.cbegin(),execution.cend(),[](const auto& completion){return completion.at(0).toBool();}));
    QVERIFY(!window.isWorking());QCOMPARE(window.resultsText(),console);
    window.configureAutosave(false);QVERIFY2(window.saveAllDocuments(&error),qPrintable(error));QVERIFY(window.close());
}

void GuiTests::printActionAlwaysShowsRenderedPreview() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),"Print action",&error),qPrintable(error));window.configureAutosave(false);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    QString source;for(int n=0;n<140;++n)source+=QStringLiteral("real sample_%1 = sin(0.5); %% green comment\n").arg(n);
    replaceEditorText(window.currentEditor(),source);const auto bytes=readBytes(window.currentDocument()->path());
    bool rendered=false,hasSettings=false,selection=false;QTimer timer;timer.setInterval(30);
    connect(&timer,&QTimer::timeout,&window,[&]{auto* dialog=window.findChild<QDialog*>("sourcePrintDialog");if(!dialog)return;
        auto* preview=dialog->findChild<QPrintPreviewWidget*>("sourcePrintPreviewWidget");if(!preview||preview->pageCount()<2)return;
        rendered=true;hasSettings=dialog->findChild<QComboBox*>("sourcePrintDestination")&&dialog->findChild<QPushButton*>("sourcePrintSubmit");
        auto* snapshot=dialog->findChild<QTextDocument*>("sourcePrintSnapshot");selection=snapshot&&snapshot->toPlainText()==source;
        const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");if(!artifacts.isEmpty()){QDir().mkpath(artifacts);dialog->grab().save(QDir(artifacts).filePath("print-preview.png"));}
        dialog->reject();});
    QTimer watchdog;watchdog.setSingleShot(true);connect(&watchdog,&QTimer::timeout,&window,[&]{if(auto* dialog=window.findChild<QDialog*>("sourcePrintDialog"))dialog->reject();});
    timer.start();watchdog.start(10000);window.findChild<QAction*>("filePrint")->trigger();timer.stop();watchdog.stop();
    QVERIFY(rendered&&hasSettings&&selection);QCOMPARE(readBytes(window.currentDocument()->path()),bytes);QVERIFY(window.currentDocument()->isModified());
    QVERIFY2(window.saveAllDocuments(&error),qPrintable(error));QVERIFY(window.close());
}

void GuiTests::projectFoldersStayCollapsedUntilOpened() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),"Folder state",&error));window.configureAutosave(false);
    const QDir project(window.activeProject()->rootPath);QVERIFY(project.mkpath(".penta-cache/odes/generated"));QVERIFY(project.mkpath("data/nested"));
    QVERIFY(window.addSource("data/first.p",&error));auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);
    auto folder=[tree](const QString& name)->QTreeWidgetItem*{for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole+1).toBool()&&(*it)->data(0,Qt::UserRole).toString()==name)return *it;return nullptr;};
    QVERIFY(!folder(".penta-cache")&&!folder(".penta-cache/odes")&&folder("data"));
    QVERIFY(!folder("data")->isExpanded());
    folder("data")->setExpanded(true);QVERIFY(window.addSource("second.p",&error));
    QVERIFY(folder("data")->isExpanded());QVERIFY(!folder(".penta-cache"));
    replaceEditorText(window.currentEditor(),"display(1);\n");QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QVERIFY(window.runCurrentDocument(&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    QVERIFY(folder("data")->isExpanded());QVERIFY(!folder(".penta-cache"));
    QVERIFY(window.createProjectAt(temporary.path(),"Another project",&error));QVERIFY(window.close());
}

void GuiTests::createEditSaveReopenTwoFiles() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    QString error, manifest, mainPath, secondPath;
    const QString firstText = QString::fromUtf8("% Unicode source: λ 🙂\nreal x = 0.25;\n");
    const QString secondText = QStringLiteral("pentarnion p = (i*j)*j;\n");
    {
        MainWindow window(false);
        QVERIFY2(window.createProjectAt(temporary.path(), QString::fromUtf8("Research Δ"), &error), qPrintable(error));
        QVERIFY(window.activeProject());
        QCOMPARE(window.documentCount(), 1);
        manifest = window.activeProject()->manifestPath;
        mainPath = window.currentDocument()->path();
        replaceEditorText(window.currentEditor(), firstText);
        QVERIFY(window.currentDocument()->isModified());
        QVERIFY2(window.saveCurrentDocument(&error), qPrintable(error));
        QVERIFY(!window.currentDocument()->isModified());
        QVERIFY2(window.addSource(QStringLiteral("models/second.p"), &error), qPrintable(error));
        secondPath = window.currentDocument()->path();
        replaceEditorText(window.currentEditor(), secondText);
        QVERIFY2(window.saveAllDocuments(&error), qPrintable(error));
        QCOMPARE(window.documentCount(), 2);
        QCOMPARE(window.activeProject()->sources.size(), 2);
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QVERIFY(window.close());
    }
    QCOMPARE(readBytes(mainPath), firstText.toUtf8());
    QCOMPARE(readBytes(secondPath), secondText.toUtf8());
    MainWindow reopened(false);
    QVERIFY2(reopened.openProjectPath(manifest, &error), qPrintable(error));
    QVERIFY2(reopened.openSourcePath(mainPath, &error), qPrintable(error));
    QCOMPARE(reopened.currentDocument()->text(), firstText);
    QVERIFY2(reopened.openSourcePath(secondPath, &error), qPrintable(error));
    QCOMPARE(reopened.currentDocument()->text(), secondText);
    QCOMPARE(reopened.documentCount(), 2);
    QVERIFY(reopened.close());
}

void GuiTests::closeHighlightedTabsWithExplorerPresent() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());QString error;
    MainWindow window(false,nullptr,false);
    QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("researchExplorerDock")));
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("Close lifetime"),&error),qPrintable(error));
    for(int iteration=0;iteration<3;++iteration){
        replaceEditorText(window.currentEditor(),QStringLiteral("real angle=sin(0.5);\n"));
        QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
        const auto path=window.currentDocument()->path();
        QPointer<EditorWidget> editor=window.currentEditor();
        QPointer<Document> document=window.currentDocument();
        editor->syntaxHighlighter()->rehighlight();
        auto* close=window.findChild<QAction*>(QStringLiteral("fileCloseTab"));
        QVERIFY(close);close->trigger();
        QVERIFY(editor.isNull());QVERIFY(document.isNull());
        QCoreApplication::processEvents();
        QCOMPARE(window.documentCount(),0);
        QVERIFY2(window.openSourcePath(path,&error),qPrintable(error));
        QCOMPARE(window.currentDocument()->text(),QStringLiteral("real angle=sin(0.5);\n"));
    }
    QVERIFY(window.close());
}

void GuiTests::invalidProjectKeepsCurrentBuffers() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("Safe"), &error), qPrintable(error));
    const QString activeManifest = window.activeProject()->manifestPath;
    auto* document = window.currentDocument();
    replaceEditorText(window.currentEditor(), QStringLiteral("real preserve_me = 3;\n"));
    const QString badManifest = QDir(temporary.path()).filePath(QStringLiteral("broken.pprj"));
    QVERIFY(writeBytes(badManifest, QByteArrayLiteral("{\"schema_version\":999}")));
    QVERIFY(!window.openProjectPath(badManifest, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(window.activeProject()->manifestPath, activeManifest);
    QCOMPARE(window.currentDocument(), document);
    QCOMPARE(document->text(), QStringLiteral("real preserve_me = 3;\n"));
    QVERIFY(document->isModified());
    QVERIFY2(window.saveCurrentDocument(&error), qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::cancellingDirtyCloseKeepsTabAndText() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("CancelClose"), &error));
    const QString path = window.currentDocument()->path();
    const QByteArray original = readBytes(path);
    replaceEditorText(window.currentEditor(), QStringLiteral("real unsaved = 7;\n"));
    answerDirtyDialog(QMessageBox::Cancel);
    QVERIFY(!window.close());
    QCOMPARE(window.documentCount(), 1);
    QCOMPARE(window.currentDocument()->text(), QStringLiteral("real unsaved = 7;\n"));
    QVERIFY(window.currentDocument()->isModified());
    QCOMPARE(readBytes(path), original);
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::binaryInitialSourceKeepsCurrentProject() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Readable"), &error));
    const QString originalManifest = window.activeProject()->manifestPath;
    auto* originalDocument = window.currentDocument();
    replaceEditorText(window.currentEditor(), QStringLiteral("real valuable_unsaved_edit = 4;\n"));
    Project other;
    QVERIFY(ProjectService::create(temporary.path(), QStringLiteral("Binary"), other, &error));
    QVERIFY(writeBytes(QDir(other.rootPath).filePath(QStringLiteral("main.p")), QByteArray("\x00\xff", 2)));
    // No dirty-close prompt should appear: candidate source validation precedes switching.
    QVERIFY(!window.openProjectPath(other.manifestPath, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(window.activeProject()->manifestPath, originalManifest);
    QCOMPARE(window.currentDocument(), originalDocument);
    QVERIFY(originalDocument->isModified());
    QCOMPARE(originalDocument->text(), QStringLiteral("real valuable_unsaved_edit = 4;\n"));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::cancellingProjectSwitchKeepsAllTabs() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Original"), &error));
    const QString manifest = window.activeProject()->manifestPath;
    replaceEditorText(window.currentEditor(), QStringLiteral("real pending = 1;\n"));
    Project candidate;
    QVERIFY(ProjectService::create(temporary.path(), QStringLiteral("Other"), candidate, &error));
    answerDirtyDialog(QMessageBox::Cancel);
    QVERIFY(!window.openProjectPath(candidate.manifestPath, &error));
    QCOMPARE(window.activeProject()->manifestPath, manifest);
    QCOMPARE(window.currentDocument()->text(), QStringLiteral("real pending = 1;\n"));
    QVERIFY(window.currentDocument()->isModified());
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::cancelledSecondPromptRetainsFirstDiscardedBuffer() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("TwoBuffers"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("real first_edit = 1;\n"));
    Document* first = window.currentDocument();
    QVERIFY(window.addSource(QStringLiteral("second.p"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("real second_edit = 2;\n"));
    Project other;
    QVERIFY(ProjectService::create(temporary.path(), QStringLiteral("Destination"), other, &error));
    int answered = 0;
    QTimer responder;
    responder.setInterval(1);
    connect(&responder, &QTimer::timeout, &window, [&answered] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* question = qobject_cast<QMessageBox*>(widget);
                    question && question->isVisible() && question->objectName() == QStringLiteral("dirtyClosePrompt")) {
                question->done(answered++ == 0 ? QMessageBox::Discard : QMessageBox::Cancel);
                return;
            }
    });
    responder.start();
    QVERIFY(!window.openProjectPath(other.manifestPath, &error));
    responder.stop();
    QCOMPARE(answered, 2);
    QCOMPARE(window.documentCount(), 2);
    QCOMPARE(first->text(), QStringLiteral("real first_edit = 1;\n"));
    QVERIFY(first->isModified());
    QVERIFY(window.saveAllDocuments(&error));
    QVERIFY(window.close());
}

void GuiTests::canonicalOpenDoesNotDuplicateWritableBuffer() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Canonical"), &error));
    auto* first = window.currentDocument();
    const QString alias = QDir(window.activeProject()->rootPath).filePath(QStringLiteral("./main.p"));
    QVERIFY(window.openSourcePath(alias, &error));
    QCOMPARE(window.documentCount(), 1);
    QCOMPARE(window.currentDocument(), first);
    QVERIFY(window.close());
}

void GuiTests::renameOpenDirtySourceKeepsItsEdits() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Rename"), &error));
    QVERIFY(window.addSource(QStringLiteral("models/before.p"), &error));
    const QString oldPath = window.currentDocument()->path();
    auto* document = window.currentDocument();
    replaceEditorText(window.currentEditor(), QStringLiteral("real retained = 6;\n"));
    QVERIFY2(window.renameProjectSource(QStringLiteral("models/before.p"),
                                       QStringLiteral("models/after.p"), &error), qPrintable(error));
    QCOMPARE(window.currentDocument(), document);
    QVERIFY(document->isModified());
    QVERIFY(!QFileInfo::exists(oldPath));
    QVERIFY(document->path().endsWith(QStringLiteral("models/after.p")));
    QCOMPARE(document->text(), QStringLiteral("real retained = 6;\n"));
    QVERIFY2(window.saveCurrentDocument(&error), qPrintable(error));
    QCOMPARE(readBytes(document->path()), QByteArrayLiteral("real retained = 6;\n"));
    QVERIFY(window.close());
}

void GuiTests::removeFromProjectPreservesDiskFile() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Remove"), &error));
    QVERIFY(window.addSource(QStringLiteral("keep.p"), &error));
    const QString path = window.currentDocument()->path();
    const QByteArray original = readBytes(path);
    QVERIFY2(window.removeProjectSource(QStringLiteral("keep.p"), false, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(readBytes(path), original);
    QVERIFY(!window.activeProject()->sources.contains(QStringLiteral("keep.p")));
    QCOMPARE(window.currentDocument()->path(), path);
    QVERIFY(window.close());
}

void GuiTests::failedSaveLeavesDirtyBufferIntact() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Failure"), &error));
    auto* document = window.currentDocument();
    const QString path = document->path();
    replaceEditorText(window.currentEditor(), QStringLiteral("real buffer_wins_only_with_permission = 9;\n"));
    // External change is an authentic failed-save condition, independent of root permissions.
    QVERIFY(writeBytes(path, QByteArrayLiteral("% Changed in another editor\n")));
    QVERIFY(!window.saveCurrentDocument(&error));
    QVERIFY(!error.isEmpty());
    QVERIFY(document->isModified());
    QCOMPARE(document->text(), QStringLiteral("real buffer_wins_only_with_permission = 9;\n"));
    QCOMPARE(readBytes(path), QByteArrayLiteral("% Changed in another editor\n"));
    // End this isolated test by deliberately reloading, rather than invoking modal conflict UI.
    QVERIFY(document->reload(&error));
    QVERIFY(window.close());
}

void GuiTests::shellHasInterpreterButNoNativeOrOdeActions() {
    MainWindow window(false);
    QVERIFY(window.findChild<QAction*>(QStringLiteral("fileNewProject")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("fileOpenProject")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("fileSave")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("helpLanguageReference")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("checkFile")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("runFile")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("stopExecution")));
    QVERIFY(window.commandWindow());
    for (const auto* item : window.findChildren<QAction*>()) {
        QString text = item->text().toLower(); text.remove('&');
        QVERIFY2(!text.contains(QStringLiteral("run script")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("compile script")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("ode")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("parallel")), qPrintable(text));
    }
    QVERIFY(window.close());
}

void GuiTests::saveAsRegistersProjectCopyAndKeepsOriginal() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Copy"), &error));
    const QString originalPath = window.currentDocument()->path();
    const QByteArray originalBytes = readBytes(originalPath);
    const QString destination = QDir(window.activeProject()->rootPath).filePath(QStringLiteral("copy.p"));
    replaceEditorText(window.currentEditor(), QStringLiteral("real copied = 10;\n"));
    QVERIFY2(window.saveCurrentDocumentAs(destination, &error), qPrintable(error));
    QCOMPARE(window.currentDocument()->path(), QDir::cleanPath(destination));
    QVERIFY(!window.currentDocument()->isModified());
    QCOMPARE(readBytes(originalPath), originalBytes);
    QCOMPARE(readBytes(destination), QByteArrayLiteral("real copied = 10;\n"));
    QVERIFY(window.activeProject()->sources.contains(QStringLiteral("main.p")));
    QVERIFY(window.activeProject()->sources.contains(QStringLiteral("copy.p")));
    Project reopened;
    QVERIFY(ProjectService::load(window.activeProject()->manifestPath, reopened, &error));
    QCOMPARE(reopened.sources.size(), 2);
    QVERIFY(window.close());
}

void GuiTests::saveAsRejectsAnotherOpenBuffer() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("NoCompetingWriters"), &error));
    const QString firstPath = window.currentDocument()->path();
    const QByteArray firstBytes = readBytes(firstPath);
    QVERIFY(window.addSource(QStringLiteral("second.p"), &error));
    const QString secondPath = window.currentDocument()->path();
    replaceEditorText(window.currentEditor(), QStringLiteral("real retain_second = 5;\n"));
    QVERIFY(!window.saveCurrentDocumentAs(firstPath, &error, true));
    QVERIFY(!error.isEmpty());
    QCOMPARE(window.currentDocument()->path(), secondPath);
    QVERIFY(window.currentDocument()->isModified());
    QCOMPARE(readBytes(firstPath), firstBytes);
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::saveAsRejectsNonportableProjectFilenameBeforeWriting() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Portable"), &error));
    const QString originalPath = window.currentDocument()->path();
    const QString forbidden = QDir(window.activeProject()->rootPath).filePath(QStringLiteral("CON.p"));
    replaceEditorText(window.currentEditor(), QStringLiteral("real safe_path = 2;\n"));
    QVERIFY(!window.saveCurrentDocumentAs(forbidden, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(window.currentDocument()->path(), originalPath);
    QVERIFY(window.currentDocument()->isModified());
    QVERIFY(!QFileInfo::exists(forbidden));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::reopeningSameProjectAfterSaveUsesLatestText() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Reopen"), &error));
    const QString manifest = window.activeProject()->manifestPath;
    const QString path = window.currentDocument()->path();
    const QString latest = QStringLiteral("real saved_during_switch = 123;\n");
    replaceEditorText(window.currentEditor(), latest);
    answerDirtyDialog(QMessageBox::Save);
    QVERIFY2(window.openProjectPath(manifest, &error), qPrintable(error));
    QCOMPARE(window.currentDocument()->text(), latest);
    QVERIFY(!window.currentDocument()->isModified());
    QCOMPARE(readBytes(path), latest.toUtf8());
    QVERIFY(window.close());
}

void GuiTests::externalChangeKeepProtectsFormerlyCleanBuffer_data() {
    QTest::addColumn<bool>("deleteExternally");
    QTest::newRow("external-modification") << false;
    QTest::newRow("external-deletion") << true;
}

void GuiTests::externalChangeKeepProtectsFormerlyCleanBuffer() {
    QFETCH(bool, deleteExternally);
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("RetainedCleanBuffer"), &error));
    auto* document = window.currentDocument();
    const QString path = document->path();
    const QString originalText = document->text();
    QVERIFY(!document->isModified());
    int prompted = 0;
    QTimer responder;
    responder.setInterval(10);
    connect(&responder, &QTimer::timeout, &window, [&prompted] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* question = qobject_cast<QMessageBox*>(widget);
                    question && question->isVisible() && question->objectName() == QStringLiteral("externalChangePrompt")) {
                ++prompted;
                question->reject(); // Dismissal has the same safe Keep Buffer result.
                return;
            }
    });
    responder.start();
    if (deleteExternally) QVERIFY(QFile::remove(path));
    else QVERIFY(writeBytes(path, QByteArrayLiteral("% Newer external source\n")));
    QTRY_COMPARE_WITH_TIMEOUT(prompted, 1, 4000);
    responder.stop();
    QCOMPARE(document->text(), originalText);
    QVERIFY(document->isModified());
    QVERIFY(document->hasExternalChange());
    QVERIFY(!window.saveCurrentDocument(&error));
    answerDirtyDialog(QMessageBox::Cancel);
    QVERIFY(!window.close());
    QCOMPARE(window.currentDocument(), document);
    QCOMPARE(document->text(), originalText);
    if (deleteExternally) {
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(document->save(&error, true)); // Explicitly recreate only for test cleanup.
    } else {
        QCOMPARE(readBytes(path), QByteArrayLiteral("% Newer external source\n"));
        QVERIFY(document->reload(&error));
    }
    QVERIFY(window.close());
}

void GuiTests::removeMissingSourceKeepsOtherFilesAndRetainedBuffer() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("MissingSource"), &error));
    const QString mainPath = window.currentDocument()->path();
    const QByteArray mainBytes = readBytes(mainPath);
    QVERIFY(window.addSource(QStringLiteral("missing.p"), &error));
    auto* retained = window.currentDocument();
    const QString missingPath = retained->path();
    const QString retainedText = retained->text();
    QVERIFY(QFile::remove(missingPath));
    // A metadata-only removal must also work before the watcher event arrives.
    QVERIFY2(window.removeProjectSource(QStringLiteral("missing.p"), false, &error), qPrintable(error));
    QVERIFY(!window.activeProject()->sources.contains(QStringLiteral("missing.p")));
    QCOMPARE(window.currentDocument(), retained);
    QCOMPARE(retained->text(), retainedText);
    QCOMPARE(readBytes(mainPath), mainBytes);
    Project reopened;
    QVERIFY(ProjectService::load(window.activeProject()->manifestPath, reopened, &error));
    QCOMPARE(reopened.sources, QStringList{QStringLiteral("main.p")});
    // Closing immediately must protect the old buffer even if the watcher is queued.
    answerDirtyDialog(QMessageBox::Cancel);
    QVERIFY(!window.close());
    QVERIFY(retained->isModified());
    QCOMPARE(retained->text(), retainedText);
    QVERIFY(retained->save(&error, true));
    QVERIFY(window.close());
}

void GuiTests::studioBrandingAndAuthorHelp() {
    MainWindow window(false);
    QVERIFY(window.windowTitle().contains(QStringLiteral("Pentarnion Studio v0.2")));
    QVERIFY(!window.windowTitle().contains(QStringLiteral("Workbench")));
    auto* aboutAction = window.findChild<QAction*>(QStringLiteral("helpAbout"));
    QVERIFY(aboutAction);
    QCOMPARE(aboutAction->text(), QStringLiteral("About Pentarnion Studio…"));
    aboutAction->trigger();
    auto* about = window.findChild<QDialog*>(QStringLiteral("aboutStudio"));
    QVERIFY(about);
    QVERIFY(about->isVisible());
    QVERIFY(!about->isModal());
    QCOMPARE(about->windowTitle(), QStringLiteral("About Pentarnion Studio v0.2"));
    QVERIFY(!window.windowIcon().isNull());
    auto* studioLogo=about->findChild<QLabel*>(QStringLiteral("aboutStudioLogo"));
    QVERIFY(studioLogo);
    QVERIFY(!studioLogo->pixmap().isNull());
    QCOMPARE(studioLogo->pixmap().deviceIndependentSize().toSize(),QSize(72,72));
    QVERIFY(studioLogo->accessibleName().contains(QStringLiteral("quaternion"),Qt::CaseInsensitive));
    auto labelText = [about](const char* name) {
        const auto* label = about->findChild<QLabel*>(QString::fromLatin1(name));
        if(!label)return QString();
        if(label->textFormat()!=Qt::RichText)return label->text();
        QTextDocument rendered;rendered.setHtml(label->text());return rendered.toPlainText();
    };
    QCOMPARE(labelText("aboutAuthor1Name"), QStringLiteral("Dr Alireza Poshtkohi"));
    QCOMPARE(labelText("aboutAuthor1Affiliation"), QStringLiteral("School of Physics, Engineering and Computer Science, University of Hertfordshire, Hatfield, Hertfordshire, United Kingdom"));
    QCOMPARE(labelText("aboutAuthor1Email"), QStringLiteral("a.poshtkohi@herts.ac.uk"));
    QCOMPARE(labelText("aboutAuthor2Name"), QString::fromUtf8("Dr Susanne Pumplün"));
    QCOMPARE(labelText("aboutAuthor2Affiliation"), QStringLiteral("School of Mathematical Sciences, University of Nottingham, Nottingham NG7 2RD, United Kingdom"));
    QCOMPARE(labelText("aboutAuthor2Email"), QStringLiteral("Susanne.Pumpluen@nottingham.ac.uk"));
    auto* aboutTabs=about->findChild<QTabWidget*>(QStringLiteral("aboutTabs"));
    QVERIFY(aboutTabs);QCOMPARE(aboutTabs->count(),5);QCOMPARE(aboutTabs->tabText(0),QStringLiteral("Authors"));
    QStringList pages;for(int index=0;index<aboutTabs->count();++index)pages.push_back(aboutTabs->tabText(index));
    for(const auto& page:{QStringLiteral("Overview"),QStringLiteral("Authors"),QStringLiteral("Libraries"),QStringLiteral("Licences"),QStringLiteral("Build")})QVERIFY(pages.contains(page));
    auto* overview=about->findChild<QTreeWidget*>(QStringLiteral("aboutOverview"));QVERIFY(overview);
    QString capabilities;
    for(int index=0;index<overview->topLevelItemCount();++index){
        const auto* row=overview->topLevelItem(index);QVERIFY(row->text(1).size()<100);
        capabilities+=row->text(0)+QLatin1Char(' ')+row->text(1)+QLatin1Char('\n');
    }
    QVERIFY(capabilities.contains(QStringLiteral("ODEs")));QVERIFY(capabilities.contains(QStringLiteral("PDEs")));
    QVERIFY(capabilities.contains(QStringLiteral("matrices")));QVERIFY(capabilities.contains(QStringLiteral("Command Window")));
    QVERIFY(capabilities.contains(QStringLiteral("2D/3D")));QVERIFY(capabilities.contains(QStringLiteral("Copilot")));
    QVERIFY(!about->findChild<QLabel*>(QStringLiteral("aboutCapabilities")));
    QVERIFY(labelText("aboutTagline").size()<80);
    QCOMPARE(labelText("aboutCopyright"), QString::fromUtf8("© 2026 Pentranion Team"));
    QVERIFY(labelText("aboutQtNotice").contains(QStringLiteral("Qt 6")));
    for (const auto* label : about->findChildren<QLabel*>())
        QVERIFY2(!label->text().contains(QStringLiteral("Workbench")), qPrintable(label->text()));
    for (const auto* item : window.findChildren<QAction*>())
        QVERIFY2(!item->text().contains(QStringLiteral("Workbench")), qPrintable(item->text()));
    const QString artifactDirectory = qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if (!artifactDirectory.isEmpty()) {
        QVERIFY2(QDir().mkpath(artifactDirectory), qPrintable(artifactDirectory));
        QApplication::processEvents();
        const QString artifactPath = QDir(artifactDirectory).filePath(QStringLiteral("about-studio.png"));
        QVERIFY2(about->grab().save(artifactPath, "PNG"), qPrintable(artifactPath));
    }
    about->close();
    QVERIFY(window.close());
}

void GuiTests::runSavesCurrentBufferBeforeEvaluation() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("UnsavedRun"), &error));
    const QString path = window.currentDocument()->path();
    QVERIFY(writeBytes(path, QByteArray("\xEF\xBB\xBF") + "% original\r\n"));
    QVERIFY(window.currentDocument()->reload(&error));
    const QString source = QStringLiteral("display(\"saved-output\", 2+3);\n"
        "display(coeff((i*j)*j,3), coeff(i*(j*j),1));\n");
    replaceEditorText(window.currentEditor(), source);
    QByteArray saved = source.toUtf8(); saved.replace("\n", "\r\n"); saved.prepend("\xEF\xBB\xBF");
    QVERIFY2(window.runCurrentDocument(&error), qPrintable(error));
    QCOMPARE(readBytes(path), saved); // Persistence completes before dispatch returns.
    QVERIFY(!window.currentDocument()->isModified());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().contains(QStringLiteral("saved-output 5")), qPrintable(window.resultsText()));
    QVERIFY2(window.resultsText().contains(QStringLiteral("0.5 -1")), qPrintable(window.resultsText()));
    QCOMPARE(window.problemCount(), 0);
    QCOMPARE(readBytes(path), saved);
    QVERIFY(!window.currentDocument()->isModified());
    const QString artifacts = qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) {
        QVERIFY(QDir().mkpath(artifacts));
        window.show(); window.resize(1180, 780); QApplication::processEvents();
        QVERIFY(window.grab().save(QDir(artifacts).filePath(QStringLiteral("studio-interpreter.png")), "PNG"));
    }
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::staticErrorPreventsEveryStatement() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("StaticError"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral(
        "display(\"must-not-print\"); real broken = missing_variable;\n"));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(!window.resultsText().contains(QStringLiteral("must-not-print")));
    QVERIFY(window.calculationStatus().contains(QStringLiteral("No statements")));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::runtimeErrorRetainsEarlierDisplayOutput() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("RuntimeError"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral(
        "display(\"before-failure\"); real zero=0; real failure=1/zero; display(\"after-failure\");\n"));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().contains(QStringLiteral("before-failure")), qPrintable(window.resultsText()));
    QVERIFY(!window.resultsText().contains(QStringLiteral("after-failure")));
    QVERIFY(window.problemCount() > 0);
    auto* problems = window.findChild<QTreeWidget*>(QStringLiteral("problemsList"));
    QVERIFY(problems->topLevelItem(0)->text(1).startsWith('R'));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::unicodeDiagnosticNavigatesUtf16Editor() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("UnicodeLocation"), &error));
    const QString text = QString::fromUtf8("% λ 🙂\nreal okay=2; display(\"🙂\", missing_name);\n");
    replaceEditorText(window.currentEditor(), text);
    const QByteArray savedBeforeCheck = readBytes(window.currentDocument()->path());
    const QString transcript = window.resultsText();
    QVERIFY(window.checkCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    auto* problems = window.findChild<QTreeWidget*>(QStringLiteral("problemsList"));
    int row = -1;
    for (int i=0; i<problems->topLevelItemCount(); ++i)
        if (problems->topLevelItem(i)->text(1) == QStringLiteral("E1201")) row=i;
    QVERIFY(row >= 0);
    QCOMPARE(problems->topLevelItem(row)->text(3), QStringLiteral("2"));
    window.navigateProblem(row);
    QCOMPARE(window.currentEditor()->textCursor().selectionStart(), static_cast<int>(text.indexOf(QStringLiteral("missing_name"))));
    QCOMPARE(window.currentEditor()->textCursor().selectedText(), QStringLiteral("missing_name"));
    // Check has no evaluation side effects and does not clear prior transcript.
    QVERIFY(window.resultsText().startsWith(transcript));
    QVERIFY(window.resultsText().contains(QStringLiteral(":2:27: Error E1201")));
    QCOMPARE(readBytes(window.currentDocument()->path()), savedBeforeCheck);
    QVERIFY(window.currentDocument()->isModified());
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::projectCheckUsesIndependentUnsavedScopes() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("IndependentScopes"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("real isolated=3; display(isolated);\n"));
    QVERIFY(window.addSource(QStringLiteral("nested/second.p"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("display(isolated);\n"));
    const QString transcript = window.resultsText();
    QVERIFY(window.checkProject(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    auto* problems = window.findChild<QTreeWidget*>(QStringLiteral("problemsList"));
    bool found = false;
    for (int row=0; row<problems->topLevelItemCount(); ++row)
        if (problems->topLevelItem(row)->text(1) == QStringLiteral("E1201") &&
            problems->topLevelItem(row)->text(2).endsWith(QStringLiteral("second.p"))) found=true;
    QVERIFY(found);
    QVERIFY(window.resultsText().startsWith(transcript));
    QVERIFY(window.resultsText().contains(QStringLiteral("second.p:1:9: Error E1201")));
    QVERIFY(window.saveAllDocuments(&error));
    QVERIFY(window.close());
}

void GuiTests::legacyProjectRequiresExplicitUpgrade() {
    QTemporaryDir temporary;
    Project project;
    QString error;
    QVERIFY(ProjectService::create(temporary.path(), QStringLiteral("Legacy"), project, &error));
    QJsonObject manifest = QJsonDocument::fromJson(readBytes(project.manifestPath)).object();
    manifest.insert(QStringLiteral("language_version"), QStringLiteral("0.1"));
    QVERIFY(writeBytes(project.manifestPath, QJsonDocument(manifest).toJson()));
    MainWindow window(false);
    QVERIFY(window.openProjectPath(project.manifestPath, &error));
    const QString path = window.currentDocument()->path();
    const QByteArray disk = readBytes(path);
    replaceEditorText(window.currentEditor(), QStringLiteral("display(7);\n"));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("use_algebra(\"A_gamma\",gamma=0)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(!window.runCurrentDocument(&error));
    QVERIFY(window.workspaceContextText().contains(QString::fromUtf8("γ=0")));
    QCOMPARE(window.activeProject()->languageVersion, QStringLiteral("0.1"));
    QCOMPARE(QJsonDocument::fromJson(readBytes(project.manifestPath)).object().value(QStringLiteral("language_version")).toString(), QStringLiteral("0.1"));
    QVERIFY(window.checkCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY2(window.enableInterpreter(&error), qPrintable(error));
    QCOMPARE(window.activeProject()->languageVersion, QStringLiteral("0.2"));
    QCOMPARE(readBytes(path), disk); // Upgrade changed metadata only.
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("7")));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::commandWindowSharesVariablesAndHistory() {
    MainWindow window(false);
    QString error;
    auto* prompt = window.commandWindow();
    QVERIFY(prompt);
    prompt->setInputText(QStringLiteral("pentarnion p = i+j"));
    QTest::keyClick(prompt, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().contains(QStringLiteral("p = i + j")), qPrintable(window.resultsText()));
    prompt->setInputText(QStringLiteral("p*p"));
    QTest::keyClick(prompt, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("-2")), qPrintable(window.resultsText()));
    QTest::keyClick(prompt, Qt::Key_Up);
    QCOMPARE(prompt->inputText(), QStringLiteral("p*p"));
    QTest::keyClick(prompt, Qt::Key_Up);
    QCOMPARE(prompt->inputText(), QStringLiteral("pentarnion p = i+j"));
    QTest::keyClick(prompt, Qt::Key_Down);
    QCOMPARE(prompt->inputText(), QStringLiteral("p*p"));
    QVERIFY(window.submitCommand(QStringLiteral("real hidden=9;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(!window.resultsText().contains(QStringLiteral("hidden = 9"))); // Final ; suppresses echo.
    QVERIFY(window.submitCommand(QStringLiteral("display(hidden)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("9")));
    QCOMPARE(window.documentCount(), 0);
    QVERIFY(window.close());
}

void GuiTests::fileWorkspaceAndGammaAreAvailableToCommands() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("FileWorkspace"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral(
        "use_algebra(\"A_gamma\", gamma=0); pentarnion p=i*j;\n"));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("display(coeff(p,3),coeff(p,4))"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("1 0")), qPrintable(window.resultsText()));
    QVERIFY(window.submitCommand(QStringLiteral("display(coeff(i*j,3),coeff(i*j,4))"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("1 0")));
    QVERIFY(window.workspaceContextText().contains(QString::fromUtf8("γ=0")));
    window.clearWorkspace();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("p"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(window.submitCommand(QStringLiteral("display(coeff(i*j,3),coeff(i*j,4))"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    const auto values = window.resultsText().section('\n', -1).split(' ', Qt::SkipEmptyParts);
    QCOMPARE(values.size(), 2);
    QVERIFY(qAbs(values[0].toDouble() - 0.7071067811865475) < 1e-14);
    QVERIFY(qAbs(values[1].toDouble() - 0.7071067811865475) < 1e-14);
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::projectSwitchClearsCommandWorkspace() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("FirstWorkspace"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("real retained=42"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("SecondWorkspace"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("retained"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(window.resultsText().contains(QStringLiteral("[Workspace cleared]")));
    QVERIFY(window.close());
}

void GuiTests::staleRunCannotPublishOutputOrVariables() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("StaleRun"), &error));
    QString source;
    for (int n=0; n<10000; ++n) source += QStringLiteral("real old%1=%1;\n").arg(n);
    source += QStringLiteral("display(\"obsolete-result\");\n");
    replaceEditorText(window.currentEditor(), source);
    QVERIFY(window.runCurrentDocument(&error));
    replaceEditorText(window.currentEditor(), QStringLiteral("display(\"current-source\");\n"));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 15000);
    QVERIFY(!window.resultsText().contains(QStringLiteral("obsolete-result")));
    QVERIFY2(window.resultsText().contains(QStringLiteral("stale source")), qPrintable(window.resultsText()));
    QVERIFY(window.submitCommand(QStringLiteral("old0"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::importsResolveFromSourceAndProjectConsole() {
    QTemporaryDir temporary;MainWindow window(false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Imports"),&error));
    window.configureAutosave(false,60000);
    const QString root=QFileInfo(window.currentDocument()->path()).absolutePath();
    QVERIFY(writeBytes(QDir(root).filePath(QStringLiteral("helpers.p")),
        "function real square(real x){return x*x;}\r\n"));
    replaceEditorText(window.currentEditor(),QStringLiteral("import \"helpers.p\" as science;display(science.square(3.0));"));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),15000);
    QCOMPARE(window.problemCount(),0);QVERIFY(window.resultsText().contains(QStringLiteral("9")));
    QVERIFY(window.submitCommand(QStringLiteral("import \"helpers.p\" as tools;display(tools.square(4.0));"),&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),15000);
    QCOMPARE(window.problemCount(),0);QVERIFY(window.resultsText().contains(QStringLiteral("16")));
    QVERIFY(window.close());
}

void GuiTests::dirtyImportedSourceCannotPublishOlderResults() {
    QTemporaryDir temporary;MainWindow window(false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("DirtyImport"),&error));
    window.configureAutosave(false,60000);
    const QString main=window.currentDocument()->path();
    const QString helper=QDir(QFileInfo(main).absolutePath()).filePath(QStringLiteral("helpers.p"));
    QVERIFY(writeBytes(helper,"function real answer(){return 1.0;}\r\n"));
    replaceEditorText(window.currentEditor(),QStringLiteral("import \"helpers.p\" as science;display(\"obsolete-import\",science.answer());"));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.openSourcePath(helper,&error));
    replaceEditorText(window.currentEditor(),QStringLiteral("function real answer(){return 2.0;}\n"));
    QVERIFY(window.openSourcePath(main,&error));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),15000);
    QVERIFY(!window.resultsText().contains(QStringLiteral("obsolete-import 1")));
    QVERIFY2(window.resultsText().contains(QStringLiteral("stale source")),qPrintable(window.resultsText()));
    QVERIFY(window.openSourcePath(helper,&error));QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::stopCancelsWork() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Cancellation"), &error));
    QString source;
    for (int n=0; n<30000; ++n) source += QStringLiteral("real value%1=%1;\n").arg(n);
    source += QStringLiteral("display(\"must-not-finish\");\n");
    replaceEditorText(window.currentEditor(), source);
    QVERIFY(window.runCurrentDocument(&error));
    window.stopExecution();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 15000);
    QVERIFY(!window.resultsText().contains(QStringLiteral("must-not-finish")));
    QVERIFY(window.calculationStatus().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::closingRunningDocumentIsSafe() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("CloseRunningDocument"), &error));
    QString source;
    for (int n=0; n<10000; ++n) source += QStringLiteral("real value%1=%1;\n").arg(n);
    source += QStringLiteral("display(\"closed-document-output\");\n");
    replaceEditorText(window.currentEditor(), source);
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.runCurrentDocument(&error));
    auto* closeAction = window.findChild<QAction*>(QStringLiteral("fileCloseTab"));
    QVERIFY(closeAction); closeAction->trigger();
    QCOMPARE(window.documentCount(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 15000);
    QVERIFY(!window.resultsText().contains(QStringLiteral("closed-document-output")));
    QVERIFY(window.close());
}

void GuiTests::rejectedContextAndStaticErrorsKeepWorkspaceLabel() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    auto context = [&window] { return window.workspaceContextText(); };
    QVERIFY(!context().isEmpty());
    QVERIFY(window.submitCommand(QStringLiteral("use_algebra(\"A_gamma\",gamma=0)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("pentarnion p=i+j;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(context().contains(QString::fromUtf8("γ=0")));
    QVERIFY(window.submitCommand(QStringLiteral("use_algebra(\"A_gamma\",gamma=1)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    auto* problems = window.findChild<QTreeWidget*>(QStringLiteral("problemsList"));
    bool rejected = false;
    for (int row=0; row<problems->topLevelItemCount(); ++row)
        if (problems->topLevelItem(row)->text(1) == QStringLiteral("R1009")) rejected = true;
    QVERIFY(rejected);
    QVERIFY(context().contains(QString::fromUtf8("γ=0")));
    QVERIFY(window.submitCommand(QStringLiteral("use_algebra(\"A_gamma\",gamma=1); real broken=;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(context().contains(QString::fromUtf8("γ=0")));
    const QString badFile = temporary.filePath(QStringLiteral("static-error.p"));
    QVERIFY(writeBytes(badFile, QByteArrayLiteral("use_algebra(\"A_gamma\",gamma=1); real broken=;\n")));
    QVERIFY(window.openSourcePath(badFile, &error));
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(context().contains(QString::fromUtf8("γ=0")));
    QVERIFY(window.submitCommand(QStringLiteral("display(coeff(i*j,3),coeff(i*j,4))"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("1 0")), qPrintable(window.resultsText()));
    QVERIFY(context().contains(QString::fromUtf8("γ=0")));
    QVERIFY(window.close());
}

void GuiTests::blankDisplayOutputIsPreservedWithoutFabricatingQuietOutput() {
    MainWindow window(false);
    QString error;
    auto* transcript = window.commandWindow();
    QVERIFY(transcript);
    QVERIFY(window.submitCommand(QStringLiteral("display(\"before-blank\")"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    const int beforeBlank = transcript->document()->blockCount();
    QVERIFY(window.submitCommand(QStringLiteral("display(\"\")"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    // One command marker plus exactly one emitted blank output line.
    QCOMPARE(transcript->document()->blockCount(), beforeBlank + 2);
    QVERIFY(window.resultsText().endsWith('\n'));
    const int beforeQuiet = transcript->document()->blockCount();
    QVERIFY(window.submitCommand(QStringLiteral("real quiet=1;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    // The semicolon-suppressed declaration appends only its command marker.
    QCOMPARE(transcript->document()->blockCount(), beforeQuiet + 1);
    QVERIFY(window.resultsText().endsWith(QStringLiteral(">> real quiet=1;")));
    QVERIFY(window.close());
}

void GuiTests::consoleRunAndSelectedFileActionRunTheRequestedSource() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("ScriptCommands"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("display(\"main-run-output\");\n"));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.addSource(QStringLiteral("nested/second.p"), &error));
    replaceEditorText(window.currentEditor(), QStringLiteral("display(\"selected-run-output\");\n"));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.submitCommand(QStringLiteral("run(\"main.p\");"), &error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("main-run-output")), qPrintable(window.resultsText()));
    QVERIFY(window.currentDocument()->path().endsWith(QStringLiteral("main.p")));
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
    const auto second = tree->findItems(QStringLiteral("second.p"), Qt::MatchRecursive | Qt::MatchExactly);
    QCOMPARE(second.size(), 1);
    tree->setCurrentItem(second.front());
    auto* runSelected = window.findChild<QAction*>(QStringLiteral("projectRunSource"));
    QVERIFY(runSelected && runSelected->isEnabled());
    runSelected->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("selected-run-output")), qPrintable(window.resultsText()));
    QVERIFY(window.currentDocument()->path().endsWith(QStringLiteral("nested/second.p")));
    QVERIFY(window.close());
}

void GuiTests::consoleRunErrorsStayInlineAndPathsAreLiteralOnly() {
    MainWindow window(false);
    QString error;
    QVERIFY(!window.submitCommand(QStringLiteral("run(1+1)"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(window.resultsText().contains(QStringLiteral("Error:")));
    QVERIFY(!window.isWorking());
    QVERIFY(!window.submitCommand(QStringLiteral("run(\"does-not-exist.p\")"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!window.isWorking());
    QCOMPARE(window.documentCount(), 0);
    for (auto* widget : QApplication::topLevelWidgets())
        QVERIFY(!qobject_cast<QMessageBox*>(widget) || !widget->isVisible());
    QVERIFY(window.close());
}

void GuiTests::clcAndClearKeepTheirDistinctEffects() {
    MainWindow window(false);
    QString error;
    QVERIFY(window.submitCommand(QStringLiteral("use_algebra(\"A_gamma\",gamma=0)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("real p=2; real q=3; display(\"old-visible-output\");"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().contains(QStringLiteral("old-visible-output")));
    QVERIFY(window.submitCommand(QStringLiteral("clc"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().isEmpty(), qPrintable(window.resultsText()));
    QCOMPARE(window.commandWindow()->toPlainText(), QStringLiteral(">> "));
    auto* prompt = window.commandWindow();
    QTest::keyClick(prompt, Qt::Key_Up);
    QCOMPARE(prompt->inputText(), QStringLiteral("clc"));
    QVERIFY(window.submitCommand(QStringLiteral("p+q"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("5")));
    QVERIFY(window.submitCommand(QStringLiteral("clear p"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("q"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("3")));
    QVERIFY(window.submitCommand(QStringLiteral("p"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(window.submitCommand(QStringLiteral("clear all"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("q"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.problemCount() > 0);
    QVERIFY(window.workspaceContextText().contains(QString::fromUtf8("γ=0")));
    QVERIFY(window.submitCommand(QStringLiteral("display(coeff(i*j,3),coeff(i*j,4))"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("1 0")));
    QVERIFY(window.close());
}

void GuiTests::commandWindowScrollPositionSurvivesNewOutput() {
    QTemporaryDir temporary;
    MainWindow window(false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("Scrollable"), &error));
    QString source;
    for (int row=0; row<400; ++row) source += QStringLiteral("display(%1);\n").arg(row);
    replaceEditorText(window.currentEditor(), source);
    window.resize(900, 650); window.show();
    QVERIFY(window.runCurrentDocument(&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    auto* transcript = window.commandWindow();
    QCOMPARE(transcript->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QTRY_VERIFY_WITH_TIMEOUT(transcript->verticalScrollBar()->maximum() > 0, 2000);
    transcript->verticalScrollBar()->setValue(0);
    const int previous = transcript->verticalScrollBar()->value();
    QVERIFY(window.submitCommand(QStringLiteral("display(9999)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QCOMPARE(transcript->verticalScrollBar()->value(), previous);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("9999")));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::calculationsStayInsideOneMainWindow() {
    MainWindow window(false);
    window.show(); QApplication::processEvents();
    auto* commandDock = window.findChild<QDockWidget*>(QStringLiteral("resultsDock"));
    auto* problemsDock = window.findChild<QDockWidget*>(QStringLiteral("problemsDock"));
    QVERIFY(commandDock && problemsDock);
    QVERIFY(!(commandDock->features() & QDockWidget::DockWidgetFloatable));
    QVERIFY(!(problemsDock->features() & QDockWidget::DockWidgetFloatable));
    QVERIFY(!commandDock->isFloating());
    auto* layout=window.findChild<desktop::StudioWorkspaceLayout*>();QVERIFY(layout);
    QVERIFY(!layout->workingHost()->isWindow());
    QCOMPARE(commandDock->parentWidget(),layout->workingHost());
    QCOMPARE(layout->workingHost()->dockWidgetArea(commandDock), Qt::BottomDockWidgetArea);
    auto visibleTopLevels = [] {
        int count=0;
        for (auto* widget : QApplication::topLevelWidgets()) if (widget->isWindow() && widget->isVisible()) ++count;
        return count;
    };
    const int before = visibleTopLevels();
    QString error;
    QVERIFY(window.submitCommand(QStringLiteral("display(2+2)"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QCOMPARE(visibleTopLevels(), before);
    QVERIFY(window.submitCommand(QStringLiteral("missing_variable"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QCOMPARE(visibleTopLevels(), before);
    QVERIFY(window.resultsText().contains(QStringLiteral("Error E1201:")));
    QVERIFY(!commandDock->isFloating());
    QVERIFY(window.close());
}

void GuiTests::unifiedConsoleProtectsHistoryAndActivePrompt() {
    CommandWindow console;
    console.resize(600, 300); console.show();
    QVERIFY(console.findChildren<QLineEdit*>().isEmpty());
    QCOMPARE(console.toPlainText(), QStringLiteral(">> "));
    console.commitCommand(QStringLiteral("display(1)")); console.appendOutput(QStringLiteral("1"));
    console.setInputText(QStringLiteral("draft"));
    const QString protectedHistory = console.transcriptText();
    QTextCursor old(console.document()); old.setPosition(0); old.setPosition(2, QTextCursor::KeepAnchor);
    console.setTextCursor(old);
    console.appendOutput(QStringLiteral("later"));
    QCOMPARE(console.textCursor().selectedText(), QStringLiteral(">>"));
    QCOMPARE(console.inputText(), QStringLiteral("draft"));
    QVERIFY(console.transcriptText().startsWith(protectedHistory));
    const QString history = console.transcriptText();
    QTest::mouseClick(console.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(8, 8));
    QTest::keyClicks(&console, QStringLiteral("x"));
    QCOMPARE(console.inputText(), QStringLiteral("draftx"));
    QCOMPARE(console.transcriptText(), history);
    QTest::keyClick(&console, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(&console, Qt::Key_Backspace);
    QCOMPARE(console.inputText(), QString());
    QCOMPARE(console.transcriptText(), history);
    QTest::keyClick(&console, Qt::Key_Backspace);
    QVERIFY(console.toPlainText().endsWith(QStringLiteral(">> ")));
    console.setBusy(true);
    QTest::keyClicks(&console, QStringLiteral("blocked"));
    QCOMPARE(console.inputText(), QString());
}

void GuiTests::unifiedConsolePastesMultilineWithoutExecuting() {
    CommandWindow console;
    console.show();console.setFocus();QApplication::processEvents();
    QSignalSpy submitted(&console, &CommandWindow::commandSubmitted);
    QApplication::clipboard()->setText(QStringLiteral("real a=1;\r\nreal b=2;"));
    QTRY_COMPARE(QApplication::clipboard()->text(),QStringLiteral("real a=1;\r\nreal b=2;"));
    QTest::keyClick(&console, Qt::Key_V, Qt::ControlModifier);
    QTRY_COMPARE(console.inputText(), QStringLiteral("real a=1;\nreal b=2;"));
    QCOMPARE(submitted.count(), 0);
    QTest::keyClick(&console, Qt::Key_Return);
    QCOMPARE(submitted.count(), 1);
    QCOMPARE(submitted.front().front().toString(), QStringLiteral("real a=1;\nreal b=2;"));
    console.commitCommand(console.inputText()); console.appendOutput(QStringLiteral("done"));
    console.clearTranscript();
    QCOMPARE(console.toPlainText(), QStringLiteral(">> "));
    QTest::keyClick(&console, Qt::Key_Up);
    QCOMPARE(console.inputText(), QStringLiteral("real a=1;\nreal b=2;"));
}

void GuiTests::autosaveOnViewportLeaveWhileEditorRetainsFocus() {
    QTemporaryDir temporary;
    Project project; QString error;
    QVERIFY(ProjectService::create(temporary.path(), QStringLiteral("LeaveAutosave"), project, &error));
    const QString path = QDir(project.rootPath).filePath(QStringLiteral("main.p"));
    const QByteArray original = QByteArray("\xEF\xBB\xBF") + "% initial\r\nreal value=1;\r\n";
    QVERIFY(writeBytes(path, original));
    MainWindow window(false, nullptr, false);
    QVERIFY(window.openProjectPath(project.manifestPath, &error));
    window.configureAutosave(true, 60000);
    window.show(); window.activateWindow();
    auto* editor = window.currentEditor(); editor->setFocus();
    QTRY_VERIFY_WITH_TIMEOUT(editor->hasFocus(), 2000);
    const QString edited = QString::fromUtf8("% λ 🙂\nreal value=2;\n");
    replaceEditorText(editor, edited);
    const auto revision = window.currentDocument()->revision();
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(editor->viewport(), &leave);
    QVERIFY(editor->hasFocus()); // Genuine viewport Leave, without a focus-out.
    QCOMPARE(readBytes(path), original); // The boundary is debounced, not immediate.
    QByteArray expected = edited.toUtf8(); expected.replace("\n", "\r\n"); expected.prepend("\xEF\xBB\xBF");
    QTRY_COMPARE_WITH_TIMEOUT(readBytes(path), expected, 3000);
    QVERIFY(!window.currentDocument()->isModified());
    QCOMPARE(window.currentDocument()->revision(), revision);
    QVERIFY(editor->hasFocus());
    QVERIFY(window.close());
}

void GuiTests::autosavePeriodicallySavesNamedButNeverInventsUnnamedPaths() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("PeriodicAutosave"), &error));
    const QString path = window.currentDocument()->path();
    const QByteArray created = readBytes(path);
    QTest::qWait(150);
    QCOMPARE(readBytes(path), created); // Creating/opening alone never rewrites it.
    window.configureAutosave(true, 100);
    replaceEditorText(window.currentEditor(), QStringLiteral("real periodic=8;\n"));
    QTRY_COMPARE_WITH_TIMEOUT(readBytes(path), QByteArrayLiteral("real periodic=8;\n"), 2000);
    QVERIFY(!window.currentDocument()->isModified());
    window.currentDocument()->initializeNew(QStringLiteral("real unnamed=9;\n"));
    QEvent leave(QEvent::Leave); QApplication::sendEvent(window.currentEditor()->viewport(), &leave);
    QTest::qWait(700);
    QVERIFY(window.currentDocument()->path().isEmpty());
    QVERIFY(window.currentDocument()->isModified());
    QCOMPARE(readBytes(path), QByteArrayLiteral("real periodic=8;\n"));
    window.configureAutosave(false);
    answerDirtyDialog(QMessageBox::Discard); QVERIFY(window.close());
}

void GuiTests::autosaveSkipsExternalChangesWithoutDialogs() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("ConflictAutosave"), &error));
    const QString path = window.currentDocument()->path();
    // Isolate autosave from the separately tested interactive filesystem watcher.
    auto* watcher = window.findChild<QFileSystemWatcher*>(); QVERIFY(watcher); watcher->blockSignals(true);
    window.configureAutosave(true, 60000);
    replaceEditorText(window.currentEditor(), QStringLiteral("real retained=3;\n"));
    const QByteArray external = QByteArrayLiteral("% changed in another program\n");
    QVERIFY(writeBytes(path, external));
    QEvent leave(QEvent::Leave); QApplication::sendEvent(window.currentEditor()->viewport(), &leave);
    QTest::qWait(700);
    QCOMPARE(readBytes(path), external);
    QVERIFY(window.currentDocument()->isModified());
    QCOMPARE(window.currentDocument()->text(), QStringLiteral("real retained=3;\n"));
    for (auto* widget : QApplication::topLevelWidgets())
        QVERIFY(!qobject_cast<QMessageBox*>(widget) || !widget->isVisible());
    window.configureAutosave(false);
    QVERIFY(window.currentDocument()->reload(&error));
    QVERIFY(window.close());
}

void GuiTests::watcherBurstsAreCoalescedWithoutStarvingChanges() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("WatcherBurst"), &error), qPrintable(error));
    auto* watcher = window.findChild<QFileSystemWatcher*>();
    auto* timer = window.findChild<QTimer*>(QStringLiteral("externalChangeTimer"));
    QVERIFY(watcher && timer);
    QTest::qWait(250); // Drain creation notifications before the controlled burst.
    QSignalSpy checks(timer, &QTimer::timeout);
    for (int i = 0; i < 100; ++i)
        QVERIFY(QMetaObject::invokeMethod(watcher, "directoryChanged", Qt::DirectConnection,
                                         Q_ARG(QString, window.activeProject()->rootPath)));
    QVERIFY(timer->isActive());
    QTRY_COMPARE_WITH_TIMEOUT(checks.count(), 1, 1000);
    QTest::qWait(180);
    QCOMPARE(checks.count(), 1);
    const auto watchedFiles = watcher->files();
    const auto watchedDirectories = watcher->directories();
    MainWindowIdleIoTestAccess::refreshWatchers(window);
    QCOMPARE(watcher->files(), watchedFiles);
    QCOMPARE(watcher->directories(), watchedDirectories);

    checks.clear();
    QTimer activity;
    activity.setInterval(10);
    connect(&activity, &QTimer::timeout, &window, [watcher, &window] {
        QMetaObject::invokeMethod(watcher, "directoryChanged", Qt::DirectConnection,
                                  Q_ARG(QString, window.activeProject()->rootPath));
    });
    activity.start();
    QTRY_VERIFY_WITH_TIMEOUT(checks.count() >= 1, 500);
    QVERIFY(activity.isActive()); // A continually restarted debounce would starve.
    activity.stop();
    QVERIFY(window.close());
}

void GuiTests::replacedSourceWatchIsReinstalled() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("ReplacedWatch"), &error), qPrintable(error));
    QTest::qWait(200);
    auto* watcher = window.findChild<QFileSystemWatcher*>();
    QVERIFY(watcher);
    auto* document = window.currentDocument();
    const QString path = document->path();
    const QString retained = document->text();
    watcher->blockSignals(true);
    QSaveFile replacement(path);
    const QByteArray replaced = QByteArrayLiteral("% atomic replacement\n");
    QVERIFY(replacement.open(QIODevice::WriteOnly));
    QCOMPARE(replacement.write(replaced), replaced.size());
    QVERIFY(replacement.commit());
    // Emulate the dropped file registration reported by native watchers after
    // atomic replacement, independently of platform-specific delivery timing.
    watcher->removePath(path);
    QVERIFY(!watcher->files().contains(path));
    MainWindowIdleIoTestAccess::refreshWatchers(window);
    QVERIFY(watcher->files().contains(path));
    watcher->blockSignals(false);
    int prompted = 0;
    QTimer responder;
    responder.setInterval(10);
    connect(&responder, &QTimer::timeout, &window, [&prompted] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* question = qobject_cast<QMessageBox*>(widget);
                    question && question->isVisible() && question->objectName() == QStringLiteral("externalChangePrompt")) {
                ++prompted;
                question->reject();
                return;
            }
    });
    responder.start();
    QVERIFY(writeBytes(path, QByteArrayLiteral("% second external edit\n")));
    QTRY_COMPARE_WITH_TIMEOUT(prompted, 1, 3000);
    responder.stop();
    QCOMPARE(document->text(), retained);
    QVERIFY(document->isModified());
    QVERIFY(document->hasExternalChange());
    QVERIFY2(document->reload(&error), qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::unchangedRecoveryDoesNotRewriteAndChangesAreCheckpointed() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("RecoveryCheckpoints"), &error), qPrintable(error));
    MainWindowIdleIoTestAccess::enableRecovery(window, temporary.filePath(QStringLiteral("recovery")));
    replaceEditorText(window.currentEditor(), QStringLiteral("real recovered=1;\n"));
    MainWindowIdleIoTestAccess::snapshot(window);
    const QString path = MainWindowIdleIoTestAccess::recoveryPath(window);
    const QByteArray first = readBytes(path);
    QVERIFY(!first.isEmpty());
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 1);
    const QDateTime firstModified = QFileInfo(path).lastModified();
    const QDateTime firstMetadata = QFileInfo(path).metadataChangeTime();
    QTest::qWait(40);
    for (int i = 0; i < 4; ++i) MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(readBytes(path), first);
    QCOMPARE(QFileInfo(path).lastModified(), firstModified);
    QCOMPARE(QFileInfo(path).metadataChangeTime(), firstMetadata);

    // Metadata changes invalidate the fast path and revalidate the owned file.
    QFile checkpoint(path);
    QVERIFY(checkpoint.open(QIODevice::ReadWrite));
    const auto backdated = QDateTime::currentDateTimeUtc().addDays(-2);
    QVERIFY(checkpoint.setFileTime(backdated, QFileDevice::FileModificationTime));
    checkpoint.close();
    MainWindowIdleIoTestAccess::snapshot(window);
    QVERIFY(QFileInfo(path).lastModified() > backdated.addSecs(60));
    QCOMPARE(readBytes(path), first);

    QTextCursor cursor = window.currentEditor()->textCursor();
    cursor.setPosition(2);
    window.currentEditor()->setTextCursor(cursor);
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object().value("cursor_position").toInt(), 2);
    replaceEditorText(window.currentEditor(), QStringLiteral("real recovered=2;\n"));
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object().value("text").toString(), QStringLiteral("real recovered=2;\n"));
    QVERIFY(window.renameProjectSource(QStringLiteral("main.p"), QStringLiteral("renamed.p"), &error));
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object().value("path").toString(), window.currentDocument()->path());
    QVERIFY(QFile::remove(path));
    MainWindowIdleIoTestAccess::snapshot(window);
    QVERIFY(QFileInfo::exists(path));
    QVERIFY2(window.saveCurrentDocument(&error), qPrintable(error));
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 0);
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(window.close());
}

void GuiTests::recoveryFailuresRetryAndForeignReplacementsStayUntouched() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("RecoveryRetry"), &error), qPrintable(error));
    const QString directory = temporary.filePath(QStringLiteral("recovery"));
    MainWindowIdleIoTestAccess::enableRecovery(window, directory);
    replaceEditorText(window.currentEditor(), QStringLiteral("real retained=8;\n"));
    QVERIFY(writeBytes(directory, QByteArrayLiteral("block directory creation")));
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 0);
    QVERIFY(QFile::remove(directory));
    MainWindowIdleIoTestAccess::snapshot(window);
    const QString path = MainWindowIdleIoTestAccess::recoveryPath(window);
    QVERIFY(QFileInfo::exists(path));
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 1);
    const QByteArray foreign = QByteArrayLiteral("{\"owner\":\"another-application\",\"text\":\"keep me\"}");
    QVERIFY(writeBytes(path, foreign));
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(readBytes(path), foreign);
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 0);
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(readBytes(path), foreign);
    QVERIFY(QFile::remove(path));
    MainWindowIdleIoTestAccess::snapshot(window);
    QCOMPARE(MainWindowIdleIoTestAccess::checkpointCount(window), 1);
    QCOMPARE(QJsonDocument::fromJson(readBytes(path)).object().value("text").toString(), QStringLiteral("real retained=8;\n"));
    QVERIFY2(window.saveCurrentDocument(&error), qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::cursorDisplayContextIsCachedAndExecutionStillValidates() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY2(window.createProjectAt(temporary.path(), QStringLiteral("DisplayContextCache"), &error), qPrintable(error));
    // Isolate explicit invalidation from native filesystem notification timing.
    auto* watcher = window.findChild<QFileSystemWatcher*>();
    QVERIFY(watcher);
    watcher->blockSignals(true);
    auto* timer = window.findChild<QTimer*>(QStringLiteral("externalChangeTimer"));
    QVERIFY(timer);
    timer->stop();
    MainWindowIdleIoTestAccess::setProjectContext(window, 2, QStringLiteral("0.2"));
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 2.0);
    // A sentinel makes repeated validation observable without adding production
    // counters or changing filesystem permissions. Cursor updates must reuse the
    // cached display result; run/check snapshots must independently revalidate.
    QVERIFY(MainWindowIdleIoTestAccess::setCachedDisplayGamma(window, 99));
    auto* editor = window.currentEditor();
    for (int position = 1; position < 30; ++position) {
        QTextCursor cursor = editor->textCursor();
        cursor.setPosition(position);
        editor->setTextCursor(cursor);
    }
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 99.0);
    QCOMPARE(MainWindowIdleIoTestAccess::validatedSnapshotGamma(window), 2.0);
    MainWindowIdleIoTestAccess::refreshWatchers(window);
    MainWindowIdleIoTestAccess::updateUi(window);
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 2.0);
    QVERIFY(MainWindowIdleIoTestAccess::setCachedDisplayGamma(window, 99));
    MainWindowIdleIoTestAccess::setProjectContext(window, 3, QStringLiteral("0.2"));
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 3.0);
    QVERIFY(window.renameProjectSource(QStringLiteral("main.p"), QStringLiteral("renamed.p"), &error));
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayPath(window), window.currentDocument()->path());
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 3.0);
    QVERIFY(MainWindowIdleIoTestAccess::setCachedDisplayGamma(window, 99));
    MainWindowIdleIoTestAccess::setProjectContext(window, 3, QStringLiteral("0.1"));
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayGamma(window), 3.0);
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayVersion(window), QStringLiteral("0.1"));
    QVERIFY(window.enableInterpreter(&error));
    QCOMPARE(MainWindowIdleIoTestAccess::cachedDisplayVersion(window), QStringLiteral("0.2"));
    QCOMPARE(MainWindowIdleIoTestAccess::validatedSnapshotGamma(window), 3.0);
    QVERIFY(window.close());
}

void GuiTests::acceptedFiguresStayEmbeddedAndStaleFiguresAreDiscarded() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.submitCommand(QStringLiteral("figure(2); pentarnion p=1+i+2*j+3*k+4*ell; plot3(\"i\",\"j\",\"k\",p);"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QCOMPARE(window.problemCount(), 0);
    QVERIFY(window.figureManager()->containsFigure(2));
    const auto figure = window.figureManager()->figureState(2);
    QVERIFY(figure.has_value()); QCOMPARE(figure->dimensions, 3);
    QCOMPARE(figure->vectors.size(), std::size_t(1));
    QCOMPARE(figure->vectors.front().coefficients[4], 4.0); // Readout never projects away original coefficients.
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("figuresDock"));
    QVERIFY(dock && !dock->isFloating());
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("StalePlots"), &error));
    QString source;
    for (int row=0; row<1000; ++row) source += QStringLiteral("real value%1=%1;\n").arg(row);
    source += QStringLiteral("figure(7); plot2(\"i\",\"j\",i+j);\n");
    replaceEditorText(window.currentEditor(), source);
    QVERIFY(window.runCurrentDocument(&error));
    replaceEditorText(window.currentEditor(), QStringLiteral("display(1);\n"));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 15000);
    QVERIFY(!window.figureManager()->containsFigure(7));
    QVERIFY(window.figureManager()->containsFigure(2));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.close());
}

void GuiTests::unnamedRunSaveAsCancelPreservesWorkspace() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("CancelRunSaveAs"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("real prior=42;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    const QString originalPath = window.currentDocument()->path();
    const QByteArray original = readBytes(originalPath);
    window.currentDocument()->initializeNew(QStringLiteral("real forbidden=99; display(\"cancelled-script-output\");\n"));
    QSignalSpy work(&window, &MainWindow::workFinished);
    bool sawSaveAs = false;
    QTimer responder; responder.setInterval(10);
    connect(&responder, &QTimer::timeout, &window, [&] {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
                sawSaveAs = true; dialog->reject(); return;
            }
    });
    responder.start();
    const bool scheduled = window.runCurrentDocument(&error);
    responder.stop();
    QVERIFY(!scheduled);
    QVERIFY(sawSaveAs); QVERIFY(!error.isEmpty());
    QVERIFY(!window.isWorking()); QCOMPARE(work.count(), 0);
    QVERIFY(window.currentDocument()->path().isEmpty());
    QVERIFY(window.currentDocument()->isModified());
    QCOMPARE(readBytes(originalPath), original);
    QVERIFY(!window.resultsText().contains(QStringLiteral("cancelled-script-output")));
    QVERIFY(window.submitCommand(QStringLiteral("prior"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.resultsText().endsWith(QStringLiteral("42")));
    answerDirtyDialog(QMessageBox::Discard); QVERIFY(window.close());
}

void GuiTests::unnamedRunSaveAsSuccessPersistsBeforeExecution() {
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("SavedNamedRun"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    const QString source = QStringLiteral("real saved=11; display(\"saved-as-output\",saved);\n");
    window.currentDocument()->initializeNew(source);
    const QString path = QDir(window.activeProject()->rootPath).filePath(QString::fromUtf8("λ saved script.p"));
    bool accepted = false, prepared = false;
    int stableSelection = 0;
    QString selectedBeforeAccept;
    QString dialogFailure;
    QElapsedTimer elapsed; elapsed.start();
    QTimer responder; responder.setInterval(10);
    connect(&responder, &QTimer::timeout, &window, [&] {
        if (accepted) return;
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) {
                if (elapsed.elapsed() > 5000) {
                    dialogFailure = QStringLiteral("Save As selection did not stabilize: %1").arg(dialog->selectedFiles().join('|'));
                    responder.stop(); dialog->reject(); return;
                }
                if (!prepared) {
                    prepared = true;
                    dialog->setDirectory(QFileInfo(path).absolutePath());
                    dialog->selectFile(QFileInfo(path).fileName());
                    return; // Let the asynchronous QFileSystemModel update first.
                }
                auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
                if (name && name->text() != QFileInfo(path).fileName()) {
                    name->setText(QFileInfo(path).fileName()); stableSelection = 0; return;
                }
                const auto selected = dialog->selectedFiles();
                if (selected.size() != 1 || Document::canonicalIdentity(selected.front()) != Document::canonicalIdentity(path)) {
                    stableSelection = 0; dialog->selectFile(QFileInfo(path).fileName()); return;
                }
                if (++stableSelection < 2) return;
                selectedBeforeAccept = selected.front();
                accepted = true; responder.stop();
                // Accept the verified selection synchronously; a queued accept
                // allows directory-model updates to replace it in between.
                if (!QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection)) {
                    dialogFailure = QStringLiteral("Could not accept the verified Save As dialog.");
                    dialog->reject();
                }
                return;
            }
    });
    responder.start();
    const bool scheduled = window.runCurrentDocument(&error);
    responder.stop();
    QVERIFY2(dialogFailure.isEmpty(), qPrintable(dialogFailure));
    QVERIFY2(scheduled, qPrintable(error));
    QVERIFY(accepted);
    QCOMPARE(Document::canonicalIdentity(selectedBeforeAccept), Document::canonicalIdentity(path));
    QCOMPARE(window.currentDocument()->identity(), Document::canonicalIdentity(path));
    QCOMPARE(readBytes(path), source.toUtf8());
    QVERIFY(!window.currentDocument()->isModified());
    QVERIFY(window.activeProject()->sources.contains(QFileInfo(path).fileName()));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("saved-as-output 11")), qPrintable(window.resultsText()));
    QVERIFY(window.close());
}

void GuiTests::unsafeDiskRunPreflightBlocksAllEntryPoints_data() {
    QTest::addColumn<QString>("diskState");
    QTest::addColumn<QString>("entryPoint");
    for (const QString& state : {QStringLiteral("external-clean"), QStringLiteral("external-dirty"),
            QStringLiteral("missing"), QStringLiteral("directory"), QStringLiteral("metadata")})
        for (const QString& entry : {QStringLiteral("F5-action"), QStringLiteral("selected-file"), QStringLiteral("console-run")})
            QTest::newRow(qPrintable(state + '-' + entry)) << state << entry;
}

void GuiTests::unsafeDiskRunPreflightBlocksAllEntryPoints() {
    QFETCH(QString, diskState); QFETCH(QString, entryPoint);
    QTemporaryDir temporary;
    MainWindow window(false, nullptr, false);
    QString error;
    QVERIFY(window.createProjectAt(temporary.path(), QStringLiteral("PreflightFailure"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY(window.submitCommand(QStringLiteral("real prior=17;"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    const QString path = window.currentDocument()->path();
    const QString manifest = window.activeProject()->manifestPath;
    const QByteArray original = readBytes(path), originalManifest = readBytes(manifest);
    auto* watcher = window.findChild<QFileSystemWatcher*>(); QVERIFY(watcher); watcher->blockSignals(true);
    if (diskState != QStringLiteral("external-clean"))
        replaceEditorText(window.currentEditor(), QStringLiteral("real forbidden=99; display(\"must-not-execute\");\n"));
    if (diskState.startsWith(QStringLiteral("external")))
        QVERIFY(writeBytes(path, QByteArrayLiteral("% newer disk version\n")));
    else if (diskState == QStringLiteral("missing")) QVERIFY(QFile::remove(path));
    else if (diskState == QStringLiteral("directory")) { QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path)); }
    else QVERIFY(writeBytes(manifest, originalManifest + "\n"));
    const QByteArray before = readBytes(path);
    QSignalSpy work(&window, &MainWindow::workFinished);
    if (entryPoint == QStringLiteral("console-run"))
        QVERIFY(!window.submitCommand(QStringLiteral("run(\"main.p\")"), &error));
    else if (entryPoint == QStringLiteral("F5-action")) {
        auto* action = window.findChild<QAction*>(QStringLiteral("runFile")); QVERIFY(action);
        QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("F5"))); action->trigger();
    } else {
        auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
        const auto items = tree->findItems(QStringLiteral("main.p"), Qt::MatchRecursive | Qt::MatchExactly);
        QCOMPARE(items.size(), 1); tree->setCurrentItem(items.front());
        auto* action = window.findChild<QAction*>(QStringLiteral("projectRunSource")); QVERIFY(action && action->isEnabled());
        action->trigger();
    }
    QTest::qWait(80);
    QVERIFY(!window.isWorking()); QCOMPARE(work.count(), 0);
    QCOMPARE(readBytes(path), before);
    QVERIFY(!window.resultsText().contains(QStringLiteral("must-not-execute")));
    QVERIFY(window.resultsText().contains(QStringLiteral("Error:")));
    QVERIFY(window.submitCommand(QStringLiteral("prior"), &error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(), 10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("17")), qPrintable(window.resultsText()));
    if (diskState == QStringLiteral("directory")) QVERIFY(QDir().rmdir(path));
    QVERIFY(writeBytes(path, original)); QVERIFY(writeBytes(manifest, originalManifest));
    QVERIFY(window.currentDocument()->reload(&error));
    QVERIFY(window.close());
}

void GuiTests::nativeTiledNamedFiguresWhenRequested() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void GuiTests::nativePaperOdeSubplotsWhenRequested() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void GuiTests::nativeMatrixAndArkodeExamplesWhenRequested() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void GuiTests::destructionWithoutCloseSafelyDisposesOpenDocuments() {
    QTemporaryDir temporary;
    for (int iteration=0; iteration<3; ++iteration) {
        auto window = std::make_unique<MainWindow>(false, nullptr, false);
        QString error;
        QVERIFY(window->createProjectAt(temporary.path(), QStringLiteral("Destructor%1").arg(iteration), &error));
        const QString path = window->currentDocument()->path();
        const QByteArray original = readBytes(path);
        replaceEditorText(window->currentEditor(), QStringLiteral("real unacknowledged=5;\n"));
        QVERIFY(window->checkCurrentDocument(&error));
        QPointer<Document> document(window->currentDocument());
        QPointer<EditorWidget> editor(window->currentEditor());
        // Deliberately skip close(): assertion unwinding and QApplication quit
        // must also safely dispose open editors with queued worker callbacks.
        window.reset();
        QVERIFY(document.isNull()); QVERIFY(editor.isNull());
        QApplication::processEvents();
        QCOMPARE(readBytes(path), original);
    }
}

void GuiTests::consoleDiagnosticsNavigateUnicodeAndRejectStale() {
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    window.resize(1100,700);window.show();
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("DiagnosticLinks"),&error));window.configureAutosave(false);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    const QString source=QString::fromUtf8("% Unicode λ 🙂\nreal good=2;\ndisplay(\"λ\",missing_name);\n");
    replaceEditorText(window.currentEditor(),source);
    QVERIFY2(window.runCurrentDocument(&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    auto* console=window.commandWindow();QVERIFY(console);
    QVERIFY2(window.resultsText().contains(QStringLiteral("main.p:3:13: Error E1201")),qPrintable(window.resultsText()));
    auto location=console->document()->find(QStringLiteral("main.p:3:13: Error E1201"));QVERIFY(!location.isNull());
    location.setPosition(location.selectionStart()+3);console->setTextCursor(location);console->centerCursor();
    QSignalSpy activated(console,&CommandWindow::diagnosticActivated);QSignalSpy submitted(console,&CommandWindow::commandSubmitted);
    console->setInputText(QStringLiteral("pending_command"));
    location=console->document()->find(QStringLiteral("main.p:3:13: Error E1201"));QVERIFY(!location.isNull());
    location.setPosition(location.selectionStart()+3);
    console->setTextCursor(location);console->centerCursor();
    const QPoint point=console->cursorRect(location).center();QVERIFY(console->viewport()->rect().contains(point));
    // The QWidget overload warps QCursor and may emit no move when Windows
    // already has that position. The QWindow overload always delivers a move
    // through Qt's window-system path, exercising the real hover handler.
    QTest::mouseMove(window.windowHandle(),console->viewport()->mapTo(&window,point));
    QCOMPARE(console->viewport()->cursor().shape(),Qt::PointingHandCursor);
    QTest::mouseClick(console->viewport(),Qt::LeftButton,Qt::NoModifier,point);
    QCOMPARE(activated.count(),1);QCOMPARE(submitted.count(),0);
    QCOMPARE(window.currentEditor()->textCursor().selectedText(),QStringLiteral("missing_name"));
    QCOMPARE(window.currentEditor()->textCursor().blockNumber(),2);
    QCOMPARE(console->inputText(),QStringLiteral("pending_command"));
    const auto bytes=readBytes(window.currentDocument()->path());
    auto* editor=window.currentEditor();editor->moveCursor(QTextCursor::Start);editor->insertPlainText(QStringLiteral("% changed\n"));
    const auto before=editor->textCursor();const auto revision=window.currentDocument()->revision();
    location=console->document()->find(QStringLiteral("main.p:3:13: Error E1201"));QVERIFY(!location.isNull());
    location.setPosition(location.selectionStart()+3);console->setTextCursor(location);console->centerCursor();
    QTest::mouseClick(console->viewport(),Qt::LeftButton,Qt::NoModifier,console->cursorRect(location).center());
    QCOMPARE(activated.count(),2);QCOMPARE(editor->textCursor().position(),before.position());
    QCOMPARE(window.currentDocument()->revision(),revision);QCOMPARE(readBytes(window.currentDocument()->path()),bytes);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("earlier revision")));
    QVERIFY(window.saveAllDocuments(&error));QVERIFY(window.close());
}

void GuiTests::consoleDiagnosticsNavigateRuntimeAndMultipleFiles() {
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    window.resize(1100,700);window.show();
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("DiagnosticFiles"),&error));window.configureAutosave(false);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    const QString mainPath=window.currentDocument()->path();
    replaceEditorText(window.currentEditor(),QStringLiteral("real zero=0;\nreal result=1/zero;\n"));
    QVERIFY(window.runCurrentDocument(&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    auto* console=window.commandWindow();
    auto click=[console](const QString& needle){
        auto cursor=console->document()->find(needle);if(cursor.isNull())return false;
        cursor.setPosition(cursor.selectionStart()+2);console->setTextCursor(cursor);console->centerCursor();
        QTest::mouseClick(console->viewport(),Qt::LeftButton,Qt::NoModifier,console->cursorRect(cursor).center());return true;
    };
    QVERIFY2(window.resultsText().contains(QStringLiteral("main.p:2:13: Error R1001")),qPrintable(window.resultsText()));
    QVERIFY(click(QStringLiteral("main.p:2:13: Error R1001")));
    QCOMPARE(window.currentEditor()->textCursor().selectedText(),QStringLiteral("1/zero"));
    QCOMPARE(window.currentEditor()->textCursor().blockNumber(),1);
    replaceEditorText(window.currentEditor(),QStringLiteral("display(missing_A);\n"));QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    QVERIFY(window.addSource(QStringLiteral("nested/second.p"),&error));
    const QString secondPath=window.currentDocument()->path();
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));
    auto* closeTab=window.findChild<QAction*>(QStringLiteral("fileCloseTab"));QVERIFY(closeTab);closeTab->trigger();
    QCOMPARE(window.currentDocument()->path(),mainPath);
    const QByteArray secondSource=QString::fromUtf8("% λ 🙂\r\nreal okay=1;\r\ndisplay(missing_B);\r\n").toUtf8();
    QVERIFY(writeBytes(secondPath,secondSource));
    QVERIFY(window.checkProject(&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    QVERIFY2(window.resultsText().contains(QStringLiteral("second.p:3:9: Error E1201")),qPrintable(window.resultsText()));
    QVERIFY(click(QStringLiteral("second.p:3:9: Error E1201")));
    QCOMPARE(window.currentDocument()->path(),secondPath);
    QCOMPARE(window.currentEditor()->textCursor().selectedText(),QStringLiteral("missing_B"));
    QCOMPARE(window.currentEditor()->textCursor().blockNumber(),2);
    QVERIFY(click(QStringLiteral("main.p:1:9: Error E1201")));
    QCOMPARE(window.currentDocument()->path(),mainPath);
    QCOMPARE(window.currentEditor()->textCursor().selectedText(),QStringLiteral("missing_A"));
    QCOMPARE(readBytes(secondPath),secondSource);QVERIFY(window.close());
}

void GuiTests::consoleDiagnosticLinksProtectInputAndKeyboard() {
    CommandWindow console;console.resize(700,350);console.show();
    console.appendDiagnostic(QStringLiteral("test.p:4:2: Error E1201: missing name"),17);
    console.setInputText(QStringLiteral("real draft=2;"));console.appendOutput(QStringLiteral("other output"));
    QSignalSpy activated(&console,&CommandWindow::diagnosticActivated),submitted(&console,&CommandWindow::commandSubmitted);
    auto cursor=console.document()->find(QStringLiteral("test.p"));QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionStart()+2);console.setTextCursor(cursor);console.ensureCursorVisible();
    QTest::keyClick(&console,Qt::Key_Return);
    QCOMPARE(activated.count(),1);QCOMPARE(activated.at(0).at(0).toULongLong(),quint64(17));
    QCOMPARE(submitted.count(),0);QCOMPARE(console.inputText(),QStringLiteral("real draft=2;"));
    cursor=console.document()->find(QStringLiteral("test.p"));cursor.movePosition(QTextCursor::Right,QTextCursor::KeepAnchor);
    QVERIFY(cursor.charFormat().isAnchor());
    QSignalSpy removed(&console,&CommandWindow::diagnosticLinksRemoved);console.clearTranscript();
    QCOMPARE(removed.count(),1);QCOMPARE(console.inputText(),QStringLiteral("real draft=2;"));
    QVERIFY(!console.document()->toPlainText().contains(QStringLiteral("test.p")));
}

PENTA_OFFSCREEN_TEST_MAIN(GuiTests)

#include "gui_tests.moc"
