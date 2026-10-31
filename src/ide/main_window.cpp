#include "main_window.hpp"
#include "desktop/desktop_shell.hpp"
#include "desktop/about_dialog.hpp"
#include "desktop/project_file_icons.hpp"
#include "assistant/assistant_controller.hpp"
#include "workbook/workbook_document.hpp"
#include <QTabBar>

#include "document.hpp"
#include "editor_widget.hpp"
#include "find_replace_bar.hpp"
#include "find_replace_dialog.hpp"
#include "desktop/studio_workspace_layout.hpp"
#include "desktop/parallel_pool_indicator.hpp"
#include "desktop/worker_activity.hpp"
#include "map_explorer.hpp"
#include "research_explorer.hpp"
#include "command_window.hpp"
#include "figure_manager.hpp"
#include "studio_branding.hpp"
#include "source_printing.hpp"
#include "source_print_dialog.hpp"
#include "code_export_dialog.hpp"
#include "syntax_highlighter.hpp"
#include "source_assistance.hpp"
#include "feature_browser.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPrinter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPageSetupDialog>
#include <QPushButton>
#include <QSettings>
#include "distribution_paths.hpp"
#include <QSet>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace penta {
namespace {
constexpr int RelativeRole = Qt::UserRole;
constexpr int FolderRole = Qt::UserRole + 1;
constexpr int SnapshotRole = Qt::UserRole + 10;
constexpr int OffsetRole = Qt::UserRole + 11;
constexpr int LengthRole = Qt::UserRole + 12;

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

class ProjectSourceRenameDelegate final : public QStyledItemDelegate {
public:
    using Commit = std::function<void(const QString&,const QString&,const QString&)>;
    ProjectSourceRenameDelegate(QObject* parent,Commit commit)
        :QStyledItemDelegate(parent),m_commit(std::move(commit)){}
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,
                          const QModelIndex& index) const override {
        if(index.data(FolderRole).toBool()||index.data(RelativeRole).toString().isEmpty())return nullptr;
        auto* editor=QStyledItemDelegate::createEditor(parent,option,index);
        if(editor)editor->setObjectName(QStringLiteral("projectSourceRenameEditor"));
        return editor;
    }
    void setEditorData(QWidget* editor,const QModelIndex& index) const override {
        QStyledItemDelegate::setEditorData(editor,index);
        if(auto* line=qobject_cast<QLineEdit*>(editor)){
            const int extension=line->text().lastIndexOf(QLatin1Char('.'));
            line->setSelection(0,extension>0?extension:line->text().size());
        }
    }
    void setModelData(QWidget* editor,QAbstractItemModel*,const QModelIndex& index) const override {
        const auto* line=qobject_cast<QLineEdit*>(editor);
        if(!line)return;
        QModelIndex root=index;
        while(root.parent().isValid())root=root.parent();
        const QString projectRoot=root.data(Qt::ToolTipRole).toString();
        const QString relative=index.data(RelativeRole).toString();
        const QString filename=line->text();
        // renameProjectSource rebuilds the tree. Let Qt close this transient
        // editor before committing so no live delegate/model index is deleted.
        QTimer::singleShot(0,parent(),[commit=m_commit,projectRoot,relative,filename]{
            commit(projectRoot,relative,filename);
        });
    }
private:
    Commit m_commit;
};

class SourcePrintInteraction final {
public:
    SourcePrintInteraction(bool& active,QTimer* debounce,QTimer* periodic,QSet<quint64>& pending,int interval)
        :m_active(active),m_previous(active),m_periodic(periodic),m_restart(periodic&&periodic->isActive()),m_interval(interval){
        m_active=true;if(debounce)debounce->stop();pending.clear();if(periodic)periodic->stop();
    }
    ~SourcePrintInteraction(){m_active=m_previous;if(m_restart)m_periodic->start(m_interval);}
private:
    bool& m_active;bool m_previous;QTimer* m_periodic;bool m_restart;int m_interval;
};

QString asPortableRelative(const QString& root, const QString& path) {
    return QDir::fromNativeSeparators(QDir(root).relativeFilePath(path));
}

enum class RunCommandMatch { NotRun, Valid, Invalid };

RunCommandMatch parseRunCommand(const QString& command, QString& path, QString* error) {
    QString literal=command.trimmed();
    if(literal.endsWith(QLatin1Char(';')))literal.chop(1);
    literal=literal.trimmed();
    if(literal.startsWith(QStringLiteral("run"))&&literal.size()>3&&literal[3].isSpace()) {
        path=literal.mid(3).trimmed();
        if(path.startsWith(QLatin1Char('"'))) {
            return parseRunCommand(QStringLiteral("run(")+path+QStringLiteral(")"),path,error);
        }
        if(path.isEmpty()||std::any_of(path.begin(),path.end(),[](QChar c){return c.unicode()<32;})) {
            fail(error,QStringLiteral("Use run main.p, run \"path with spaces.p\", or run(\"path.p\")."));
            return RunCommandMatch::Invalid;
        }
        return RunCommandMatch::Valid;
    }
    const QByteArray bytes = command.toUtf8();
    const auto lexed = language::lex(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    std::vector<language::Token> tokens;
    for (const auto& token : lexed.tokens)
        if (token.kind != language::TokenKind::Comment && token.kind != language::TokenKind::EndOfFile)
            tokens.push_back(token);
    auto spelling = [&bytes](const language::Token& token) {
        return bytes.mid(static_cast<qsizetype>(token.span.offset), static_cast<qsizetype>(token.span.length));
    };
    if (tokens.size() < 2 || spelling(tokens[0]) != "run" || spelling(tokens[1]) != "(")
        return RunCommandMatch::NotRun;
    if (!lexed.diagnostics.empty() || (tokens.size() != 4 && tokens.size() != 5) ||
        tokens[2].kind != language::TokenKind::String || spelling(tokens[3]) != ")" ||
        (tokens.size() == 5 && spelling(tokens[4]) != ";")) {
        fail(error, QStringLiteral("Use run main.p, run \"path with spaces.p\", or run(\"path.p\") with one literal path."));
        return RunCommandMatch::Invalid;
    }
    const QByteArray synthetic = "display(" + spelling(tokens[2]) + ");";
    const auto decoded = language::analyse(std::string_view(synthetic.constData(), static_cast<std::size_t>(synthetic.size())));
    if (!decoded.ok() || decoded.program.statements.size() != 1 ||
        decoded.program.statements.front().arguments.size() != 1 ||
        decoded.program.statements.front().arguments.front()->kind != language::ExprKind::StringLiteral) {
        fail(error, QStringLiteral("The run path must be a valid quoted string literal."));
        return RunCommandMatch::Invalid;
    }
    path = QString::fromStdString(decoded.program.statements.front().arguments.front()->text);
    if (path.isEmpty() || std::any_of(path.begin(), path.end(), [](QChar c) { return c.unicode() < 32; })) {
        fail(error, QStringLiteral("The script path must be nonempty and contain no control characters."));
        return RunCommandMatch::Invalid;
    }
    return RunCommandMatch::Valid;
}
} // namespace

MainWindow::MainWindow(bool persistenceEnabled, QWidget* parent, bool figureRenderingEnabled)
    : QMainWindow(parent), m_persistenceEnabled(persistenceEnabled),
      m_figureRenderingEnabled(figureRenderingEnabled), m_autosaveEnabled(persistenceEnabled) {
    setObjectName(QStringLiteral("studioMainWindow"));
    setWindowIcon(studioApplicationIcon());
    if (m_persistenceEnabled) {
        const auto installedSettings=distributionSettingsPath(QStringLiteral("Workbench.ini"));
        if(!installedSettings.isEmpty())m_settings=new QSettings(installedSettings,QSettings::IniFormat,this);
        else m_settings = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                  QStringLiteral("Pentarnion"),
                                  QStringLiteral("Workbench"), this); // Stable settings identifier.
    }
    buildUi();
    initializeDesktop();
    qApp->installEventFilter(this);
    m_execution = new ExecutionCoordinator(this);
    connect(m_execution, &ExecutionCoordinator::finished, this, &MainWindow::applyWorkResult);
    connect(m_execution,&ExecutionCoordinator::figureProgress,this,[this](const FigureProgress& progress){
        if(progress.requestId!=m_latestRequest||!m_workPending)return;
        if((!progress.snapshot.command&&!snapshotIsCurrent(progress.snapshot))||!importSnapshotsAreCurrent(progress.snapshot))return;
        m_figures->apply(progress.actions);
        if(auto* dock=findChild<QDockWidget*>(QStringLiteral("figuresDock"))){if(m_figures->figureCount()){dock->show();dock->raise();}}
    });
    m_autosaveDebounce = new QTimer(this);
    m_autosaveDebounce->setSingleShot(true); m_autosaveDebounce->setInterval(500);
    connect(m_autosaveDebounce, &QTimer::timeout, this, &MainWindow::autosavePendingDocuments);
    m_autosavePeriodic = new QTimer(this);
    connect(m_autosavePeriodic, &QTimer::timeout, this, &MainWindow::autosaveNamedDocuments);
    readSettings();
    if(m_desktop)m_desktop->restorePreferences(!m_settings||m_settings->contains(QStringLiteral("window/outerLayoutV2")));
    if(m_workspaceLayout)m_workspaceLayout->ensureSeparatedLayout();
    configureAutosave(m_autosaveEnabled, m_autosaveInterval);
    updateUi();
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setInterval(1500);
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::snapshotRecovery);
    if (m_persistenceEnabled) m_recoveryTimer->start();
}

MainWindow::~MainWindow() {
    qApp->removeEventFilter(this);
    if(m_assistant)m_assistant->shutdown();
    if (m_autosaveDebounce) m_autosaveDebounce->stop();
    if (m_autosavePeriodic) m_autosavePeriodic->stop();
    if (m_recoveryTimer) m_recoveryTimer->stop();
    if (m_externalChangeTimer) m_externalChangeTimer->stop();
    delete m_execution; // Cancel/join the worker before destroying editor state.
    m_execution = nullptr;
    // Unexpected destruction must not silently acknowledge unsaved snapshots.
    if (!m_closing) snapshotRecovery();
    m_closing = true;
    // QObject-owned widgets outlive the derived members during base-class
    // destruction. Disconnect their callbacks and dispose source tabs now,
    // while the document/analysis containers they use are still alive.
    for (auto* child : findChildren<QObject*>()) QObject::disconnect(child, nullptr, this, nullptr);
    if(m_findBar)m_findBar->setEditor(nullptr);
    disposeAllTabs(false);
}

QAction* MainWindow::action(const QString& name, const QString& text,
                            const QKeySequence& shortcut) {
    auto* result = new QAction(text, this);
    result->setObjectName(name);
    if (!shortcut.isEmpty()) result->setShortcut(shortcut);
    m_actions.insert(name, result);
    return result;
}

void MainWindow::bindEditorAction(QAction* target, void (EditorWidget::*method)()) {
    connect(target, &QAction::triggered, this, [this, method] {
        if (auto* editor = currentEditor()) (editor->*method)();
    });
}

