#include "offscreen_test_bootstrap.hpp"
#include "ide/desktop/studio_workspace_layout.hpp"
#include <QDockWidget>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolBar>
#include <QWindow>

using penta::desktop::StudioWorkspaceLayout;
namespace {
QDockWidget* pane(QMainWindow& host,const char* name,Qt::DockWidgetArea area){
    auto* dock=new QDockWidget(QString::fromLatin1(name),&host);
    dock->setObjectName(QString::fromLatin1(name));dock->setWidget(new QLabel(QString::fromLatin1(name),dock));
    dock->setMinimumSize(70,40);host.addDockWidget(area,dock);dock->show();return dock;
}
QRect bounds(QWidget* widget,QWidget* host){return {widget->mapTo(host,QPoint()),widget->size()};}
QString geometryDescription(QWidget* widget,QWidget* host){
    const auto rect=bounds(widget,host);
    return QStringLiteral("%1: visible=%2 hidden=%3 bounds=(%4,%5 %6x%7) window=%8 flags=%9")
        .arg(widget->objectName()).arg(widget->isVisible()).arg(widget->isHidden())
        .arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()).arg(widget->isWindow()).arg(quint64(widget->windowFlags()));
}
}
class StudioWorkspaceLayoutTests final:public QObject {
    Q_OBJECT
private slots:
    void ownsOnlyTheExistingWorkingWidgets();
    void nativeQtHiddenTabSaveControl();
    void copilotIsAFullHeightIndependentColumn();
    void stateRoundTripAndInvalidStateRetainViews();
    void explicitMigrationRehomesOnlyTheWorkingPane();
    void mismatchedSourceIsNotDisposed();
    void lazyWorkingPaneUsesItsOwnHostAndSavedPlaceholder();
    void outerStateNeverBindsNestedWorkingDocksOrToolbars();
};
void StudioWorkspaceLayoutTests::ownsOnlyTheExistingWorkingWidgets(){
    QMainWindow outer;auto* source=new QPlainTextEdit;source->setPlainText(QStringLiteral("Original unsaved source"));outer.setCentralWidget(source);
    outer.setDockOptions(QMainWindow::AllowNestedDocks|QMainWindow::AllowTabbedDocks);
    auto* project=pane(outer,"projectDock",Qt::LeftDockWidgetArea);auto* command=pane(outer,"resultsDock",Qt::BottomDockWidgetArea);
    auto* problems=pane(outer,"problemsDock",Qt::BottomDockWidgetArea);auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    outer.resize(1000,650);outer.show();QApplication::processEvents();
    outer.tabifyDockWidget(command,problems);QApplication::processEvents();
    problems->hide();copilot->hide();
    QVERIFY2(outer.tabifiedDockWidgets(command).contains(problems),"Fixture must start with a tabified Command/Problems group");
    QPointer<QWidget> sourceIdentity=source,plotIdentity=figures->widget();
    auto* layout=new StudioWorkspaceLayout(&outer,source,{project,command,problems,figures},copilot);
    QVERIFY(layout->isInstalled());QVERIFY(!layout->workingHost()->isWindow());QCOMPARE(outer.centralWidget(),layout->workingHost());QCOMPARE(layout->workingHost()->centralWidget(),source);
    QCOMPARE(layout->workingDocks().size(),4);QCOMPARE(outer.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly).size(),1);
    QCOMPARE(layout->hostForDock(figures),layout->workingHost());QCOMPARE(layout->hostForDock(copilot),&outer);
    QCOMPARE(sourceIdentity.data(),source);QCOMPARE(plotIdentity.data(),figures->widget());QCOMPARE(source->toPlainText(),QStringLiteral("Original unsaved source"));
    QVERIFY(problems->isHidden());QVERIFY(copilot->isHidden());
    // Qt 6.12 omits hidden members from tabifiedDockWidgets after saveState;
    // validate retained placement when the user actually reveals the pane.
    problems->show();QApplication::processEvents();
    QVERIFY(layout->workingHost()->tabifiedDockWidgets(command).contains(problems));
}
void StudioWorkspaceLayoutTests::nativeQtHiddenTabSaveControl(){
    QMainWindow host;host.setDockOptions(QMainWindow::AllowNestedDocks|QMainWindow::AllowTabbedDocks);
    host.setCentralWidget(new QLabel(QStringLiteral("Native Qt control")));
    auto* command=pane(host,"controlCommand",Qt::BottomDockWidgetArea);auto* problems=pane(host,"controlProblems",Qt::BottomDockWidgetArea);
    host.resize(500,320);host.show();QApplication::processEvents();host.tabifyDockWidget(command,problems);problems->hide();
    const auto saved=host.saveState(2);QVERIFY(!saved.isEmpty());
    problems->show();QApplication::processEvents();
    QVERIFY2(host.tabifiedDockWidgets(command).contains(problems),"Native Qt must restore the hidden tab group when Problems is shown");
    problems->hide();QVERIFY(host.restoreState(saved,2));problems->show();QApplication::processEvents();
    QVERIFY2(host.tabifiedDockWidgets(command).contains(problems),"Native Qt saved state must retain hidden-tab grouping");
}
void StudioWorkspaceLayoutTests::copilotIsAFullHeightIndependentColumn(){
    QMainWindow outer;auto* source=new QPlainTextEdit;source->setMinimumSize(80,60);outer.setCentralWidget(source);
    auto* command=pane(outer,"resultsDock",Qt::BottomDockWidgetArea);auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    auto* layout=new StudioWorkspaceLayout(&outer,source,{command,figures},copilot);
    outer.resize(1000,650);outer.show();
    QVERIFY(!layout->workingHost()->isWindow());
    QTRY_VERIFY2_WITH_TIMEOUT(command->isVisible()&&figures->isVisible()&&copilot->isVisible(),
        qPrintable(geometryDescription(command,&outer)+QLatin1Char(';')+geometryDescription(figures,&outer)+QLatin1Char(';')+geometryDescription(copilot,&outer)+QLatin1Char(';')+geometryDescription(layout->workingHost(),&outer)),1000);
    outer.resizeDocks({copilot},{260},Qt::Horizontal);layout->workingHost()->resizeDocks({command},{160},Qt::Vertical);layout->workingHost()->resizeDocks({figures},{230},Qt::Horizontal);QApplication::processEvents();
    // Nested QMainWindow layouts may post a second LayoutRequest. Check the
    // settled geometry, not an intermediate rectangle from the initial show.
    QTRY_VERIFY2_WITH_TIMEOUT(bounds(command,&outer).right()<bounds(copilot,&outer).left(),
        qPrintable(geometryDescription(command,&outer)+QLatin1Char(';')+geometryDescription(copilot,&outer)+QLatin1Char(';')+geometryDescription(layout->workingHost(),&outer)),1000);
    const auto chat=bounds(copilot,&outer),console=bounds(command,&outer),workspace=bounds(layout->workingHost(),&outer);
    QCOMPARE(outer.corner(Qt::BottomRightCorner),Qt::RightDockWidgetArea);QCOMPARE(outer.corner(Qt::TopRightCorner),Qt::RightDockWidgetArea);
    QVERIFY(console.right()<chat.left());QVERIFY(chat.top()<=console.top());QVERIFY(chat.bottom()>=console.bottom());QCOMPARE(chat.height(),workspace.height());
    const int chatWidth=copilot->width();const auto owner=figures->parentWidget();
    layout->workingHost()->resizeDocks({figures},{300},Qt::Horizontal);QApplication::processEvents();
    QTRY_COMPARE_WITH_TIMEOUT(copilot->width(),chatWidth,1000);QCOMPARE(figures->parentWidget(),owner);QVERIFY(bounds(command,&outer).right()<bounds(copilot,&outer).left());
}
void StudioWorkspaceLayoutTests::stateRoundTripAndInvalidStateRetainViews(){
    QMainWindow outer;auto* source=new QLabel(QStringLiteral("source identity"));outer.setCentralWidget(source);
    auto* command=pane(outer,"resultsDock",Qt::BottomDockWidgetArea);auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    const auto legacyOuter=outer.saveState();
    auto* layout=new StudioWorkspaceLayout(&outer,source,{command,figures},copilot);auto* plot=figures->widget();const auto saved=layout->saveWorkingState();QVERIFY(!saved.isEmpty());
    layout->workingHost()->removeDockWidget(figures);layout->workingHost()->addDockWidget(Qt::LeftDockWidgetArea,figures);
    QVERIFY(layout->restoreWorkingState(saved));QCOMPARE(layout->workingHost()->dockWidgetArea(figures),Qt::RightDockWidgetArea);
    QVERIFY(!layout->restoreWorkingState(QByteArrayLiteral("invalid opaque state")));QVERIFY(!layout->restoreWorkingState(legacyOuter));QVERIFY(!layout->restoreWorkingState(QByteArray(1024*1024+1,'x')));
    QCOMPARE(layout->workingHost()->centralWidget(),source);QCOMPARE(figures->widget(),plot);QCOMPARE(layout->hostForDock(copilot),&outer);
    layout->captureDefaultLayout();layout->workingHost()->removeDockWidget(figures);layout->workingHost()->addDockWidget(Qt::LeftDockWidgetArea,figures);
    QVERIFY(layout->resetWorkingLayout());QCOMPARE(layout->workingHost()->dockWidgetArea(figures),Qt::RightDockWidgetArea);
}
void StudioWorkspaceLayoutTests::explicitMigrationRehomesOnlyTheWorkingPane(){
    QMainWindow outer;auto* source=new QLabel(QStringLiteral("retained"));outer.setCentralWidget(source);
    auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    auto* layout=new StudioWorkspaceLayout(&outer,source,{figures},copilot);auto* plot=figures->widget();
    layout->workingHost()->removeDockWidget(figures);figures->setParent(&outer);outer.addDockWidget(Qt::RightDockWidgetArea,figures);
    layout->ensureSeparatedLayout();
    QCOMPARE(figures->parentWidget(),layout->workingHost());QCOMPARE(figures->widget(),plot);QCOMPARE(copilot->parentWidget(),&outer);QCOMPARE(layout->workingHost()->centralWidget(),source);
}
void StudioWorkspaceLayoutTests::mismatchedSourceIsNotDisposed(){
    QMainWindow outer;auto* actual=new QLabel(QStringLiteral("actual"));outer.setCentralWidget(actual);QLabel other(QStringLiteral("unrelated"));QPointer<QWidget> identity=actual;
    auto* layout=new StudioWorkspaceLayout(&outer,&other,{});QVERIFY(!layout->isInstalled());QCOMPARE(outer.centralWidget(),identity.data());QVERIFY(!identity.isNull());
}
void StudioWorkspaceLayoutTests::lazyWorkingPaneUsesItsOwnHostAndSavedPlaceholder(){
    QMainWindow outer;auto* source=new QLabel(QStringLiteral("source identity"));outer.setCentralWidget(source);
    auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    auto* layout=new StudioWorkspaceLayout(&outer,source,{figures},copilot);
    auto* late=new QDockWidget(QStringLiteral("Lazy inspector"),layout->workingHost());late->setObjectName(QStringLiteral("featureRulesDock"));late->setWidget(new QLabel(QStringLiteral("inspector data"),late));
    QCOMPARE(layout->hostForDock(late),layout->workingHost());QVERIFY(layout->registerWorkingDock(late,Qt::LeftDockWidgetArea));
    QCOMPARE(layout->workingHost()->dockWidgetArea(late),Qt::LeftDockWidgetArea);QVERIFY(layout->workingDocks().contains(late));
    const auto saved=layout->saveWorkingState();layout->workingHost()->removeDockWidget(late);delete late;
    QVERIFY(layout->restoreWorkingState(saved));
    auto* replacement=new QDockWidget(QStringLiteral("Lazy inspector"),layout->workingHost());replacement->setObjectName(QStringLiteral("featureRulesDock"));replacement->setWidget(new QLabel(QStringLiteral("retained lazy data"),replacement));
    QVERIFY(layout->registerWorkingDock(replacement,Qt::RightDockWidgetArea));
    QCOMPARE(layout->workingHost()->dockWidgetArea(replacement),Qt::LeftDockWidgetArea);QCOMPARE(outer.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly).size(),1);
    QCOMPARE(layout->workingHost()->centralWidget(),source);QCOMPARE(copilot->parentWidget(),&outer);
}
void StudioWorkspaceLayoutTests::outerStateNeverBindsNestedWorkingDocksOrToolbars(){
    QMainWindow outer;auto* source=new QPlainTextEdit;source->setPlainText(QStringLiteral("Retained unsaved text"));outer.setCentralWidget(source);
    auto* figures=pane(outer,"figuresDock",Qt::RightDockWidgetArea);auto* copilot=pane(outer,"copilotDock",Qt::RightDockWidgetArea);
    auto* tools=new QToolBar(QStringLiteral("Working controls"),&outer);tools->setObjectName(QStringLiteral("workingTools"));tools->addAction(QStringLiteral("Retained action"));
    outer.addToolBar(Qt::LeftToolBarArea,tools);outer.resize(740,480);outer.show();QApplication::processEvents();
    figures->setFloating(true);QApplication::processEvents();const auto legacy=outer.saveState(2);QVERIFY(!legacy.isEmpty());
    auto* layout=new StudioWorkspaceLayout(&outer,source,{figures},copilot);auto* working=layout->workingHost();
    figures->setFloating(false);working->removeDockWidget(figures);working->addDockWidget(Qt::LeftDockWidgetArea,figures);figures->show();
    outer.removeToolBar(tools);tools->setParent(working);working->addToolBar(Qt::TopToolBarArea,tools);tools->show();
    QApplication::processEvents();
    QPointer<QWidget> sourceIdentity=source,plotIdentity=figures->widget();QPointer<QWindow> windowIdentity=outer.windowHandle();const auto nativeIdentity=outer.winId();
    QVERIFY(!figures->isFloating());QCOMPARE(working->dockWidgetArea(figures),Qt::LeftDockWidgetArea);QCOMPARE(working->toolBarArea(tools),Qt::TopToolBarArea);
    QSignalSpy dockNames(figures,&QObject::objectNameChanged),toolNames(tools,&QObject::objectNameChanged),floating(figures,&QDockWidget::topLevelChanged);
    // A legacy outer snapshot contains these same object names. Qt searches
    // recursively and can otherwise bind inner-owned layout items into its
    // deferred restore snapshot, where the next inner restore frees them.
    QVERIFY(layout->restoreOuterState(legacy,2));
    QApplication::processEvents();
    QVERIFY(!figures->isFloating());QCOMPARE(figures->parentWidget(),working);QCOMPARE(working->dockWidgetArea(figures),Qt::LeftDockWidgetArea);
    QCOMPARE(tools->parentWidget(),working);QCOMPARE(working->toolBarArea(tools),Qt::TopToolBarArea);QCOMPARE(tools->orientation(),Qt::Horizontal);
    QCOMPARE(figures->objectName(),QStringLiteral("figuresDock"));QCOMPARE(tools->objectName(),QStringLiteral("workingTools"));
    QCOMPARE(dockNames.size(),0);QCOMPARE(toolNames.size(),0);QCOMPARE(floating.size(),0);
    for(int i=0;i<outer.layout()->count();++i){auto* widget=outer.layout()->itemAt(i)->widget();QVERIFY(widget!=figures&&widget!=tools);}
    QCOMPARE(outer.windowHandle(),windowIdentity.data());QCOMPARE(outer.winId(),nativeIdentity);QCOMPARE(working->centralWidget(),sourceIdentity.data());QCOMPARE(figures->widget(),plotIdentity.data());
    QCOMPARE(source->toPlainText(),QStringLiteral("Retained unsaved text"));
    QVERIFY(!layout->restoreOuterState(QByteArrayLiteral("invalid state"),2));QVERIFY(!layout->restoreOuterState(legacy,99));QVERIFY(!layout->restoreOuterState(QByteArray(1024*1024+1,'x'),2));
    QCOMPARE(figures->parentWidget(),working);QVERIFY(!figures->isFloating());QCOMPARE(tools->parentWidget(),working);
    QCOMPARE(figures->objectName(),QStringLiteral("figuresDock"));QCOMPARE(tools->objectName(),QStringLiteral("workingTools"));
    QCOMPARE(dockNames.size(),0);QCOMPARE(toolNames.size(),0);QCOMPARE(floating.size(),0);
}
PENTA_OFFSCREEN_TEST_MAIN(StudioWorkspaceLayoutTests)
#include "studio_workspace_layout_tests.moc"
