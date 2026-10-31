#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QImage>
#include <QMainWindow>
#include <QDockWidget>
#include <QLineEdit>
#include <QPointer>
#include <QPainter>
#include <QGridLayout>
#include <QTabWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTimer>
#include <QXmlStreamReader>
#include <QtSvg/QSvgRenderer>

#include <cmath>
#include <climits>
#include <limits>

using namespace penta;

namespace {
runtime::FigureAction action(runtime::FigureActionKind kind,int number=0) {
    runtime::FigureAction result;
    result.kind=kind;result.figure=number;
    return result;
}
runtime::FigureAction plot(int dimensions=2) {
    auto result=action(runtime::FigureActionKind::Plot);
    result.dimensions=dimensions;result.axes={{1,2,3}};
    result.algebraId="A_gamma_v1";result.gamma=0.5;
    result.vectors={{{1,2,-1,3,4},"p"},{{-7,-1,2,0,8},"q"}};
    return result;
}
runtime::FigureAction label(runtime::FigureActionKind kind,const std::string& text,bool latex=true) {
    auto result=action(kind);result.text=text;result.latex=latex;return result;
}
runtime::FigureAction subplot(int rows,int columns,int index) {
    auto result=action(runtime::FigureActionKind::Subplot);
    result.rows=rows;result.columns=columns;result.subplotIndex=index;return result;
}
runtime::FigureAction curvePlot(int dimensions=2,bool generic=true) {
    auto result=plot(dimensions);result.vectors.clear();
    if(generic)result.axes={{-1,-1,-1}};
    runtime::PlotCurve curve;curve.label="trajectory";
    for(int index=0;index<=500;++index){
        const double t=index*0.02;
        curve.points.push_back({t,std::sin(t),std::cos(t)});
    }
    result.curves.push_back(std::move(curve));return result;
}
QByteArray bytes(const QString& path) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return{};
    return file.readAll();
}

}

class FigureTests final : public QObject {
    Q_OBJECT
private slots:
    void selectionReplacementAndFullCoefficients();
    void nativeAlgebraProjectionRetainsAllCoordinates();
    void clearCloseAndReuse();
    void invalidProjectionPreservesLastFigure();
    void figureAndVectorLimits();
    void zeroProjectionKeepsOriginalValue();
    void unavailableRenderingDoesNotFakeExport();
    void nativePngAndJpegExport();
    void namedFigurePropertiesAndLabels();
    void dockingTilingAndInvalidLayoutSafety();
    void closingFloatingFigureAndOwnerIsSafe();
    void queuedCloseDoesNotCloseReusedFigureNumber();
    void nativeDockTileAndMathLabels();
    void subplotSelectionPreservesIndependentPanels();
    void subplotClearResetsGrid();
    void invalidSubplotIsTransactional();
    void invalidCurvesPreserveExistingPanels();
    void retainedCurveBudgetIsBounded();
    void retainedPanelBudgetIsBounded();
    void nativeSubplotCurvesExportAndCamera();
    void nativeGenericAxesUseOuterFrame();
    void nativeEscapeClearsDataTip();
    void nativeSvgGeometryAndSubplotFormats();
    void nativeSubplotSaveButtonAndCancel();
    void nativeGeneric3DCurvePlotAndExport();
    void nativeSubplotSaveRejectsReusedTarget();
    void nativeHiddenDockRendersWithoutTextureErrors();
};

