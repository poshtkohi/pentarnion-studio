#pragma once

#include "project_service.hpp"
#include "recovery_store.hpp"
#include "execution_coordinator.hpp"

#include <QHash>
#include <QDateTime>
#include <QKeySequence>
#include <QMainWindow>
#include <QMargins>
#include <QPointer>
#include <QSet>
#include <QSizePolicy>
#include <QVector>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <memory>

class QAction;
class QDockWidget;
class QCloseEvent;
class QFileSystemWatcher;
class QLabel;
class QMenu;
class QLineEdit;
class QPlainTextEdit;
class QSettings;
class QTabWidget;
class QTimer;
class QPrinter;
class QTextDocument;
class QTreeWidget;
class QTreeWidgetItem;

namespace penta {

namespace desktop {class DesktopShell;class StudioWorkspaceLayout;class SnapshotController;class ParallelPoolIndicator;class FigureDockController;class WorkerActivityMonitor;}
namespace assistant {class AssistantController;struct SourceSnapshot;}
namespace workbook {class WorkbookDocument;}

class Document;
class EditorWidget;
class CommandWindow;
class FigureManager;
class FindReplaceBar;
class FindReplaceDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    // Disabling persistence gives tests and smoke launches isolated state.
    explicit MainWindow(bool persistenceEnabled = true, QWidget* parent = nullptr,
                        bool figureRenderingEnabled = true);
    ~MainWindow() override;