void MainWindow::buildUi() {
    resize(1400, 900);
    setDockOptions(QMainWindow::AllowTabbedDocks|QMainWindow::AllowNestedDocks);
    auto* editorSurface=new QWidget(this);
    editorSurface->setObjectName(QStringLiteral("editorSurface"));
    auto* editorLayout=new QVBoxLayout(editorSurface);
    editorLayout->setContentsMargins(0,0,0,0);editorLayout->setSpacing(0);
    m_tabs = new QTabWidget(editorSurface);
    m_tabs->setObjectName(QStringLiteral("editorTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(false); // Registry order is the visible tab order.
    m_tabs->setDocumentMode(true);
    editorLayout->addWidget(m_tabs,1);
    m_findDialog=new FindReplaceDialog(this,m_settings);
    m_findBar=m_findDialog->bar();
    m_sourceSurface=editorSurface;
    m_workspaceTabs=new QTabWidget(this);m_workspaceTabs->setObjectName(QStringLiteral("workspaceTabs"));
    m_workspaceTabs->setDocumentMode(true);m_workspaceTabs->setTabsClosable(true);m_workspaceTabs->setMovable(true);
    m_workspaceTabs->tabBar()->setAutoHide(true);m_workspaceTabs->addTab(editorSurface,tr("Scripts"));
    m_workspaceTabs->tabBar()->setTabButton(0,QTabBar::RightSide,nullptr);
    setCentralWidget(m_workspaceTabs);
    connect(m_workspaceTabs,&QTabWidget::tabCloseRequested,this,[this](int index){closeWorkspacePage(index);});
    connect(m_workspaceTabs,&QTabWidget::currentChanged,this,[this]{if(m_findDialog)m_findDialog->followEditor(currentEditor(),currentDocument()?QFileInfo(currentDocument()->path()).fileName():QString());if(m_assistant)m_assistant->invalidateSource();if(m_console)updateUi();});
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        for (const auto& tab : m_documents)
            if (tab.id == m_activeDocumentId) scheduleAutosave(tab.document);
        const int current = m_tabs->currentIndex();
        m_activeDocumentId = current >= 0 && current < m_documents.size() ? m_documents[current].id : 0;
        if(m_findDialog)m_findDialog->followEditor(currentEditor(),currentDocument()?QFileInfo(currentDocument()->path()).fileName():QString());
        updateUi();
    });

    auto* projectDock = new QDockWidget(tr("Project"), this);
    projectDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    projectDock->setObjectName(QStringLiteral("projectDock"));
    m_tree = new QTreeWidget(projectDock);
    m_tree->setObjectName(QStringLiteral("projectTree"));
    m_tree->setHeaderHidden(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setItemDelegate(new ProjectSourceRenameDelegate(m_tree,
        [this](const QString& projectRoot,const QString& relative,const QString& proposed){
            if(!m_project||m_project->rootPath!=projectRoot)return;
            QString filename=proposed;
            QString error;
            if(filename.trimmed().isEmpty())error=tr("Enter a source filename.");
            else if(filename.contains(QLatin1Char('/'))||filename.contains(QLatin1Char('\\')))
                error=tr("Enter a filename in the current folder, without path separators.");
            if(!error.isEmpty()){
                statusBar()->showMessage(tr("Rename failed: %1").arg(error),15000);return;
            }
            if(!filename.contains(QLatin1Char('.')))filename+=QStringLiteral(".p");
            const int separator=relative.lastIndexOf(QLatin1Char('/'));
            const QString renamed=relative.left(separator+1)+filename;
            if(renamed==relative)return;
            if(!renameProjectSource(relative,renamed,&error))
                statusBar()->showMessage(tr("Rename failed: %1").arg(error),15000);
            else statusBar()->showMessage(tr("Renamed %1 to %2.")
                .arg(QFileInfo(relative).fileName(),filename),8000);
        }));
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    projectDock->setWidget(m_tree);
    addDockWidget(Qt::LeftDockWidgetArea, projectDock);
    connect(m_tree,&QTreeWidget::itemDoubleClicked,this,
            [this](QTreeWidgetItem* item,int){
        if(!m_project||!item||item->data(0,FolderRole).toBool())return;
        const QString relative=item->data(0,RelativeRole).toString();
        QString path,error;
        if(m_project->sources.contains(relative)&&
            QFileInfo(relative).suffix().compare(QStringLiteral("p"),Qt::CaseInsensitive)==0){
            if(!ProjectService::resolveSource(*m_project,relative,path,&error)||
                !openSourcePath(path,&error))showError(tr("Open source"),error);
            return;
        }
        if(!workbook::WorkbookDocument::supportsPath(relative))return;
        const QString canonicalRoot=QFileInfo(m_project->rootPath).canonicalFilePath();
        const QString rootPrefix=canonicalRoot.endsWith('/')?canonicalRoot:canonicalRoot+'/';
        bool safe=!relative.isEmpty()&&!QDir::isAbsolutePath(relative)&&
            QDir::cleanPath(relative)==relative&&!relative.contains('\\');
        QString current=m_project->rootPath;
        for(const auto& part:relative.split('/',Qt::SkipEmptyParts)){
            if(part==QStringLiteral(".")||part==QStringLiteral("..")){safe=false;break;}
            current=QDir(current).filePath(part);
            const QFileInfo component(current);
            if(component.isSymLink()||component.isJunction()){safe=false;break;}
        }
        const QFileInfo file(current);
#ifdef Q_OS_WIN
        constexpr auto sensitivity=Qt::CaseInsensitive;
#else
        constexpr auto sensitivity=Qt::CaseSensitive;
#endif
        if(!safe||canonicalRoot.isEmpty()||!file.isFile()||
            !file.canonicalFilePath().startsWith(rootPrefix,sensitivity)){
            showError(tr("Open workbook"),tr("The selected data file is no longer an ordinary file inside this project."));
            return;
        }
        if(!openWorkbook(current,&error))showError(tr("Open workbook"),error);
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::updateUi);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        if (auto* item = m_tree->itemAt(point)) m_tree->setCurrentItem(item);
        QMenu menu(this);
        if (!selectedRelativeSource().isEmpty()) {
            menu.addAction(m_actions.value(QStringLiteral("projectRunSource")));
            menu.addSeparator();
        }
        menu.addAction(m_actions.value(QStringLiteral("fileNewSource")));
        menu.addAction(m_actions.value(QStringLiteral("projectNewFolder")));
        if (!selectedRelativeSource().isEmpty()) {
            menu.addSeparator();
            menu.addAction(m_actions.value(QStringLiteral("projectRenameSource")));
            menu.addAction(m_actions.value(QStringLiteral("projectRemoveSource")));
            menu.addAction(m_actions.value(QStringLiteral("projectDeleteSource")));
        }
        menu.exec(m_tree->viewport()->mapToGlobal(point));
    });

    auto* problemsDock = new QDockWidget(tr("Problems"), this);
    problemsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    problemsDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    problemsDock->setObjectName(QStringLiteral("problemsDock"));
    m_problems = new QTreeWidget(problemsDock);
    m_problems->setObjectName(QStringLiteral("problemsList"));
    m_problems->setHeaderLabels({tr("Severity"), tr("Code"), tr("File"), tr("Line"), tr("Column"), tr("Message")});
    m_problems->setRootIsDecorated(false);
    m_problems->setAlternatingRowColors(true);
    m_problems->header()->setStretchLastSection(true);
    for (int column : {0, 1, 3, 4}) m_problems->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    m_problems->setColumnWidth(2, 220);
    connect(m_problems, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        navigateProblem(m_problems->indexOfTopLevelItem(item));
    });
    problemsDock->setWidget(m_problems);
    addDockWidget(Qt::BottomDockWidgetArea, problemsDock);
    problemsDock->hide();

    auto* resultsDock = new QDockWidget(tr("Command Window"), this);
    resultsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    resultsDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    resultsDock->setObjectName(QStringLiteral("resultsDock"));
    m_console = new CommandWindow(resultsDock);
    connect(m_console,&CommandWindow::diagnosticActivated,this,[this](quint64 identifier){
        const auto found=m_consoleDiagnostics.constFind(identifier);
        if(found!=m_consoleDiagnostics.constEnd()) {
            const auto diagnostic=*found;navigateDiagnostic(diagnostic.source,diagnostic.span);
        }
    });
    connect(m_console,&CommandWindow::diagnosticLinksRemoved,this,[this](const QList<quint64>& identifiers){
        for(const auto identifier:identifiers)m_consoleDiagnostics.remove(identifier);
    });
    connect(m_console, &CommandWindow::commandSubmitted, this, [this](const QString& command) {
        QString error;
        if (!submitCommand(command, &error) && !error.isEmpty())
            statusBar()->showMessage(error, 10000);
    });
    resultsDock->setWidget(m_console);
    connect(m_console,&CommandWindow::interruptRequested,this,&MainWindow::stopExecution);
    addDockWidget(Qt::BottomDockWidgetArea, resultsDock);
    tabifyDockWidget(problemsDock, resultsDock);
    resultsDock->raise();

    auto* figuresDock = new QDockWidget(tr("Figures"), this);
    figuresDock->setObjectName(QStringLiteral("figuresDock"));
    figuresDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_figures = new FigureManager(figuresDock, m_figureRenderingEnabled);
    figuresDock->setWidget(m_figures); addDockWidget(Qt::RightDockWidgetArea, figuresDock);
    figuresDock->hide();
    connect(m_figures, &FigureManager::error, this, [this](const QString& error) {
        m_figureError = error;
        m_console->appendError(tr("Plot error: %1").arg(error));
        setCalculationStatus(tr("Plotting failed: %1").arg(error));
    });

    auto* file = menuBar()->addMenu(tr("&File"));file->setObjectName(QStringLiteral("studioFileMenu"));
    auto* a = action(QStringLiteral("fileNewProject"), tr("New &Project…"),
                     QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    a->setIcon(style()->standardIcon(QStyle::SP_DirIcon));
    connect(a, &QAction::triggered, this, &MainWindow::requestNewProject); file->addAction(a);
    a = action(QStringLiteral("fileOpenProject"), tr("&Open Project…"),
               QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    a->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(a, &QAction::triggered, this, &MainWindow::requestOpenProject); file->addAction(a);
    m_recentMenu = file->addMenu(tr("Recent Projects"));
    a = action(QStringLiteral("fileCloseProject"), tr("Close Project"));
    connect(a, &QAction::triggered, this, &MainWindow::requestCloseProject); file->addAction(a);
    file->addSeparator();
    a = action(QStringLiteral("fileNewSource"), tr("&New Source File…"), QKeySequence::New);
    a->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    connect(a, &QAction::triggered, this, &MainWindow::requestNewSource); file->addAction(a);
    a = action(QStringLiteral("fileOpenSource"), tr("Open Source &File…"), QKeySequence::Open);
    connect(a, &QAction::triggered, this, &MainWindow::requestOpenSource); file->addAction(a);
    file->addSeparator();
    a = action(QStringLiteral("fileSave"), tr("&Save"), QKeySequence::Save);
    a->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())saveWorkbookInteractive(book);else saveInteractive(currentDocument()); }); file->addAction(a);
    a = action(QStringLiteral("fileSaveAs"), tr("Save &As…"), QKeySequence::SaveAs);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())saveWorkbookInteractive(book,true);else saveAsInteractive(currentDocument()); }); file->addAction(a);
    a = action(QStringLiteral("fileSaveAll"), tr("Save A&ll"),
               QKeySequence(QStringLiteral("Ctrl+Alt+Shift+S")));
    connect(a, &QAction::triggered, this, [this] {
        for (const auto& tab : m_documents)
            if (tab.document->isModified() && !saveInteractive(tab.document)) return;
        if(m_workspaceTabs)for(int i=0;i<m_workspaceTabs->count();++i)if(auto* book=qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i)))if(book->isModified()&&!saveWorkbookInteractive(book))return;
    }); file->addAction(a);
    a = action(QStringLiteral("fileCloseTab"), tr("&Close Tab"), QKeySequence::Close);
    connect(a, &QAction::triggered, this, [this] { if(currentWorkbook())closeWorkspacePage(m_workspaceTabs->currentIndex());else closeTab(m_tabs->currentIndex()); }); file->addAction(a);
    file->addSeparator();
    a=action(QStringLiteral("fileCloseAllDocuments"),tr("Close All Documents"),QKeySequence(QStringLiteral("Ctrl+Shift+W")));
    connect(a,&QAction::triggered,this,[this]{closeAllDocuments();});file->addAction(a);
    a = action(QStringLiteral("filePrint"), tr("&Print…"), QKeySequence::Print);
    connect(a,&QAction::triggered,this,&MainWindow::requestPrintSource);file->addAction(a);
    a = action(QStringLiteral("filePrintPreview"),tr("Print Pre&view…"));
    connect(a,&QAction::triggered,this,&MainWindow::requestPrintPreview);file->addAction(a);
    a = action(QStringLiteral("fileExportCode"),tr("Export to MATLAB or Python…"));
    connect(a,&QAction::triggered,this,[this]{
        auto* document=currentDocument();if(!document||m_workPending)return;
        if(!saveInteractive(document))return;
        const auto snapshot=snapshotForDocument(m_tabs->currentIndex());
        language::AnalysisOptions options;options.algebraId=snapshot.algebraId.toStdString();options.gamma=snapshot.gamma;options.languageVersion=snapshot.languageVersion.toStdString();
        const auto sourcePath=std::filesystem::path(document->path().toStdU16String());options.sourceDirectory=sourcePath.parent_path();
        const auto bytes=document->text().toUtf8();
        const auto exported=ide::showCodeExportDialog(this,std::string(bytes.constData(),static_cast<std::size_t>(bytes.size())),options,sourcePath);
        if(exported.written)statusBar()->showMessage(exported.summary,15000);
    });file->addAction(a);
    a = action(QStringLiteral("filePageSetup"),tr("Page Set&up…"));
    connect(a,&QAction::triggered,this,&MainWindow::requestPageSetup);file->addAction(a);
    file->addSeparator();
    a = action(QStringLiteral("fileQuit"), tr("E&xit"), QKeySequence::Quit);
    connect(a, &QAction::triggered, this, &QWidget::close); file->addAction(a);

    auto* edit = menuBar()->addMenu(tr("&Edit"));edit->setObjectName(QStringLiteral("studioEditMenu"));
    a = action(QStringLiteral("editUndo"), tr("&Undo"), QKeySequence::Undo);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())book->undo();else if (auto* e = currentEditor()) e->undo(); }); edit->addAction(a);
    a = action(QStringLiteral("editRedo"), tr("&Redo"), QKeySequence::Redo);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())book->redo();else if (auto* e = currentEditor()) e->redo(); }); edit->addAction(a);
    edit->addSeparator();
    a = action(QStringLiteral("editCut"), tr("Cu&t"), QKeySequence::Cut);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())book->cut();else if (auto* e = currentEditor()) e->cut(); }); edit->addAction(a);
    a = action(QStringLiteral("editCopy"), tr("&Copy"), QKeySequence::Copy);
    connect(a, &QAction::triggered, this, [this] {
        if(m_console->hasFocus()) {
            if(m_console->textCursor().hasSelection())m_console->copy();else m_console->interruptInput();
        }else if(auto* book=currentWorkbook())book->copy();else if(auto* e=currentEditor())e->copy();
    }); edit->addAction(a);
    a = action(QStringLiteral("editPaste"), tr("&Paste"), QKeySequence::Paste);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())book->paste();else if (auto* e = currentEditor()) e->paste(); }); edit->addAction(a);
    a = action(QStringLiteral("editSelectAll"), tr("Select &All"), QKeySequence::SelectAll);
    connect(a, &QAction::triggered, this, [this] { if(auto* book=currentWorkbook())book->selectAll();else if (auto* e = currentEditor()) e->selectAll(); }); edit->addAction(a);
    edit->addSeparator();
    a = action(QStringLiteral("editFindReplace"), tr("&Find / Replace…"), QKeySequence::Find);
    connect(a, &QAction::triggered, this, &MainWindow::showFindReplace); edit->addAction(a);
    a=action(QStringLiteral("editComplete"),tr("Complete Name\tCtrl+Space"));
    connect(a,&QAction::triggered,this,[this]{if(auto* editor=currentEditor())if(auto* assistance=editor->findChild<SourceAssistance*>())assistance->showCompletion();});edit->addAction(a);
    a=action(QStringLiteral("editInspectSymbol"),tr("Inspect Symbol\tCtrl+Shift+I"));
    connect(a,&QAction::triggered,this,[this]{if(auto* editor=currentEditor())if(auto* assistance=editor->findChild<SourceAssistance*>())assistance->showInspection();});edit->addAction(a);
    a=action(QStringLiteral("editGoToDefinition"),tr("Go to Declaration\tF12"));
    connect(a,&QAction::triggered,this,[this]{if(auto* editor=currentEditor())if(auto* assistance=editor->findChild<SourceAssistance*>())assistance->jumpToDefinition();});edit->addAction(a);
    a = action(QStringLiteral("editToggleComment"), tr("Toggle &Comment"),
               QKeySequence(QStringLiteral("Ctrl+/")));
    connect(a, &QAction::triggered, this, [this] { if (auto* e = currentEditor()) e->toggleComment(); }); edit->addAction(a);
    a = action(QStringLiteral("editIndent"), tr("Indent"), QKeySequence(QStringLiteral("Ctrl+]")));
    connect(a, &QAction::triggered, this, [this] { if (auto* e = currentEditor()) e->indentSelection(); }); edit->addAction(a);
    a = action(QStringLiteral("editUnindent"), tr("Unindent"), QKeySequence(QStringLiteral("Ctrl+[")));
    connect(a, &QAction::triggered, this, [this] { if (auto* e = currentEditor()) e->indentSelection(true); }); edit->addAction(a);

    auto* project = menuBar()->addMenu(tr("&Project"));
    a = action(QStringLiteral("projectRunSource"), tr("Run Selected File"));
    a->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    connect(a, &QAction::triggered, this, [this] {
        if (!m_project) return;
        const QString relative = selectedRelativeSource();
        if (relative.isEmpty()) return;
        QString path, error;
        if (!ProjectService::resolveSource(*m_project, relative, path, &error) || !runScript(path, &error))
            showCalculationError(error);
    });
    project->addAction(a); project->addSeparator();
    project->addAction(m_actions.value(QStringLiteral("fileNewSource")));
    a = action(QStringLiteral("projectAddSource"), tr("Add Existing Source…"));
    connect(a, &QAction::triggered, this, &MainWindow::requestAddExistingSource); project->addAction(a);
    a = action(QStringLiteral("projectNewFolder"), tr("New Folder…"));
    connect(a, &QAction::triggered, this, &MainWindow::requestNewFolder); project->addAction(a);
    project->addSeparator();
    a = action(QStringLiteral("projectRenameSource"), tr("Rename Source"),QKeySequence(Qt::Key_F2));
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_tree->addAction(a);
    connect(a, &QAction::triggered, this, &MainWindow::requestRenameSource); project->addAction(a);
    a = action(QStringLiteral("projectRemoveSource"), tr("Remove Source from Project…"));
    connect(a, &QAction::triggered, this, [this] { requestRemoveSource(false); }); project->addAction(a);
    a = action(QStringLiteral("projectDeleteSource"), tr("Delete Source from Disk…"));
    connect(a, &QAction::triggered, this, [this] { requestRemoveSource(true); }); project->addAction(a);
    project->addSeparator();
    a = action(QStringLiteral("projectEnableInterpreter"), tr("Enable Interpreter for Project…"));
    connect(a, &QAction::triggered, this, &MainWindow::requestEnableInterpreter); project->addAction(a);

    auto* check = menuBar()->addMenu(tr("&Check"));
    a = action(QStringLiteral("checkFile"), tr("Check &File"), QKeySequence(QStringLiteral("F7")));
    a->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    connect(a, &QAction::triggered, this, [this] {
        QString error; if (!checkCurrentDocument(&error)) showCalculationError(error);
    }); check->addAction(a);
    a = action(QStringLiteral("checkProject"), tr("Check &Project"), QKeySequence(QStringLiteral("Ctrl+F7")));
    connect(a, &QAction::triggered, this, [this] {
        QString error; if (!checkProject(&error)) showCalculationError(error);
    }); check->addAction(a);
    auto* run = menuBar()->addMenu(tr("&Run"));
    a = action(QStringLiteral("runFile"), tr("Run &File"), QKeySequence(QStringLiteral("F5")));
    a->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    a->setToolTip(tr("Save the script first, then run its saved snapshot in a fresh workspace. Inspect resulting variables in the Command Window."));
    connect(a, &QAction::triggered, this, [this] {
        QString error; if (!runCurrentDocument(&error)) showCalculationError(error);
    }); run->addAction(a);
    a = action(QStringLiteral("stopExecution"), tr("&Stop"), QKeySequence(QStringLiteral("Shift+F5")));
    a->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    connect(a, &QAction::triggered, this, &MainWindow::stopExecution); run->addAction(a);
    run->addSeparator();
    a = action(QStringLiteral("clearWorkspace"), tr("Reset Workspace"));
    a->setToolTip(tr("Remove variables and reset algebra context to project defaults, keeping command history and transcript. Use clear commands to remove variables while preserving context."));
    connect(a, &QAction::triggered, this, &MainWindow::clearWorkspace); run->addAction(a);
    a = action(QStringLiteral("clearCommandWindow"), tr("Clear Command Window"));
    a->setToolTip(tr("Clear the visible transcript while keeping workspace variables and command history."));
    connect(a, &QAction::triggered, this, [this] {
        m_console->clearTranscript();
        setCalculationStatus(tr("Command Window cleared. Workspace variables are unchanged."));
    }); run->addAction(a);
    auto* calculationToolbar = addToolBar(tr("Calculation"));
    calculationToolbar->setObjectName(QStringLiteral("calculationToolbar"));
    calculationToolbar->addAction(m_actions.value(QStringLiteral("checkFile")));
    calculationToolbar->addAction(m_actions.value(QStringLiteral("runFile")));
    calculationToolbar->addAction(m_actions.value(QStringLiteral("stopExecution")));

    auto* view = menuBar()->addMenu(tr("&View"));
    view->setObjectName(QStringLiteral("studioViewMenu"));
    view->addAction(projectDock->toggleViewAction());
    view->addAction(problemsDock->toggleViewAction());
    view->addAction(resultsDock->toggleViewAction());
    view->addAction(figuresDock->toggleViewAction());
    auto* mapDock=new QDockWidget(tr("Map Explorer"),this);mapDock->setObjectName(QStringLiteral("mapExplorerDock"));
    auto* explorer=new MapExplorer(mapDock);explorer->setObjectName(QStringLiteral("mapExplorer"));mapDock->setWidget(explorer);
    addDockWidget(Qt::LeftDockWidgetArea,mapDock);mapDock->hide();view->addAction(mapDock->toggleViewAction());
    auto submitExplorer=[this](const QString& source){QString error;if(!submitCommand(source,&error)&&!error.isEmpty())showCalculationError(error);};
    connect(explorer,&MapExplorer::runRequested,this,submitExplorer);connect(explorer,&MapExplorer::exportRequested,this,submitExplorer);
    connect(explorer,&MapExplorer::scriptSaved,this,[this](const QString& path){QString error;if(!openSourcePath(path,&error)&&!error.isEmpty())showCalculationError(error);});
    auto* researchDock=new QDockWidget(tr("Research Explorer"),this);researchDock->setObjectName(QStringLiteral("researchExplorerDock"));
    auto* researchExplorer=new ResearchExplorer(researchDock);researchExplorer->setObjectName(QStringLiteral("researchExplorer"));researchDock->setWidget(researchExplorer);
    addDockWidget(Qt::LeftDockWidgetArea,researchDock);researchDock->hide();view->addAction(researchDock->toggleViewAction());
    auto submitResearch=[this,researchExplorer](const QString& source){QString error;if(!submitCommand(source,&error)){researchExplorer->submissionRejected(error);if(!error.isEmpty())showCalculationError(error);}};
    connect(researchExplorer,&ResearchExplorer::runRequested,this,submitResearch);connect(researchExplorer,&ResearchExplorer::exportRequested,this,submitResearch);
    connect(researchExplorer,&ResearchExplorer::cancelRequested,this,&MainWindow::stopExecution);
    connect(researchExplorer,&ResearchExplorer::scriptSaved,this,[this](const QString& path){QString error;if(!openSourcePath(path,&error)&&!error.isEmpty())showCalculationError(error);});
    view->addAction(calculationToolbar->toggleViewAction());
    a = action(QStringLiteral("viewFontSize"), tr("Editor Font Size…"));
    connect(a, &QAction::triggered, this, [this] {
        bool accepted = false;
        const int size = QInputDialog::getInt(this, tr("Editor font size"),
                            tr("Point size:"), m_fontSize, 7, 40, 1, &accepted);
        if (accepted) {
            m_fontSize = size;
            for (const auto& tab : m_documents) tab.editor->setFontPointSize(size);
        }
    }); view->addAction(a);
    a = action(QStringLiteral("viewAutosave"), tr("Autosave Settings…"));
    connect(a, &QAction::triggered, this, &MainWindow::showAutosaveSettings); view->addAction(a);
    a = action(QStringLiteral("viewRestoreSession"), tr("Restore Previous Session at Startup"));
    a->setCheckable(true); a->setChecked(false); view->addAction(a);

    auto* inspectorDock=new QDockWidget(tr("Symbol Inspector"),this);inspectorDock->setObjectName(QStringLiteral("sourceInspectorDock"));
    auto* inspector=new QPlainTextEdit(inspectorDock);inspector->setObjectName(QStringLiteral("sourceInspectorText"));inspector->setReadOnly(true);inspectorDock->setWidget(inspector);
    addDockWidget(Qt::RightDockWidgetArea,inspectorDock);inspectorDock->hide();view->addAction(inspectorDock->toggleViewAction());
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->setObjectName(QStringLiteral("studioHelpMenu"));
    a=action(QStringLiteral("helpFeatureRules"),tr("Feature Rules and Native Modules…"));
    connect(a,&QAction::triggered,this,[this,view]{
        auto* dock=findChild<QDockWidget*>(QStringLiteral("featureRulesDock"));
        if(!dock){
            dock=new QDockWidget(tr("Feature Rules and Native Modules"),m_workspaceLayout?m_workspaceLayout->workingHost():this);
            dock->setObjectName(QStringLiteral("featureRulesDock"));const auto reference=referencePath(QStringLiteral("language.txt"));
            dock->setWidget(new FeatureBrowser(reference.isEmpty()?QString():QFileInfo(reference).absolutePath(),[this]{return m_workPending;},dock));
            if(m_workspaceLayout)m_workspaceLayout->registerWorkingDock(dock,Qt::RightDockWidgetArea);else addDockWidget(Qt::RightDockWidgetArea,dock);
            if(m_desktop)m_desktop->registerDock(dock,QStringLiteral("featureRules"),false,false,dockHost(dock));
            view->addAction(dock->toggleViewAction());
        }
        dock->show();dock->raise();if(auto* browser=dock->findChild<FeatureBrowser*>())browser->refreshModules();
    });help->addAction(a);
    a = action(QStringLiteral("helpLanguageReference"), tr("Language Reference…"));
    connect(a, &QAction::triggered, this, &MainWindow::showLanguageReference); help->addAction(a);
    a = action(QStringLiteral("helpAbout"), tr("About Pentarnion Studio…"));
    connect(a, &QAction::triggered, this, &MainWindow::showAbout); help->addAction(a);

    m_documentStatus = new QLabel(this);
    m_documentStatus->setObjectName(QStringLiteral("documentStatus"));
    statusBar()->addWidget(m_documentStatus, 1);
    connect(statusBar(), &QStatusBar::messageChanged, m_documentStatus,
            [this](const QString& message) { m_documentStatus->setVisible(message.isEmpty()); });
    m_contextStatus = new QLabel(this);
    m_contextStatus->setObjectName(QStringLiteral("contextStatus"));
    m_contextStatus->setToolTip(tr("Check or Run determines a file directive's effective algebra context."));
    statusBar()->addPermanentWidget(m_contextStatus);

    m_watcher = new QFileSystemWatcher(this);
    m_externalChangeTimer = new QTimer(this);
    m_externalChangeTimer->setObjectName(QStringLiteral("externalChangeTimer"));
    m_externalChangeTimer->setSingleShot(true);
    m_externalChangeTimer->setInterval(100);
    connect(m_externalChangeTimer, &QTimer::timeout, this, &MainWindow::checkExternalChanges);
    const auto scheduleExternalCheck = [this] {
        // Coalesce a burst without restarting the deadline: sustained directory
        // activity must not postpone detection of an actual source change.
        if (!m_closing && !m_externalChangeTimer->isActive()) m_externalChangeTimer->start();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, scheduleExternalCheck);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, scheduleExternalCheck);
    refreshTree();
}