void FigureTests::nativeAlgebraProjectionRetainsAllCoordinates() {
    const auto calculated=runtime::execute("algebra S=builtin_algebra(\"sedenion\");element p=1+2*S.e10+3*S.e15;plot2(\"e10\",\"e15\",p);");
    QVERIFY(calculated.ok());FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);manager.apply(calculated.figureActions);
    QCOMPARE(errors.size(),0);const auto state=manager.figureState(1);QVERIFY(state);QCOMPARE(state->basisLabels.size(),std::size_t(16));
    QCOMPARE(state->axes[0],10);QCOMPARE(state->axes[1],15);QVERIFY(state->algebraId.startsWith(QStringLiteral("builtin/sedenion/")));
    QCOMPARE(state->vectors.size(),std::size_t(1));QCOMPARE(state->vectors[0].allCoefficients().size(),std::size_t(16));
    QCOMPARE(state->vectors[0].coefficient(10),2.0);QCOMPARE(state->vectors[0].coefficient(15),3.0);
    auto invalid=calculated.figureActions.back();invalid.axes[0]=16;manager.apply({invalid});QCOMPARE(errors.size(),1);
    QCOMPARE(manager.figureState(1)->vectors[0].coefficient(15),3.0);
    invalid=calculated.figureActions.back();invalid.vectors[0].originalCoefficients.resize(15);manager.apply({invalid});QCOMPARE(errors.size(),2);
    invalid=calculated.figureActions.back();invalid.basisLabels[15]=invalid.basisLabels[14];manager.apply({invalid});QCOMPARE(errors.size(),3);
    auto configure=action(runtime::FigureActionKind::ConfigurePlot);runtime::PlotStylePatch style;style.hold=true;configure.plotStyle=style;manager.apply({configure});
    invalid=calculated.figureActions.back();invalid.algebraId="table/another-definition-hash";manager.apply({invalid});QCOMPARE(errors.size(),4);
    QCOMPARE(manager.figureState(1)->vectors.size(),std::size_t(1));
}

void FigureTests::nativeHiddenDockRendersWithoutTextureErrors() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeGeneric3DCurvePlotAndExport() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeSubplotSaveRejectsReusedTarget() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeSvgGeometryAndSubplotFormats() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeSubplotSaveButtonAndCancel() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeEscapeClearsDataTip() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::nativeGenericAxesUseOuterFrame() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::subplotSelectionPreservesIndependentPanels() {
    FigureManager manager(nullptr,false);
    manager.apply({plot(3),label(runtime::FigureActionKind::SetTitle,"Original vectors"),
        subplot(2,3,5),curvePlot(),label(runtime::FigureActionKind::SetTitle,"Time series"),
        label(runtime::FigureActionKind::SetXLabel,"time"),label(runtime::FigureActionKind::SetYLabel,"coefficient")});
    auto state=manager.figureState(1);QVERIFY(state);
    QCOMPARE(state->rows,2);QCOMPARE(state->columns,3);QCOMPARE(state->activeSubplot,5);
    QCOMPARE(state->curves.size(),std::size_t(1));QVERIFY(state->vectors.empty());
    QCOMPARE(state->subplots.size(),std::size_t(2));
    QCOMPARE(state->subplots[0].index,1);QCOMPARE(state->subplots[0].dimensions,3);
    QCOMPARE(state->subplots[0].title,QStringLiteral("Original vectors"));
    QCOMPARE(state->subplots[1].index,5);QCOMPARE(state->subplots[1].axes[0],-1);
    QCOMPARE(state->subplots[1].curves[0].points.back()[0],10.0);
    manager.apply({subplot(2,3,1)});state=manager.figureState(1);
    QCOMPARE(state->dimensions,3);QCOMPARE(state->vectors[0].coefficients[4],4.0);
    QCOMPARE(state->title,QStringLiteral("Original vectors"));QVERIFY(state->curves.empty());
    manager.apply({plot()});state=manager.figureState(1);
    QCOMPARE(state->dimensions,2);QCOMPARE(state->subplots[1].title,QStringLiteral("Time series"));
    QCOMPARE(state->subplots[1].curves[0].points.size(),std::size_t(501));
    manager.apply({subplot(3,2,5)});state=manager.figureState(1);
    QCOMPARE(state->rows,3);QCOMPARE(state->columns,2);QCOMPARE(state->title,QStringLiteral("Time series"));
}

void FigureTests::subplotClearResetsGrid() {
    FigureManager manager(nullptr,false);
    auto named=action(runtime::FigureActionKind::Select,1);named.name="ODE solution";named.numberTitle=false;
    manager.apply({named,subplot(2,2,1),curvePlot(),subplot(2,2,4),plot(3),
        label(runtime::FigureActionKind::SetCaption,"old"),action(runtime::FigureActionKind::Clear)});
    const auto state=manager.figureState(1);QVERIFY(state);
    QCOMPARE(state->rows,1);QCOMPARE(state->columns,1);QCOMPARE(state->activeSubplot,1);
    QCOMPARE(state->subplots.size(),std::size_t(1));QCOMPARE(state->dimensions,0);
    QVERIFY(state->vectors.empty());QVERIFY(state->curves.empty());QVERIFY(state->caption.isEmpty());
    QCOMPARE(state->name,QStringLiteral("ODE solution"));QVERIFY(!state->numberTitle);
}

