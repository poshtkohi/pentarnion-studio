#include "main_window.hpp"
#include "document.hpp"
#include "command_window.hpp"
#include "figure_manager.hpp"
#include "editor_widget.hpp"
#include "find_replace_bar.hpp"
#include "assistant/assistant_controller.hpp"
#include "assistant/help_search.hpp"
#include "desktop/desktop_shell.hpp"
#include "desktop/collapsible_dock.hpp"
#include "desktop/studio_workspace_layout.hpp"
#include "desktop/figure_docking.hpp"
#include "desktop/snapshot_controller.hpp"
#include "desktop/parallel_pool_indicator.hpp"
#include "desktop/worker_activity.hpp"
#include "runtime/thread_budget.hpp"
#include "desktop/studio_icons.hpp"
#include "desktop/project_file_icons.hpp"
#include "workbook/workbook_document.hpp"
#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QTimer>
#include <QTabBar>
#include <algorithm>

namespace penta {
void MainWindow::initializeDesktop(){
    auto* bar=menuBar();auto* view=findChild<QMenu*>(QStringLiteral("studioViewMenu"));
    auto* help=findChild<QMenu*>(QStringLiteral("studioHelpMenu"));
    auto* data=bar->addMenu(tr("&Data"));data->setObjectName(QStringLiteral("studioDataMenu"));
    auto* open=action(QStringLiteral("dataOpenWorkbook"),tr("Open workbook or data…"),QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    open->setIcon(desktop::projectKindIcon(desktop::ProjectIconKind::Spreadsheet));data->addAction(open);
    connect(open,&QAction::triggered,this,&MainWindow::requestOpenWorkbook);
    auto* create=action(QStringLiteral("dataNewWorkbook"),tr("New workbook…"));data->addAction(create);
    create->setIcon(desktop::projectKindIcon(desktop::ProjectIconKind::Spreadsheet));connect(create,&QAction::triggered,this,&MainWindow::requestNewWorkbook);
    const auto reference=referencePath(QStringLiteral("language.txt"));
    m_assistant=new assistant::AssistantController(this,[this]{return assistantSnapshot();},
        [this](const assistant::SourceSnapshot& expected,const QString& candidate,QString* error){return applyAssistantSource(expected,candidate,error);},
        reference.isEmpty()?QString():QFileInfo(reference).absolutePath(),m_persistenceEnabled);
    QList<QDockWidget*> panes;
    for(auto* dock:findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly))if(dock!=m_assistant->dock())panes.push_back(dock);
    m_workspaceLayout=new desktop::StudioWorkspaceLayout(this,m_workspaceTabs,panes,m_assistant->dock());
    m_snapshots=new desktop::SnapshotController(this);
    m_snapshots->feedback=[this](const QString& message,bool){statusBar()->showMessage(message,8000);};
    if(auto* edit=findChild<QMenu*>(QStringLiteral("studioEditMenu"))){edit->addSeparator();edit->addAction(m_snapshots->regionAction());edit->addAction(m_snapshots->windowAction());}
    if(auto* toolbar=findChild<QToolBar*>(QStringLiteral("calculationToolbar")))toolbar->addAction(m_snapshots->regionAction());
    auto* website=new QLabel("<a href=\"https://pentarnion.com\" style=\"color:#245c72;\">pentarnion.com</a>",statusBar());
    website->setObjectName("studioWebsiteLink");website->setToolTip("Pentarnion website");website->setAccessibleName("Pentarnion website");
    website->setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::LinksAccessibleByKeyboard);website->setOpenExternalLinks(true);website->setStyleSheet("QLabel { padding:0 6px; }");
    statusBar()->addPermanentWidget(website);
    m_workerIndicator=new desktop::ParallelPoolIndicator(this);statusBar()->addPermanentWidget(m_workerIndicator);
    m_workerMonitor=std::make_unique<desktop::WorkerActivityMonitor>();
    m_workerStatusTimer=new QTimer(this);m_workerStatusTimer->setInterval(120);m_workerStatusTimer->setTimerType(Qt::CoarseTimer);
    connect(m_workerStatusTimer,&QTimer::timeout,this,&MainWindow::refreshWorkerActivity);
    if(view)view->addAction(m_assistant->toggleAction());
    auto* context=action(QStringLiteral("helpContext"),tr("Help for selection"),QKeySequence(Qt::Key_F1));
    connect(context,&QAction::triggered,this,[this]{
        QString term;if(auto* editor=currentEditor()){auto cursor=editor->textCursor();if(!cursor.hasSelection())cursor.select(QTextCursor::WordUnderCursor);term=cursor.selectedText();}
        m_assistant->contextHelp(term);
    });if(help)help->insertAction(help->actions().isEmpty()?nullptr:help->actions().front(),context);
    auto* findHelp=action(QStringLiteral("helpSearch"),tr("Search Help…"),QKeySequence(QStringLiteral("Ctrl+Shift+Space")));
    connect(findHelp,&QAction::triggered,this,[this]{for(auto* box:findChildren<assistant::HelpSearchBox*>())if(box->isVisible()){box->setFocus(Qt::ShortcutFocusReason);box->selectAll();return;}m_assistant->showHelp();});
    if(help)help->addAction(findHelp);
    for(auto it=m_actions.begin();it!=m_actions.end();++it)if(it.value()->icon().isNull())it.value()->setIcon(desktop::studioCommandIcon(it.value()->text().remove('&'),it.key()));
    m_desktop=new desktop::DesktopShell(this,m_settings);m_desktop->attachViewMenu(view);
    if(auto* toolbar=findChild<QToolBar*>(QStringLiteral("calculationToolbar")))m_desktop->registerClassicToolbar(toolbar);
    const auto add=[this](const QString& page,const QString& group,const char* name,bool prominent=false){if(auto* item=m_actions.value(QString::fromLatin1(name)))m_desktop->addCommand(page,group,item,prominent);};
    for(const char* name:{"fileNewProject","fileOpenProject","fileNewSource","fileOpenSource"})add(tr("Home"),tr("Project"),name,true);
    for(const char* name:{"fileSave","fileSaveAs","fileSaveAll","fileCloseTab","fileCloseAllDocuments","filePrint","filePrintPreview"})add(tr("Home"),tr("Documents"),name);
    m_desktop->addCommand(tr("Home"),tr("Snapshot"),m_snapshots->regionAction(),true);
    m_desktop->addCommand(tr("Home"),tr("Snapshot"),m_snapshots->windowAction());
    for(const char* name:{"editUndo","editRedo","editCut","editCopy","editPaste","editSelectAll","editFindReplace"})add(tr("Home"),tr("Edit"),name);
    for(const char* name:{"checkFile","checkProject","runFile","projectRunSource","stopExecution"})add(tr("Compute"),tr("Run"),name,true);
    for(const char* name:{"clearWorkspace","clearCommandWindow"})add(tr("Compute"),tr("Workspace"),name);
    for(const char* name:{"dataNewWorkbook","dataOpenWorkbook"})add(tr("Data"),tr("Workbooks"),name,true);
    for(const char* name:{"fileSave","fileSaveAs","editUndo","editRedo","editCopy","editPaste"})add(tr("Data"),tr("Edit worksheet"),name);
    if(auto* toolbar=findChild<QToolBar*>(QStringLiteral("figureToolbar")))for(auto* command:toolbar->actions())if(!command->isSeparator()&&!command->text().isEmpty())m_desktop->addCommand(tr("Figures"),tr("Figure controls"),command);
    auto controlled=panes;controlled.push_back(m_assistant->dock());
    for(auto* dock:controlled){
        const bool hidden=dock->isHidden();
        QString key=dock->objectName();if(key==QStringLiteral("projectDock"))key=QStringLiteral("project");else if(key==QStringLiteral("resultsDock"))key=QStringLiteral("command");else if(dock==m_assistant->dock())key=QStringLiteral("copilot");
        // Working docks belong to the inner host. The outer Copilot column has
        // no Command/Figures peers, so its pinned/drawer widths stay independent.
        auto* controller=m_desktop->registerDock(dock,key,false,dock==m_assistant->dock(),dockHost(dock));
        controller->setUndockingEnabled(dock->objectName()==QStringLiteral("figuresDock")||dock==m_assistant->dock());
        if(hidden)controller->setPanelVisible(false);
        m_desktop->addCommand(tr("View"),tr("Panels"),dock->toggleViewAction());
    }
    for(const char* name:{"helpContext","helpSearch","helpLanguageReference","helpFeatureRules","helpAbout"})add(tr("Help"),tr("Documentation"),name);
    m_desktop->addCommand(tr("Help"),tr("Copilot"),m_assistant->toggleAction(),true);
    m_desktop->setClassicHeaderAccessory(m_assistant->createHelpSearch());
    m_desktop->setRibbonHeaderAccessory(m_assistant->createHelpSearch());
    if(auto* figures=findChild<QDockWidget*>(QStringLiteral("figuresDock"))){
        m_figureFrame=new desktop::FigureDockController(m_figures,tr("Figures"),QStringLiteral("figurePaneDockToggle"));
        m_figureFrame->setDockContainer(figures);
    }
    connect(m_desktop->defaultLayoutAction(),&QAction::triggered,this,[this]{if(m_workspaceLayout)m_workspaceLayout->resetWorkingLayout();});
    m_workspaceLayout->captureDefaultLayout();
    m_desktop->captureDefaultLayout();
    for(auto* tabs:{m_tabs,m_workspaceTabs}){
        tabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tabs->tabBar(),&QWidget::customContextMenuRequested,this,[this,tabs](const QPoint& point){
            const int at=tabs->tabBar()->tabAt(point);if(at>=0)tabs->setCurrentIndex(at);
            QMenu menu(this);menu.addAction(m_actions.value(QStringLiteral("fileCloseTab")));menu.addAction(m_actions.value(QStringLiteral("fileCloseAllDocuments")));menu.exec(tabs->tabBar()->mapToGlobal(point));
        });
    }
}

