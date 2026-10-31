#include "offscreen_test_bootstrap.hpp"
#include "ide/main_window.hpp"
#include "ide/document.hpp"
#include "ide/editor_widget.hpp"
#include "ide/find_replace_dialog.hpp"
#include "ide/desktop/studio_workspace_layout.hpp"
#include "ide/desktop/snapshot_controller.hpp"
#include "ide/desktop/parallel_pool_indicator.hpp"
#include "ide/desktop/worker_activity.hpp"
#include "ide/desktop/about_dialog.hpp"
#include "ide/assistant/offline_help.hpp"
#include "runtime/thread_budget.hpp"
#include <QAction>
#include <QAbstractButton>
#include <QClipboard>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace penta;
class DesktopRefinementTests final:public QObject {
    Q_OBJECT
private slots:
    void layoutKeepsCopilotOutsideFiguresAndCommand();
    void closeAllDocumentsPreservesProjectAndCancelKeepsTabs();
    void snapshotUsesOnlyStudioPixelsAndSupportsDprCrop();
    void workerIndicatorUsesLiveBudgetAndStopsWhenIdle();
    void findIsModelessAndFollowsDocuments();
    void boundedOffscreenReviewImages();
};
void DesktopRefinementTests::layoutKeepsCopilotOutsideFiguresAndCommand(){
    MainWindow window(false,nullptr,false);window.resize(1100,720);window.show();
    auto* layout=window.findChild<desktop::StudioWorkspaceLayout*>();QVERIFY(layout);
    auto* command=window.findChild<QDockWidget*>("resultsDock");auto* figures=window.findChild<QDockWidget*>("figuresDock");auto* copilot=window.findChild<QDockWidget*>("copilotDock");QVERIFY(command&&figures&&copilot);
    figures->show();copilot->show();window.resizeDocks({copilot},{280},Qt::Horizontal);QApplication::processEvents();
    QCOMPARE(figures->parentWidget(),layout->workingHost());QCOMPARE(command->parentWidget(),layout->workingHost());QCOMPARE(copilot->parentWidget(),&window);
    const auto original=copilot->geometry();layout->workingHost()->resizeDocks({figures},{200},Qt::Horizontal);QApplication::processEvents();
    QCOMPARE(copilot->geometry(),original);QVERIFY(command->mapTo(&window,QPoint(command->width(),0)).x()<copilot->x());
    QCOMPARE(window.corner(Qt::BottomRightCorner),Qt::RightDockWidgetArea);
    QVERIFY(!window.findChild<QTimer*>("parallelExecutionPulse")->isActive());
}
void DesktopRefinementTests::closeAllDocumentsPreservesProjectAndCancelKeepsTabs(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;QVERIFY(window.createProjectAt(temporary.path(),"Close all",&error));window.configureAutosave(false);
    QVERIFY(window.addSource("second.p",&error));window.currentEditor()->insertPlainText("display(2);\n");
    auto* source=window.currentDocument();QTimer::singleShot(0,&window,[&window]{auto* prompt=window.findChild<QMessageBox*>("dirtyClosePrompt");QVERIFY(prompt);prompt->button(QMessageBox::Cancel)->click();});
    QVERIFY(!window.closeAllDocuments());QCOMPARE(window.documentCount(),2);QCOMPARE(window.currentDocument(),source);
    QVERIFY(window.saveAllDocuments(&error));QVERIFY(window.closeAllDocuments());QCOMPARE(window.documentCount(),0);QVERIFY(window.activeProject());
}
void DesktopRefinementTests::snapshotUsesOnlyStudioPixelsAndSupportsDprCrop(){
    QWidget owner;owner.resize(180,120);owner.setStyleSheet("background:#123456");owner.show();
    desktop::SnapshotController snapshots(&owner);QWidget foreign;foreign.resize(40,30);foreign.show();snapshots.captureWidget(&foreign);QVERIFY(snapshots.latestImage().isNull());
    snapshots.captureWidget(&owner);QVERIFY(!snapshots.latestImage().isNull());QCOMPARE(QApplication::clipboard()->image().size(),snapshots.latestImage().size());
    QImage image(100,80,QImage::Format_RGB32);image.fill(Qt::red);image.setDevicePixelRatio(2);
    const auto crop=desktop::SnapshotController::cropImage(image,QRectF(5,4,10,8));QCOMPARE(crop.size(),QSize(20,16));QCOMPARE(crop.devicePixelRatio(),qreal(2));
    QTemporaryDir temporary;QString error;QVERIFY(snapshots.saveImage(temporary.filePath("snapshot.png"),&error));QVERIFY(!snapshots.saveImage(temporary.filePath("snapshot.jpg"),&error));
}
void DesktopRefinementTests::workerIndicatorUsesLiveBudgetAndStopsWhenIdle(){
    desktop::ParallelPoolIndicator indicator;indicator.show();indicator.beginRequest(1,"Scientific execution");
    struct Gate {std::mutex mutex;std::condition_variable changed;bool release=false;} gate;
    runtime::ParallelSettings settings;settings.threads=3;settings.serialThreshold=1;runtime::ParallelStatistics stats;std::string error;int result=-1;
    auto task=[](void* context,std::size_t,std::size_t,char*,std::size_t)->int{auto& state=*static_cast<Gate*>(context);std::unique_lock lock(state.mutex);state.changed.wait(lock,[&]{return state.release;});return PENTA_NUMERIC_OK;};
    std::jthread driver([&]{result=runtime::ThreadBudget::instance().run(1536,1,task,&gate,settings,{},stats,error);});
    struct Release {Gate& gate;~Release(){{std::lock_guard lock(gate.mutex);gate.release=true;}gate.changed.notify_all();}} release{gate};
    QTRY_COMPARE_WITH_TIMEOUT(runtime::ThreadBudget::instance().activity().active,std::size_t(3),2000);
    const auto sample=desktop::sampleWorkerActivity();QCOMPARE(sample.workers(),std::size_t(3));indicator.setWorkers(1,sample.workers(),"Parallel numerical task");QCOMPARE(indicator.workers(),std::size_t(3));QVERIFY(indicator.active());QVERIFY(indicator.toolTip().contains("3 parallel workers"));
    {std::lock_guard lock(gate.mutex);gate.release=true;}gate.changed.notify_all();driver.join();QCOMPARE(result,PENTA_NUMERIC_OK);QCOMPARE(runtime::ThreadBudget::instance().activity().active,std::size_t(0));
    indicator.finishRequest(1);QCOMPARE(indicator.workers(),std::size_t(0));QVERIFY(!indicator.animationRunning());QVERIFY(!indicator.active());
    desktop::WorkerActivity reservation;reservation.blasTeamWidth=4;reservation.blasOwners=1;
    QCOMPARE(reservation.workers(),std::size_t(0));QCOMPARE(reservation.reservedWorkers(),std::size_t(4));
    indicator.beginRequest(2,"Native backend");indicator.setWorkerReservation(2,4,"BLAS team reservation");QVERIFY(indicator.toolTip().contains("Up to 4"));QVERIFY(!indicator.active());indicator.finishRequest(2);
    desktop::WorkerActivityMonitor monitor;QCOMPARE(monitor.snapshot().reservedWorkers(),std::size_t(0));monitor.setRunning(true);monitor.setRunning(false);QCOMPARE(monitor.snapshot().reservedWorkers(),std::size_t(0));
}
void DesktopRefinementTests::findIsModelessAndFollowsDocuments(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;QVERIFY(window.createProjectAt(temporary.path(),"Find",&error));window.configureAutosave(false);window.show();
    window.currentEditor()->insertPlainText("real alpha=1;\n");window.findChild<QAction*>("editFindReplace")->trigger();auto* find=window.findChild<FindReplaceDialog*>();QVERIFY(find&&find->isVisible());QVERIFY(!find->isModal());QCOMPARE(find->windowType(),Qt::Tool);
    QVERIFY(window.addSource("second.p",&error));QVERIFY(find->isVisible());QVERIFY(window.currentEditor()->isEnabled());
    QVERIFY(window.saveAllDocuments(&error));find->hide();QVERIFY(window.close());
}
void DesktopRefinementTests::boundedOffscreenReviewImages(){
    const auto directory=qEnvironmentVariable("PENTA_GUI_REBUILD_ARTIFACTS");
    if(directory.isEmpty())return;
    QVERIFY(QDir().mkpath(directory));
    desktop::AboutDialog about;about.show();QApplication::processEvents();
    QVERIFY(about.grab().save(directory+"/refined-about.png"));about.hide();
    assistant::HelpDialog help;help.openLanguageReference();help.resize(1000,680);help.show();QApplication::processEvents();
    QVERIFY(help.grab().save(directory+"/refined-language-help.png"));help.hide();
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),"Desktop layout",&error));window.configureAutosave(false);
    window.currentEditor()->insertPlainText("use_algebra(\"A_gamma\", gamma=1);\npentarnion p=1+i+j;\ndisplay(p*p);\n");
    window.resize(1100,720);window.show();auto* copilot=window.findChild<QDockWidget*>("copilotDock");QVERIFY(copilot);copilot->show();
    window.resizeDocks({copilot},{300},Qt::Horizontal);QApplication::processEvents();
    QVERIFY(window.grab().save(directory+"/refined-workspace.png"));QVERIFY(window.saveAllDocuments(&error));QVERIFY(window.close());
}
PENTA_OFFSCREEN_TEST_MAIN(DesktopRefinementTests)
#include "desktop_refinement_tests.moc"