void FigureTests::invalidSubplotIsTransactional() {
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);
    for(const auto& invalid:std::vector<runtime::FigureAction>{subplot(0,2,1),subplot(2,-1,1),
        subplot(9,8,1),subplot(2,2,0),subplot(2,2,5),subplot(INT_MAX,INT_MAX,1)}){
        manager.apply({invalid});QCOMPARE(manager.figureCount(),0);
    }
    QCOMPARE(errors.size(),6);
    manager.apply({subplot(2,2,4),curvePlot(),label(runtime::FigureActionKind::SetTitle,"Keep me")});
    manager.apply({subplot(1,1,1)});QCOMPARE(errors.size(),7);
    const auto state=manager.figureState(1);QCOMPARE(state->rows,2);QCOMPARE(state->columns,2);
    QCOMPARE(state->activeSubplot,4);QCOMPARE(state->title,QStringLiteral("Keep me"));
}

void FigureTests::invalidCurvesPreserveExistingPanels() {
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);
    manager.apply({subplot(1,2,1),plot(),subplot(1,2,2),curvePlot()});
    for(int variant=0;variant<7;++variant){
        auto invalid=curvePlot();
        if(variant==0)invalid.curves[0].points.resize(1);
        else if(variant==1)invalid.curves[0].points[1][0]=std::numeric_limits<double>::quiet_NaN();
        else if(variant==2)invalid.curves[0].points[2][2]=std::numeric_limits<double>::infinity();
        else if(variant==3)invalid.vectors=plot().vectors;
        else if(variant==4)invalid.axes={{-1,0,-1}}; // Cannot mix generic and canonical coordinates.
        else if(variant==5)invalid.curves.resize(257,invalid.curves.front());
        else invalid.curves[0].points.resize(1000001);
        manager.apply({invalid});QCOMPARE(errors.size(),variant+1);
        const auto state=manager.figureState(1);
        QCOMPARE(state->activeSubplot,2);QCOMPARE(state->curves[0].points.size(),std::size_t(501));
        QCOMPARE(state->subplots[0].vectors[0].coefficients[4],4.0);
    }
}

void FigureTests::retainedCurveBudgetIsBounded() {
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);
    auto large=curvePlot();large.curves[0].points.resize(600000,{1,2,0});
    manager.apply({subplot(2,2,1),large,subplot(2,2,2),large,subplot(2,2,3),large,subplot(2,2,4),large});
    QCOMPARE(errors.size(),1);
    auto state=manager.figureState(1);QCOMPARE(state->activeSubplot,4);QCOMPARE(state->dimensions,0);
    for(std::size_t index=0;index<3;++index)QCOMPARE(state->subplots[index].curves[0].points.size(),std::size_t(600000));
    manager.apply({subplot(2,2,1),large});QCOMPARE(errors.size(),1); // Replacement releases its old budget.
    manager.apply({action(runtime::FigureActionKind::Clear),large});QCOMPARE(errors.size(),1);
    state=manager.figureState(1);QCOMPARE(state->rows,1);QCOMPARE(state->curves[0].points.size(),std::size_t(600000));
}

void FigureTests::retainedPanelBudgetIsBounded() {
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);
    for(int figure=1;figure<=2;++figure){
        manager.apply({action(runtime::FigureActionKind::Select,figure)});
        for(int index=1;index<=64;++index)manager.apply({subplot(8,8,index)});
    }
    QCOMPARE(errors.size(),0);
    manager.apply({action(runtime::FigureActionKind::SelectNew)});QCOMPARE(errors.size(),1);
    auto newPlot=curvePlot();newPlot.figure=3;manager.apply({newPlot});QCOMPARE(errors.size(),2);
    auto newLabel=label(runtime::FigureActionKind::SetTitle,"Capacity");newLabel.figure=3;
    manager.apply({newLabel});QCOMPARE(errors.size(),3);QVERIFY(!manager.containsFigure(3));
    manager.apply({subplot(8,8,1)});QCOMPARE(errors.size(),3); // Existing panels remain selectable.
    manager.apply({action(runtime::FigureActionKind::Clear),action(runtime::FigureActionKind::SelectNew)});
    QCOMPARE(errors.size(),3);QCOMPARE(manager.figureCount(),3);
}

