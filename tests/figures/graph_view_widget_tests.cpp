#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "plotting/graph_view.hpp"
#include <QAction>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>
namespace {
penta::runtime::FigureAction graph(){penta::runtime::FigureAction a;a.kind=penta::runtime::FigureActionKind::Plot;a.dimensions=2;a.axes={-1,-1,-1};a.algebraId="A_gamma_v1";a.gamma=1;penta::runtime::PlotGraph g;g.directed=true;g.layout="manual";g.positions={{0,0,0},{1,0,0},{1,1,0}};g.edges={{1,2},{2,3}};g.weights={2,3};g.labels={"start","corner","end"};a.graph=std::move(g);return a;}
}
class GraphViewWidgetTests:public QObject{
    Q_OBJECT
private slots:
    void moveAndSelectKeepScientificTopology(){
        penta::FigureManager manager(nullptr,false);manager.apply({graph()});QString error;QVERIFY(manager.moveGraphNode(1,1,2,{.5,.25,0},&error));QVERIFY(manager.selectGraphItems(1,1,{2,3},{1},&error));const auto state=manager.figureState(1);QVERIFY(state&&state->graph);QCOMPARE(state->graph->positions[1],(std::array<double,3>{.5,.25,0}));QCOMPARE(state->graph->edges,graph().graph->edges);QCOMPARE(state->graph->weights,graph().graph->weights);QCOMPARE(state->graph->labels,graph().graph->labels);QCOMPARE(state->graph->selectedNodes,(std::vector<std::uint32_t>{2,3}));QCOMPARE(state->graph->selectedEdges,(std::vector<std::uint32_t>{1}));QVERIFY(state->curves.empty()&&state->vectors.empty()&&!state->mesh);
    }
    void invalidEditsAreTransactional(){
        penta::FigureManager manager(nullptr,false);manager.apply({graph()});QString error;const auto before=manager.figureState(1)->graph->positions;QVERIFY(!manager.moveGraphNode(1,1,4,{0,0,0},&error));QVERIFY(!manager.moveGraphNode(1,1,1,{std::numeric_limits<double>::infinity(),0,0},&error));QVERIFY(!manager.selectGraphItems(1,1,{1,1},{},&error));QVERIFY(!manager.selectGraphItems(1,1,{}, {3},&error));QCOMPARE(manager.figureState(1)->graph->positions,before);QVERIFY(manager.figureState(1)->graph->selectedNodes.empty());
    }
    void versionedSavedLayoutPreservesStableAssociations(){
        penta::FigureManager manager(nullptr,false);manager.apply({graph()});QTemporaryDir folder;QVERIFY(folder.isValid());const auto path=folder.filePath("view.json");QString error;QVERIFY(manager.moveGraphNode(1,1,2,{.4,.6,0},&error));QVERIFY(manager.selectGraphItems(1,1,{2},{1},&error));QVERIFY(manager.saveGraphLayout(1,1,path,&error));QVERIFY(manager.setGraphLayout(1,1,"random",123,&error));QVERIFY(manager.loadGraphLayout(1,1,path,&error));QCOMPARE(manager.figureState(1)->graph->positions[1],(std::array<double,3>{.4,.6,0}));QCOMPARE(manager.figureState(1)->graph->selectedEdges,(std::vector<std::uint32_t>{1}));
        auto changed=graph();changed.graph->weights[0]=9;manager.apply({changed});const auto before=manager.figureState(1)->graph->positions;QVERIFY(!manager.loadGraphLayout(1,1,path,&error));QCOMPARE(manager.figureState(1)->graph->positions,before);QCOMPARE(manager.figureState(1)->graph->weights[0],9.);
    }
    void subplotAndCameraMetadataSurviveLayout(){
        penta::FigureManager manager(nullptr,false);auto a=graph();manager.apply({a});penta::runtime::FigureAction label;label.kind=penta::runtime::FigureActionKind::SetTitle;label.text="An annotated network";manager.apply({label});QString error;QVERIFY(manager.setGraphLayout(1,1,"grid",0,&error));QCOMPARE(manager.figureState(1)->title,QString("An annotated network"));QCOMPARE(manager.figureState(1)->graph->edges,a.graph->edges);QCOMPARE(manager.figureState(1)->gamma,1.);
        penta::runtime::FigureAction subplot;subplot.kind=penta::runtime::FigureActionKind::Subplot;subplot.rows=1;subplot.columns=2;subplot.subplotIndex=2;manager.apply({subplot,a});QVERIFY(manager.moveGraphNode(1,2,1,{8,9,0},&error));const auto snapshot=manager.figureState(1);QCOMPARE(snapshot->subplots.size(),std::size_t(2));QVERIFY(snapshot->subplots[0].graph->positions[0]!=snapshot->subplots[1].graph->positions[0]);
    }
    void staticFallbackAndToolAvailability(){
        penta::FigureManager manager(nullptr,false);auto a=graph();a.graph->positions=penta::plotting::graphPositions(1025,2,"circle");a.graph->labels.resize(1025);a.graph->interactive=false;a.graph->nodeLabels=false;manager.apply({a});QString error;QVERIFY(manager.figureState(1)->graph);QVERIFY(!manager.moveGraphNode(1,1,1,{0,0,0},&error));QVERIFY(!manager.setPlotTool(1,1,penta::PlotTool::GraphDrag,&error));auto* action=manager.findChild<QAction*>("plotGraphDrag");QVERIFY(action&&action->isVisible()&&!action->isEnabled());
    }
    void numericReplacementDropsGraphAndRestoresTools(){
        penta::FigureManager manager(nullptr,false);manager.apply({graph()});penta::runtime::FigureAction numeric;numeric.kind=penta::runtime::FigureActionKind::Plot;numeric.dimensions=2;numeric.axes={-1,-1,-1};numeric.algebraId="A_gamma_v1";numeric.gamma=1;numeric.curves.push_back({{{0,0,0},{1,1,0}},"replacement"});manager.apply({numeric});QVERIFY(!manager.figureState(1)->graph);QCOMPARE(manager.figureState(1)->curves[0].label,std::string("replacement"));QVERIFY(!manager.findChild<QAction*>("plotGraphDrag")->isVisible());
    }
};
PENTA_OFFSCREEN_TEST_MAIN(GraphViewWidgetTests)
#include "graph_view_widget_tests.moc"
