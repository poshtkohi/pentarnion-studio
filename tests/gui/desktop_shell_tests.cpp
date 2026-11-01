#include "offscreen_test_bootstrap.hpp"
#include "ide/desktop/desktop_shell.hpp"
#include "ide/desktop/toolstrip.hpp"
#include "ide/desktop/collapsible_dock.hpp"
#include "ide/desktop/pane_hover.hpp"
#include "ide/desktop/project_file_icons.hpp"
#include "ide/desktop/about_dialog.hpp"
#include "ide/desktop/figure_group_host.hpp"
#include "ide/desktop/figure_docking.hpp"
#include "ide/figure_manager.hpp"
#include "ide/main_window.hpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>

using namespace penta::desktop;

class DesktopShellTests final:public QObject {
    Q_OBJECT
private slots:
    void modesShareActionsAndKeepSource();
    void preferencesRestorePresentationWithoutExecutingCommands();
    void preferencesRestoreCollapsedAndHiddenDockControllers();
    void defaultLayoutRetainsDocumentsAndInterface();
    void paneStylingPreservesContentPalette();
    void compactAboutPreservesAttributionAndLocalNotices();
    void semanticFileIconsUsePentarnionTypes();
    void figureGroupsRetainViewsAcrossSplitsTabsAndFloats();
    void figureGroupLayoutRestorePreservesViews();
    void queuedCloseDoesNotCloseAReplacementFigure();
    void normalFigureArrangementsRetainFloatingFrames();
    void independentFigureControllerReturnsTheOriginalView();
    void websiteLinkImmediatelyPrecedesParallelWorkerIndicator();
};

void DesktopShellTests::modesShareActionsAndKeepSource() {
    QMainWindow window;auto* source=new QPlainTextEdit(&window);
    source->setPlainText(QStringLiteral("pentarnion p = 1 + i;"));window.setCentralWidget(source);
    auto* menu=window.menuBar()->addMenu(QStringLiteral("File"));
    auto* run=menu->addAction(QStringLiteral("Run File"));run->setShortcut(Qt::Key_F5);
    auto* tools=window.addToolBar(QStringLiteral("Run"));tools->setObjectName(QStringLiteral("runToolbar"));tools->addAction(run);
    int runs=0;QObject::connect(run,&QAction::triggered,&window,[&]{++runs;});
    QPointer<QPlainTextEdit> retained=source;QPointer<QMenuBar> originalMenu=window.menuBar();
    auto* shell=new DesktopShell(&window);shell->registerClassicToolbar(tools);
    auto* button=shell->addCommand(QStringLiteral("Home"),QStringLiteral("Execution"),run,true);
    QVERIFY(button);QCOMPARE(button->defaultAction(),run);shell->restorePreferences();
    window.resize(900,600);window.show();QCoreApplication::processEvents();
    shell->setRibbonMode(true);QVERIFY(shell->ribbonMode());QVERIFY(originalMenu->isHidden());QVERIFY(tools->isHidden());
    button->click();QCOMPARE(runs,1);QCOMPARE(window.actions().count(run),1);
    run->setEnabled(false);QVERIFY(!button->isEnabled());button->click();QCOMPARE(runs,1);
    run->setEnabled(true);shell->setRibbonMode(false);QVERIFY(!shell->ribbonMode());QVERIFY(!originalMenu->isHidden());QVERIFY(!tools->isHidden());
    QCOMPARE(originalMenu.data(),shell->classicMenuBar());QCOMPARE(retained.data(),source);
    QCOMPARE(source->toPlainText(),QStringLiteral("pentarnion p = 1 + i;"));
    QCOMPARE(run->shortcut(),QKeySequence(Qt::Key_F5));
    tools->hide();shell->setRibbonMode(true);shell->setRibbonMode(false);QVERIFY(tools->isHidden());
}