QMainWindow* MainWindow::dockHost(QDockWidget* dock)const{return m_workspaceLayout?m_workspaceLayout->hostForDock(dock):const_cast<MainWindow*>(this);}
void MainWindow::refreshWorkerActivity(){
    if(!m_workerIndicator||!m_workerStatusTimer)return;
    if(!m_workPending){m_workerStatusTimer->stop();m_workerMonitor->setRunning(false);if(m_workerRequest)m_workerIndicator->finishRequest(m_workerRequest);m_workerRequest=0;return;}
    const bool execution=m_latestKind==WorkKind::RunFile||m_latestKind==WorkKind::RunCommand;
    m_workerMonitor->setRunning(execution);
    if(m_workerRequest!=m_latestRequest){
        m_workerRequest=m_latestRequest;m_peakWorkers=0;m_workerSequence=runtime::ThreadBudget::instance().activity().sequence;
        m_workerIndicator->beginRequest(m_workerRequest,execution?tr("Scientific execution"):tr("Checking source"),execution);
    }
    if(execution){
        const auto observed=m_workerMonitor->snapshot();
        const auto count=std::max<std::size_t>(1,observed.workers());
        m_peakWorkers=std::max(m_peakWorkers,count);
        if(observed.poolSequence!=m_workerSequence)m_peakWorkers=std::max(m_peakWorkers,observed.poolPeak);
        QString phase=tr("Scientific execution · observed peak %1").arg(m_peakWorkers);
        if(observed.blasOwners)phase+=tr("\nBLAS team reservation: %1 per owner; %2 owner(s)").arg(observed.blasTeamWidth).arg(observed.blasOwners);
        if(observed.reservedWorkers()>count)m_workerIndicator->setWorkerReservation(m_workerRequest,observed.reservedWorkers(),phase);
        else m_workerIndicator->setWorkers(m_workerRequest,count,phase);
        if(!m_workerStatusTimer->isActive())m_workerStatusTimer->start();
    }else m_workerStatusTimer->stop();
    if(m_console&&m_console->isStopping())m_workerIndicator->setStopping(m_workerRequest);
}

