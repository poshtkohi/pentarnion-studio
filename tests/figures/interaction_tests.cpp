#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "language/frontend.hpp"
#include "runtime/interpreter.hpp"

#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>

#ifndef PENTA_SOURCE_DIR
#define PENTA_SOURCE_DIR "."
#endif

using namespace penta;
namespace {
runtime::FigureAction command(runtime::FigureActionKind kind,int figure=0) {
    runtime::FigureAction action;action.kind=kind;action.figure=figure;return action;
}
runtime::FigureAction panel(int index) {
    auto action=command(runtime::FigureActionKind::Subplot);action.rows=1;action.columns=2;action.subplotIndex=index;return action;
}
runtime::FigureAction sampledPlot(int dimensions=2) {
    auto action=command(runtime::FigureActionKind::Plot);action.dimensions=dimensions;
    action.axes=dimensions==2?std::array<int,3>{{-1,-1,-1}}:std::array<int,3>{{1,2,4}};
    action.algebraId="A_gamma_v1";action.gamma=.5;
    action.curves={{{{10,-30,-2},{14,-10,1},{18,-27,3},{22,-12,-1},{26,-24,2},{30,-16,-3},{34,-20,0}},"measured alpha"},
                   {{{11,-34,1},{20,-32,-1},{32,-31,0}},"reference"}};
    return action;
}
}

class InteractionTests final:public QObject {
    Q_OBJECT
private slots:
    void headlessStateAndValidation();
    void nativeToolbarZoomPanResetAndFxaa();
    void nativeCursorSelectsStoredVertices();
    void nativeCursorRejectsOccludedVertices();
    void nativeIndependentPanelsAndDocking();
    void nativeFxaaChangesLineCoverage();
    void nativeSampleMarkersAndPaperDensity();
    void nativeMultipleDataTipsCanBeSelectedRemovedAndExported();
};

void InteractionTests::headlessStateAndValidation() {
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);
    manager.apply({panel(1),sampledPlot(),panel(2),sampledPlot(3)});QCOMPARE(errors.count(),0);
    QString error;
    QVERIFY(manager.setPlotTool(1,1,PlotTool::Pan,&error));QVERIFY(error.isEmpty());
    QVERIFY(manager.setDataPointsVisible(1,1,true,&error));QVERIFY(manager.dataPointsVisible(1,1));
    QVERIFY(!manager.dataPointsVisible(1,2));
    QVERIFY(manager.plotTool(1,1).has_value());QCOMPARE(static_cast<int>(*manager.plotTool(1,1)),static_cast<int>(PlotTool::Pan));
    QVERIFY(!manager.setPlotTool(1,1,PlotTool::Rotate,&error));QVERIFY(!error.isEmpty());
    QCOMPARE(static_cast<int>(*manager.plotTool(1,1)),static_cast<int>(PlotTool::Pan));
    QVERIFY(manager.setPlotTool(1,2,PlotTool::Rotate,&error));
    QVERIFY(!manager.zoomPlot(1,1,2,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.resetPlotView(1,1,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.pickPlotPoint(1,1,{20,20}));
    QVERIFY(!manager.setDataPointsVisible(1,64,true,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.setPlotTool(64,1,PlotTool::Pan,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.zoomPlot(1,1,0,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.zoomPlot(1,1,std::numeric_limits<double>::infinity(),&error));QVERIFY(!error.isEmpty());
    const auto state=manager.figureState(1);QVERIFY(state);QCOMPARE(state->subplots.size(),std::size_t(2));
    QVERIFY(state->subplots[0].curves[0].points==sampledPlot().curves[0].points);
    QVERIFY(manager.dataTips(1,1).empty());QVERIFY(manager.clearDataTips(1,1,&error));
    QVERIFY(!manager.removeSelectedDataTip(1,1,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!manager.clearDataTips(99,1,&error));QVERIFY(!error.isEmpty());
}

void InteractionTests::nativeMultipleDataTipsCanBeSelectedRemovedAndExported() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeToolbarZoomPanResetAndFxaa() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeCursorSelectsStoredVertices() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeIndependentPanelsAndDocking() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeCursorRejectsOccludedVertices() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeFxaaChangesLineCoverage() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void InteractionTests::nativeSampleMarkersAndPaperDensity() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

PENTA_OFFSCREEN_TEST_MAIN(InteractionTests)

#include "interaction_tests.moc"
