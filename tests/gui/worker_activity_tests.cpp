#include "ide/desktop/worker_activity.hpp"
#include "runtime/thread_budget.hpp"
#include <QtTest>
#include <chrono>
#include <limits>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
bool blasLoaded(){
#ifdef _WIN32
    return GetModuleHandleW(L"penta_blas.dll")!=nullptr;
#elif defined(RTLD_NOLOAD)
    auto* module=dlopen("libpenta_blas.so",RTLD_LAZY|RTLD_NOLOAD);
    const bool loaded=module!=nullptr;if(module)dlclose(module);return loaded;
#else
    return false;
#endif
}
}
class WorkerActivityTests final:public QObject {
    Q_OBJECT
private slots:
    void reservationsDoNotMasqueradeAsActiveWorkers();
    void constructionAndPoolSnapshotsDoNotLoadBlas();
    void activeSamplerPausesWithoutLoadingAModule();
};
void WorkerActivityTests::reservationsDoNotMasqueradeAsActiveWorkers(){
    penta::desktop::WorkerActivity sample;sample.blasTeamWidth=4;sample.blasOwners=2;
    QCOMPARE(sample.workers(),std::size_t(0));QCOMPARE(sample.reservedWorkers(),std::size_t(8));
    sample.poolActive=3;QCOMPARE(sample.workers(),std::size_t(3));
    sample.blasTeamWidth=std::numeric_limits<std::size_t>::max();sample.blasOwners=std::numeric_limits<std::size_t>::max();
    QCOMPARE(sample.reservedWorkers(),std::size_t(64));QCOMPARE(sample.workers(),std::size_t(3));
    sample.blasTeamWidth=0;QCOMPARE(sample.reservedWorkers(),std::size_t(0));
}
void WorkerActivityTests::constructionAndPoolSnapshotsDoNotLoadBlas(){
    const bool loaded=blasLoaded();penta::desktop::WorkerActivityMonitor monitor;
    const auto before=penta::runtime::ThreadBudget::instance().activity();
    const auto observed=monitor.snapshot(),direct=penta::desktop::sampleWorkerActivity();
    QCOMPARE(observed.poolActive,before.active);QCOMPARE(direct.poolActive,before.active);
    QCOMPARE(observed.blasTeamWidth,std::size_t(0));QCOMPARE(observed.blasOwners,std::size_t(0));
    QCOMPARE(blasLoaded(),loaded);monitor.setRunning(false);QCOMPARE(monitor.snapshot().reservedWorkers(),std::size_t(0));
}
void WorkerActivityTests::activeSamplerPausesWithoutLoadingAModule(){
    const bool loaded=blasLoaded();penta::desktop::WorkerActivityMonitor monitor;
    monitor.setRunning(true);std::this_thread::sleep_for(std::chrono::milliseconds(20));monitor.setRunning(false);
    QCOMPARE(monitor.snapshot().reservedWorkers(),std::size_t(0));QCOMPARE(blasLoaded(),loaded);
    monitor.setRunning(true);monitor.setRunning(false);QCOMPARE(monitor.snapshot().reservedWorkers(),std::size_t(0));
}
QTEST_APPLESS_MAIN(WorkerActivityTests)
#include "worker_activity_tests.moc"