const Project* MainWindow::activeProject() const {
    return m_project ? &*m_project : nullptr;
}

Document* MainWindow::currentDocument() const {
    if(m_workspaceTabs&&m_workspaceTabs->currentWidget()!=m_sourceSurface)return nullptr;
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_documents.size() ? m_documents[index].document : nullptr;
}

EditorWidget* MainWindow::currentEditor() const {
    if(m_workspaceTabs&&m_workspaceTabs->currentWidget()!=m_sourceSurface)return nullptr;
    const int index = m_tabs->currentIndex();
    return index >= 0 && index < m_documents.size() ? m_documents[index].editor : nullptr;
}

std::unique_ptr<QTextDocument> MainWindow::currentSourcePrintSnapshot(QString* error) {
    if(error)error->clear();
    auto* editor=currentEditor();auto* document=currentDocument();
    if(!editor||!document){fail(error,tr("Open a source document to print."));return {};}
    editor->syntaxHighlighter()->rehighlight();
    SourcePrintSettings settings;settings.font=editor->font();settings.tabStopDistance=editor->tabStopDistance();settings.logicalDpi=editor->logicalDpiX();
    settings.textColor=editor->palette().color(QPalette::Text);
    if(settings.textColor.lightness()>220)settings.textColor=Qt::black;
    settings.title=document->path().isEmpty()?tr("Untitled source"):QFileInfo(document->path()).fileName();
    return createSourcePrintSnapshot(*editor->document(),settings,error);
}

QPrinter* MainWindow::sourcePrinter() {
    if(!m_sourcePrinter){
        m_sourcePrinter=std::make_unique<QPrinter>(QPrinter::HighResolution);
        m_sourcePrinter->setColorMode(QPrinter::Color);
        if(!m_sourcePrinter->isValid())m_sourcePrinter->setOutputFormat(QPrinter::PdfFormat);
    }
    return m_sourcePrinter.get();
}

void MainWindow::requestPrintSource() {
    SourcePrintInteraction pause(m_printDialogActive,m_autosaveDebounce,m_autosavePeriodic,m_autosavePending,m_autosaveInterval);
    auto* editor=currentEditor();if(!editor)return;
    const auto selected=editor->textCursor();QString message;
    auto snapshot=currentSourcePrintSnapshot(&message);if(!snapshot){showError(tr("Print source"),message);return;}
    std::unique_ptr<QTextDocument> selection;
    if(selected.hasSelection())selection=createSourcePrintSelectionSnapshot(*snapshot,selected.selectionStart(),selected.selectionEnd(),&message);
    auto* printer=sourcePrinter();printer->setDocName(snapshot->metaInformation(QTextDocument::DocumentTitle));printer->setPrintRange(QPrinter::AllPages);
    SourcePrintDialog dialog(*printer,std::move(snapshot),std::move(selection),this);dialog.exec();
}

void MainWindow::requestPrintPreview() {
    SourcePrintInteraction pause(m_printDialogActive,m_autosaveDebounce,m_autosavePeriodic,m_autosavePending,m_autosaveInterval);
    QString message;auto snapshot=currentSourcePrintSnapshot(&message);if(!snapshot){showError(tr("Print preview"),message);return;}
    auto* printer=sourcePrinter();printer->setDocName(snapshot->metaInformation(QTextDocument::DocumentTitle));printer->setPrintRange(QPrinter::AllPages);
    SourcePrintDialog preview(*printer,std::move(snapshot),{},this);preview.setObjectName(QStringLiteral("sourcePrintPreview"));
    preview.exec();
}

void MainWindow::requestPageSetup() {
    SourcePrintInteraction pause(m_printDialogActive,m_autosaveDebounce,m_autosavePeriodic,m_autosavePending,m_autosaveInterval);
    if(!currentEditor())return;
    QPageSetupDialog dialog(sourcePrinter(),this);dialog.setObjectName(QStringLiteral("sourcePageSetup"));dialog.exec();
}

int MainWindow::documentCount() const { return static_cast<int>(m_documents.size()); }

int MainWindow::indexOf(Document* document) const {
    for (int i = 0; i < m_documents.size(); ++i)
        if (m_documents[i].document == document) return i;
    return -1;
}

int MainWindow::indexOfPath(const QString& path) const {
    const QString identity = Document::canonicalIdentity(path);
    for (int i = 0; i < m_documents.size(); ++i)
        if (!identity.isEmpty() && m_documents[i].document->identity() == identity) return i;
    return -1;
}

void MainWindow::attachDocument(Document* document, const QString& recoveryId,
                                int cursorPosition) {
    showSourceWorkspace();
    document->setParent(this);
    auto* editor = new EditorWidget(m_tabs);
    editor->setObjectName(QStringLiteral("sourceEditor"));
    editor->setDocument(document->textDocument());
    editor->setFontPointSize(m_fontSize);
    auto* assistance=new SourceAssistance(editor);
    connect(assistance,&SourceAssistance::analysisRequested,this,[this,editor]{if(currentEditor()!=editor||m_workPending)return;QString error;if(!checkCurrentDocument(&error)&&!error.isEmpty())statusBar()->showMessage(error,8000);});
    connect(assistance,&SourceAssistance::inspectionRequested,this,[this](const QString& text){if(auto* inspector=findChild<QPlainTextEdit*>(QStringLiteral("sourceInspectorText")))inspector->setPlainText(text);if(auto* dock=findChild<QDockWidget*>(QStringLiteral("sourceInspectorDock"))){dock->show();dock->raise();}});
    connect(assistance,&SourceAssistance::externalDefinitionRequested,this,[this](const QString& path,const QByteArray& bytes,quint64 offset,quint64 length){SourceSnapshot snapshot;snapshot.path=path;snapshot.source=bytes;snapshot.retainedFrame=true;snapshot.command=path.startsWith(QLatin1Char('<'));navigateDiagnostic(snapshot,{static_cast<std::size_t>(offset),static_cast<std::size_t>(length)});});
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(std::clamp(cursorPosition, 0,
                       std::max(0, document->textDocument()->characterCount() - 1)));
    editor->setTextCursor(cursor);
    m_documents.append({document, editor, recoveryId.isEmpty() ? RecoveryStore::newId() : recoveryId, ++m_nextDocumentId});
    const int index = m_tabs->addTab(editor, QString());
    m_tabs->setCurrentIndex(index);
    m_observedDisk.insert(document, diskFingerprint(document->path()));
    connect(document, &Document::stateChanged, this, [this, document] {
        invalidateDocumentAnalysis(document);
        updateUi();
        if (!document->isModified()) {
            const int at = indexOf(document);
            if (at >= 0) acknowledgeRecovery(m_documents[at]);
        }
    });
    connect(document, &Document::pathChanged, this, [this, document] {
        invalidateDocumentAnalysis(document); refreshWatchers(); updateUi();
    });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::updateUi);
    connect(editor, &QPlainTextEdit::copyAvailable, this, &MainWindow::updateUi);
    connect(editor, &EditorWidget::mouseLeftViewport, this, [this, document] { scheduleAutosave(document); });
    connect(editor, &EditorWidget::focusLeftEditor, this, [this, document] { scheduleAutosave(document); });
    refreshWatchers(); updateUi(); editor->setFocus();
}

bool MainWindow::createProjectAt(const QString& parent, const QString& name, QString* error) {
    if (!prepareCloseAll()) return fail(error, tr("Project creation cancelled; open buffers were retained."));
    Project candidate;
    if (!ProjectService::create(parent, name, candidate, error)) return false;
    disposeAllTabs(true);
    m_project = candidate;
    refreshTree(); rememberProject(candidate.manifestPath);
    QString sourcePath;
    if (!ProjectService::resolveSource(candidate, QStringLiteral("main.p"), sourcePath, error) ||
        !openSourcePath(sourcePath, error)) return false;
    updateUi();
    clearWorkspace();
    return true;
}

bool MainWindow::openProjectPath(const QString& manifest, QString* error) {
    Project candidate;
    if (!ProjectService::load(manifest, candidate, error)) return false;
    // Validate the initially opened source before relinquishing the old project.
    // A valid manifest can still refer to a binary or otherwise unreadable file.
    std::unique_ptr<Document> firstSource;
    if (!candidate.sources.isEmpty()) {
        QString path;
        if (!ProjectService::resolveSource(candidate, candidate.sources.front(), path, error)) return false;
        firstSource = std::make_unique<Document>();
        if (!firstSource->load(path, error)) return false;
    }
    if (!prepareCloseAll()) return fail(error, tr("Project switch cancelled; open buffers were retained."));
    // Prompts may have saved this same project's buffers, or changed its source
    // list through Save As. Re-read after those decisions so reopening never
    // installs a stale pre-save clean buffer.
    if (!ProjectService::load(manifest, candidate, error)) return false;
    firstSource.reset();
    if (!candidate.sources.isEmpty()) {
        QString path;
        if (!ProjectService::resolveSource(candidate, candidate.sources.front(), path, error)) return false;
        firstSource = std::make_unique<Document>();
        if (!firstSource->load(path, error)) return false;
    }
    disposeAllTabs(true);
    m_project = candidate;
    refreshTree(); rememberProject(candidate.manifestPath);
    if (firstSource) attachDocument(firstSource.release());
    updateUi();
    clearWorkspace();
    return true;
}