bool MainWindow::closeAllDocuments(){
    if(!prepareCloseAll()||!prepareCloseWorkbooks())return false;
    disposeAllTabs(true);
    for(int i=m_workspaceTabs->count()-1;i>=0;--i)if(auto* book=qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i))){m_workspaceTabs->removeTab(i);delete book;}
    showSourceWorkspace();updateUi();return true;
}

assistant::SourceSnapshot MainWindow::assistantSnapshot()const{
    assistant::SourceSnapshot result;auto* document=currentDocument();auto* editor=currentEditor();if(!document||!editor)return result;
    result.text=document->text();result.path=document->path();result.projectRoot=m_project?m_project->rootPath:QString();
    const auto cursor=editor->textCursor();result.selection=cursor.selectedText().replace(QChar::ParagraphSeparator,QLatin1Char('\n'));
    result.selectionStart=cursor.selectionStart();result.selectionEnd=cursor.selectionEnd();
    const int index=indexOf(document);if(index<0)return {};
    const auto context=snapshotForDocument(index);result.algebraId=context.algebraId;result.gamma=context.gamma;result.languageVersion=context.languageVersion;
    QByteArray identity=QByteArray::number(m_documents[index].id)+':'+QByteArray::number(document->revision())+':'+result.path.toUtf8()+':'+result.projectRoot.toUtf8()+':'+result.algebraId.toUtf8()+':'+QByteArray::number(result.gamma,'g',17)+':'+result.languageVersion.toUtf8()+':'+result.text.toUtf8();
    result.identity=QCryptographicHash::hash(identity,QCryptographicHash::Sha256);return result;
}
bool MainWindow::applyAssistantSource(const assistant::SourceSnapshot& expected,const QString& candidate,QString* error){
    if(m_workPending){if(error)*error=tr("Wait for the current calculation before applying a code proposal.");return false;}
    if(!assistant::sourceSnapshotMatches(expected,assistantSnapshot())){if(error)*error=tr("The script, selection or project changed. Review a new proposal for the current source.");return false;}
    auto* editor=currentEditor();auto* document=currentDocument();if(!editor||!document)return false;
    QTextCursor cursor(editor->document());cursor.beginEditBlock();cursor.select(QTextCursor::Document);cursor.insertText(candidate);cursor.endEditBlock();
    if(!document->path().isEmpty()&&!saveCurrentDocument(error)){editor->undo();return false;}
    if(m_assistant)m_assistant->invalidateSource();
    return true;
}