    bool createProjectAt(const QString& parent, const QString& name,
                         QString* error = nullptr);
    bool openProjectPath(const QString& manifest, QString* error = nullptr);
    bool openSourcePath(const QString& path, QString* error = nullptr);
    bool addSource(const QString& relative, QString* error = nullptr);
    bool saveCurrentDocument(QString* error = nullptr);
    bool saveCurrentDocumentAs(const QString& path, QString* error = nullptr,
                               bool overwriteExisting = false);
    bool saveAllDocuments(QString* error = nullptr);
    bool renameProjectSource(const QString& oldRelative, const QString& newRelative,
                             QString* error = nullptr);
    bool removeProjectSource(const QString& relative, bool deleteFile = false,
                             QString* error = nullptr);
    const Project* activeProject() const;
    Document* currentDocument() const;
    EditorWidget* currentEditor() const;
    int documentCount() const;
    void restoreStartupState(bool restoreSession = true);
    bool checkCurrentDocument(QString* error = nullptr);
    bool checkProject(QString* error = nullptr);
    bool runCurrentDocument(QString* error = nullptr);
    void stopExecution();
    bool isWorking() const { return m_workPending; }
    QString resultsText() const;
    int problemCount() const;
    bool enableInterpreter(QString* error = nullptr);
    void navigateProblem(int row);
    bool submitCommand(const QString& command, QString* error = nullptr);
    bool runScript(const QString& path, QString* error = nullptr);
    void clearWorkspace();
    CommandWindow* commandWindow() const { return m_console; }
    QString consoleInput() const;
    void setConsoleInput(const QString& text);
    QString workspaceContextText() const;
    QString calculationStatus() const { return m_calculationStatus; }
    FigureManager* figureManager() const { return m_figures; }
    void configureAutosave(bool enabled, int periodicMilliseconds = 60000);
    bool openWorkbook(const QString& path,QString* error=nullptr);
    bool closeAllDocuments();
    workbook::WorkbookDocument* currentWorkbook()const;
    desktop::DesktopShell* desktopShell()const{return m_desktop;}
    bool autosaveEnabled() const { return m_autosaveEnabled; }
    int autosaveInterval() const { return m_autosaveInterval; }
    std::unique_ptr<QTextDocument> currentSourcePrintSnapshot(QString* error = nullptr);

signals:
    void workFinished(bool execution, bool stale);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class MainWindowIdleIoTestAccess;
    struct Tab {
        Document* document = nullptr;
        EditorWidget* editor = nullptr;
        QString recoveryId;
        quint64 id = 0;
    };
    void buildUi();
    void initializeDesktop();
    void refreshWorkerActivity();
    QMainWindow* dockHost(QDockWidget*)const;
    void showSourceWorkspace();
    void requestOpenWorkbook();
    void requestNewWorkbook();
    bool saveWorkbookInteractive(workbook::WorkbookDocument*,bool saveAs=false);
    bool prepareCloseWorkbooks();
    bool closeWorkspacePage(int index);
    assistant::SourceSnapshot assistantSnapshot()const;
    bool applyAssistantSource(const assistant::SourceSnapshot&,const QString&,QString*);
    QAction* action(const QString& name, const QString& text,
                    const QKeySequence& shortcut = {});
    void bindEditorAction(QAction* action, void (EditorWidget::*method)());
    void attachDocument(Document* document, const QString& recoveryId = {},
                        int cursorPosition = 0);
    int indexOf(Document* document) const;
    int indexOfPath(const QString& path) const;
    void updateUi();
    void refreshTree();
    void refreshWatchers();
    void checkExternalChanges();
    QByteArray diskFingerprint(const QString& path) const;
    void snapshotRecovery();
    void acknowledgeRecovery(const Tab& tab);
    bool prepareClose(Document* document);
    bool prepareCloseAll();
    void disposeTab(int index, bool acknowledged);
    void disposeAllTabs(bool acknowledged);
    bool closeTab(int index);
    bool saveInteractive(Document* document);
    bool saveAsInteractive(Document* document);
    bool saveDocumentAs(Document* document, const QString& path, bool overwriteExisting,
                        QString* error, QString* projectWarning = nullptr);
    void showError(const QString& title, const QString& message);
    void requestNewProject();
    void requestOpenProject();
    void requestNewSource();
    void requestOpenSource();
    void requestAddExistingSource();
    void requestNewFolder();
    void requestRenameSource();
    void requestRemoveSource(bool deleteFile);
    void requestCloseProject();
    void showFindReplace();
    void showLanguageReference();
    void showAbout();
    void readSettings();
    void writeSettings();
    void rememberProject(const QString& manifest);
    void rebuildRecentMenu();
    QString selectedRelativeSource() const;
    QString selectedRelativeFolder() const;
    QString referencePath(const QString& name) const;
    SourceSnapshot snapshotForDocument(int index) const;
    void assignSnapshotContext(SourceSnapshot& snapshot) const;
    bool snapshotIsCurrent(const SourceSnapshot& snapshot) const;
    bool importSnapshotsAreCurrent(const SourceSnapshot& snapshot) const;
    void applyWorkResult(const WorkResult& result);
    void appendProblems(const SourceSnapshot& source,
                        const std::vector<language::Diagnostic>& diagnostics);
    void appendConsoleDiagnostics(const SourceSnapshot& source,
                                  const std::vector<language::Diagnostic>& diagnostics);
    void navigateDiagnostic(const SourceSnapshot& source, language::SourceSpan span);
    void invalidateDocumentAnalysis(Document* document);
    void requestEnableInterpreter();
    void refreshRunFreshness();
    void appendCommandOutput(const QString& text);
    void refreshCommandContext();
    bool scheduleCurrentRun(QString* error, bool showMarker);
    bool runScriptImpl(const QString& path, QString* error, bool showMarker);
    void rememberCommand(const QString& command);
    void showCalculationError(const QString& error);
    void setCalculationStatus(const QString& status);
    void scheduleAutosave(Document* document);
    void autosavePendingDocuments();
    void autosaveNamedDocuments();
    void tryAutosave(Document* document);
    void showAutosaveSettings();
    QPrinter* sourcePrinter();
    void requestPrintSource();
    void requestPrintPreview();
    void requestPageSetup();