bool MainWindow::openSourcePath(const QString& path, QString* error) {
    showSourceWorkspace();
    if (QFileInfo(path).suffix().compare(QStringLiteral("p"), Qt::CaseInsensitive) != 0)
        return fail(error, tr("Select a .p source file."));
    const int existing = indexOfPath(path);
    if (existing >= 0) {
        m_tabs->setCurrentIndex(existing);
        return true;
    }
    auto* document = new Document;
    if (!document->load(path, error)) { delete document; return false; }
    attachDocument(document);
    return true;
}

bool MainWindow::addSource(const QString& relative, QString* error) {
    if (!m_project) return fail(error, tr("Create or open a project first."));
    if (!ProjectService::addSource(*m_project, relative,
            QStringLiteral("% Pentarnion Studio source.\n% Use display(expression) to print a value when running.\n"), error)) return false;
    refreshTree();
    QString absolute;
    if (!ProjectService::resolveSource(*m_project, relative, absolute, error)) return false;
    return openSourcePath(absolute, error);
}

bool MainWindow::saveCurrentDocument(QString* error) {
    if(auto* book=currentWorkbook())return book->save(error);
    auto* document = currentDocument();
    if (!document) return fail(error, tr("There is no source file to save."));
    if (!document->save(error)) return false;
    m_observedDisk.insert(document, diskFingerprint(document->path()));
    refreshWatchers(); updateUi();
    return true;
}

bool MainWindow::saveAllDocuments(QString* error) {
    for (const auto& tab : m_documents) {
        if (!tab.document->isModified()) continue;
        if (!tab.document->save(error)) return false;
        m_observedDisk.insert(tab.document, diskFingerprint(tab.document->path()));
    }
    for(int i=0;i<m_workspaceTabs->count();++i)
        if(auto* book=qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i)))
            if(book->isModified()&&!book->save(error))return false;
    refreshWatchers(); updateUi();
    return true;
}

bool MainWindow::saveCurrentDocumentAs(const QString& path, QString* error,
                                      bool overwriteExisting) {
    if(auto* book=currentWorkbook())return book->saveAs(path,error,overwriteExisting);
    QString warning;
    const bool saved = saveDocumentAs(currentDocument(), path, overwriteExisting, error, &warning);
    if (!warning.isEmpty()) statusBar()->showMessage(warning, 15000);
    return saved;
}

bool MainWindow::renameProjectSource(const QString& oldRelative, const QString& newRelative,
                                     QString* error) {
    if (!m_project) return fail(error, tr("There is no active project."));
    QString oldAbsolute;
    if (!ProjectService::resolveSource(*m_project, oldRelative, oldAbsolute, error)) return false;
    const int index = indexOfPath(oldAbsolute);
    QString newAbsolute;
    if (!ProjectService::resolveSource(*m_project, newRelative, newAbsolute, error, false)) return false;
    const int other = indexOfPath(newAbsolute);
    if (other >= 0 && other != index)
        return fail(error, tr("The destination is already open in another tab. Close that buffer before renaming."));
    if (!ProjectService::renameSource(*m_project, oldRelative, newRelative, error)) return false;
    if (index >= 0) {
        if (!ProjectService::resolveSource(*m_project, newRelative, newAbsolute, error)) return false;
        m_documents[index].document->adoptRenamedPath(newAbsolute);
        m_observedDisk.insert(m_documents[index].document, diskFingerprint(newAbsolute));
    }
    refreshTree();
    for(QTreeWidgetItemIterator it(m_tree);*it;++it){
        if(!(*it)->data(0,FolderRole).toBool()&&(*it)->data(0,RelativeRole).toString()==newRelative){
            m_tree->setCurrentItem(*it);m_tree->scrollToItem(*it);break;
        }
    }
    refreshWatchers(); updateUi(); snapshotRecovery();
    return true;
}

bool MainWindow::removeProjectSource(const QString& relative, bool deleteFile, QString* error) {
    if (!m_project) return fail(error, tr("There is no active project."));
    QString absolute;
    if (!ProjectService::resolveSource(*m_project, relative, absolute, error, deleteFile)) return false;
    const int index = indexOfPath(absolute);
    if (deleteFile && index >= 0 && m_documents[index].document->isModified())
        return fail(error, tr("Save or explicitly discard the source buffer before deleting its file."));
    if (!ProjectService::removeSource(*m_project, relative, deleteFile, error)) return false;
    if (deleteFile && index >= 0) disposeTab(index, true);
    refreshTree(); refreshWatchers(); updateUi();
    return true;
}

void MainWindow::assignSnapshotContext(SourceSnapshot& snapshot) const {
    if (!m_project || snapshot.path.isEmpty()) return;
    const QString relative = asPortableRelative(m_project->rootPath, snapshot.path);
    QString checked;
    if (relative.startsWith(QStringLiteral("../")) || relative == QStringLiteral("..") ||
        QDir::isAbsolutePath(relative) ||
        !ProjectService::resolveSource(*m_project, relative, checked, nullptr, false)) return;
    snapshot.projectPath = m_project->manifestPath;
    snapshot.algebraId = m_project->algebraId;
    snapshot.gamma = m_project->gamma;
    snapshot.languageVersion = m_project->languageVersion;
}

SourceSnapshot MainWindow::snapshotForDocument(int index) const {
    SourceSnapshot snapshot;
    const auto& tab = m_documents[index];
    snapshot.documentId = tab.id;
    snapshot.revision = tab.document->revision();
    snapshot.path = tab.document->path();
    snapshot.source = tab.document->text().toUtf8();
    assignSnapshotContext(snapshot);
    return snapshot;
}

bool MainWindow::importSnapshotsAreCurrent(const SourceSnapshot& snapshot) const {
    for(const auto& imported:snapshot.imports){
        if(diskFingerprint(imported.path)!=imported.diskHash)return false;
        const int open=indexOfPath(imported.path);
        if(open>=0){
            // Imports use saved UTF-8 source. A dirty dependency must not make
            // results from older disk contents look current in its open editor.
            const auto editorHash=QCryptographicHash::hash(m_documents[open].document->text().toUtf8(),QCryptographicHash::Sha256);
            if(editorHash!=imported.editorHash)return false;
        }
    }
    return true;
}

bool MainWindow::snapshotIsCurrent(const SourceSnapshot& snapshot) const {
    if(snapshot.retainedFrame){
        auto normalized=[](QString text){if(!text.isEmpty()&&text.front()==QChar(0xfeff))text.remove(0,1);text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));return text;};
        const auto expected=normalized(QString::fromUtf8(snapshot.source));const int open=indexOfPath(snapshot.path);
        if(open>=0)return normalized(m_documents[open].document->text())==expected;
        QFile file(snapshot.path);if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return false;
        return normalized(QString::fromUtf8(file.readAll()))==expected;
    }
    if(!importSnapshotsAreCurrent(snapshot))return false;
    SourceSnapshot context = snapshot;
    context.projectPath.clear(); context.algebraId = QStringLiteral("A_gamma_v1");
    context.gamma = 1; context.languageVersion = QStringLiteral("0.2");
    assignSnapshotContext(context);
    if (context.projectPath != snapshot.projectPath || context.gamma != snapshot.gamma ||
        context.algebraId != snapshot.algebraId || context.languageVersion != snapshot.languageVersion) return false;
    if (snapshot.documentId) {
        for (const auto& tab : m_documents)
            if (tab.id == snapshot.documentId)
                return tab.document->revision() == snapshot.revision && tab.document->path() == snapshot.path;
        return false;
    }
    const int open = indexOfPath(snapshot.path);
    if (open >= 0) return m_documents[open].document->text().toUtf8() == snapshot.source;
    return diskFingerprint(snapshot.path) == snapshot.diskHash;
}

bool MainWindow::checkCurrentDocument(QString* error) {
    if (m_workPending && m_latestKind == WorkKind::RunCommand)
        return fail(error, tr("Wait for the current command to finish, or choose Stop."));
    const int index = m_tabs->currentIndex();
    if (index < 0) return fail(error, tr("Open a source file to check."));
    if (m_workPending && m_latestKind == WorkKind::RunFile)
        setCalculationStatus(tr("The pending run was superseded by a Check request."));
    m_problems->clear();
    m_latestKind = WorkKind::CheckFile;
    m_workPending = true;
    m_latestRequest = m_execution->submit(m_latestKind, {snapshotForDocument(index)});
    statusBar()->showMessage(tr("Checking current buffer…")); updateUi();
    return true;
}

bool MainWindow::checkProject(QString* error) {
    if (m_workPending && m_latestKind == WorkKind::RunCommand)
        return fail(error, tr("Wait for the current command to finish, or choose Stop."));
    if (!m_project) return fail(error, tr("Open a project to check."));
    if (m_project->sources.size() > 512)
        return fail(error, tr("A single Check Project request is limited to 512 source files."));
    QVector<SourceSnapshot> sources;
    qsizetype bytes = 0;
    for (const auto& relative : m_project->sources) {
        QString path;
        if (!ProjectService::resolveSource(*m_project, relative, path, error)) return false;
        const int open = indexOfPath(path);
        SourceSnapshot snapshot;
        if (open >= 0) snapshot = snapshotForDocument(open);
        else {
            const QByteArray before = diskFingerprint(path);
            Document document;
            if (!document.load(path, error)) return false;
            snapshot.path = document.path(); snapshot.source = document.text().toUtf8();
            snapshot.diskHash = diskFingerprint(path);
            if (snapshot.diskHash != before)
                return fail(error, tr("A source changed while its snapshot was being captured. Check the project again."));
            assignSnapshotContext(snapshot);
        }
        bytes += snapshot.source.size();
        if (bytes > 16 * 1024 * 1024)
            return fail(error, tr("A single Check Project request is limited to 16 MiB of source snapshots."));
        sources.append(std::move(snapshot));
    }
    if (m_workPending && m_latestKind == WorkKind::RunFile)
        setCalculationStatus(tr("The pending run was superseded by a Check request."));
    m_problems->clear(); m_latestKind = WorkKind::CheckProject; m_workPending = true;
    m_latestRequest = m_execution->submit(m_latestKind, std::move(sources));
    statusBar()->showMessage(tr("Checking project files independently…")); updateUi();
    return true;
}

bool MainWindow::runCurrentDocument(QString* error) {
    return scheduleCurrentRun(error, true);
}

bool MainWindow::runScript(const QString& path, QString* error) {
    return runScriptImpl(path, error, true);
}

bool MainWindow::runScriptImpl(const QString& path, QString* error, bool showMarker) {
    if (m_workPending && m_latestKind == WorkKind::RunCommand)
        return fail(error, tr("Wait for the current command to finish, or choose Stop."));
    QString absolute = path;
    if (!QDir::isAbsolutePath(absolute)) {
        const QString base = m_project ? m_project->rootPath
            : currentDocument() && !currentDocument()->path().isEmpty()
                ? QFileInfo(currentDocument()->path()).absolutePath() : QDir::currentPath();
        absolute = QDir(base).filePath(path);
    }
    if (!openSourcePath(QDir::cleanPath(absolute), error)) return false;
    return scheduleCurrentRun(error, showMarker);
}

bool MainWindow::scheduleCurrentRun(QString* error, bool showMarker) {
    if (m_workPending && m_latestKind == WorkKind::RunCommand)
        return fail(error, tr("Wait for the current command to finish, or choose Stop."));
    const int index = m_tabs->currentIndex();
    if (index < 0) return fail(error, tr("Open a source file to run."));
    const SourceSnapshot beforeSave = snapshotForDocument(index);
    if (beforeSave.languageVersion != QStringLiteral("0.2"))
        return fail(error, tr("This file uses the 0.1 editor profile. Choose Project → Enable Interpreter for Project to explicitly upgrade its metadata before running."));
    if (!beforeSave.projectPath.isEmpty() && m_project &&
        diskFingerprint(m_project->manifestPath) != m_project->manifestDigest)
        return fail(error, tr("Project metadata changed outside Studio. Reopen the project before running its sources."));
    auto* document = m_documents[index].document;
    if (document->path().isEmpty()) {
        if (!saveAsInteractive(document))
            return fail(error, tr("Run cancelled: save the script with Save As before executing it."));
    } else if (document->hasExternalChange()) {
        return fail(error, tr("The disk script changed or became unavailable. Reload it or resolve the save conflict before running; the retained buffer was not executed."));
    } else if (document->isModified()) {
        QString saveError;
        if (!document->save(&saveError))
            return fail(error, tr("The script could not be saved, so it was not run: %1").arg(saveError));
        m_observedDisk.insert(document, diskFingerprint(document->path()));
        refreshWatchers(); updateUi();
    }
    // Save As can change the source identity and effective project/profile.
    // Capture the interpreter snapshot only after persistence has succeeded.
    const int savedIndex = indexOf(document);
    if (savedIndex < 0 || document->path().isEmpty())
        return fail(error, tr("The script is no longer open and was not run."));
    const QByteArray savedDisk = diskFingerprint(document->path());
    const SourceSnapshot source = snapshotForDocument(savedIndex);
    if (source.languageVersion != QStringLiteral("0.2"))
        return fail(error, tr("The saved script uses the 0.1 editor profile. Explicitly enable the interpreter for its project before running."));
    if (!source.projectPath.isEmpty() && m_project &&
        diskFingerprint(m_project->manifestPath) != m_project->manifestDigest)
        return fail(error, tr("Project metadata changed while preparing the run. Reopen the project before executing its sources."));
    if (document->hasExternalChange() || diskFingerprint(document->path()) != savedDisk)
        return fail(error, tr("The saved script changed while preparing execution. Check its disk version and run again."));
    m_problems->clear();
    if (showMarker) appendCommandOutput(tr(">> Run File: %1").arg(QFileInfo(source.path).fileName()));
    setCalculationStatus(tr("Running the saved script snapshot in a fresh file workspace…"));
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("resultsDock"))) { dock->show(); dock->raise(); }
    m_lastRunSnapshot = source;
    m_returnToCommandPrompt = !showMarker;
    m_latestKind = WorkKind::RunFile; m_workPending = true;
    m_latestRequest = m_execution->submit(m_latestKind, {source});
    statusBar()->showMessage(tr("Interpreter running…")); updateUi();
    return true;
}

bool MainWindow::submitCommand(const QString& command, QString* error) {
    if (error) error->clear();
    if (m_workPending) return fail(error, tr("Wait for current work to finish, or choose Stop."));
    if (command.trimmed().isEmpty()) return false;
    QString scriptPath;
    QString runError;
    const auto runMatch = parseRunCommand(command, scriptPath, &runError);
    if (runMatch != RunCommandMatch::NotRun) {
        m_problems->clear();
        rememberCommand(command);
        if (runMatch == RunCommandMatch::Invalid || !runScriptImpl(scriptPath, &runError, false)) {
            showCalculationError(runError);
            m_console->setInputText(command, true);
            return fail(error, runError);
        }
        return true;
    }
    SourceSnapshot snapshot;
    snapshot.command = true;
    snapshot.path = QStringLiteral("<command>");
    snapshot.source = command.toUtf8();
    if (m_project) {
        snapshot.projectPath = m_project->manifestPath;
        snapshot.gamma = m_project->gamma;
        snapshot.algebraId = m_project->algebraId;
    }
    snapshot.languageVersion = QStringLiteral("0.2");
    m_lastRunSnapshot.reset(); // Console commands have their own source lifetime.
    rememberCommand(command);
    setCalculationStatus(tr("Evaluating command…"));
    m_problems->clear(); m_latestKind = WorkKind::RunCommand; m_workPending = true;
    m_latestRequest = m_execution->submit(m_latestKind, {std::move(snapshot)});
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("resultsDock"))) { dock->show(); dock->raise(); }
    updateUi();
    return true;
}

void MainWindow::rememberCommand(const QString& command) {
    m_console->commitCommand(command);
}

void MainWindow::showCalculationError(const QString& error) {
    if (error.isEmpty()) return;
    m_console->appendError(tr("Error: %1").arg(error));
    setCalculationStatus(tr("The calculation was not started."));
    statusBar()->showMessage(error, 10000);
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("resultsDock"))) { dock->show(); dock->raise(); }
}