void DesktopShellTests::preferencesRestorePresentationWithoutExecutingCommands() {
    QTemporaryDir directory;QVERIFY(directory.isValid());
    QSettings preferences(directory.filePath(QStringLiteral("desktop.ini")),QSettings::IniFormat);
    {
        QMainWindow window;auto* view=window.menuBar()->addMenu(QStringLiteral("View"));
        auto* shell=new DesktopShell(&window,&preferences);shell->attachViewMenu(view);
        shell->addPage(QStringLiteral("Home"));shell->addPage(QStringLiteral("Figures"));shell->restorePreferences();
        shell->setRibbonMode(true);shell->toolstrip()->setCurrentPage(1);
        shell->toolstrip()->setDisplayMode(Toolstrip::RibbonDisplay::TabsOnly);shell->savePreferences();
    }
    preferences.sync();QCOMPARE(preferences.status(),QSettings::NoError);
    QMainWindow restored;auto* shell=new DesktopShell(&restored,&preferences);
    auto* run=new QAction(QStringLiteral("Run File"),&restored);int runs=0;
    QObject::connect(run,&QAction::triggered,&restored,[&]{++runs;});
    shell->addCommand(QStringLiteral("Home"),QStringLiteral("Execution"),run);
    shell->addPage(QStringLiteral("Figures"));shell->restorePreferences();
    QVERIFY(shell->ribbonMode());QCOMPARE(shell->toolstrip()->currentPage(),1);
    QCOMPARE(shell->toolstrip()->displayMode(),Toolstrip::RibbonDisplay::TabsOnly);QCOMPARE(runs,0);
    shell->setRibbonMode(false);QCOMPARE(preferences.value(QStringLiteral("interface/mode")).toString(),QStringLiteral("classic"));
    shell->toolstrip()->setDisplayMode(Toolstrip::RibbonDisplay::AutoHide);
    QCOMPARE(shell->toolstrip()->displayMode(),Toolstrip::RibbonDisplay::TabsOnly);
}

void DesktopShellTests::preferencesRestoreCollapsedAndHiddenDockControllers() {
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen"))QSKIP("Dock persistence regression runs offscreen only.");
    QTemporaryDir directory;QVERIFY(directory.isValid());
    QSettings preferences(directory.filePath(QStringLiteral("docks.ini")),QSettings::IniFormat);
    QByteArray nativeState;
    {
        QMainWindow window;window.setCentralWidget(new QLabel(QStringLiteral("source"),&window));
        auto* project=new QDockWidget(QStringLiteral("Project"),&window);project->setObjectName(QStringLiteral("projectDock"));
        project->setWidget(new QLabel(QStringLiteral("retained project"),project));window.addDockWidget(Qt::LeftDockWidgetArea,project);
        auto* command=new QDockWidget(QStringLiteral("Command Window"),&window);command->setObjectName(QStringLiteral("commandDock"));
        command->setWidget(new QLabel(QStringLiteral("retained output"),command));window.addDockWidget(Qt::BottomDockWidgetArea,command);
        auto* shell=new DesktopShell(&window,&preferences);shell->addPage(QStringLiteral("Home"));
        auto* projectController=shell->registerDock(project,QStringLiteral("project"),false,false);
        auto* commandController=shell->registerDock(command,QStringLiteral("command"),false,false);
        shell->restorePreferences();window.resize(600,400);window.show();QCoreApplication::processEvents();
        projectController->collapse();commandController->setPanelVisible(false);
        nativeState=window.saveState();shell->savePreferences();
        QVERIFY(preferences.contains(QStringLiteral("interface/docks/projectDockController")));
        QVERIFY(preferences.contains(QStringLiteral("interface/docks/commandDockController")));
    }
    QMainWindow restored;restored.setCentralWidget(new QLabel(QStringLiteral("new source"),&restored));
    auto* project=new QDockWidget(QStringLiteral("Project"),&restored);project->setObjectName(QStringLiteral("projectDock"));
    auto* contents=new QLabel(QStringLiteral("retained project"),project);project->setWidget(contents);restored.addDockWidget(Qt::LeftDockWidgetArea,project);
    auto* command=new QDockWidget(QStringLiteral("Command Window"),&restored);command->setObjectName(QStringLiteral("commandDock"));
    command->setWidget(new QLabel(QStringLiteral("retained output"),command));restored.addDockWidget(Qt::BottomDockWidgetArea,command);
    auto* shell=new DesktopShell(&restored,&preferences);shell->addPage(QStringLiteral("Home"));
    auto* projectController=shell->registerDock(project,QStringLiteral("project"),false,false);
    auto* commandController=shell->registerDock(command,QStringLiteral("command"),false,false);
    QVERIFY(restored.restoreState(nativeState));shell->restorePreferences();
    QVERIFY(projectController->isCollapsed());QVERIFY(!projectController->rail()->isHidden());QVERIFY(project->isHidden());
    QVERIFY(!commandController->panelVisible());QVERIFY(command->isHidden());QVERIFY(commandController->rail()->isHidden());
    auto* restore=projectController->rail()->findChild<QToolButton*>(QStringLiteral("projectDockRestore"));QVERIFY(restore);restore->click();
    QVERIFY(!projectController->isCollapsed());QVERIFY(!project->isHidden());QCOMPARE(project->widget(),contents);
    QCOMPARE(contents->text(),QStringLiteral("retained project"));QVERIFY(!commandController->panelVisible());
}