void FigureTests::nativeSubplotCurvesExportAndCamera() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::selectionReplacementAndFullCoefficients() {
    FigureManager manager(nullptr,false);
    QCOMPARE(manager.figureCount(),0);
    manager.apply({plot()});
    QCOMPARE(manager.figureCount(),1);
    QCOMPARE(manager.currentFigureNumber(),1);
    auto state=manager.figureState(1);
    QVERIFY(state.has_value());
    QCOMPARE(state->dimensions,2);
    QCOMPARE(state->axes[0],1);QCOMPARE(state->axes[1],2);
    QCOMPARE(state->vectors.size(),std::size_t(2));
    QCOMPARE(state->vectors[0].coefficients[0],1.0);
    QCOMPARE(state->vectors[0].coefficients[4],4.0);
    QCOMPARE(state->vectors[1].coefficients[4],8.0);
    QCOMPARE(state->gamma,0.5);
    auto replacement=plot(3);
    replacement.axes={{0,1,4}};replacement.vectors.resize(1);replacement.gamma=1;
    manager.apply({replacement});
    state=manager.figureState(1);
    QCOMPARE(manager.figureCount(),1);
    QCOMPARE(state->dimensions,3);
    QCOMPARE(state->axes[0],0);QCOMPARE(state->axes[2],4);
    QCOMPARE(state->vectors.size(),std::size_t(1));
    QCOMPARE(state->vectors[0].coefficients[3],3.0); // hidden k retained
    QCOMPARE(state->gamma,1.0);
    manager.apply({action(runtime::FigureActionKind::Select,3)});
    QCOMPARE(manager.currentFigureNumber(),3);
    QCOMPARE(manager.figureCount(),2);
    manager.apply({plot()});
    QCOMPARE(manager.figureState(3)->vectors.size(),std::size_t(2));
    QCOMPARE(manager.figureState(1)->vectors.size(),std::size_t(1));
}

void FigureTests::clearCloseAndReuse() {
    FigureManager manager(nullptr,false);
    manager.apply({action(runtime::FigureActionKind::Clear)});
    QCOMPARE(manager.figureCount(),0);
    manager.apply({plot(),action(runtime::FigureActionKind::SelectNew),plot(3)});
    QCOMPARE(manager.figureCount(),2);QCOMPARE(manager.currentFigureNumber(),2);
    manager.apply({action(runtime::FigureActionKind::Clear)});
    QCOMPARE(manager.figureState(2)->dimensions,0);
    QVERIFY(manager.figureState(2)->vectors.empty());
    QCOMPARE(manager.figureState(1)->vectors.size(),std::size_t(2));
    manager.apply({action(runtime::FigureActionKind::Close,1)});
    QVERIFY(!manager.containsFigure(1));QVERIFY(manager.containsFigure(2));
    manager.apply({action(runtime::FigureActionKind::SelectNew)});
    QCOMPARE(manager.currentFigureNumber(),1); // lowest unused number, not max+1
    manager.apply({action(runtime::FigureActionKind::Close,64)});
    QCOMPARE(manager.figureCount(),2); // unknown valid id is harmless
    manager.apply({action(runtime::FigureActionKind::CloseAll)});
    QCOMPARE(manager.figureCount(),0);QCOMPARE(manager.currentFigureNumber(),0);
}

void FigureTests::invalidProjectionPreservesLastFigure() {
    FigureManager manager(nullptr,false);
    QSignalSpy errors(&manager,&FigureManager::error);
    manager.apply({plot()});
    for(int variant=0;variant<5;++variant){
        auto invalid=plot();
        if(variant==0)invalid.axes={{1,1,3}};
        else if(variant==1)invalid.axes={{1,5,3}};
        else if(variant==2)invalid.dimensions=4;
        else if(variant==3)invalid.vectors[0].coefficients[4]=std::numeric_limits<double>::infinity();
        else invalid.gamma=std::numeric_limits<double>::quiet_NaN();
        manager.apply({invalid});
        QCOMPARE(errors.size(),variant+1);
        QCOMPARE(manager.figureCount(),1);
        QCOMPARE(manager.figureState(1)->axes[1],2);
        QCOMPARE(manager.figureState(1)->vectors[0].coefficients[4],4.0);
    }
}