void MainWindow::clearWorkspace() {
    if (!m_execution) return;
    m_consoleContext.reset();
    m_lastRunSnapshot.reset();
    m_latestKind = WorkKind::ResetWorkspace; m_workPending = true;
    m_latestRequest = m_execution->resetWorkspace();
    setCalculationStatus(tr("Clearing the interpreter workspace…"));
    refreshCommandContext(); updateUi();
}

void MainWindow::appendCommandOutput(const QString& text) {
    m_console->appendOutput(text);
}

void MainWindow::refreshCommandContext() {
    updateUi();
}

QString MainWindow::workspaceContextText() const {
    const QString id = m_consoleContext ? m_consoleContext->algebraId
        : m_project ? m_project->algebraId : QStringLiteral("A_gamma_v1");
    const double gamma = m_consoleContext ? m_consoleContext->gamma : m_project ? m_project->gamma : 1;
    return tr("Workspace: %1 · γ=%2 · language 0.2").arg(id).arg(gamma, 0, 'g', 8);
}

void MainWindow::setCalculationStatus(const QString& status) {
    m_calculationStatus = status;
    statusBar()->showMessage(status);
}

QString MainWindow::consoleInput() const { return m_console->inputText(); }
void MainWindow::setConsoleInput(const QString& text) { m_console->setInputText(text); }

