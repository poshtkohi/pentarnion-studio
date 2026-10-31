#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "plotting/plot_template.hpp"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
penta::runtime::FigureAction curve(double low,double high){penta::runtime::FigureAction a;a.kind=penta::runtime::FigureActionKind::Plot;a.dimensions=2;a.axes={-1,-1,-1};a.algebraId="A_gamma_v1";a.gamma=1;a.curves.push_back({{{low,0,0},{high,1,0}},"source curve"});return a;}
penta::runtime::FigureAction properties(penta::runtime::PlotStylePatch patch){penta::runtime::FigureAction a;a.kind=penta::runtime::FigureActionKind::ConfigurePlot;a.plotStyle=std::move(patch);return a;}
}
class PublicationPlotTests:public QObject {
    Q_OBJECT
private slots:
    void holdPreservesOriginalSamples(){
        penta::FigureManager figures(nullptr,false);const auto first=curve(0,1),second=curve(2,3);figures.apply({first});
        penta::runtime::PlotStylePatch hold;hold.hold=true;figures.apply({properties(hold),second});
        auto state=figures.figureState(1);QVERIFY(state);QCOMPARE(state->curves.size(),std::size_t(2));QVERIFY(state->curves[0].points==first.curves[0].points);QVERIFY(state->curves[1].points==second.curves[0].points);
        hold.hold=false;figures.apply({properties(hold),first});state=figures.figureState(1);QCOMPARE(state->curves.size(),std::size_t(1));QVERIFY(state->curves[0].points==first.curves[0].points);
    }
    void styleChangesLeaveDataIntact(){
        penta::FigureManager figures(nullptr,false);const auto plot=curve(-2,4);figures.apply({plot});penta::runtime::PlotStylePatch patch;patch.colors=std::vector<std::array<double,3>>{{{.8,.1,.2}},{{.1,.2,.9}}};patch.lineWidth=3;patch.lineStyle="dashdot";patch.fontSize=16;patch.fontFamily="serif";patch.legendLocation="northwest";patch.legendLabels=std::vector<std::string>{"fitted model"};patch.legendBox=false;patch.equalUnits=true;patch.limitsSet[0]=true;patch.limits[0]=penta::runtime::AxisLimits{-1,2};figures.apply({properties(patch)});
        auto state=figures.figureState(1);QVERIFY(state);QVERIFY(state->curves[0].points==plot.curves[0].points);QCOMPARE(state->style.lineStyle,std::string("dashdot"));QCOMPARE(state->style.fontSize,16.);QVERIFY(state->style.equalUnits);QVERIFY(state->style.limits[0].has_value());QCOMPARE(state->style.limits[0]->upper,2.);QCOMPARE(state->style.legendLabels[0],std::string("fitted model"));
        penta::runtime::PlotStylePatch automatic;automatic.limitsSet={true,true,true};automatic.equalUnits=false;figures.apply({properties(automatic)});QVERIFY(!figures.figureState(1)->style.limits[0]);
    }
    void invalidStyleAndHoldAreTransactional(){
        penta::FigureManager figures(nullptr,false);QSignalSpy errors(&figures,&penta::FigureManager::error);const auto initial=curve(0,1);figures.apply({initial});penta::runtime::PlotStylePatch bad;bad.lineWidth=-1;figures.apply({properties(bad)});QCOMPARE(errors.count(),1);QCOMPARE(figures.figureState(1)->style.lineWidth,2.2);
        penta::runtime::PlotStylePatch hold;hold.hold=true;figures.apply({properties(hold)});auto wrong=initial;wrong.dimensions=3;figures.apply({wrong});QCOMPARE(errors.count(),2);QCOMPARE(figures.figureState(1)->dimensions,2);QCOMPARE(figures.figureState(1)->curves.size(),std::size_t(1));
    }
    void linkTwoDimensionalAxisLimits(){
        using penta::runtime::FigureActionKind;penta::FigureManager figures(nullptr,false);penta::runtime::FigureAction subplot;subplot.kind=FigureActionKind::Subplot;subplot.rows=1;subplot.columns=2;subplot.subplotIndex=1;figures.apply({subplot,curve(0,1)});subplot.subplotIndex=2;figures.apply({subplot,curve(2,4)});penta::runtime::FigureAction link;link.kind=FigureActionKind::LinkAxes;link.linkedPanels={1,2};link.linkedAxesMask=1;figures.apply({link});const auto state=figures.figureState(1);QVERIFY(state);QCOMPARE(state->linkedPanels,std::vector<int>({1,2}));for(const auto& panel:state->subplots){QVERIFY(panel.style.limits[0]);QCOMPARE(panel.style.limits[0]->lower,0.);QCOMPARE(panel.style.limits[0]->upper,4.);QVERIFY(!panel.style.limits[1]);}
        penta::runtime::PlotStylePatch limits;limits.limitsSet[0]=true;limits.limits[0]=penta::runtime::AxisLimits{-.5,.5};figures.apply({properties(limits)});const auto updated=figures.figureState(1);for(const auto& panel:updated->subplots)QCOMPARE(panel.style.limits[0]->upper,.5);
        link.linkedAxesMask=0;link.linkedPanels.clear();figures.apply({link});QVERIFY(figures.figureState(1)->linkedPanels.empty());
    }
    void invalidLinksPreserveExistingState(){
        penta::FigureManager figures(nullptr,false);figures.apply({curve(0,1)});QSignalSpy errors(&figures,&penta::FigureManager::error);penta::runtime::FigureAction link;link.kind=penta::runtime::FigureActionKind::LinkAxes;link.linkedPanels={1,42};link.linkedAxesMask=3;figures.apply({link});QCOMPARE(errors.count(),1);QVERIFY(figures.figureState(1)->linkedPanels.empty());
    }
    void savedTemplatesRestoreStylesWithoutReplacingData(){
        using namespace penta::runtime;penta::FigureManager figures(nullptr,false);const auto original=curve(0,1);figures.apply({original});PlotStylePatch patch;patch.lineWidth=4;patch.fontFamily="serif";figures.apply({properties(patch)});FigureAction title;title.kind=FigureActionKind::SetTitle;title.text="$p+q$";figures.apply({title});QTemporaryDir directory;QString error;const auto path=directory.filePath("style.json");QVERIFY2(figures.savePlotTemplate(1,0,path,false,&error),qPrintable(error));QVERIFY(!figures.savePlotTemplate(1,0,path,false,&error));auto loaded=std::make_shared<const penta::plotting::PlotTemplate>(penta::plotting::readPlotTemplate(std::filesystem::path(path.toStdWString())));patch.lineWidth=1;figures.apply({properties(patch)});FigureAction apply;apply.kind=FigureActionKind::ApplyPlotTemplate;apply.plotTemplate=loaded;figures.apply({apply});const auto state=figures.figureState(1);QCOMPARE(state->style.lineWidth,4.);QCOMPARE(state->title,QString("$p+q$"));QVERIFY(state->curves[0].points==original.curves[0].points);
    }
    void linkThreeDimensionalCameraModels(){
        using namespace penta::runtime;penta::FigureManager figures(nullptr,false);FigureAction sub;sub.kind=FigureActionKind::Subplot;sub.rows=1;sub.columns=2;auto plot=curve(0,1);plot.dimensions=3;figures.apply({sub,plot});sub.subplotIndex=2;figures.apply({sub,plot});FigureAction link;link.kind=FigureActionKind::LinkCameras;link.linkedPanels={1,2};figures.apply({link});QCOMPARE(figures.figureState(1)->linkedCameras,std::vector<int>({1,2}));QSignalSpy errors(&figures,&penta::FigureManager::error);link.linkedPanels={1,3};figures.apply({link});QCOMPARE(errors.size(),1);QCOMPARE(figures.figureState(1)->linkedCameras,std::vector<int>({1,2}));link.linkedPanels.clear();figures.apply({link});QVERIFY(figures.figureState(1)->linkedCameras.empty());
    }
    void exposedCameraNavigationPropagates(){
        QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
    }
    void vectorPdfRequiresActualRenderer(){
        penta::FigureManager figures(nullptr,false);figures.apply({curve(0,1)});QTemporaryDir directory;QVERIFY(directory.isValid());QString error;penta::runtime::FigureExportOptions options;options.widthCm=16;options.heightCm=10;const auto path=directory.filePath("figure.pdf");QVERIFY(!figures.exportFigure(1,path,options,&error));QVERIFY(error.contains("renderer",Qt::CaseInsensitive));QVERIFY(!QFileInfo::exists(path));
    }
};
PENTA_OFFSCREEN_TEST_MAIN(PublicationPlotTests)
#include "publication_plot_tests.moc"