void FigureTests::figureAndVectorLimits() {
    FigureManager manager(nullptr,false);
    QSignalSpy errors(&manager,&FigureManager::error);
    manager.apply({action(runtime::FigureActionKind::Select,65)});
    QCOMPARE(errors.size(),1);QCOMPARE(manager.figureCount(),0);
    auto tooMany=plot();tooMany.vectors.resize(257);
    manager.apply({tooMany});QCOMPARE(errors.size(),2);QCOMPARE(manager.figureCount(),0);
    auto none=plot();none.vectors.clear();manager.apply({none});
    QCOMPARE(errors.size(),3);QCOMPARE(manager.figureCount(),0);
    for(int number=1;number<=64;++number)manager.apply({action(runtime::FigureActionKind::SelectNew)});
    QCOMPARE(manager.figureCount(),64);
    manager.apply({action(runtime::FigureActionKind::SelectNew)});
    QCOMPARE(errors.size(),4);QCOMPARE(manager.figureCount(),64);
}

void FigureTests::zeroProjectionKeepsOriginalValue() {
    FigureManager manager(nullptr,false);
    auto projection=plot();projection.vectors={{{8,0,0,7,-4},"hidden-components"}};
    manager.apply({projection});
    const auto state=manager.figureState(1);
    QCOMPARE(state->vectors[0].coefficients[0],8.0);
    QCOMPARE(state->vectors[0].coefficients[1],0.0);
    QCOMPARE(state->vectors[0].coefficients[2],0.0);
    QCOMPARE(state->vectors[0].coefficients[3],7.0);
    QCOMPARE(state->vectors[0].coefficients[4],-4.0);
}

void FigureTests::unavailableRenderingDoesNotFakeExport() {
    QTemporaryDir directory;
    FigureManager manager(nullptr,false);
    manager.apply({plot()});
    QString error;
    const QString path=directory.filePath(QStringLiteral("original.png"));
    QFile original(path);QVERIFY(original.open(QIODevice::WriteOnly));
    QCOMPARE(original.write("keep me"),qint64(7));original.close();
    QVERIFY(!manager.exportFigure(1,path,&error));
    QVERIFY(error.contains(QStringLiteral("renderer"),Qt::CaseInsensitive));
    QCOMPARE(bytes(path),QByteArray("keep me"));
    QCOMPARE(manager.figureState(1)->dimensions,2);
    QCOMPARE(manager.figureState(1)->vectors[0].coefficients[4],4.0);
    QVERIFY(!manager.exportFigure(99,path,&error));
    QCOMPARE(bytes(path),QByteArray("keep me"));
    QCOMPARE(manager.figureCount(),1);
    QVERIFY(!manager.exportFigure(1,directory.filePath(QStringLiteral("unavailable.svg")),&error));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("unavailable.svg"))));
}

void FigureTests::nativePngAndJpegExport() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FigureTests::namedFigurePropertiesAndLabels() {
    FigureManager manager(nullptr,false);manager.showTabs();
    QSignalSpy errors(&manager,&FigureManager::error);
    auto named=action(runtime::FigureActionKind::Select,2);named.name="Addition";named.numberTitle=false;
    manager.apply({named,plot(),label(runtime::FigureActionKind::SetTitle,"$p+q$"),
        label(runtime::FigureActionKind::SetXLabel,"$\\alpha_1$"),
        label(runtime::FigureActionKind::SetYLabel,"j coefficient",false),
        label(runtime::FigureActionKind::SetCaption,"$\\frac{1}{2}(p+q)$")});
    QCOMPARE(errors.size(),0);
    auto state=manager.figureState(2);
    QCOMPARE(state->name,QStringLiteral("Addition"));QVERIFY(!state->numberTitle);
    QCOMPARE(state->title,QStringLiteral("$p+q$"));QVERIFY(state->titleLatex);
    QCOMPARE(state->xLabel,QStringLiteral("$\\alpha_1$"));
    QCOMPARE(state->yLabel,QStringLiteral("j coefficient"));QVERIFY(!state->yLabelLatex);
    auto* tabs=manager.findChild<QTabWidget*>(QStringLiteral("figureTabs"));
    QVERIFY(tabs);QCOMPARE(tabs->tabText(0),QStringLiteral("Addition"));
    manager.apply({action(runtime::FigureActionKind::Select,2)});
    QCOMPARE(manager.figureState(2)->name,QStringLiteral("Addition")); // no options preserves properties
    manager.apply({label(runtime::FigureActionKind::SetTitle,"$\\frac{1}{")});
    QCOMPARE(errors.size(),1);
    QCOMPARE(manager.figureState(2)->title,QStringLiteral("$p+q$"));
    manager.apply({plot(3)});
    QCOMPARE(manager.figureState(2)->title,QStringLiteral("$p+q$"));
    manager.apply({action(runtime::FigureActionKind::Clear)});
    state=manager.figureState(2);
    QCOMPARE(state->name,QStringLiteral("Addition"));QVERIFY(!state->numberTitle);
    QVERIFY(state->title.isEmpty());QVERIFY(state->xLabel.isEmpty());QVERIFY(state->caption.isEmpty());
    auto clearName=action(runtime::FigureActionKind::Select,2);clearName.name="";clearName.numberTitle=true;
    manager.apply({clearName});QVERIFY(manager.figureState(2)->name.isEmpty());
    QCOMPARE(tabs->tabText(0),QStringLiteral("Figure 2"));
}