void MainWindow::showSourceWorkspace(){if(m_workspaceTabs&&m_sourceSurface)m_workspaceTabs->setCurrentWidget(m_sourceSurface);}
workbook::WorkbookDocument* MainWindow::currentWorkbook()const{return m_workspaceTabs?qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->currentWidget()):nullptr;}
bool MainWindow::openWorkbook(const QString& path,QString* error){
    if(!workbook::WorkbookDocument::supportsPath(path)){if(error)*error=tr("Choose an XLSX, CSV or TSV file.");return false;}
    const QFileInfo file(path);const QString absolute=file.canonicalFilePath().isEmpty()?file.absoluteFilePath():file.canonicalFilePath();int count=0;
    for(int i=0;i<m_workspaceTabs->count();++i)if(auto* book=qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i))){++count;if(QFileInfo(book->path()).absoluteFilePath()==absolute){m_workspaceTabs->setCurrentWidget(book);return true;}}
    if(count>=8){if(error)*error=tr("Close a workbook before opening another; at most eight may be open.");return false;}
    auto* book=new workbook::WorkbookDocument(m_workspaceTabs);if(!book->openFile(absolute,error)){delete book;return false;}
    const int index=m_workspaceTabs->addTab(book,desktop::projectKindIcon(desktop::ProjectIconKind::Spreadsheet),file.fileName());m_workspaceTabs->setTabToolTip(index,absolute);
    const auto changed=[this,book]{const int at=m_workspaceTabs->indexOf(book);if(at>=0){m_workspaceTabs->setTabText(at,(book->path().isEmpty()?tr("Untitled workbook"):QFileInfo(book->path()).fileName())+(book->isModified()?QStringLiteral(" *"):QString()));m_workspaceTabs->setTabToolTip(at,book->path());}updateUi();};
    connect(book,&workbook::WorkbookDocument::undoStateChanged,this,changed);
    connect(book,&workbook::WorkbookDocument::modifiedChanged,this,changed);connect(book,&workbook::WorkbookDocument::pathChanged,this,changed);connect(book,&workbook::WorkbookDocument::loadingFinished,this,changed);
    connect(book,&workbook::WorkbookDocument::error,this,[this](const QString& message){statusBar()->showMessage(message,15000);});
    m_workspaceTabs->setCurrentWidget(book);updateUi();return true;
}
void MainWindow::requestOpenWorkbook(){const auto path=QFileDialog::getOpenFileName(this,tr("Open workbook or scientific data"),m_project?m_project->rootPath:QString(),tr("Workbooks and data (*.xlsx *.csv *.tsv)"));if(path.isEmpty())return;QString error;if(!openWorkbook(path,&error))showError(tr("Open workbook"),error);}
void MainWindow::requestNewWorkbook(){
    int count=0;for(int i=0;i<m_workspaceTabs->count();++i)if(qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i)))++count;
    if(count>=8){showError(tr("New workbook"),tr("At most eight workbooks may be open."));return;}
    auto* book=new workbook::WorkbookDocument(m_workspaceTabs);book->newWorkbook();m_workspaceTabs->addTab(book,tr("Untitled workbook"));
    const auto changed=[this,book]{const int at=m_workspaceTabs->indexOf(book);if(at>=0)m_workspaceTabs->setTabText(at,(book->path().isEmpty()?tr("Untitled workbook"):QFileInfo(book->path()).fileName())+(book->isModified()?QStringLiteral(" *"):QString()));updateUi();};
    connect(book,&workbook::WorkbookDocument::undoStateChanged,this,changed);
    connect(book,&workbook::WorkbookDocument::modifiedChanged,this,changed);connect(book,&workbook::WorkbookDocument::pathChanged,this,changed);connect(book,&workbook::WorkbookDocument::loadingFinished,this,changed);
    connect(book,&workbook::WorkbookDocument::error,this,[this](const QString& message){statusBar()->showMessage(message,15000);});m_workspaceTabs->setCurrentWidget(book);updateUi();
}
bool MainWindow::saveWorkbookInteractive(workbook::WorkbookDocument* book,bool saveAs){
    if(!book||book->isLoading())return false;
    QString error;bool saved=false;
    if(saveAs||book->path().isEmpty()){
        const auto path=QFileDialog::getSaveFileName(this,tr("Save workbook"),book->path().isEmpty()?(m_project?QDir(m_project->rootPath).filePath(QStringLiteral("data.xlsx")):QStringLiteral("data.xlsx")):book->path(),tr("Excel workbook (*.xlsx);;CSV data (*.csv);;Tab-separated data (*.tsv)"));
        if(path.isEmpty())return false;
        saved=book->saveAs(path,&error,QFileInfo::exists(path));
    }else saved=book->save(&error);
    if(!saved)showError(tr("Save workbook"),error);else refreshTree();return saved;
}
bool MainWindow::closeWorkspacePage(int index){
    auto* page=m_workspaceTabs->widget(index);if(page==m_sourceSurface)return closeTab(m_tabs->currentIndex());
    auto* book=qobject_cast<workbook::WorkbookDocument*>(page);if(!book)return false;
    if(book->isModified()){
        const auto choice=QMessageBox::question(this,tr("Save workbook changes?"),tr("Save edits to %1?").arg(book->path().isEmpty()?tr("Untitled workbook"):QFileInfo(book->path()).fileName()),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(choice==QMessageBox::Cancel||(choice==QMessageBox::Save&&!saveWorkbookInteractive(book)))return false;
    }
    m_workspaceTabs->removeTab(index);delete book;updateUi();return true;
}
bool MainWindow::prepareCloseWorkbooks(){
    for(int i=0;i<m_workspaceTabs->count();++i)if(auto* book=qobject_cast<workbook::WorkbookDocument*>(m_workspaceTabs->widget(i)))if(book->isModified()){
        m_workspaceTabs->setCurrentWidget(book);const auto choice=QMessageBox::question(this,tr("Save workbook changes?"),tr("Save edits to %1 before closing Studio?").arg(book->path().isEmpty()?tr("Untitled workbook"):QFileInfo(book->path()).fileName()),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
        if(choice==QMessageBox::Cancel||(choice==QMessageBox::Save&&!saveWorkbookInteractive(book)))return false;
    }
    return true;
}
}