    bool m_persistenceEnabled = true;
    bool m_handlingExternal = false;
    bool m_closing = false;
    bool m_printDialogActive = false;
    std::unique_ptr<QPrinter> m_sourcePrinter;
    int m_fontSize = 11;
    std::optional<Project> m_project;
    QVector<Tab> m_documents;
    QHash<Document*, QByteArray> m_observedDisk;
    struct RecoveryCheckpoint {
        quint64 revision = 0;
        QString recoveryId, path, projectPath, newline;
        bool bom = false;
        int cursorPosition = 0;
        qint64 fileSize = -1;
        QDateTime modified, created, metadataChanged;
        bool operator==(const RecoveryCheckpoint&) const = default;
    };
    QHash<quint64, RecoveryCheckpoint> m_recoveryCheckpoints;
    QHash<QString, QAction*> m_actions;
    RecoveryStore m_recovery;
    QSettings* m_settings = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTabWidget* m_workspaceTabs = nullptr;
    QWidget* m_sourceSurface = nullptr;
    desktop::DesktopShell* m_desktop = nullptr;
    desktop::StudioWorkspaceLayout* m_workspaceLayout = nullptr;
    desktop::SnapshotController* m_snapshots = nullptr;
    desktop::ParallelPoolIndicator* m_workerIndicator = nullptr;
    desktop::FigureDockController* m_figureFrame = nullptr;
    std::unique_ptr<desktop::WorkerActivityMonitor> m_workerMonitor;
    QTimer* m_workerStatusTimer = nullptr;
    quint64 m_workerRequest = 0;
    std::uint64_t m_workerSequence = 0;
    std::size_t m_peakWorkers = 0;
    assistant::AssistantController* m_assistant = nullptr;
    FindReplaceBar* m_findBar = nullptr;
    FindReplaceDialog* m_findDialog = nullptr;
    QTreeWidget* m_tree = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_externalChangeTimer = nullptr;
    QTimer* m_recoveryTimer = nullptr;
    QLabel* m_documentStatus = nullptr;
    QLabel* m_contextStatus = nullptr;
    QMenu* m_recentMenu = nullptr;
    QTreeWidget* m_problems = nullptr;
    CommandWindow* m_console = nullptr;
    struct ConsoleDiagnostic {SourceSnapshot source;language::SourceSpan span;};
    QHash<quint64,ConsoleDiagnostic> m_consoleDiagnostics;
    quint64 m_nextConsoleDiagnostic=0;
    FigureManager* m_figures = nullptr;
    bool m_figureRenderingEnabled = true;
    QString m_calculationStatus;
    QString m_figureError;
    bool m_autosaveEnabled = true;
    int m_autosaveInterval = 60000;
    QTimer* m_autosaveDebounce = nullptr;
    QTimer* m_autosavePeriodic = nullptr;
    QSet<quint64> m_autosavePending;
    quint64 m_activeDocumentId = 0;
    ExecutionCoordinator* m_execution = nullptr;
    quint64 m_nextDocumentId = 0;
    quint64 m_latestRequest = 0;
    WorkKind m_latestKind = WorkKind::CheckFile;
    bool m_workPending = false;
    bool m_returnToCommandPrompt = false;
    std::optional<SourceSnapshot> m_lastRunSnapshot;
    struct AnalysedContext { quint64 revision; QString algebraId; double gamma; QString version; };
    struct DefaultDisplayContextKey {
        QString path, projectRoot, projectPath, projectAlgebra, projectVersion;
        double projectGamma = 1;
        bool hasProject = false;
        bool operator==(const DefaultDisplayContextKey&) const = default;
    };
    struct DefaultDisplayContext {
        DefaultDisplayContextKey key;
        QString algebraId, version;
        double gamma = 1;
    };
    QHash<quint64, DefaultDisplayContext> m_defaultDisplayContexts;
    QHash<quint64, AnalysedContext> m_analysisContexts;
    std::optional<AnalysedContext> m_consoleContext;
};

} // namespace penta