void DesktopShellTests::defaultLayoutRetainsDocumentsAndInterface() {
    QMainWindow window;auto* source=new QPlainTextEdit(&window);source->setPlainText(QStringLiteral("unsaved source"));window.setCentralWidget(source);
    auto* project=new QDockWidget(QStringLiteral("Project"),&window);project->setObjectName(QStringLiteral("projectDock"));
    auto* contents=new QLabel(QStringLiteral("Original project tree"),project);project->setWidget(contents);window.addDockWidget(Qt::LeftDockWidgetArea,project);
    auto* shell=new DesktopShell(&window);shell->addPage(QStringLiteral("Home"));
    auto* controller=shell->registerDock(project,QStringLiteral("project"),false,false);QVERIFY(controller);
    shell->restorePreferences();window.resize(900,600);window.show();QCoreApplication::processEvents();
    shell->captureDefaultLayout();QPointer<QWidget> retained=project->widget();controller->collapse();QVERIFY(controller->isCollapsed());
    shell->setRibbonMode(true);source->appendPlainText(QStringLiteral("new draft"));
    QVERIFY(shell->restoreDefaultLayout());QVERIFY(shell->ribbonMode());QVERIFY(!controller->isCollapsed());
    QCOMPARE(project->widget(),retained.data());QCOMPARE(source->toPlainText(),QStringLiteral("unsaved source\nnew draft"));
    QVERIFY(!project->isHidden());
}

void DesktopShellTests::paneStylingPreservesContentPalette() {
    QMainWindow window;auto* dock=new QDockWidget(QStringLiteral("Command Window"),&window);dock->setObjectName(QStringLiteral("resultsDock"));
    auto* output=new QPlainTextEdit(dock);auto palette=output->palette();palette.setColor(QPalette::Text,Qt::black);palette.setColor(QPalette::Base,Qt::white);output->setPalette(palette);
    output->setPlainText(QStringLiteral(">> display(p);"));dock->setWidget(output);window.addDockWidget(Qt::BottomDockWidgetArea,dock);
    installStudioPaneHover(&window);window.show();QCoreApplication::processEvents();
    QEvent entering(QEvent::Enter);QApplication::sendEvent(output,&entering);QTest::qWait(100);
    QCOMPARE(output->palette().color(QPalette::Text),QColor(Qt::black));
    QCOMPARE(output->palette().color(QPalette::Base),QColor(Qt::white));
    QCOMPARE(output->toPlainText(),QStringLiteral(">> display(p);"));
}

