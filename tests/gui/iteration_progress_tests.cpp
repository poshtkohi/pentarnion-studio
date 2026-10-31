#include "offscreen_test_bootstrap.hpp"
#include "ide/raster_plot_canvas.hpp"
#include "main_window.hpp"
#include "execution_coordinator.hpp"
#include "figure_manager.hpp"
#include "editor_widget.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace penta;
namespace {
struct ProgressProbe {
    int messages=0,modelUpdatesWhileWorking=0;bool finished=false,wrongThread=false;
    std::vector<int> figureCounts;
};
bool nativeRendering(){return false; /* Desktop screenshot/stress opt-ins are disabled. */}
bool saveStudioScreenshot(MainWindow& window,const QString& path){QPixmap screenshot=window.grab();{QPainter painter(&screenshot);for(auto* canvas:window.findChildren<penta::RasterPlotCanvas*>())if(canvas->isVisibleTo(&window)){const auto frame=canvas->grabFramebuffer();if(frame.isNull())return false;painter.drawImage(QRect(canvas->mapTo(&window,QPoint(0,0)),canvas->size()),frame);}}return screenshot.save(path,"PNG");}
void observeProgress(MainWindow& window,ProgressProbe& probe){
    auto* coordinator=window.findChild<ExecutionCoordinator*>();
    QObject::connect(coordinator,&ExecutionCoordinator::figureProgress,&window,[&window,&probe](const FigureProgress& value){
        ++probe.messages;probe.wrongThread|=QThread::currentThread()!=window.thread();probe.figureCounts.push_back(window.figureManager()->figureCount());
        if(!probe.finished&&window.isWorking()&&!value.actions.empty()&&window.figureManager()->figureCount()>0){const auto figure=window.figureManager()->figureState(window.figureManager()->currentFigureNumber());if(figure&&!figure->curves.empty())++probe.modelUpdatesWhileWorking;}
    });
    QObject::connect(&window,&MainWindow::workFinished,&window,[&probe](bool execution,bool){if(execution)probe.finished=true;});
}
bool sameCurve(const FigureState& a,const FigureState& b){
    if(a.curves.size()!=1||b.curves.size()!=1)return false;
    return a.curves.front().points==b.curves.front().points;
}
}

class IterationProgressTests final:public QObject {
    Q_OBJECT
private slots:
    void anonymousFigureStreamsBeforeFinishAndIsNotReplayed();
    void ceresFittedModelSurvivesCleanStopAndLaterOrdinaryPlot();
    void obsoleteProgressCannotPublishFigures();
};