void MainWindow::stopExecution() {
    if (!m_execution || !m_workPending) return;
    m_execution->cancel();
    m_console->setStopping();
    statusBar()->showMessage(tr("Stopping current work…"));
    if (m_latestKind == WorkKind::RunFile || m_latestKind == WorkKind::RunCommand)
        setCalculationStatus(tr("Cancellation requested…"));
    updateUi();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Claim Ctrl+C before editor/menu Copy shortcuts while this Studio is busy.
    // Idle copying and unrelated application/windows retain their usual behavior.
    if (m_workPending && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        const auto* widget = qobject_cast<QWidget*>(watched);
        for(auto* owner=widget;owner;owner=owner->parentWidget())if(owner->property("studioSnapshotInteraction").toBool())return false;
        auto* key = static_cast<QKeyEvent*>(event);
        if (widget && (widget == this || isAncestorOf(widget)) &&
            key->key() == Qt::Key_C && key->modifiers() == Qt::ControlModifier) {
            event->accept();
            if (event->type() == QEvent::KeyPress) m_console->interruptInput();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

QString MainWindow::resultsText() const {
    return m_console->transcriptText();
}
int MainWindow::problemCount() const { return m_problems->topLevelItemCount(); }

bool MainWindow::enableInterpreter(QString* error) {
    if (!m_project) return fail(error, tr("There is no active project."));
    if (!ProjectService::upgradeLanguageVersion(*m_project, QStringLiteral("0.2"), error)) return false;
    if (m_execution) m_execution->cancel();
    m_analysisContexts.clear(); m_defaultDisplayContexts.clear(); m_problems->clear(); updateUi();
    statusBar()->showMessage(tr("Project language profile is now 0.2. Source files were left unchanged."), 8000);
    return true;
}

void MainWindow::requestEnableInterpreter() {
    if (!m_project || m_project->languageVersion == QStringLiteral("0.2")) return;
    if (QMessageBox::question(this, tr("Enable interpreter for this project?"),
            tr("Upgrade %1 from the 0.1 editor profile to language 0.2?\n\nOnly project metadata changes. Source files are preserved, and execution occurs only when you choose Run File.")
                .arg(m_project->name), QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel) != QMessageBox::Yes) return;
    QString error;
    if (!enableInterpreter(&error)) showError(tr("Enable interpreter"), error);
}

void MainWindow::appendProblems(const SourceSnapshot& source,
                                const std::vector<language::Diagnostic>& diagnostics) {
    const std::string_view text(source.source.constData(), static_cast<std::size_t>(source.source.size()));
    for (const auto& diagnostic : diagnostics) {
        const auto location = language::sourceLocation(text, diagnostic.span.offset);
        auto* item = new QTreeWidgetItem(m_problems, {
            diagnostic.severity == language::Severity::Error ? tr("Error") : tr("Warning"),
            QString::fromStdString(diagnostic.code), source.path.isEmpty() ? tr("Unsaved source") : source.path,
            QString::number(location.line), QString::number(location.column), QString::fromStdString(diagnostic.message)});
        item->setData(0, SnapshotRole, QVariant::fromValue(source));
        item->setData(0, OffsetRole, static_cast<qulonglong>(diagnostic.span.offset));
        item->setData(0, LengthRole, static_cast<qulonglong>(diagnostic.span.length));
        item->setToolTip(2, source.path);
        item->setToolTip(5, QString::fromStdString(diagnostic.message));
    }
}

void MainWindow::appendConsoleDiagnostics(const SourceSnapshot& source,
                                          const std::vector<language::Diagnostic>& diagnostics) {
    const std::string_view bytes(source.source.constData(),static_cast<std::size_t>(source.source.size()));
    const QString file=source.command?tr("Command Window"):(source.path.isEmpty()?tr("Unsaved source"):source.path);
    for(const auto& diagnostic:diagnostics) {
        const auto location=language::sourceLocation(bytes,diagnostic.span.offset);
        const auto identifier=++m_nextConsoleDiagnostic;
        m_consoleDiagnostics.insert(identifier,{source,diagnostic.span});
        m_console->appendDiagnostic(tr("%1:%2:%3: %4 %5: %6")
            .arg(file,QString::number(location.line),QString::number(location.column),
                 diagnostic.severity==language::Severity::Error?tr("Error"):tr("Warning"),
                 QString::fromStdString(diagnostic.code),QString::fromStdString(diagnostic.message)),identifier);
        for(const auto& frame:diagnostic.frames){
            if(!frame.source)continue;
            SourceSnapshot callee=source;callee.documentId=0;callee.revision=0;callee.imports.clear();callee.retainedFrame=true;
            callee.path=QString::fromStdString(frame.sourceIdentity);callee.command=callee.path.isEmpty()||callee.path.startsWith(QLatin1Char('<'));
            callee.sourceOwner=frame.source;callee.source=QByteArray::fromRawData(frame.source->data(),static_cast<qsizetype>(frame.source->size()));
            const auto at=language::sourceLocation(*frame.source,frame.span.offset);const auto frameIdentifier=++m_nextConsoleDiagnostic;
            m_consoleDiagnostics.insert(frameIdentifier,{callee,frame.span});
            m_console->appendDiagnostic(tr("    at %1 — %2:%3:%4").arg(QString::fromStdString(frame.function),callee.path,QString::number(at.line),QString::number(at.column)),frameIdentifier);
        }
    }
}

void MainWindow::applyWorkResult(const WorkResult& result) {
    const bool ran = result.kind == WorkKind::RunFile || result.kind == WorkKind::RunCommand;
    if (result.requestId != m_latestRequest) { emit workFinished(ran, true); return; }
    m_workPending = false;
    if (result.kind == WorkKind::ResetWorkspace) {
        if(auto* explorer=findChild<ResearchExplorer*>(QStringLiteral("researchExplorer")))explorer->submissionRejected(tr("Workspace cleared. Run the current research settings again before export or replay."));
        if (result.internalError.isEmpty()) {
            setCalculationStatus(tr("Workspace cleared. Enter a command at >>."));
            appendCommandOutput(tr("[Workspace cleared]"));
        } else setCalculationStatus(tr("Workspace reset failed: %1").arg(result.internalError));
        refreshCommandContext(); updateUi(); emit workFinished(false, false); return;
    }
    bool stale = false;
    for (const auto& file : result.files)
        if ((!file.snapshot.command && !snapshotIsCurrent(file.snapshot)) ||
            !importSnapshotsAreCurrent(file.snapshot)) stale = true;
    if (result.kind == WorkKind::RunFile && m_lastRunSnapshot && !snapshotIsCurrent(*m_lastRunSnapshot)) stale = true;
    if (stale) {
        if(auto* explorer=findChild<ResearchExplorer*>(QStringLiteral("researchExplorer")))explorer->submissionRejected(tr("Discarded a stale source snapshot. Run the current settings again."));
        if (ran) setCalculationStatus(tr("Source or project context changed during this run. Its stale output was discarded; run again for current results."));
        statusBar()->showMessage(tr("Discarded results for an earlier source revision."), 10000);
        if (ran) {
            appendCommandOutput(tr("[Discarded output from a stale source snapshot; workspace cleared]"));
            clearWorkspace();
        }
        updateUi(); emit workFinished(ran, true); return;
    }
    if (!result.internalError.isEmpty()) {
        if(auto* explorer=findChild<ResearchExplorer*>(QStringLiteral("researchExplorer")))explorer->submissionRejected(result.internalError);
        if (ran) setCalculationStatus(tr("The run failed: %1").arg(result.internalError));
        statusBar()->showMessage(tr("Worker failure: %1").arg(result.internalError), 15000);
        updateUi(); emit workFinished(ran, false); return;
    }
    bool hasErrors = false;
    m_figureError.clear();
    for (const auto& file : result.files) {
        if(result.kind==WorkKind::RunFile&&m_lastRunSnapshot)
            m_lastRunSnapshot->imports=file.snapshot.imports;
        appendProblems(file.snapshot, file.analysis.diagnostics);
        if(file.snapshot.documentId)for(const auto& tab:m_documents)if(tab.id==file.snapshot.documentId)if(auto* assistance=tab.editor->findChild<SourceAssistance*>())assistance->updateAnalysis(file.analysis,file.snapshot.source,file.snapshot.path);
        hasErrors = hasErrors || !file.analysis.ok();
        if (file.snapshot.documentId && file.analysis.ok())
            m_analysisContexts.insert(file.snapshot.documentId, {file.snapshot.revision,
                QString::fromStdString(file.analysis.program.algebraId), file.analysis.program.gamma,
                QString::fromStdString(file.analysis.program.languageVersion)});
        if (file.execution) {
            if(auto* explorer=findChild<ResearchExplorer*>(QStringLiteral("researchExplorer")))explorer->acceptResult(*file.execution);
            appendProblems(file.snapshot, file.execution->runtimeDiagnostics);
            if (file.execution->clearConsole) m_console->clearTranscript();
            if (!result.cancelled && file.execution->status != runtime::ExecutionStatus::Cancelled &&
                !file.execution->figureActions.empty()) {
                m_figures->apply(file.execution->figureActions);
                if (auto* dock = findChild<QDockWidget*>(QStringLiteral("figuresDock"))) {
                    if (m_figures->figureCount()) { dock->show(); dock->raise(); }
                    else dock->hide();
                }
            }
            QStringList normalLines;
            auto flushNormal=[&]{if(!normalLines.isEmpty()){appendCommandOutput(normalLines.join('\n'));normalLines.clear();}};
            for(std::size_t index=0;index<file.execution->output.size();++index) {
                const QString line=QString::fromStdString(file.execution->output[index]);
                if(std::find(file.execution->warningOutputLines.begin(),file.execution->warningOutputLines.end(),index)!=file.execution->warningOutputLines.end()) {
                    flushNormal();m_console->appendError(line);
                }else normalLines.append(line);
            }
            flushNormal();
            appendConsoleDiagnostics(file.snapshot,file.analysis.diagnostics);
            appendConsoleDiagnostics(file.snapshot,file.execution->runtimeDiagnostics);
            const bool rejectedContext = std::any_of(file.execution->runtimeDiagnostics.begin(),
                file.execution->runtimeDiagnostics.end(), [](const language::Diagnostic& diagnostic) {
                    return diagnostic.code == "R1009";
                });
            const bool contextCommitted = file.execution->status == runtime::ExecutionStatus::Success ||
                file.execution->status == runtime::ExecutionStatus::RuntimeError;
            if (file.analysis.ok() && contextCommitted && !rejectedContext) {
                m_consoleContext = AnalysedContext{0, QString::fromStdString(file.analysis.program.algebraId),
                    file.analysis.program.gamma, QStringLiteral("0.2")};
                refreshCommandContext();
            }
            switch (file.execution->status) {
            case runtime::ExecutionStatus::Success:
                setCalculationStatus(m_figureError.isEmpty() ? tr("%1 completed")
                    .arg(file.snapshot.command ? tr("Command") : tr("Run"))
                    : tr("Calculation completed with a plotting error: %1").arg(m_figureError)); break;
            case runtime::ExecutionStatus::StaticError:
                setCalculationStatus(tr("Source checking failed. No statements were evaluated.")); break;
            case runtime::ExecutionStatus::RuntimeError:
                setCalculationStatus(tr("Stopped at a runtime error. Earlier output and completed assignments are retained.")); break;
            case runtime::ExecutionStatus::Cancelled:
                setCalculationStatus(tr("Cancelled. The prior workspace was restored; earlier printed output remains in the transcript.")); break;
            }
            if (file.snapshot.command && !file.execution->ok() &&
                file.execution->status != runtime::ExecutionStatus::Cancelled) {
                m_console->setInputText(QString::fromUtf8(file.snapshot.source), true);
            }
        }else appendConsoleDiagnostics(file.snapshot,file.analysis.diagnostics);
    }
    if (result.cancelled) {
        if (ran) appendCommandOutput(tr("Execution interrupted."));
        if (ran && result.files.isEmpty()) setCalculationStatus(tr("Run cancelled before evaluation."));
        statusBar()->showMessage(tr("Work cancelled."), 8000);
    } else if (!ran) {
        statusBar()->showMessage(hasErrors
            ? tr("Check finished with errors; see Problems.")
            : tr("Check finished: no static errors in %1 file(s). Source was not executed.").arg(result.files.size()), 10000);
    } else {
        statusBar()->showMessage(m_calculationStatus, 10000);
    }
    if (problemCount() && !ran) if (auto* dock = findChild<QDockWidget*>(QStringLiteral("problemsDock"))) {
        dock->show(); dock->raise();
    }
    if (ran) if (auto* dock = findChild<QDockWidget*>(QStringLiteral("resultsDock"))) { dock->show(); dock->raise(); }
    updateUi(); emit workFinished(ran, false);
    if (result.kind == WorkKind::RunCommand || m_returnToCommandPrompt) m_console->setFocus();
}

void MainWindow::navigateProblem(int row) {
    auto* item = m_problems->topLevelItem(row);
    if (!item) return;
    const SourceSnapshot source = item->data(0, SnapshotRole).value<SourceSnapshot>();
    navigateDiagnostic(source,{static_cast<std::size_t>(item->data(0,OffsetRole).toULongLong()),
                               static_cast<std::size_t>(item->data(0,LengthRole).toULongLong())});
}

void MainWindow::navigateDiagnostic(const SourceSnapshot& source,language::SourceSpan span) {
    if (source.command) {
        m_console->setInputText(QString::fromUtf8(source.source), true);
        const auto offset=std::min<std::size_t>(span.offset,static_cast<std::size_t>(source.source.size()));
        const auto length=std::min<std::size_t>(span.length,static_cast<std::size_t>(source.source.size())-offset);
        const int start=m_console->inputStart()+QString::fromUtf8(source.source.constData(),static_cast<qsizetype>(offset)).size();
        const int count=QString::fromUtf8(source.source.constData()+offset,static_cast<qsizetype>(length)).size();
        auto cursor=m_console->textCursor();cursor.setPosition(start);
        cursor.setPosition(std::min(start+std::max(1,count),m_console->document()->characterCount()-1),QTextCursor::KeepAnchor);
        m_console->setTextCursor(cursor);m_console->ensureCursorVisible();
        m_console->setFocus();
        statusBar()->showMessage(tr("Command diagnostic: edit the recalled command at >>."), 8000); return;
    }
    if (!snapshotIsCurrent(source)) {
        statusBar()->showMessage(tr("This diagnostic belongs to an earlier revision. Check the file again."), 10000); return;
    }
    int index = -1;
    if (source.documentId) {
        for (int i = 0; i < m_documents.size(); ++i) if (m_documents[i].id == source.documentId) index = i;
    } else {
        QString error;
        if (!openSourcePath(source.path, &error)) { showError(tr("Open diagnostic source"), error); return; }
        index = m_tabs->currentIndex();
    }
    if (index < 0) return;
    m_tabs->setCurrentIndex(index);
    auto* editor = m_documents[index].editor;
    const qsizetype byteOffset = std::min<std::size_t>(span.offset,static_cast<std::size_t>(source.source.size()));
    const qsizetype byteLength = std::min<std::size_t>(span.length,static_cast<std::size_t>(source.source.size()-byteOffset));
    auto editorText=[](QString text){if(!text.isEmpty()&&text.front()==QChar(0xfeff))text.remove(0,1);text.replace(QStringLiteral("\r\n"),QStringLiteral("\n"));text.replace(QLatin1Char('\r'),QLatin1Char('\n'));return text;};
    const int start = static_cast<int>(editorText(QString::fromUtf8(source.source.constData(), byteOffset)).size());
    const int length = static_cast<int>(editorText(QString::fromUtf8(source.source.constData() + byteOffset, byteLength)).size());
    QTextCursor cursor = editor->textCursor();
    const int maximum = std::max(0, editor->document()->characterCount() - 1);
    cursor.setPosition(std::min(start, maximum));
    cursor.setPosition(std::min(start + std::max(1, length), maximum), QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor); editor->ensureCursorVisible(); editor->setFocus();
}

void MainWindow::invalidateDocumentAnalysis(Document* document) {
    const int index = indexOf(document);
    if (index < 0) return;
    const quint64 id = m_documents[index].id;
    if(m_assistant)m_assistant->invalidateSource();
    const auto context = m_analysisContexts.constFind(id);
    if (context != m_analysisContexts.constEnd() && context->revision != document->revision()) m_analysisContexts.remove(id);
    for (int row = problemCount() - 1; row >= 0; --row) {
        const SourceSnapshot source = m_problems->topLevelItem(row)->data(0, SnapshotRole).value<SourceSnapshot>();
        if (source.documentId == id && (source.revision != document->revision() || source.path != document->path()))
            delete m_problems->takeTopLevelItem(row);
    }
    refreshRunFreshness();
}

void MainWindow::refreshRunFreshness() {
    if (m_lastRunSnapshot && !snapshotIsCurrent(*m_lastRunSnapshot))
        setCalculationStatus(tr("Results belong to an earlier source revision or a closed document. Run File again for current results."));
}

void MainWindow::updateUi() {
    refreshWorkerActivity();
    if(auto* closeAll=m_actions.value(QStringLiteral("fileCloseAllDocuments")))closeAll->setEnabled(!m_documents.isEmpty()||(m_workspaceTabs&&m_workspaceTabs->count()>1));
    if(m_assistant)m_assistant->setScienceBusy(m_workPending);
    for (int i = 0; i < m_documents.size(); ++i) {
        const auto& tab = m_documents[i];
        QString name = tab.document->path().isEmpty() ? tr("Recovered source") : QFileInfo(tab.document->path()).fileName();
        if (tab.document->isModified()) name += QStringLiteral(" *");
        m_tabs->setTabText(i, name);
        m_tabs->setTabToolTip(i, tab.document->path().isEmpty() ? tr("Unsaved recovery buffer") : tab.document->path());
    }
    const bool hasDocument = currentDocument();
    for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
        if (it.key().startsWith(QStringLiteral("edit")) ||
            it.key() == QStringLiteral("fileSave") || it.key() == QStringLiteral("fileSaveAs") ||
              it.key() == QStringLiteral("fileSaveAll") || it.key() == QStringLiteral("fileCloseTab") ||
              it.key().startsWith(QStringLiteral("filePrint")) || it.key() == QStringLiteral("filePageSetup"))
            it.value()->setEnabled(hasDocument);
    }
    for (const auto& name : {"fileNewSource", "fileCloseProject", "projectAddSource", "projectNewFolder"})
        m_actions.value(QString::fromLatin1(name))->setEnabled(m_project.has_value());
    m_actions.value(QStringLiteral("projectEnableInterpreter"))->setEnabled(
        m_project && m_project->languageVersion == QStringLiteral("0.1"));
    const bool commandBusy = m_workPending && m_latestKind == WorkKind::RunCommand;
    m_actions.value(QStringLiteral("fileExportCode"))->setEnabled(hasDocument&&!m_workPending);
    m_actions.value(QStringLiteral("checkFile"))->setEnabled(hasDocument && !commandBusy);
    m_actions.value(QStringLiteral("checkProject"))->setEnabled(m_project.has_value() && !commandBusy);
    m_actions.value(QStringLiteral("runFile"))->setEnabled(hasDocument && !commandBusy);
    m_actions.value(QStringLiteral("stopExecution"))->setEnabled(m_workPending);
    m_console->setBusy(m_workPending);
    if(auto* explorer=findChild<ResearchExplorer*>(QStringLiteral("researchExplorer")))explorer->setExecutionBusy(m_workPending);
    if (auto* dock = findChild<QDockWidget*>(QStringLiteral("resultsDock")))
        dock->setWindowTitle(m_workPending
            ? (m_console->isStopping() ? tr("Command Window — Stopping…") : tr("Command Window — Running…"))
            : tr("Command Window"));
    const bool selectedSource = m_project && !selectedRelativeSource().isEmpty();
    m_actions.value(QStringLiteral("projectRunSource"))->setEnabled(selectedSource && !commandBusy);
    for (const auto& name : {"projectRenameSource", "projectRemoveSource", "projectDeleteSource"})
        m_actions.value(QString::fromLatin1(name))->setEnabled(selectedSource);
    if (auto* editor = currentEditor()) {
        m_actions.value(QStringLiteral("editUndo"))->setEnabled(editor->document()->isUndoAvailable());
        m_actions.value(QStringLiteral("editRedo"))->setEnabled(editor->document()->isRedoAvailable());
        m_actions.value(QStringLiteral("editCut"))->setEnabled(editor->textCursor().hasSelection());
        m_actions.value(QStringLiteral("editCopy"))->setEnabled(editor->textCursor().hasSelection());
    }
    if(auto* book=currentWorkbook()){
        for(const auto& key:{"fileSave","fileSaveAs","fileCloseTab","editCopy","editCut","editPaste","editSelectAll"})if(auto* item=m_actions.value(QString::fromLatin1(key)))item->setEnabled(!book->isLoading());
        m_actions.value(QStringLiteral("editUndo"))->setEnabled(book->canUndo());m_actions.value(QStringLiteral("editRedo"))->setEnabled(book->canRedo());
        if(auto* all=m_actions.value(QStringLiteral("fileSaveAll")))all->setEnabled(true);
    }
    QString title = QStringLiteral("Pentarnion Studio v0.2");
    if (m_project) title = m_project->name + QStringLiteral(" — ") + title;
    if (auto* document = currentDocument()) {
        if (!document->path().isEmpty()) title = QFileInfo(document->path()).fileName() +
            (document->isModified() ? QStringLiteral(" * — ") : QStringLiteral(" — ")) + title;
        const auto cursor = currentEditor()->textCursor();
        m_documentStatus->setText(tr("%1 · %2 · Ln %3, Col %4")
            .arg(document->encodingDescription(), document->newlineDescription())
            .arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1));
    } else if(auto* book=currentWorkbook()){
        title=(book->path().isEmpty()?tr("Untitled workbook"):QFileInfo(book->path()).fileName())+(book->isModified()?QStringLiteral(" * — "):QStringLiteral(" — "))+title;
        m_documentStatus->setText(book->statusText());
    } else {
        m_documentStatus->setText(tr("Create or open a project · Check source or Run File for calculations"));
    }
    m_documentStatus->setVisible(statusBar()->currentMessage().isEmpty());
    setWindowTitle(title);
    const int current = m_tabs->currentIndex();
    if (current >= 0) {
        const auto context = m_analysisContexts.constFind(m_documents[current].id);
        if (context != m_analysisContexts.constEnd() && context->revision == m_documents[current].document->revision()) {
            m_contextStatus->setText(tr("Effective: %1 · γ=%2 · language %3")
                .arg(context->algebraId).arg(context->gamma, 0, 'g', 6).arg(context->version) + QStringLiteral(" | ") + workspaceContextText());
            return;
        }
        // Display context is stable while source/path/project metadata and
        // watcher state are unchanged. Do not copy source or enumerate project
        // directories on every cursor movement. Actual run/check snapshots use
        // assignSnapshotContext directly and always perform fresh validation.
        DefaultDisplayContextKey key;
        key.path = m_documents[current].document->path();
        if (m_project) {
            key.hasProject = true;
            key.projectRoot = m_project->rootPath;
            key.projectPath = m_project->manifestPath;
            key.projectAlgebra = m_project->algebraId;
            key.projectGamma = m_project->gamma;
            key.projectVersion = m_project->languageVersion;
        }
        const quint64 id = m_documents[current].id;
        auto cached = m_defaultDisplayContexts.find(id);
        if (cached == m_defaultDisplayContexts.end() || cached->key != key) {
            SourceSnapshot snapshot;
            snapshot.path = key.path;
            assignSnapshotContext(snapshot);
            cached = m_defaultDisplayContexts.insert(id, {key, snapshot.algebraId, snapshot.languageVersion, snapshot.gamma});
        }
        m_contextStatus->setText(tr("Default: %1 · γ=%2 · language %3")
            .arg(cached->algebraId).arg(cached->gamma, 0, 'g', 6).arg(cached->version) + QStringLiteral(" | ") + workspaceContextText());
    } else m_contextStatus->setText(workspaceContextText());
}

void MainWindow::refreshTree() {
    QSignalBlocker blocker(m_tree);
    QSet<QString> expanded;
    const bool sameProject=m_project&&m_tree->topLevelItemCount()>0&&
        m_tree->topLevelItem(0)->toolTip(0)==m_project->rootPath;
    QString selected;
    bool hadSelection=false;
    if(sameProject){
        if(auto* item=m_tree->currentItem()){
            selected=item->data(0,RelativeRole).toString();
            hadSelection=true;
        }
        for(QTreeWidgetItemIterator it(m_tree);*it;++it)
            if((*it)->data(0,FolderRole).toBool()&&(*it)->isExpanded())
                expanded.insert((*it)->data(0,RelativeRole).toString());
    }
    m_tree->clear();
    m_tree->setProperty("pentarnionProjectTreeLimited",false);
    if(!m_project){
        auto* hint=new QTreeWidgetItem(m_tree,{tr("No project open")});
        hint->setFlags(Qt::NoItemFlags);
        return;
    }
    auto* root=new QTreeWidgetItem(m_tree,{m_project->name});
    root->setIcon(0,desktop::projectKindIcon(desktop::ProjectIconKind::Project));
    root->setData(0,FolderRole,true);
    root->setData(0,RelativeRole,QString());
    root->setToolTip(0,m_project->rootPath);
    QHash<QString,QTreeWidgetItem*> folders,items;
    folders.insert(QString(),root);
    items.insert(QString(),root);
    const auto ensureFolder=[&](const QString& relative){
        QString current;
        QTreeWidgetItem* parent=root;
        for(const auto& part:relative.split('/',Qt::SkipEmptyParts)){
            current=current.isEmpty()?part:current+'/'+part;
            if(!folders.contains(current)){
                auto* item=new QTreeWidgetItem(parent,{part});
                item->setIcon(0,desktop::projectKindIcon(desktop::projectIconKind(
                    QFileInfo(QDir(m_project->rootPath).filePath(current)))));
                item->setData(0,FolderRole,true);
                item->setData(0,RelativeRole,current);
                item->setToolTip(0,QDir(m_project->rootPath).filePath(current));
                folders.insert(current,item);
                items.insert(current,item);
            }
            parent=folders.value(current);
        }
        return parent;
    };
    // Walk at most a bounded number of entries, without following links. Filter
    // generated-cache roots before opening them, rather than recursively finding
    // their many native build files and filtering the final tree afterward.
    constexpr int maximumEntries=10000,maximumWorkbooks=2048,maximumDepth=64;
    const QSet<QString> internalDirectories{
        QStringLiteral(".penta-cache"),QStringLiteral(".penta-native"),QStringLiteral(".git")};
    QStringList pendingDirectories{m_project->rootPath};
    const QString canonicalRoot=QFileInfo(m_project->rootPath).canonicalFilePath();
    const QString rootPrefix=canonicalRoot.endsWith('/')?canonicalRoot:canonicalRoot+'/';
#ifdef Q_OS_WIN
    constexpr auto pathSensitivity=Qt::CaseInsensitive;
#else
    constexpr auto pathSensitivity=Qt::CaseSensitive;
#endif
    int scanned=0,workbooks=0;
    bool limited=false;
    while(!pendingDirectories.isEmpty()&&scanned<maximumEntries){
        const QString directory=pendingDirectories.takeLast();
        QDirIterator children(directory,QDir::Dirs|QDir::Files|QDir::Hidden|
            QDir::NoDotAndDotDot|QDir::NoSymLinks,QDirIterator::NoIteratorFlags);
        while(children.hasNext()){
            if(scanned>=maximumEntries){limited=true;break;}
            children.next();
            ++scanned;
            const auto child=children.fileInfo();
            const QString name=child.fileName();
#ifdef Q_OS_WIN
            const bool internal=internalDirectories.contains(name.toLower());
#else
            const bool internal=internalDirectories.contains(name);
#endif
            if(child.isSymLink()||child.isJunction()||(child.isDir()&&internal))continue;
            const QString canonical=child.canonicalFilePath();
            if(canonicalRoot.isEmpty()||canonical.isEmpty()||
                !canonical.startsWith(rootPrefix,pathSensitivity))continue;
            const QString relative=asPortableRelative(m_project->rootPath,child.absoluteFilePath());
            if(child.isDir()){
                ensureFolder(relative);
                if(relative.count('/')+1<maximumDepth)pendingDirectories.append(child.absoluteFilePath());
                else limited=true;
            }else if(child.isFile()&&workbook::WorkbookDocument::supportsPath(child.absoluteFilePath())){
                if(workbooks>=maximumWorkbooks){limited=true;continue;}
                const int separator=relative.lastIndexOf('/');
                auto* item=new QTreeWidgetItem(ensureFolder(separator<0?QString():relative.left(separator)),{name});
                item->setData(0,RelativeRole,relative);
                item->setData(0,FolderRole,false);
                item->setData(0,Qt::UserRole+2,QStringLiteral("workbook"));
                item->setFlags(item->flags()&~Qt::ItemIsEditable);
                item->setIcon(0,desktop::projectKindIcon(desktop::projectIconKind(child)));
                item->setToolTip(0,child.absoluteFilePath());
                items.insert(relative,item);
                ++workbooks;
            }
        }
    }
    if(!pendingDirectories.isEmpty())limited=true;
    // Registered source entries remain authoritative and are shown even when the
    // bounded ancillary-data scan reaches its limit. Never register data files as
    // executable source merely because they appear in the Project browser.
    for(const auto& relative:m_project->sources){
        const int separator=relative.lastIndexOf('/');
        auto* item=new QTreeWidgetItem(ensureFolder(separator<0?QString():relative.left(separator)),{relative.mid(separator+1)});
        item->setData(0,RelativeRole,relative);
        item->setData(0,FolderRole,false);
        item->setData(0,Qt::UserRole+2,QStringLiteral("source"));
        item->setFlags(item->flags()|Qt::ItemIsEditable);
        item->setIcon(0,desktop::projectKindIcon(desktop::projectIconKind(
            QFileInfo(QDir(m_project->rootPath).filePath(relative)))));
        item->setToolTip(0,QDir(m_project->rootPath).filePath(relative));
        items.insert(relative,item);
    }
    for(auto it=folders.cbegin();it!=folders.cend();++it)
        it.value()->setExpanded(sameProject?expanded.contains(it.key()):it.key().isEmpty());
    if(hadSelection&&items.contains(selected))m_tree->setCurrentItem(items.value(selected));
    m_tree->setProperty("pentarnionProjectTreeLimited",limited);
    if(limited)root->setWhatsThis(0,tr("The ancillary project browser is bounded to 10,000 entries, 2,048 workbooks and 64 directory levels. Registered source files remain available."));
}

QString MainWindow::selectedRelativeSource() const {
    const auto* item=m_tree->currentItem();
    if(!m_project||!item||item->data(0,FolderRole).toBool())return {};
    const QString relative=item->data(0,RelativeRole).toString();
    return QFileInfo(relative).suffix().compare(QStringLiteral("p"),Qt::CaseInsensitive)==0&&
        m_project->sources.contains(relative)?relative:QString();
}

QString MainWindow::selectedRelativeFolder() const {
    auto* item = m_tree->currentItem();
    if (!item) return {};
    QString relative = item->data(0, RelativeRole).toString();
    if (item->data(0, FolderRole).toBool()) return relative;
    const int separator = relative.lastIndexOf('/');
    return separator < 0 ? QString() : relative.left(separator);
}

QByteArray MainWindow::diskFingerprint(const QString& path) const {
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArrayLiteral("missing-or-unreadable");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return QByteArrayLiteral("read-failure");
    return hash.result();
}

void MainWindow::refreshWatchers() {
    m_defaultDisplayContexts.clear();
    if (!m_watcher) return;
    QSet<QString> watch;
    for (const auto& tab : m_documents) {
        const QString path = tab.document->path();
        if (path.isEmpty()) continue;
        if (QFileInfo::exists(path)) watch.insert(path);
        const QString parent = QFileInfo(path).absolutePath();
        if (QFileInfo::exists(parent)) watch.insert(parent);
    }
    QSet<QString> current;
    for (const auto& path : m_watcher->files()) current.insert(path);
    for (const auto& path : m_watcher->directories()) current.insert(path);
    const auto removed = current - watch;
    const auto added = watch - current;
    if (!removed.isEmpty()) m_watcher->removePaths(removed.values());
    // Atomic source replacement can remove its file watch. Compare against
    // the watcher's current list so that such watches are reinstalled.
    if (!added.isEmpty()) m_watcher->addPaths(added.values());
}

void MainWindow::checkExternalChanges() {
    if (m_closing) return;
    if (m_handlingExternal) {
        // A source-change prompt runs a nested event loop. Preserve further
        // notifications until that decision has completed.
        if (!m_externalChangeTimer->isActive()) m_externalChangeTimer->start();
        return;
    }
    m_handlingExternal = true;
    m_defaultDisplayContexts.clear();
    for (const auto& tab : m_documents) {
        auto* document = tab.document;
        const QByteArray fingerprint = diskFingerprint(document->path());
        if (fingerprint == m_observedDisk.value(document)) continue;
        m_observedDisk.insert(document, fingerprint);
        if (!document->hasExternalChange()) continue;
        const bool hadUnsavedEdits = document->isModified();
        // The retained buffer now differs from disk even if it was clean before
        // the external edit. Treat it as unsaved so recovery and close protect it.
        document->textDocument()->setModified(true);
        const int index = indexOf(document);
        if (index >= 0) m_tabs->setCurrentIndex(index);
        QMessageBox question(QMessageBox::Warning, tr("Source changed outside Studio"),
            tr("%1 was changed or deleted outside Pentarnion Studio.\n\nYour current buffer has been kept.")
                .arg(QFileInfo(document->path()).fileName()), QMessageBox::NoButton, this);
        question.setObjectName(QStringLiteral("externalChangePrompt"));
        auto* reload = question.addButton(tr("Reload Disk Version"), QMessageBox::AcceptRole);
        auto* keep = question.addButton(tr("Keep Buffer"), QMessageBox::RejectRole);
        auto* saveAs = question.addButton(tr("Save Buffer As…"), QMessageBox::ActionRole);
        reload->setEnabled(QFileInfo::exists(document->path()));
        question.setDefaultButton(keep);
        question.exec();
        if (question.clickedButton() == reload) {
            if (hadUnsavedEdits && QMessageBox::warning(this, tr("Discard buffer edits?"),
                    tr("Reloading will discard the unsaved edits in this buffer."),
                    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard)
                continue;
            QString error;
            if (!document->reload(&error)) showError(tr("Reload source"), error);
            else m_observedDisk.insert(document, diskFingerprint(document->path()));
        } else if (question.clickedButton() == saveAs) {
            saveAsInteractive(document);
        }
    }
    refreshWatchers(); updateUi();
    m_handlingExternal = false;
}

void MainWindow::snapshotRecovery() {
    if (!m_persistenceEnabled) return;
    for (const auto& tab : m_documents) {
        if (!tab.document->isModified()) continue;
        RecoveryCheckpoint checkpoint;
        checkpoint.revision = tab.document->revision();
        checkpoint.recoveryId = tab.recoveryId;
        checkpoint.path = tab.document->path();
        checkpoint.projectPath = m_project ? m_project->manifestPath : QString();
        checkpoint.bom = tab.document->hasBom();
        checkpoint.newline = tab.document->newline();
        checkpoint.cursorPosition = tab.editor->textCursor().position();
        const QString recoveryPath = QDir(m_recovery.directory()).filePath(tab.recoveryId + QStringLiteral(".json"));
        const auto readMetadata = [&checkpoint, &recoveryPath] {
            const QFileInfo file(recoveryPath);
            if (!file.isFile() || file.isSymLink()) return false;
            checkpoint.fileSize = file.size();
            checkpoint.modified = file.lastModified();
            checkpoint.created = file.birthTime();
            checkpoint.metadataChanged = file.metadataChangeTime();
            return true;
        };
        const bool present = readMetadata();
        const auto previous = m_recoveryCheckpoints.constFind(tab.id);
        if (present && previous != m_recoveryCheckpoints.constEnd() && *previous == checkpoint)
            continue;
        RecoveryRecord record;
        record.id = checkpoint.recoveryId;
        record.path = checkpoint.path;
        record.text = tab.document->text();
        record.projectPath = checkpoint.projectPath;
        record.bom = checkpoint.bom;
        record.newline = checkpoint.newline;
        record.cursorPosition = checkpoint.cursorPosition;
        QString error;
        if (!m_recovery.write(record, &error)) {
            // Failed snapshots must be retried, and externally replaced files
            // must pass RecoveryStore's ownership validation before any write.
            m_recoveryCheckpoints.remove(tab.id);
            statusBar()->showMessage(tr("Recovery snapshot could not be saved: %1").arg(error), 10000);
        } else if (readMetadata()) {
            m_recoveryCheckpoints.insert(tab.id, checkpoint);
        } else {
            m_recoveryCheckpoints.remove(tab.id);
        }
    }
}

void MainWindow::configureAutosave(bool enabled, int periodicMilliseconds) {
    m_autosaveEnabled = enabled;
    m_autosaveInterval = std::clamp(periodicMilliseconds, 100, 3600000);
    if (!m_autosavePeriodic || !m_autosaveDebounce) return;
    m_autosavePeriodic->stop();
    if (enabled) m_autosavePeriodic->start(m_autosaveInterval);
    else { m_autosaveDebounce->stop(); m_autosavePending.clear(); }
}

void MainWindow::scheduleAutosave(Document* document) {
    if (m_printDialogActive || !m_autosaveEnabled || !m_autosaveDebounce || !document ||
        document->path().isEmpty() || !document->isModified()) return;
    const int index = indexOf(document);
    if (index < 0) return;
    m_autosavePending.insert(m_documents[index].id);
    m_autosaveDebounce->start();
}

void MainWindow::autosavePendingDocuments() {
    const auto pending = std::exchange(m_autosavePending, {});
    if (!m_autosaveEnabled) return;
    for (const auto& tab : m_documents)
        if (pending.contains(tab.id)) tryAutosave(tab.document);
}

void MainWindow::autosaveNamedDocuments() {
    if (!m_autosaveEnabled || m_closing) return;
    for (const auto& tab : m_documents) tryAutosave(tab.document);
}

void MainWindow::tryAutosave(Document* document) {
    if(m_printDialogActive)return;
    if (QApplication::activeModalWidget()) return; // Do not race a save/close decision.
    if (!document || document->path().isEmpty() || !document->isModified()) return;
    const QFileInfo file(document->path());
    if (!file.exists() || !file.isFile() || !file.isReadable() || !file.isWritable() || document->hasExternalChange()) {
        statusBar()->showMessage(tr("Autosave skipped for %1: the disk source changed, is missing or is not writable. Your buffer is retained.")
            .arg(file.fileName()), 10000);
        return;
    }
    QString error;
    if (!document->save(&error)) {
        statusBar()->showMessage(tr("Autosave could not save %1: %2. Your edits are retained.")
            .arg(file.fileName(), error), 10000);
        return;
    }
    m_observedDisk.insert(document, diskFingerprint(document->path()));
    refreshWatchers(); updateUi();
    statusBar()->showMessage(tr("Autosaved %1").arg(file.fileName()), 4000);
}

void MainWindow::showAutosaveSettings() {
    QDialog dialog(this); dialog.setWindowTitle(tr("Autosave Settings"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* enabled = new QCheckBox(tr("Automatically save named source files"), &dialog);
    enabled->setChecked(m_autosaveEnabled); layout->addWidget(enabled);
    auto* row = new QFormLayout;
    auto* seconds = new QSpinBox(&dialog); seconds->setRange(5, 3600);
    seconds->setValue(std::max(5, m_autosaveInterval / 1000)); seconds->setSuffix(tr(" seconds"));
    row->addRow(tr("Periodic interval:"), seconds); layout->addLayout(row);
    auto* explanation = new QLabel(tr("Dirty named files are safely saved about half a second after the mouse leaves the source editor, or focus/tab changes, and periodically. Unnamed buffers use recovery only. External changes and save failures retain your edits for manual resolution."), &dialog);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons); dialog.resize(560, 230);
    if (dialog.exec() == QDialog::Accepted) configureAutosave(enabled->isChecked(), seconds->value() * 1000);
}

void MainWindow::acknowledgeRecovery(const Tab& tab) {
    m_recoveryCheckpoints.remove(tab.id);
    if (!m_persistenceEnabled) return;
    QString error;
    if (!m_recovery.remove(tab.recoveryId, &error))
        statusBar()->showMessage(tr("Recovery cleanup: %1").arg(error), 8000);
}

bool MainWindow::prepareClose(Document* document) {
    // A close can race a queued filesystem notification. Protect the retained
    // buffer even when the watcher has not yet marked an external change.
    if (document && !document->isModified() && document->hasExternalChange())
        document->textDocument()->setModified(true);
    if (!document || !document->isModified()) return true;
    const QString name = document->path().isEmpty() ? tr("recovered source") : QFileInfo(document->path()).fileName();
    QMessageBox question(QMessageBox::Question, tr("Save source changes?"),
        tr("Save your changes to %1?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    question.setObjectName(QStringLiteral("dirtyClosePrompt"));
    question.setDefaultButton(QMessageBox::Save);
    const int choice = question.exec();
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) return saveInteractive(document);
    return choice == QMessageBox::Discard;
}

bool MainWindow::prepareCloseAll() {
    // Ask before removing any tab. Cancelling a later prompt keeps all buffers.
    for (const auto& tab : m_documents)
        if (!prepareClose(tab.document)) { snapshotRecovery(); return false; }
    return true;
}

void MainWindow::disposeTab(int index, bool acknowledged) {
    if (index < 0 || index >= m_documents.size()) return;
    const Tab tab = m_documents[index];
    m_autosavePending.remove(tab.id);
    m_recoveryCheckpoints.remove(tab.id);
    if (acknowledged) acknowledgeRecovery(tab);
    m_observedDisk.remove(tab.document);
    m_defaultDisplayContexts.remove(tab.id);
    m_analysisContexts.remove(tab.id);
    // Closing a tab also destroys its highlighter. Formatting cleanup may emit
    // document/editor signals; they no longer describe an attached source tab.
    QObject::disconnect(tab.editor,nullptr,this,nullptr);
    QObject::disconnect(tab.document,nullptr,this,nullptr);
    QObject::disconnect(tab.document->textDocument(),nullptr,this,nullptr);
    m_documents.removeAt(index);
    m_tabs->removeTab(index);
    delete tab.editor;
    delete tab.document;
    refreshWatchers(); refreshRunFreshness(); updateUi();
}

void MainWindow::disposeAllTabs(bool acknowledged) {
    while (!m_documents.isEmpty()) disposeTab(static_cast<int>(m_documents.size()) - 1, acknowledged);
}

bool MainWindow::closeTab(int index) {
    if (index < 0 || index >= m_documents.size()) return true;
    if (!prepareClose(m_documents[index].document)) return false;
    disposeTab(index, true);
    return true;
}

bool MainWindow::saveInteractive(Document* document) {
    if (!document) return false;
    if (document->path().isEmpty()) return saveAsInteractive(document);
    bool overwrite = false;
    if (document->hasExternalChange()) {
        document->textDocument()->setModified(true);
        QMessageBox question(QMessageBox::Warning, tr("Resolve external source changes"),
            tr("The disk version of %1 differs from the version you opened. "
               "Choose how to preserve your buffer.").arg(QFileInfo(document->path()).fileName()),
            QMessageBox::NoButton, this);
        question.setObjectName(QStringLiteral("saveConflictPrompt"));
        auto* saveAs = question.addButton(tr("Save Buffer As…"), QMessageBox::ActionRole);
        auto* replace = question.addButton(tr("Overwrite Disk Version…"), QMessageBox::DestructiveRole);
        auto* keep = question.addButton(tr("Keep Buffer"), QMessageBox::RejectRole);
        question.setDefaultButton(keep); question.exec();
        if (question.clickedButton() == saveAs) return saveAsInteractive(document);
        if (question.clickedButton() != replace) return false;
        if (QMessageBox::warning(this, tr("Overwrite externally changed file?"),
                tr("Replace the current disk version with your buffer? The disk version will be overwritten."),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return false;
        overwrite = true;
    }
    QString error;
    if (!document->save(&error, overwrite)) { showError(tr("Save source"), error); return false; }
    m_observedDisk.insert(document, diskFingerprint(document->path()));
    refreshWatchers(); updateUi();
    return true;
}

bool MainWindow::saveAsInteractive(Document* document) {
    if (!document) return false;
    QString initial = document->path();
    if (initial.isEmpty()) initial = m_project ? QDir(m_project->rootPath).filePath(QStringLiteral("recovered.p")) : QStringLiteral("recovered.p");
    QString path = QFileDialog::getSaveFileName(this, tr("Save source as"), initial,
                                               tr("Pentarnion source (*.p)"), nullptr,
                                               QFileDialog::DontConfirmOverwrite);
    if (path.isEmpty()) return false;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".p");
    if (QFileInfo(path).suffix().compare(QStringLiteral("p"), Qt::CaseInsensitive) != 0) {
        showError(tr("Save source as"), tr("Source filenames must end in .p.")); return false;
    }
    const int other = indexOfPath(path);
    if (other >= 0 && m_documents[other].document != document) {
        showError(tr("Save source as"), tr("That file is already open in another tab. Save or close its buffer first.")); return false;
    }
    bool overwrite = false;
    const bool sameFile = Document::canonicalIdentity(path) == document->identity();
    if (QFileInfo::exists(path) && (!sameFile || document->hasExternalChange())) {
        if (QMessageBox::warning(this, tr("Replace existing source?"),
                tr("%1 already exists. Replace it with this buffer?").arg(QDir::toNativeSeparators(path)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return false;
        overwrite = true;
    }
    QString error, projectWarning;
    if (!saveDocumentAs(document, path, overwrite, &error, &projectWarning)) {
        showError(tr("Save source as"), error); return false;
    }
    if (!projectWarning.isEmpty()) showError(tr("Source saved; project list unchanged"), projectWarning);
    return true;
}

bool MainWindow::saveDocumentAs(Document* document, const QString& path,
                                bool overwriteExisting, QString* error,
                                QString* projectWarning) {
    if (error) error->clear();
    if (projectWarning) projectWarning->clear();
    if (!document) return fail(error, tr("There is no source file to save."));
    if (QFileInfo(path).suffix().compare(QStringLiteral("p"), Qt::CaseInsensitive) != 0)
        return fail(error, tr("Source filenames must end in .p."));
    const int other = indexOfPath(path);
    if (other >= 0 && m_documents[other].document != document)
        return fail(error, tr("That destination is already open in another writable tab."));
    QString projectRelative;
    if (m_project) {
        const QString relative = asPortableRelative(m_project->rootPath, path);
        if (!relative.startsWith(QStringLiteral("../")) && relative != QStringLiteral("..") &&
            !QDir::isAbsolutePath(relative)) {
            // Validate portability, canonical containment and links before writing.
            QString checkedPath;
            if (!ProjectService::resolveSource(*m_project, relative, checkedPath, error, false)) return false;
            for (const auto& source : m_project->sources)
                if (source != relative && source.compare(relative, Qt::CaseInsensitive) == 0)
                    return fail(error, tr("That filename differs from an existing project source only by case."));
            projectRelative = relative;
        }
    }
    if (!document->saveAs(path, error, overwriteExisting)) return false;
    if (m_project && !projectRelative.isEmpty() && !m_project->sources.contains(projectRelative)) {
        QString registrationError;
        if (!ProjectService::includeSource(*m_project, projectRelative, &registrationError) && projectWarning)
            *projectWarning = tr("The source was saved successfully, but could not be added to the project:\n%1")
                .arg(registrationError);
    }
    m_observedDisk.insert(document, diskFingerprint(document->path()));
    refreshTree(); refreshWatchers(); updateUi();
    return true;
}

void MainWindow::showError(const QString& title, const QString& message) {
    if (!message.isEmpty()) QMessageBox::warning(this, title, message);
}

void MainWindow::requestNewProject() {
    const QString parent = QFileDialog::getExistingDirectory(this, tr("Choose parent folder for new project"),
        m_project ? QFileInfo(m_project->rootPath).absolutePath() : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (parent.isEmpty()) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Pentarnion project"), tr("Project name:"),
                                              QLineEdit::Normal, QStringLiteral("MyPentarnionProject"), &accepted);
    if (!accepted || name.isEmpty()) return;
    QString error;
    if (!createProjectAt(parent, name, &error) && !error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
        showError(tr("Create project"), error);
}

void MainWindow::requestOpenProject() {
    const QString manifest = QFileDialog::getOpenFileName(this, tr("Open Pentarnion project"),
        m_project ? m_project->rootPath : QString(), tr("Pentarnion project (*.pprj)"));
    if (manifest.isEmpty()) return;
    QString error;
    if (!openProjectPath(manifest, &error) && !error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
        showError(tr("Open project"), error);
}

void MainWindow::requestNewSource() {
    if (!m_project) return;
    const QString folder = selectedRelativeFolder();
    bool accepted = false;
    const QString relative = QInputDialog::getText(this, tr("New source file"),
        tr("Project-relative filename (subfolders allowed):"), QLineEdit::Normal,
        folder.isEmpty() ? QStringLiteral("untitled.p") : folder + QStringLiteral("/untitled.p"), &accepted);
    if (!accepted || relative.isEmpty()) return;
    QString error;
    if (!addSource(relative, &error)) showError(tr("New source file"), error);
}

void MainWindow::requestOpenSource() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open source file"),
        m_project ? m_project->rootPath : QString(), tr("Pentarnion source (*.p)"));
    if (path.isEmpty()) return;
    QString error;
    if (!openSourcePath(path, &error)) showError(tr("Open source"), error);
}

void MainWindow::requestAddExistingSource() {
    if (!m_project) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Add existing project source"),
        m_project->rootPath, tr("Pentarnion source (*.p)"));
    if (path.isEmpty()) return;
    QString error;
    if (!ProjectService::includeSource(*m_project, asPortableRelative(m_project->rootPath, path), &error)) {
        showError(tr("Add source"), error); return;
    }
    refreshTree();
    if (!openSourcePath(path, &error)) showError(tr("Open source"), error);
}

void MainWindow::requestNewFolder() {
    if (!m_project) return;
    bool accepted = false;
    const QString folder = selectedRelativeFolder();
    const QString relative = QInputDialog::getText(this, tr("New folder"),
        tr("Project-relative folder name:"), QLineEdit::Normal,
        folder.isEmpty() ? QStringLiteral("sources") : folder + QStringLiteral("/sources"), &accepted);
    if (!accepted || relative.isEmpty()) return;
    QString error;
    if (!ProjectService::createFolder(*m_project, relative, &error)) showError(tr("New folder"), error);
    refreshTree();
}

void MainWindow::requestRenameSource() {
    if(!m_project||selectedRelativeSource().isEmpty())return;
    auto* item=m_tree->currentItem();
    for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
    m_tree->setFocus(Qt::ShortcutFocusReason);
    m_tree->scrollToItem(item);
    m_tree->editItem(item,0);
    // QAbstractItemView focuses the editor after setEditorData; that focus can
    // select the whole string. Apply basename selection after focus settles.
    if(auto* editor=m_tree->findChild<QLineEdit*>(QStringLiteral("projectSourceRenameEditor"))){
        const int extension=editor->text().lastIndexOf(QLatin1Char('.'));
        editor->setSelection(0,extension>0?extension:editor->text().size());
    }
}

void MainWindow::requestRemoveSource(bool deleteFile) {
    const QString relative = selectedRelativeSource();
    if (relative.isEmpty() || !m_project) return;
    const QString explanation = deleteFile
        ? tr("Delete %1 from disk and remove it from the project? This deletes only this source file.").arg(relative)
        : tr("Remove %1 from the project list? The file will remain on disk.").arg(relative);
    if (QMessageBox::warning(this, deleteFile ? tr("Delete source file?") : tr("Remove source from project?"),
            explanation, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    QString error, absolute;
    if (!ProjectService::resolveSource(*m_project, relative, absolute, &error, deleteFile)) {
        showError(tr("Remove source"), error); return;
    }
    const int index = indexOfPath(absolute);
    if (deleteFile && index >= 0 && m_documents[index].document->isModified()) {
        if (!prepareClose(m_documents[index].document)) return;
        // Explicit Discard was acknowledged. Delete closes the tab after the file operation.
        // Keep modified state intact until removal succeeds.
        if (!ProjectService::removeSource(*m_project, relative, true, &error)) {
            showError(tr("Delete source"), error); return;
        }
        disposeTab(index, true); refreshTree(); return;
    }
    if (!removeProjectSource(relative, deleteFile, &error)) showError(tr("Remove source"), error);
}

void MainWindow::requestCloseProject() {
    if (!prepareCloseAll()) return;
    disposeAllTabs(true); m_project.reset(); refreshTree(); updateUi(); clearWorkspace();
}

void MainWindow::showFindReplace() {
    auto* editor=currentEditor();if(!editor||!m_findDialog)return;
    m_findDialog->showFind(editor,currentDocument()?QFileInfo(currentDocument()->path()).fileName():QString(),true);
}

QString MainWindow::referencePath(const QString& name) const {
    QStringList roots;
#ifdef PENTA_DISTRIBUTION_BUILD
    roots << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/pentarnion-studio"))
          << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(".."));
#else
#ifdef PENTA_SOURCE_DIR
    roots << QString::fromUtf8(PENTA_SOURCE_DIR);
#endif
    roots << QCoreApplication::applicationDirPath()
          << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../../.."))
          << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/pentarnion"));
#endif
    for (const auto& root : roots) {
        const QString candidate = QDir(root).filePath(name);
        if (QFileInfo::exists(candidate)) return candidate;
    }
    return {};
}

void MainWindow::showLanguageReference() {
    if(m_assistant)m_assistant->showHelp(QStringLiteral("language-reference"));
}

void MainWindow::showAbout() {
    if(auto* existing=findChild<QDialog*>(QStringLiteral("aboutStudio"))){existing->show();existing->raise();return;}
    desktop::AboutOptions options;options.version=QStringLiteral("0.2");
    auto* dialog=new desktop::AboutDialog(this,options);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();
}

void MainWindow::readSettings() {
    if (!m_settings) return;
    m_fontSize = std::clamp(m_settings->value(QStringLiteral("editor/fontSize"), 11).toInt(), 7, 40);
    m_console->setHistory(m_settings->value(QStringLiteral("console/history")).toStringList());
    m_autosaveEnabled = m_settings->value(QStringLiteral("autosave/enabled"), true).toBool();
    m_autosaveInterval = std::clamp(m_settings->value(QStringLiteral("autosave/intervalMs"), 60000).toInt(), 1000, 3600000);
    restoreGeometry(m_settings->value(QStringLiteral("window/geometry")).toByteArray());
    // Versioned ownership boundary: never restore an old outer layout that
    // grouped Figures/Copilot and stretched Command beneath the right column.
    {
        // Saved visibility can differ from the controllers' initial auto-hide
        // preferences. Do not let their visibility/top-level handlers hide or
        // reparent docks while Qt is reconstructing its layout items. Saved
        // controller preferences are applied after this traversal completes.
        std::vector<std::unique_ptr<QSignalBlocker>> dockSignals;
        for(auto* dock:findChildren<QDockWidget*>())dockSignals.push_back(std::make_unique<QSignalBlocker>(dock));
        const auto outerState=m_settings->value(QStringLiteral("window/outerLayoutV2")).toByteArray();
        if(m_workspaceLayout)m_workspaceLayout->restoreOuterState(outerState,2);
        else restoreState(outerState,2);
        if(m_workspaceLayout)m_workspaceLayout->restoreWorkingState(m_settings->value(QStringLiteral("workspace/layoutV2")).toByteArray());
        // Old saved layouts must not resurrect separate calculation windows.
        for (const auto& name : {"resultsDock", "problemsDock"}) {
            if (auto* dock = findChild<QDockWidget*>(QString::fromLatin1(name))) {
                dock->setFloating(false);
                if (dockHost(dock)->dockWidgetArea(dock) != Qt::BottomDockWidgetArea) dockHost(dock)->addDockWidget(Qt::BottomDockWidgetArea, dock);
            }
        }
    }
    m_actions.value(QStringLiteral("viewRestoreSession"))->setChecked(
        m_settings->value(QStringLiteral("session/restore"), false).toBool());
    if (m_settings->status() != QSettings::NoError)
        statusBar()->showMessage(tr("Some saved application settings could not be read; project files were left untouched."), 15000);
    rebuildRecentMenu();
}

void MainWindow::writeSettings() {
    if(m_desktop)m_desktop->savePreferences();
    if (!m_settings) return;
    m_settings->setValue(QStringLiteral("editor/fontSize"), m_fontSize);
    m_settings->setValue(QStringLiteral("console/history"), m_console->history());
    m_settings->setValue(QStringLiteral("autosave/enabled"), m_autosaveEnabled);
    m_settings->setValue(QStringLiteral("autosave/intervalMs"), m_autosaveInterval);
    m_settings->setValue(QStringLiteral("window/geometry"), saveGeometry());
    m_settings->setValue(QStringLiteral("window/outerLayoutV2"), saveState(2));
    if(m_workspaceLayout)m_settings->setValue(QStringLiteral("workspace/layoutV2"),m_workspaceLayout->saveWorkingState());
    m_settings->setValue(QStringLiteral("session/restore"), m_actions.value(QStringLiteral("viewRestoreSession"))->isChecked());
    m_settings->setValue(QStringLiteral("session/project"), m_project ? m_project->manifestPath : QString());
    QVariantList tabs;
    for (const auto& tab : m_documents) {
        if (tab.document->path().isEmpty()) continue;
        QVariantMap item;
        item.insert(QStringLiteral("path"), tab.document->path());
        item.insert(QStringLiteral("cursor"), tab.editor->textCursor().position());
        tabs.append(item);
    }
    m_settings->setValue(QStringLiteral("session/tabs"), tabs);
    m_settings->setValue(QStringLiteral("session/current"), m_tabs->currentIndex());
    m_settings->sync();
}

void MainWindow::rememberProject(const QString& manifest) {
    if (!m_settings) return;
    QStringList recent = m_settings->value(QStringLiteral("recent/projects")).toStringList();
    recent.removeAll(manifest); recent.prepend(manifest);
    while (recent.size() > 10) recent.removeLast();
    m_settings->setValue(QStringLiteral("recent/projects"), recent); rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu() {
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    const auto paths = m_settings ? m_settings->value(QStringLiteral("recent/projects")).toStringList() : QStringList();
    for (const auto& path : paths) {
        auto* entry = m_recentMenu->addAction(QDir::toNativeSeparators(path));
        entry->setToolTip(path);
        connect(entry, &QAction::triggered, this, [this, path] {
            QString error;
            if (!openProjectPath(path, &error) && !error.contains(QStringLiteral("cancelled"), Qt::CaseInsensitive))
                showError(tr("Open recent project"), error);
        });
    }
    if (paths.isEmpty()) { auto* empty = m_recentMenu->addAction(tr("No recent projects")); empty->setEnabled(false); }
}

void MainWindow::restoreStartupState(bool restoreSession) {
    if (!m_settings) return;
    if (restoreSession && !m_project && m_actions.value(QStringLiteral("viewRestoreSession"))->isChecked()) {
        QString error;
        const QString manifest = m_settings->value(QStringLiteral("session/project")).toString();
        if (!manifest.isEmpty() && !openProjectPath(manifest, &error))
            statusBar()->showMessage(tr("Previous project could not be restored: %1").arg(error), 12000);
        const auto tabs = m_settings->value(QStringLiteral("session/tabs")).toList();
        for (const auto& entry : tabs) {
            const auto values = entry.toMap();
            if (openSourcePath(values.value(QStringLiteral("path")).toString(), &error)) {
                auto* editor = currentEditor(); QTextCursor cursor = editor->textCursor();
                cursor.setPosition(std::clamp(values.value(QStringLiteral("cursor")).toInt(), 0,
                    std::max(0, editor->document()->characterCount() - 1)));
                editor->setTextCursor(cursor);
            }
        }
        const int current = m_settings->value(QStringLiteral("session/current"), 0).toInt();
        if (current >= 0 && current < m_tabs->count()) m_tabs->setCurrentIndex(current);
    }
    if(restoreSession&&!m_project&&!m_settings->contains(QStringLiteral("session/project"))){
        const auto initial=qEnvironmentVariable("PENTA_DEFAULT_PROJECT");
        if(!initial.isEmpty()&&QFileInfo(initial).isFile()){
            QString error;
            if(!openProjectPath(initial,&error))statusBar()->showMessage(tr("Initial example could not be opened: %1").arg(error),12000);
        }
    }
    QStringList warnings;
    const auto records = m_recovery.records(&warnings);
    if (!warnings.isEmpty()) QMessageBox::warning(this, tr("Recovery record warning"), warnings.join('\n'));
    for (const auto& record : records) {
        QMessageBox question(QMessageBox::Question, tr("Recover unsaved source?"),
            tr("Unsaved edits were found for:\n%1\n\nRestoring opens an edited buffer and leaves the disk file untouched.")
                .arg(record.path.isEmpty() ? tr("Unnamed source") : record.path), QMessageBox::NoButton, this);
        auto* restore = question.addButton(tr("Restore Buffer"), QMessageBox::AcceptRole);
        auto* later = question.addButton(tr("Keep for Later"), QMessageBox::RejectRole);
        auto* discard = question.addButton(tr("Discard Snapshot"), QMessageBox::DestructiveRole);
        question.setDefaultButton(restore); question.exec();
        if (question.clickedButton() == later || !question.clickedButton()) continue;
        QString error;
        if (question.clickedButton() == discard) {
            if (!m_recovery.remove(record.id, &error)) showError(tr("Discard recovery"), error);
            continue;
        }
        const int existing = indexOfPath(record.path);
        if (existing >= 0 && !m_documents[existing].document->isModified()) {
            auto& tab = m_documents[existing];
            if (!tab.document->restoreUnsaved(record.path, record.text, record.bom, record.newline, &error)) {
                showError(tr("Restore recovery"), error); continue;
            }
            tab.recoveryId = record.id;
            m_tabs->setCurrentIndex(existing);
            QTextCursor cursor = tab.editor->textCursor();
            cursor.setPosition(std::clamp(record.cursorPosition, 0,
                std::max(0, tab.editor->document()->characterCount() - 1)));
            tab.editor->setTextCursor(cursor);
            updateUi();
            if (!record.path.isEmpty() && tab.document->path().isEmpty())
                statusBar()->showMessage(tr("Recovered text is in an unnamed buffer because the original could not be safely opened. Use Save As to save it."), 20000);
            continue;
        }
        auto* document = new Document;
        const QString recoveryPath = existing >= 0 ? QString() : record.path;
        if (!document->restoreUnsaved(recoveryPath, record.text, record.bom, record.newline, &error)) {
            delete document; showError(tr("Restore recovery"), error); continue;
        }
        attachDocument(document, record.id, record.cursorPosition);
        if (!record.path.isEmpty() && document->path().isEmpty()) {
            m_tabs->setTabToolTip(m_tabs->currentIndex(), tr("Recovered from: %1\nOriginal left untouched; use Save As.").arg(record.path));
            statusBar()->showMessage(tr("Recovered text is in an unnamed buffer because the original could not be safely opened. Use Save As to save it."), 20000);
        }
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!prepareCloseAll()||!prepareCloseWorkbooks()) { event->ignore(); return; }
    writeSettings();
    m_closing = true;
    if (m_execution) m_execution->cancel();
    disposeAllTabs(true);
    event->accept();
}

} // namespace penta