void DesktopShellTests::compactAboutPreservesAttributionAndLocalNotices() {
    QTemporaryDir directory;QVERIFY(directory.isValid());QFile notice(directory.filePath(QStringLiteral("LICENSE.txt")));
    QVERIFY(notice.open(QIODevice::WriteOnly));notice.write("Retained licence fixture.\n");notice.close();
    AboutOptions options;options.version=QStringLiteral("0.2-test");options.licenseRoots={directory.path()};
    AboutDialog dialog(nullptr,options);
    auto* tabs=dialog.findChild<QTabWidget*>(QStringLiteral("aboutTabs"));QVERIFY(tabs);QCOMPARE(tabs->count(),5);
    QCOMPARE(tabs->tabText(0),QStringLiteral("Authors"));
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor1Name"))->text(),QStringLiteral("Dr Alireza Poshtkohi"));
    QTextDocument firstEmail;firstEmail.setHtml(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor1Email"))->text());QCOMPARE(firstEmail.toPlainText(),QStringLiteral("a.poshtkohi@herts.ac.uk"));
    QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor2Name"))->text(),QString::fromUtf8("Dr Susanne Pumplün"));
    QTextDocument secondEmail;secondEmail.setHtml(dialog.findChild<QLabel*>(QStringLiteral("aboutAuthor2Email"))->text());QCOMPARE(secondEmail.toPlainText(),QStringLiteral("Susanne.Pumpluen@nottingham.ac.uk"));
    QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("aboutTagline"))->text().size()<80);
    auto* overview=dialog.findChild<QTreeWidget*>(QStringLiteral("aboutOverview"));QVERIFY(overview);
    for(int i=0;i<overview->topLevelItemCount();++i)QVERIFY(overview->topLevelItem(i)->text(1).size()<100);
    auto* text=dialog.findChild<QPlainTextEdit*>(QStringLiteral("aboutLicenseText"));QVERIFY(text);QVERIFY(text->isReadOnly());
    QCOMPARE(text->toPlainText(),QStringLiteral("Retained licence fixture.\n"));
    QCOMPARE(studioCopyrightText(2026),QString::fromUtf8("© 2026 Alireza Poshtkohi and Susanne Pumplün"));
    auto* qtNotice=dialog.findChild<QLabel*>(QStringLiteral("aboutQtNotice"));QVERIFY(qtNotice);
    bool inLicences=false;for(int index=0;index<tabs->count();++index)if(tabs->tabText(index)==QStringLiteral("Licences"))inLicences=tabs->widget(index)->isAncestorOf(qtNotice);
    QVERIFY(inLicences);
    QVERIFY(studioCopyrightText(2028).contains(QString::fromUtf8("2026–2028")));
    auto* website=dialog.findChild<QLabel*>(QStringLiteral("aboutWebsite"));QVERIFY(website);
    QTextDocument websiteText;websiteText.setHtml(website->text());QCOMPARE(websiteText.toPlainText(),QStringLiteral("pentarnion.com"));
    QVERIFY(website->text().contains(QStringLiteral("href=\"https://pentarnion.com\"")));QVERIFY(website->openExternalLinks());
    QVERIFY(website->textInteractionFlags().testFlag(Qt::LinksAccessibleByKeyboard));
    QCOMPARE(website->accessibleName(),QStringLiteral("Pentarnion website"));
}

void DesktopShellTests::semanticFileIconsUsePentarnionTypes() {
    QCOMPARE(projectIconKind(QFileInfo(QStringLiteral("example.p"))),ProjectIconKind::Model);
    QCOMPARE(projectIconKind(QFileInfo(QStringLiteral("project.pprj"))),ProjectIconKind::Project);
    QCOMPARE(projectIconKind(QFileInfo(QStringLiteral("measurements.xlsx"))),ProjectIconKind::Spreadsheet);
    const auto source=projectKindIcon(ProjectIconKind::Model);const auto sheet=projectKindIcon(ProjectIconKind::Spreadsheet);
    QVERIFY(!source.isNull());QVERIFY(!sheet.isNull());QVERIFY(source.cacheKey()!=sheet.cacheKey());
}