void IterationProgressTests::anonymousFigureStreamsBeforeFinishAndIsNotReplayed(){
    const bool native=nativeRendering();QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,native);window.configureAutosave(false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("IterationLive"),&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),15000);
    QVERIFY(window.findChild<ExecutionCoordinator*>());QCOMPARE(window.figureManager()->renderingEnabled(),native);if(native){window.resize(1650,1000);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));}const WId identity=native?window.winId():0;
    ProgressProbe probe;observeProgress(window,probe);QSignalSpy finished(&window,&MainWindow::workFinished);QSignalSpy changes(window.figureManager(),&FigureManager::figuresChanged);
    const QString source=QString::fromLatin1(R"PENTA(
close("all");
native function real objective_live(real a,real b){return (1-a)^2+100*(b-a*a)^2;}
function integer monitor_live(matrix x,record info=iterationinfo(),string state="iter"){
    matrix t=[0,1,2];
    if(state=="init"){figure();}
    plot(t,x(1)*t+x(2));
    if(state=="init"){title("OutputFcn init");}
    if(state=="iter"){title("OutputFcn iterate");}
    if(state=="done"){title("OutputFcn done");}
    if(state=="iter"&&info.funccount>=6){return 1;}
    return 0;
}
record live=minimize(objective_live,[-1.2,1.0],options(algorithm="lbfgs",OutputFcn=monitor_live,max_evaluations=200));
assert(live.status=="output_function_stop");assert(!live.converged);
matrix final_t=[0,1,2];
figure();plot(final_t,live.x(1)*final_t+live.x(2));title("Ordinary plot after stop");
display("live-iteration-clean-stop");
)PENTA");
    if(native){window.currentEditor()->setPlainText(source);QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));}
    QVERIFY2(window.submitCommand(source,&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),120000);
    QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));
    QVERIFY2(window.resultsText().contains(QStringLiteral("live-iteration-clean-stop")),qPrintable(window.resultsText()));
    QVERIFY(probe.messages>0);QVERIFY(probe.modelUpdatesWhileWorking>0);QVERIFY(!probe.wrongThread);QVERIFY(probe.finished);QCOMPARE(finished.size(),1);
    QVERIFY(changes.size()>=2);QVERIFY(std::all_of(probe.figureCounts.begin(),probe.figureCounts.end(),[](int count){return count==1;}));
    // An anonymous SelectNew action replayed with the final execution result
    // would create an additional figure and direct the final plot to the wrong tab.
    QCOMPARE(window.figureManager()->figureCount(),2);QVERIFY(window.figureManager()->containsFigure(1));QVERIFY(window.figureManager()->containsFigure(2));
    const auto liveFigure=window.figureManager()->figureState(1),ordinaryFigure=window.figureManager()->figureState(2);QVERIFY(liveFigure&&ordinaryFigure);
    QCOMPARE(liveFigure->title,QStringLiteral("OutputFcn done"));QCOMPARE(ordinaryFigure->title,QStringLiteral("Ordinary plot after stop"));QVERIFY(sameCurve(*liveFigure,*ordinaryFigure));
    QCOMPARE(liveFigure->curves.front().points.size(),std::size_t(3));
    QCoreApplication::sendPostedEvents();QCoreApplication::processEvents();QCOMPARE(window.figureManager()->figureCount(),2); // No delayed replay after completion.
    if(native){QCOMPARE(window.winId(),identity);QVERIFY(window.isVisible());for(auto* canvas:window.findChildren<penta::RasterPlotCanvas*>())if(canvas->isVisibleTo(&window))QTRY_VERIFY_WITH_TIMEOUT(canvas->isValid(),10000);}
    QVERIFY(window.close());
}