void FigureTests::dockingTilingAndInvalidLayoutSafety() {
    FigureManager manager(nullptr,false);
    manager.apply({plot(),action(runtime::FigureActionKind::Select,2),plot(3),
                   action(runtime::FigureActionKind::Select,3),plot()});
    QString error;
    QVERIFY(manager.undockFigure(1,&error));
    QVERIFY(!manager.figureState(1)->docked);QVERIFY(manager.figureState(2)->docked);
    QVERIFY(!manager.tileStudio(1,1,&error)); // all validations precede any reparenting
    QVERIFY(!manager.figureState(1)->docked);QVERIFY(manager.figureState(2)->docked);
    QCOMPARE(manager.figureCount(),3);
    QVERIFY(!manager.tileDesktop(1,1,&error));
    QVERIFY(!manager.figureState(1)->docked);QVERIFY(manager.figureState(2)->docked);
    QVERIFY2(manager.tileStudio(2,2,&error),qPrintable(error));
    for(int number=1;number<=3;++number)QVERIFY(manager.figureState(number)->docked);
    auto* host=manager.findChild<QWidget*>(QStringLiteral("figureTileHost"));QVERIFY(host);
    QCOMPARE(host->layout()->count(),3);
    QCOMPARE(manager.figureState(1)->vectors[0].coefficients[4],4.0);
    manager.showTabs();
    QCOMPARE(manager.findChild<QTabWidget*>(QStringLiteral("figureTabs"))->count(),3);
    QVERIFY(!manager.dockFigure(64,&error));QCOMPARE(manager.figureCount(),3);
    QVERIFY(!manager.undockFigure(65,&error));QCOMPARE(manager.figureCount(),3);
}

void FigureTests::closingFloatingFigureAndOwnerIsSafe() {
    QPointer<QMainWindow> floating;
    {
        FigureManager manager(nullptr,false);manager.showTabs();
        manager.apply({plot(),action(runtime::FigureActionKind::SelectNew),plot()});
        QString error;QVERIFY(manager.undockFigure(1,&error));
        floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));QVERIFY(floating);
        floating->close();
        QTRY_VERIFY(!manager.containsFigure(1));QVERIFY(floating.isNull());
        QVERIFY(manager.containsFigure(2));
        QVERIFY(manager.undockFigure(2,&error));
        floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure2"));QVERIFY(floating);
    }
    QVERIFY(floating.isNull()); // manager owns every floating figure, no orphan windows
}

void FigureTests::queuedCloseDoesNotCloseReusedFigureNumber() {
    FigureManager manager(nullptr,false);manager.showTabs();manager.apply({plot()});
    QString error;QVERIFY(manager.undockFigure(1,&error));
    auto* floating=manager.findChild<QMainWindow*>(QStringLiteral("floatingFigure1"));QVERIFY(floating);
    floating->close(); // queues the old window's close callback
    manager.apply({action(runtime::FigureActionKind::Close,1),action(runtime::FigureActionKind::Select,1),plot(3)});
    QCoreApplication::processEvents();
    QVERIFY(manager.containsFigure(1));QCOMPARE(manager.figureCount(),1);
    QCOMPARE(manager.figureState(1)->dimensions,3);
    QVERIFY(manager.figureState(1)->docked);
}

void FigureTests::nativeDockTileAndMathLabels() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

PENTA_OFFSCREEN_TEST_MAIN(FigureTests)

#include "figure_tests.moc"