void DesktopShellTests::figureGroupsRetainViewsAcrossSplitsTabsAndFloats() {
    QMainWindow window;auto* host=new FigureGroupHost(&window);window.setCentralWidget(host);
    auto* first=new QPlainTextEdit;first->setPlainText(QStringLiteral("retained first scientific view"));
    auto* second=new QPlainTextEdit;second->setPlainText(QStringLiteral("retained second scientific view"));
    second->setProperty("sceneIdentity",QStringLiteral("immutable-figure-batch"));QPointer<QWidget> retained=second;
    QVERIFY(host->addFigure(1,QStringLiteral("Time series"),first));QVERIFY(host->addFigure(2,QStringLiteral("Trajectory"),second));
    QWidget rejected;QVERIFY(!host->addFigure(2,QStringLiteral("Duplicate"),&rejected));QVERIFY(rejected.parentWidget()==nullptr);
    window.resize(1000,650);window.show();QCoreApplication::processEvents();
    auto* firstDock=host->dockWidget(1);auto* secondDock=host->dockWidget(2);QVERIFY(firstDock);QVERIFY(secondDock);
    QVERIFY(host->splitFigure(1,2,Qt::Horizontal));QVERIFY(!host->tabifiedDockWidgets(firstDock).contains(secondDock));
    QVERIFY(host->tabifyFigures(1,2));QVERIFY(host->tabifiedDockWidgets(firstDock).contains(secondDock));
    QList<QPair<int,bool>> transitions;host->dockedChanged=[&](int id,bool docked){transitions.push_back({id,docked});};
    QVERIFY(host->floatFigure(2));QVERIFY(secondDock->isFloating());QVERIFY(transitions.contains({2,false}));
    QVERIFY(host->splitFigure(1,2,Qt::Vertical));QVERIFY(!secondDock->isFloating());QVERIFY(transitions.contains({2,true}));
    QCOMPARE(host->figureWidget(2),retained.data());QCOMPARE(second->toPlainText(),QStringLiteral("retained second scientific view"));
    QCOMPARE(second->property("sceneIdentity").toString(),QStringLiteral("immutable-figure-batch"));
    int closing=0;host->closeRequested=[&](int id){closing=id;};secondDock->close();
    QTRY_COMPARE(closing,2);QVERIFY(retained);QVERIFY(host->containsFigure(2));
    auto* removed=host->takeFigure(2);QCOMPARE(removed,retained.data());QVERIFY(removed->isHidden());QVERIFY(removed->parentWidget()==nullptr);
    QCOMPARE(host->count(),1);QVERIFY(host->containsFigure(1));QCOMPARE(first->toPlainText(),QStringLiteral("retained first scientific view"));
    delete removed;QVERIFY(retained.isNull());
}

void DesktopShellTests::figureGroupLayoutRestorePreservesViews() {
    FigureGroupHost host;auto* first=new QLabel(QStringLiteral("first"));auto* second=new QLabel(QStringLiteral("second"));auto* third=new QLabel(QStringLiteral("third"));
    QVERIFY(host.addFigure(1,QStringLiteral("One"),first));QVERIFY(host.addFigure(2,QStringLiteral("Two"),second));QVERIFY(host.addFigure(3,QStringLiteral("Three"),third));
    QVERIFY(host.splitFigure(1,2,Qt::Horizontal));QVERIFY(host.splitFigure(2,3,Qt::Vertical));
    const auto layout=host.saveState(7);QVERIFY(!layout.isEmpty());host.showTabs();
    QVERIFY(host.tabifiedDockWidgets(host.dockWidget(1)).contains(host.dockWidget(3)));
    QVERIFY(host.restoreState(layout,7));
    QVERIFY(!host.tabifiedDockWidgets(host.dockWidget(1)).contains(host.dockWidget(2)));
    QCOMPARE(host.figureWidget(1),first);QCOMPARE(host.figureWidget(2),second);QCOMPARE(host.figureWidget(3),third);
    QVERIFY(host.setCurrentFigure(3));
    int selected=0,selectionChanges=0;host.selected=[&](int id){selected=id;++selectionChanges;};
    QVERIFY(QMetaObject::invokeMethod(&host,"tabifiedDockWidgetActivated",Qt::DirectConnection,Q_ARG(QDockWidget*,host.dockWidget(1))));
    QCOMPARE(host.currentFigure(),1);QCOMPARE(selected,1);QCOMPARE(selectionChanges,1);
    QVERIFY(QMetaObject::invokeMethod(&host,"tabifiedDockWidgetActivated",Qt::DirectConnection,Q_ARG(QDockWidget*,host.dockWidget(1))));
    QCOMPARE(selectionChanges,1);
    QVERIFY(!host.restoreState(QByteArrayLiteral("invalid figure layout"),7));
    QCOMPARE(host.count(),3);QCOMPARE(first->text(),QStringLiteral("first"));QCOMPARE(second->text(),QStringLiteral("second"));
}