void IterationProgressTests::ceresFittedModelSurvivesCleanStopAndLaterOrdinaryPlot(){
    const bool native=nativeRendering();QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,native);window.configureAutosave(false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("FittedModelLive"),&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),15000);
    if(native){window.resize(1650,1000);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));}const WId identity=native?window.winId():0;
    ProgressProbe probe;observeProgress(window,probe);
    const QString source=QString::fromLatin1(R"PENTA(
close("all");
native function [real r0,real r1,real r2] residual_live(real a,real b){return b+1,a+b-1,2*a+b-3;}
function integer fit_monitor_live(matrix x,record info=iterationinfo(),string state="iter"){
    matrix t=[0,1,2];figure(8);plot(t,x(1)*t+x(2));
    if(state=="init"){title("Initial fitted model");}
    if(state=="iter"){title("Accepted fitted model");}
    if(state=="done"){title("Stopped fitted model");}
    if(state=="iter"&&info.iteration>=1){return 1;}
    return 0;
}
record fitted=lsqfit(residual_live,[-3.0,4.0],options(OutputFcn=fit_monitor_live,max_iterations=50));
assert(fitted.status=="output_function_stop");assert(!fitted.converged);
figure(9);plot([10,20],[30,40]);title("Ordinary plot retained");
display("ceres-live-model-stopped");
)PENTA");
    if(native){window.currentEditor()->setPlainText(source);QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));}
    QVERIFY2(window.submitCommand(source,&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),120000);
    QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));QVERIFY(probe.messages>0);QVERIFY(probe.modelUpdatesWhileWorking>0);QVERIFY(!probe.wrongThread);
    QVERIFY2(window.resultsText().contains(QStringLiteral("ceres-live-model-stopped")),qPrintable(window.resultsText()));QCOMPARE(window.figureManager()->figureCount(),2);
    const auto model=window.figureManager()->figureState(8),ordinary=window.figureManager()->figureState(9);QVERIFY(model&&ordinary);QCOMPARE(model->title,QStringLiteral("Stopped fitted model"));QCOMPARE(ordinary->title,QStringLiteral("Ordinary plot retained"));
    QCOMPARE(model->curves.size(),std::size_t(1));QCOMPARE(model->curves.front().points.size(),std::size_t(3));
    // The initial intercept is4. A real accepted Ceres update must replace it.
    QVERIFY(std::abs(model->curves.front().points.front()[1]-4)>1);
    QCOMPARE(ordinary->curves.size(),std::size_t(1));QCOMPARE(ordinary->curves.front().points.size(),std::size_t(2));
    QCOMPARE(ordinary->curves.front().points[0][0],10.0);QCOMPARE(ordinary->curves.front().points[0][1],30.0);QCOMPARE(ordinary->curves.front().points[1][0],20.0);QCOMPARE(ordinary->curves.front().points[1][1],40.0);
    if(native){QCOMPARE(window.winId(),identity);QVERIFY(window.isVisible());QVERIFY2(window.figureManager()->tileStudio(1,2,&error),qPrintable(error));QCoreApplication::processEvents();for(auto* canvas:window.findChildren<penta::RasterPlotCanvas*>())if(canvas->isVisibleTo(&window))QTRY_VERIFY_WITH_TIMEOUT(canvas->isValid(),10000);const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");if(!artifacts.isEmpty()){QVERIFY(QDir().mkpath(artifacts));QTest::qWait(100);QVERIFY(saveStudioScreenshot(window,QDir(artifacts).filePath(QStringLiteral("live-fit-studio.png"))));}}
    QVERIFY(window.close());
}

void IterationProgressTests::obsoleteProgressCannotPublishFigures(){
    const bool native=nativeRendering();MainWindow window(false,nullptr,native);if(native){window.resize(1400,900);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));}auto* coordinator=window.findChild<ExecutionCoordinator*>();QVERIFY(coordinator);QVERIFY(!window.isWorking());
    FigureProgress obsolete;obsolete.requestId=std::numeric_limits<quint64>::max();obsolete.snapshot.command=true;runtime::FigureAction select;select.kind=runtime::FigureActionKind::Select;select.figure=55;obsolete.actions.push_back(select);runtime::FigureAction plot;plot.kind=runtime::FigureActionKind::Plot;plot.dimensions=2;plot.axes={{-1,-1,-1}};runtime::PlotCurve curve;curve.points={{{0,1,0}},{{1,2,0}}};plot.curves.push_back(curve);obsolete.actions.push_back(plot);
    coordinator->figureProgress(obsolete);QCoreApplication::processEvents();QCOMPARE(window.figureManager()->figureCount(),0);QVERIFY(!window.figureManager()->containsFigure(55));
    QString error;QVERIFY2(window.submitCommand(QStringLiteral("figure(3);plot([0,1],[2,3]);"),&error),qPrintable(error));QVERIFY(window.isWorking());coordinator->figureProgress(obsolete);QVERIFY(!window.figureManager()->containsFigure(55));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QCOMPARE(window.problemCount(),0);QCOMPARE(window.figureManager()->figureCount(),1);
    coordinator->figureProgress(obsolete);QCoreApplication::processEvents();QCOMPARE(window.figureManager()->figureCount(),1);QVERIFY(!window.figureManager()->containsFigure(55));
}

PENTA_OFFSCREEN_TEST_MAIN(IterationProgressTests)

#include "iteration_progress_tests.moc"
