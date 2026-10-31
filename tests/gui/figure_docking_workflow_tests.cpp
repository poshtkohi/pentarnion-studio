#include "offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "ide/desktop/figure_group_host.hpp"
#include "ide/desktop/figure_docking.hpp"
#include "ide/raster_plot_canvas.hpp"
#include <QAction>
#include <QColor>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QRegion>
#include <QTabWidget>
#include <QWindow>
#include <algorithm>

namespace penta {
class FigureDockingWorkflowTestAccess {
public:
    static QRect cell(FigureManager& manager,int id){return manager.figureGlobalCell(id);}
    static void begin(FigureManager& manager,int id,const QPoint& point){manager.beginFigureDrag(id,point);}
    static void update(FigureManager& manager,const QPoint& point){manager.updateFigureDropPreview(point);}
    static void finish(FigureManager& manager,const QPoint& point){manager.finishFigureDrag(point);}
    static void cancel(FigureManager& manager){manager.cancelFigureDrag();}
    static QWidget* preview(FigureManager& manager){return manager.m_dropPreview;}
    static desktop::FigureGroupHost* host(FigureManager& manager){return manager.m_groups;}
};
}
using namespace penta;
namespace {
runtime::FigureAction plot(int id){runtime::FigureAction result;result.kind=runtime::FigureActionKind::Plot;result.figure=id;result.dimensions=2;result.axes={1,2,3};result.algebraId="A_gamma_v1";result.gamma=1;result.vectors={{{1,double(id),2,.5,0},"p"}};return result;}
void populate(FigureManager& manager){manager.apply({plot(1),plot(2),plot(3)});manager.resize(1100,650);manager.show();QApplication::processEvents();}
void populateSubplots(FigureManager& manager){
    std::vector<runtime::FigureAction> actions;
    for(const int id:{1,2})for(const int index:{1,2}){
        runtime::FigureAction cell;cell.kind=runtime::FigureActionKind::Subplot;cell.figure=id;cell.rows=1;cell.columns=2;cell.subplotIndex=index;
        actions.push_back(cell);actions.push_back(plot(id));
    }
    manager.apply(actions);manager.resize(800,360);manager.show();QApplication::processEvents();
}
QWidget* subplot(QWidget* figure,int index){return figure->findChild<QWidget*>(QStringLiteral("subplot%1").arg(index));}
QWidget* selection(QWidget* owner,const char* name){return owner->findChild<QWidget*>(QString::fromLatin1(name));}
}
class FigureDockingWorkflowTests final:public QObject {
    Q_OBJECT
private slots:
    void tiledTitlesOfferHalfCellDropsAndRetainViews();
    void allFourDirectionsAndCenterTabAreAvailable();
    void cancelledAndRetiredDropsLeaveReplacementsUntouched();
    void draggingATiledTitleOutsideFloatsTheSameView();
    void independentFramesKeepCaptionButtonsAndRestoreCustomTitles();
    void closeAllIncludesFloatingFigures();
    void selectionBordersFollowClicksWithoutChangingPlotState();
    void selectionFollowsTabsGroupsAndIndependentFrames();
};
void FigureDockingWorkflowTests::tiledTitlesOfferHalfCellDropsAndRetainViews(){
    FigureManager manager(nullptr,false);populate(manager);QString error;QVERIFY(manager.tileStudio(1,3,&error));QApplication::processEvents();
    auto* one=manager.findChild<QWidget*>("figurePage1");auto* two=manager.findChild<QWidget*>("figurePage2");auto* three=manager.findChild<QWidget*>("figurePage3");QVERIFY(one&&two&&three);
    const auto values=manager.figureState(1)->vectors.front().coefficients;
    const auto target=FigureDockingWorkflowTestAccess::cell(manager,2);QVERIFY(target.width()>50);
    const QPoint end(target.right()-5,target.center().y());
    FigureDockingWorkflowTestAccess::begin(manager,1,FigureDockingWorkflowTestAccess::cell(manager,1).topLeft()+QPoint(10,10));
    FigureDockingWorkflowTestAccess::update(manager,end);
    auto* preview=FigureDockingWorkflowTestAccess::preview(manager);QVERIFY(preview&&preview->isVisible());QCOMPARE(preview->property("studioDropZone").toString(),QStringLiteral("right"));
    QCOMPARE(preview->geometry().width(),target.width()-target.width()/2);
    FigureDockingWorkflowTestAccess::finish(manager,end);QTRY_VERIFY(manager.groupedFigures());
    auto* host=FigureDockingWorkflowTestAccess::host(manager);QVERIFY(host);QApplication::processEvents();
    QCOMPARE(host->figureWidget(1),one);QCOMPARE(host->figureWidget(2),two);QCOMPARE(host->figureWidget(3),three);
    QVERIFY(!host->tabifiedDockWidgets(host->dockWidget(1)).contains(host->dockWidget(2)));
    QTRY_VERIFY(host->dockWidget(1)->geometry().center().x()>host->dockWidget(2)->geometry().center().x());
    QTRY_VERIFY(std::abs(host->dockWidget(1)->width()-host->dockWidget(2)->width())<=12);
    QCOMPARE(manager.figureState(1)->vectors.front().coefficients,values);QCOMPARE(manager.figureCount(),3);
}
void FigureDockingWorkflowTests::allFourDirectionsAndCenterTabAreAvailable(){
    FigureManager manager(nullptr,false);populate(manager);QString error;QVERIFY(manager.tileStudio(1,3,&error));QApplication::processEvents();
    const auto target=FigureDockingWorkflowTestAccess::cell(manager,2);const auto start=FigureDockingWorkflowTestAccess::cell(manager,1).center();
    const QList<QPair<QString,QPoint>> zones{{"left",{target.left()+4,target.center().y()}},{"right",{target.right()-4,target.center().y()}},{"top",{target.center().x(),target.top()+50}},{"bottom",{target.center().x(),target.bottom()-4}},{"tab",target.center()}};
    for(const auto& [zone,point]:zones){FigureDockingWorkflowTestAccess::begin(manager,1,start);FigureDockingWorkflowTestAccess::update(manager,point);auto* preview=FigureDockingWorkflowTestAccess::preview(manager);QVERIFY(preview&&preview->isVisible());QCOMPARE(preview->property("studioDropZone").toString(),zone);FigureDockingWorkflowTestAccess::cancel(manager);QVERIFY(!preview->isVisible());}
}
void FigureDockingWorkflowTests::cancelledAndRetiredDropsLeaveReplacementsUntouched(){
    FigureManager manager(nullptr,false);populate(manager);QString error;QVERIFY(manager.tileStudio(1,3,&error));QApplication::processEvents();
    const auto target=FigureDockingWorkflowTestAccess::cell(manager,2),source=FigureDockingWorkflowTestAccess::cell(manager,1);const auto point=target.center();
    FigureDockingWorkflowTestAccess::begin(manager,1,source.center());FigureDockingWorkflowTestAccess::update(manager,point);FigureDockingWorkflowTestAccess::cancel(manager);QApplication::processEvents();QVERIFY(!manager.groupedFigures());QCOMPARE(manager.figureCount(),3);
    FigureDockingWorkflowTestAccess::begin(manager,1,source.center());FigureDockingWorkflowTestAccess::update(manager,point);FigureDockingWorkflowTestAccess::finish(manager,point);
    manager.closeAllFigures();manager.apply({plot(1),plot(2)});QApplication::processEvents();QCOMPARE(manager.figureCount(),2);QVERIFY(!manager.groupedFigures());
}
void FigureDockingWorkflowTests::independentFramesKeepCaptionButtonsAndRestoreCustomTitles(){
    QMainWindow outer;auto* working=new QMainWindow(&outer,Qt::Widget);outer.setCentralWidget(working);
    auto* panel=new QDockWidget(QStringLiteral("Figures"),working);auto* content=new QLabel(QStringLiteral("Retained raster figures"),panel);panel->setWidget(content);working->addDockWidget(Qt::RightDockWidgetArea,panel);
    auto* title=new QLabel(QStringLiteral("Compact Figures header"),panel);title->setObjectName("figuresDockTitleBar");panel->setTitleBarWidget(title);
    desktop::FigureDockController controller(content,QStringLiteral("Figures"),QStringLiteral("paneDockToggle"));controller.setDockContainer(panel);outer.resize(700,450);outer.show();QApplication::processEvents();
    QVERIFY(controller.undock(false));QApplication::processEvents();QVERIFY(panel->isFloating());QVERIFY(!panel->titleBarWidget());
    QCOMPARE(panel->windowFlags()&Qt::WindowType_Mask,Qt::WindowFlags(Qt::Window));
    QVERIFY(panel->windowFlags()&Qt::WindowMinimizeButtonHint);QVERIFY(panel->windowFlags()&Qt::WindowMaximizeButtonHint);QVERIFY(panel->windowFlags()&Qt::WindowCloseButtonHint);
    QVERIFY(panel->windowHandle());QVERIFY(!panel->windowHandle()->transientParent());QCOMPARE(panel->widget(),content);
    panel->showMinimized();QVERIFY(panel->isMinimized());panel->showNormal();panel->showMaximized();QVERIFY(panel->isMaximized());panel->showNormal();
    QVERIFY(controller.dock());QCOMPARE(panel->titleBarWidget(),title);QCOMPARE(panel->widget(),content);QCOMPARE(panel->parentWidget(),working);
}
void FigureDockingWorkflowTests::draggingATiledTitleOutsideFloatsTheSameView(){
    FigureManager manager(nullptr,false);populate(manager);QString error;QVERIFY(manager.tileStudio(1,3,&error));QApplication::processEvents();
    QPointer<QWidget> original=manager.findChild<QWidget*>("figurePage1");QVERIFY(original);
    const auto source=FigureDockingWorkflowTestAccess::cell(manager,1);const auto point=manager.mapToGlobal(QPoint(manager.width()+30,50));
    FigureDockingWorkflowTestAccess::begin(manager,1,source.topLeft()+QPoint(10,10));FigureDockingWorkflowTestAccess::update(manager,point);FigureDockingWorkflowTestAccess::finish(manager,point);
    QTRY_VERIFY(!manager.figureState(1)->docked);QVERIFY(original);QCOMPARE(manager.findChild<QWidget*>("figurePage1"),original.data());
    auto* frame=manager.findChild<QMainWindow*>("floatingFigure1");QVERIFY(frame);QVERIFY(frame->windowFlags()&Qt::WindowMinimizeButtonHint);QVERIFY(frame->windowFlags()&Qt::WindowMaximizeButtonHint);manager.closeAllFigures();
}
void FigureDockingWorkflowTests::closeAllIncludesFloatingFigures(){
    FigureManager manager(nullptr,false);populate(manager);QString error;QVERIFY(manager.undockFigure(1,&error));
    QPointer<QWidget> one=FigureDockingWorkflowTestAccess::host(manager)->figureWidget(1),two=FigureDockingWorkflowTestAccess::host(manager)->figureWidget(2);
    auto* action=manager.findChild<QAction*>("figuresCloseAll");QVERIFY(action&&action->isEnabled());action->trigger();QCOMPARE(manager.figureCount(),0);QVERIFY(one.isNull()&&two.isNull());QVERIFY(!action->isEnabled());QApplication::processEvents();
}
void FigureDockingWorkflowTests::selectionBordersFollowClicksWithoutChangingPlotState(){
    FigureManager manager(nullptr,false);populateSubplots(manager);QString error;QVERIFY(manager.tileStudio(1,2,&error));QApplication::processEvents();
    auto* one=manager.findChild<QWidget*>("figurePage1");auto* two=manager.findChild<QWidget*>("figurePage2");QVERIFY(one&&two);
    auto* first=subplot(one,1);auto* second=subplot(one,2);QVERIFY(first&&second);
    auto* canvas=second->findChild<QWidget*>("figureCanvasPlaceholder");QVERIFY(canvas);
    auto* figureBorder=selection(one,"figureSelectionBorder");auto* panelBorder=selection(second,"subplotSelectionBorder");QVERIFY(figureBorder&&panelBorder);
    const auto firstData=manager.figureState(1)->subplots.front().vectors.front().coefficients;
    QVERIFY(manager.setPlotTool(1,1,PlotTool::DataCursor,&error));QVERIFY(manager.setPlotTool(1,2,PlotTool::Pan,&error));
    const auto geometry=canvas->geometry();const auto margins=one->contentsMargins();const auto scenes=RasterPlotCanvas::pendingSceneCount();
    QTest::mouseClick(canvas,Qt::LeftButton);
    QCOMPARE(manager.currentFigureNumber(),1);QCOMPARE(manager.figureState(1)->activeSubplot,2);
    QVERIFY(one->property("studioFigureSelected").toBool());QVERIFY(!two->property("studioFigureSelected").toBool());
    QVERIFY(second->property("studioSubplotActive").toBool());QVERIFY(!first->property("studioSubplotActive").toBool());
    QVERIFY(!figureBorder->isHidden()&&!panelBorder->isHidden());QVERIFY(figureBorder->testAttribute(Qt::WA_TransparentForMouseEvents));QVERIFY(panelBorder->testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(figureBorder->property("studioSelectionColor").value<QColor>(),QColor(0,114,189));QCOMPARE(figureBorder->property("studioSelectionWidth").toInt(),2);QCOMPARE(panelBorder->property("studioSelectionWidth").toInt(),1);
    QCOMPARE(figureBorder->geometry(),one->rect());QCOMPARE(panelBorder->geometry(),canvas->rect());QVERIFY(!figureBorder->mask().contains(figureBorder->rect().center()));
    QImage borderImage(figureBorder->size(),QImage::Format_ARGB32_Premultiplied);borderImage.fill(Qt::transparent);figureBorder->render(&borderImage,QPoint(),QRegion(),QWidget::DrawChildren);
    QCOMPARE(borderImage.pixelColor(1,1),QColor(0,114,189));QCOMPARE(borderImage.pixelColor(borderImage.rect().center()).alpha(),0);
    QTest::mouseClick(first->findChild<QWidget*>("figureCanvasPlaceholder"),Qt::LeftButton);
    QCOMPARE(manager.figureState(1)->activeSubplot,1);QVERIFY(panelBorder->isHidden());
    QCOMPARE(manager.plotTool(1,1),std::optional<PlotTool>(PlotTool::DataCursor));QCOMPARE(manager.plotTool(1,2),std::optional<PlotTool>(PlotTool::Pan));
    QCOMPARE(manager.figureState(1)->subplots.front().vectors.front().coefficients,firstData);QCOMPARE(canvas->geometry(),geometry);QCOMPARE(one->contentsMargins(),margins);
    QCOMPARE(RasterPlotCanvas::pendingSceneCount(),scenes);QVERIFY(manager.findChildren<RasterPlotCanvas*>().isEmpty());
    auto* otherCanvas=subplot(two,2)->findChild<QWidget*>("figureCanvasPlaceholder");QTest::mouseClick(otherCanvas,Qt::LeftButton);
    QVERIFY(figureBorder->isHidden());QVERIFY(first->property("studioSubplotActive").toBool());
    auto* pan=second->findChild<QAction*>("plotPan");QVERIFY(pan);pan->trigger();
    QCOMPARE(manager.currentFigureNumber(),1);QCOMPARE(manager.figureState(1)->activeSubplot,2);QVERIFY(!figureBorder->isHidden()&&!panelBorder->isHidden());
}
void FigureDockingWorkflowTests::selectionFollowsTabsGroupsAndIndependentFrames(){
    FigureManager manager(nullptr,false);populateSubplots(manager);QString error;QVERIFY(manager.tileStudio(1,2,&error));QApplication::processEvents();
    auto* one=manager.findChild<QWidget*>("figurePage1");auto* two=manager.findChild<QWidget*>("figurePage2");QVERIFY(one&&two);
    QPointer<QWidget> border=selection(one,"figureSelectionBorder");QVERIFY(border);
    auto* second=subplot(one,2);auto* canvas=second->findChild<QWidget*>("figureCanvasPlaceholder");QVERIFY(canvas);
    QTest::mouseClick(canvas,Qt::LeftButton);QCOMPARE(manager.figureState(1)->activeSubplot,2);
    manager.showTabs();auto* tabs=manager.findChild<QTabWidget*>("figureTabs");QVERIFY(tabs);tabs->setCurrentWidget(two);QCOMPARE(manager.currentFigureNumber(),2);QVERIFY(border->isHidden());
    tabs->setCurrentWidget(one);QVERIFY(!border->isHidden());QCOMPARE(manager.figureState(1)->activeSubplot,2);
    QVERIFY(manager.splitFigures(1,2,Qt::Horizontal,&error));QApplication::processEvents();QCOMPARE(selection(one,"figureSelectionBorder"),border.data());
    QTest::mouseClick(canvas,Qt::LeftButton);QCOMPARE(manager.currentFigureNumber(),1);QVERIFY(!border->isHidden());
    QVERIFY(manager.undockFigure(1,&error));QApplication::processEvents();QCOMPARE(selection(one,"figureSelectionBorder"),border.data());
    QTest::mouseClick(subplot(two,1)->findChild<QWidget*>("figureCanvasPlaceholder"),Qt::LeftButton);QCOMPARE(manager.currentFigureNumber(),2);QVERIFY(border->isHidden());
    QTest::mouseClick(canvas,Qt::LeftButton);QCOMPARE(manager.currentFigureNumber(),1);QCOMPARE(manager.figureState(1)->activeSubplot,2);QVERIFY(!border->isHidden());
    QVERIFY(manager.dockFigure(1,&error));QCOMPARE(selection(one,"figureSelectionBorder"),border.data());manager.closeAllFigures();QVERIFY(border.isNull());
}
PENTA_OFFSCREEN_TEST_MAIN(FigureDockingWorkflowTests)
#include "figure_docking_workflow_tests.moc"