void DesktopShellTests::queuedCloseDoesNotCloseAReplacementFigure() {
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen"))QSKIP("Lifecycle regression runs offscreen only.");
    FigureGroupHost host;auto* original=new QLabel(QStringLiteral("original"));
    QVERIFY(host.addFigure(1,QStringLiteral("Original"),original));
    int closes=0;host.closeRequested=[&](int id){QCOMPARE(id,1);++closes;};
    QCloseEvent staleClose;QApplication::sendEvent(host.dockWidget(1),&staleClose);
    QCOMPARE(host.takeFigure(1),original);delete original;
    auto* replacement=new QLabel(QStringLiteral("replacement"));
    QVERIFY(host.addFigure(1,QStringLiteral("Replacement"),replacement));
    QCoreApplication::processEvents();QCOMPARE(closes,0);
    QCOMPARE(host.figureWidget(1),replacement);
    QCloseEvent currentClose;QApplication::sendEvent(host.dockWidget(1),&currentClose);
    QTRY_COMPARE(closes,1);QCOMPARE(host.figureWidget(1),replacement);
}

void DesktopShellTests::normalFigureArrangementsRetainFloatingFrames() {
    if(QGuiApplication::platformName()!=QStringLiteral("offscreen"))QSKIP("Arrangement regression runs offscreen only.");
    QTemporaryDir directory;QVERIFY(directory.isValid());penta::FigureManager figures(nullptr,false);
    penta::runtime::FigureAction one;one.kind=penta::runtime::FigureActionKind::Select;one.figure=1;
    auto two=one;two.figure=2;figures.apply({one,two});figures.showTabs();
    const auto path=directory.filePath(QStringLiteral("arrangement.json"));QString error;
    auto savedObject=[&]{QFile file(path);if(!file.open(QIODevice::ReadOnly))return QJsonObject{};return QJsonDocument::fromJson(file.readAll()).object();};
    auto writeObject=[&](const QJsonObject& object){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;const auto bytes=QJsonDocument(object).toJson();return file.write(bytes)==bytes.size();};
    QVERIFY2(figures.saveFigureArrangement(path,&error),qPrintable(error));
    QVERIFY(!savedObject().value(QStringLiteral("tiled")).toBool());QVERIFY(!savedObject().contains(QStringLiteral("tiles")));
    QVERIFY2(figures.loadFigureArrangement(path,&error),qPrintable(error));
    QVERIFY(figures.undockFigure(2,&error));
    if(auto* frame=figures.findChild<QWidget*>(QStringLiteral("floatingFigure2")))frame->resize(320,240);
    QVERIFY2(figures.saveFigureArrangement(path,&error),qPrintable(error));const auto valid=savedObject();
    QCOMPARE(valid.value(QStringLiteral("dockedFigures")).toArray(),QJsonArray{1});
    QCOMPARE(valid.value(QStringLiteral("floatingFrames")).toArray().size(),1);
    QVERIFY(figures.dockFigure(2,&error));QVERIFY2(figures.loadFigureArrangement(path,&error),qPrintable(error));
    QVERIFY(figures.figureState(1)->docked);QVERIFY(!figures.figureState(2)->docked);
    auto invalid=valid;auto frames=invalid.value(QStringLiteral("floatingFrames")).toArray();
    auto frame=frames.at(0).toObject();frame.insert(QStringLiteral("id"),1);frames[0]=frame;
    invalid.insert(QStringLiteral("floatingFrames"),frames);QVERIFY(writeObject(invalid));
    QVERIFY(!figures.loadFigureArrangement(path,&error));QVERIFY(figures.figureState(1)->docked);QVERIFY(!figures.figureState(2)->docked);
    invalid=valid;frames=invalid.value(QStringLiteral("floatingFrames")).toArray();frame=frames.at(0).toObject();
    frame.insert(QStringLiteral("geometry"),QString::fromLatin1(QByteArrayLiteral("invalid Qt geometry").toBase64()));frames[0]=frame;
    invalid.insert(QStringLiteral("floatingFrames"),frames);QVERIFY(writeObject(invalid));
    QVERIFY(!figures.loadFigureArrangement(path,&error));QVERIFY(figures.figureState(1)->docked);QVERIFY(!figures.figureState(2)->docked);
    QVERIFY(figures.tileStudio(1,2,&error));QVERIFY(figures.undockFigure(2,&error));
    QVERIFY(figures.saveFigureArrangement(path,&error));const auto tiled=savedObject();
    const auto cells=tiled.value(QStringLiteral("tiles")).toObject().value(QStringLiteral("placements")).toArray();
    QCOMPARE(cells.size(),1);QCOMPARE(cells.at(0).toObject().value(QStringLiteral("id")).toInt(),1);
    figures.showTabs();QVERIFY(figures.dockFigure(2,&error));QVERIFY2(figures.loadFigureArrangement(path,&error),qPrintable(error));
    QVERIFY(figures.figureState(1)->docked);QVERIFY(!figures.figureState(2)->docked);
    QVERIFY(figures.undockFigure(1,&error));QVERIFY(figures.saveFigureArrangement(path,&error));
    QVERIFY(!savedObject().contains(QStringLiteral("tiles")));
    QVERIFY(figures.dockFigure(1,&error));QVERIFY(figures.dockFigure(2,&error));
    QVERIFY2(figures.loadFigureArrangement(path,&error),qPrintable(error));
    QVERIFY(!figures.figureState(1)->docked);QVERIFY(!figures.figureState(2)->docked);
}

