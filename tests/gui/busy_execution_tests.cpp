#include "offscreen_test_bootstrap.hpp"
#include "command_window.hpp"
#include "document.hpp"
#include "editor_widget.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>

using namespace penta;
namespace {
QString reaction() { return QStringLiteral("native function real cancel_reaction(real u,real t,real s){return -.1*u;}\n"); }
QString solve(bool longRun) {
    return QStringLiteral("record field=pdesolve(cancel_reaction,linspace(0,1,33),ones(1,33),"
        "options(diffusion=.01,boundary=\"neumann\",t1=%1,time_step=.001,sample_step=1));\n")
        .arg(longRun ? QStringLiteral("100") : QStringLiteral(".001"));
}
void replaceSource(MainWindow& window,const QString& source) {
    window.currentEditor()->setPlainText(source);
    window.currentEditor()->document()->setModified(true);
}
}

class BusyExecutionTests final : public QObject {
    Q_OBJECT
private slots:
    void readyPromptIsHiddenUntilWorkerFinishes();
    void busyControlCInterruptsEvenWithSelection();
    void controlCCancelsPdeAndRestoresReadyState_data();
    void controlCCancelsPdeAndRestoresReadyState();
};

void BusyExecutionTests::readyPromptIsHiddenUntilWorkerFinishes() {
    CommandWindow console;
    console.setInputText(QStringLiteral("pending draft"));
    console.appendOutput(QStringLiteral("earlier result"));
    console.setBusy(true);
    QVERIFY(console.isReadOnly());
    QVERIFY(console.document()->lastBlock().text().startsWith(QStringLiteral("Running")));
    QVERIFY(!console.document()->lastBlock().text().contains(QStringLiteral(">>")));
    QVERIFY(!console.toPlainText().contains(QStringLiteral("pending draft")));
    QTest::keyClicks(&console,QStringLiteral("blocked"));
    QCOMPARE(console.inputText(),QStringLiteral("pending draft"));
    console.appendOutput(QStringLiteral("later result"));
    QVERIFY(console.document()->lastBlock().text().startsWith(QStringLiteral("Running")));
    console.setStopping();
    QVERIFY(console.document()->lastBlock().text().startsWith(QStringLiteral("Stopping")));
    console.setBusy(true); // Repeated UI refresh must not undo a stopping state.
    QVERIFY(console.isStopping());
    console.setBusy(false);
    QVERIFY(!console.isReadOnly());
    QCOMPARE(console.document()->lastBlock().text(),QStringLiteral(">> pending draft"));
    QVERIFY(!console.isStopping());
}

void BusyExecutionTests::busyControlCInterruptsEvenWithSelection() {
    CommandWindow console;console.show();console.setFocus();
    console.appendOutput(QStringLiteral("copyable transcript"));console.setBusy(true);console.selectAll();
    QSignalSpy interrupts(&console,&CommandWindow::interruptRequested);
    QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(interrupts.size(),1);QVERIFY(console.isBusy());QVERIFY(console.isStopping());
    QVERIFY(console.document()->lastBlock().text().startsWith(QStringLiteral("Stopping")));
    QVERIFY(console.transcriptText().endsWith(QStringLiteral("^C")));
    QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(interrupts.size(),1);
    console.setBusy(false);console.setInputText(QStringLiteral("idle copy"));console.selectAll();
    QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);
    QVERIFY(QApplication::clipboard()->text().contains(QStringLiteral("idle copy")));
    QCOMPARE(interrupts.size(),1);
}

void BusyExecutionTests::controlCCancelsPdeAndRestoresReadyState_data() {
    QTest::addColumn<int>("mode");
    QTest::newRow("F5-with-editor-selection") << 0;
    QTest::newRow("console-run-with-transcript-selection") << 1;
    QTest::newRow("direct-console-PDE") << 2;
}

void BusyExecutionTests::controlCCancelsPdeAndRestoresReadyState() {
    QFETCH(int,mode);
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    MainWindow window(false,nullptr,false);window.configureAutosave(false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("BusyPDE"),&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    replaceSource(window,reaction()+QStringLiteral("real retained=7;\n")+solve(false));
    QVERIFY2(window.runCurrentDocument(&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),20000);
    QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));
    replaceSource(window,reaction()+QStringLiteral("real retained=999;\n")+solve(true)+QStringLiteral("display(\"must-not-complete\");\n"));
    QVERIFY(window.saveCurrentDocument(&error));
    window.resize(1000,700);window.show();
    if(mode==0) QVERIFY2(window.runCurrentDocument(&error),qPrintable(error));
    else if(mode==1) QVERIFY2(window.submitCommand(QStringLiteral("run main.p"),&error),qPrintable(error));
    else QVERIFY2(window.submitCommand(QStringLiteral("retained=999;\n")+
        solve(true).replace(QStringLiteral("record field="),QStringLiteral("record interrupted_field=")),&error),qPrintable(error));
    QTest::qWait(60); // Warm native body: allow actual RK4 work before interrupting.
    QVERIFY2(window.isWorking(),qPrintable(window.resultsText()));
    auto* console=window.commandWindow();
    QVERIFY(console->document()->lastBlock().text().startsWith(QStringLiteral("Running")));
    QVERIFY(!console->document()->lastBlock().text().contains(QStringLiteral(">>")));
    auto* dock=window.findChild<QDockWidget*>(QStringLiteral("resultsDock"));QVERIFY(dock);
    QVERIFY(dock->windowTitle().contains(QStringLiteral("Running")));
    QElapsedTimer cancellation;cancellation.start();
    if(mode==0){
        window.currentEditor()->setFocus();window.currentEditor()->selectAll();
        QTest::keyClick(window.currentEditor(),Qt::Key_C,Qt::ControlModifier);
    }else{
        console->setFocus();console->selectAll();
        QTest::keyClick(console,Qt::Key_C,Qt::ControlModifier);
    }
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),5000);
    QVERIFY(cancellation.elapsed()<5000);
    QVERIFY2(window.calculationStatus().contains(QStringLiteral("cancel"),Qt::CaseInsensitive),qPrintable(window.calculationStatus()));
    QVERIFY(window.resultsText().contains(QStringLiteral("Execution interrupted.")));
    QVERIFY(!window.resultsText().contains(QStringLiteral("R2100")));
    QVERIFY(!window.resultsText().contains(QStringLiteral("must-not-complete\n")));
    QCOMPARE(console->inputText(),QString());
    QCOMPARE(console->document()->lastBlock().text(),QStringLiteral(">> "));
    QCOMPARE(dock->windowTitle(),QStringLiteral("Command Window"));
    QVERIFY(!console->isReadOnly());
    QVERIFY(window.submitCommand(QStringLiteral("display(\"restored prior:\",retained);"),&error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    QVERIFY2(window.resultsText().endsWith(QStringLiteral("restored prior: 7")),qPrintable(window.resultsText()));
    QVERIFY(window.close());
}

PENTA_OFFSCREEN_TEST_MAIN(BusyExecutionTests)
#include "busy_execution_tests.moc"