void DesktopShellTests::independentFigureControllerReturnsTheOriginalView() {
    QTabWidget tabs;auto* view=new QPlainTextEdit;view->setPlainText(QStringLiteral("unchanged figure state"));
    tabs.addTab(view,QStringLiteral("Figure one"));FigureDockController controller(view,QStringLiteral("Figure one"),QStringLiteral("testDockToggle"));
    tabs.resize(500,400);tabs.show();QCoreApplication::processEvents();
    QVERIFY(controller.undock(false));QVERIFY(controller.isUndocked());QVERIFY(tabs.widget(0)!=view);
    QVERIFY(controller.dock());QVERIFY(!controller.isUndocked());QCOMPARE(tabs.widget(0),view);
    QCOMPARE(view->toPlainText(),QStringLiteral("unchanged figure state"));
}

void DesktopShellTests::websiteLinkImmediatelyPrecedesParallelWorkerIndicator() {
    penta::MainWindow window(false);auto* status=window.statusBar();
    auto* website=status->findChild<QLabel*>(QStringLiteral("studioWebsiteLink"));
    auto* workers=status->findChild<QWidget*>(QStringLiteral("parallelPoolIndicator"));QVERIFY(website&&workers);
    QTextDocument label;label.setHtml(website->text());QCOMPARE(label.toPlainText(),QStringLiteral("pentarnion.com"));
    QVERIFY(website->text().contains(QStringLiteral("href=\"https://pentarnion.com\"")));QVERIFY(website->openExternalLinks());
    QCOMPARE(website->textInteractionFlags(),Qt::TextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::LinksAccessibleByKeyboard));
    QCOMPARE(website->accessibleName(),QStringLiteral("Pentarnion website"));QCOMPARE(website->toolTip(),QStringLiteral("Pentarnion website"));
    QList<QWidget*> orderedWidgets;
    const auto collect=[&](auto&& self,QLayout* layout)->void{if(!layout)return;for(int index=0;index<layout->count();++index){auto* item=layout->itemAt(index);if(auto* widget=item->widget())orderedWidgets.append(widget);else if(auto* nested=item->layout())self(self,nested);}};
    collect(collect,status->layout());const auto websiteIndex=orderedWidgets.indexOf(website);QVERIFY(websiteIndex>=0);QCOMPARE(orderedWidgets.indexOf(workers),websiteIndex+1);
}

PENTA_OFFSCREEN_TEST_MAIN(DesktopShellTests)
#include "desktop_shell_tests.moc"
