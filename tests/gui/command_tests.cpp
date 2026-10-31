#include "offscreen_test_bootstrap.hpp"
#include "command_window.hpp"
#include "main_window.hpp"
#include "editor_widget.hpp"
#include "document.hpp"
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTest>
using namespace penta;
namespace {
QTextCharFormat at(QTextDocument* document,int offset){QTextCursor cursor(document);cursor.setPosition(offset);cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);return cursor.charFormat();}
}
class CommandTests final:public QObject {
    Q_OBJECT
private slots:
    void controlCAbandonsInputButSelectionCopies();
    void controlCRequestsBusyCancellation();
    void diagnosticsStayRedAndOrdinaryTextStaysBlack();
    void matlabStyleRunSavesAndPublishesWorkspace();
    void warningsUseMetadataAndHelpIsSearchable();
};
void CommandTests::controlCAbandonsInputButSelectionCopies(){
    CommandWindow console;console.show();console.setFocus();console.setInputText(QStringLiteral("real unfinished ="));
    QSignalSpy interrupts(&console,&CommandWindow::interruptRequested);
    QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);
    QCOMPARE(console.inputText(),QString());QCOMPARE(console.document()->lastBlock().text(),QStringLiteral(">> "));
    QVERIFY(console.transcriptText().contains(QStringLiteral("real unfinished =^C")));QCOMPARE(interrupts.size(),1);QVERIFY(console.history().isEmpty());
    console.setInputText(QStringLiteral("keep this"));auto cursor=console.textCursor();cursor.setPosition(console.inputStart());cursor.movePosition(QTextCursor::End,QTextCursor::KeepAnchor);console.setTextCursor(cursor);
    QApplication::clipboard()->setText(QStringLiteral("before"));QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);
    QTRY_COMPARE(QApplication::clipboard()->text(),QStringLiteral("keep this"));QCOMPARE(console.inputText(),QStringLiteral("keep this"));QCOMPARE(interrupts.size(),1);
}
void CommandTests::controlCRequestsBusyCancellation(){
    CommandWindow console;console.show();console.setBusy(true);QSignalSpy interrupts(&console,&CommandWindow::interruptRequested);
    QTest::keyClick(&console,Qt::Key_C,Qt::ControlModifier);QCOMPARE(interrupts.size(),1);QVERIFY(console.isBusy());QCOMPARE(console.inputText(),QString());
}
void CommandTests::diagnosticsStayRedAndOrdinaryTextStaysBlack(){
    CommandWindow console;console.appendOutput(QStringLiteral("ordinary"));console.appendDiagnostic(QStringLiteral("main.p:4:1 Error E1: message"),19);
    auto cursor=console.textCursor();cursor.setPosition(10);console.setTextCursor(cursor); // Caret in the diagnostic before rebuild.
    console.appendOutput(QStringLiteral("normal after error"));console.setInputText(QStringLiteral("display(1)"));
    const auto text=console.toPlainText();const auto diagnostic=at(console.document(),text.indexOf(QStringLiteral("Error")));
    QVERIFY(diagnostic.foreground().color().red()>150);QCOMPARE(diagnostic.foreground().color().green(),0);QVERIFY(diagnostic.isAnchor());
    for(const auto word:{"ordinary","normal after error",">> ","display(1)"}){const auto format=at(console.document(),text.indexOf(QString::fromLatin1(word)));QCOMPARE(format.foreground().color(),QColor(Qt::black));QVERIFY(!format.isAnchor());QVERIFY(!format.fontUnderline());}
    console.appendError(QStringLiteral("Warning: example"));QVERIFY(at(console.document(),console.toPlainText().indexOf(QStringLiteral("Warning"))).foreground().color().red()>150);
    console.clearTranscript();console.appendOutput(QStringLiteral("fresh black"));QCOMPARE(at(console.document(),0).foreground().color(),QColor(Qt::black));QVERIFY(!at(console.document(),0).isAnchor());
}
void CommandTests::matlabStyleRunSavesAndPublishesWorkspace(){
    QTemporaryDir temp;MainWindow window(false,nullptr,false);window.configureAutosave(false);QString error;
    QVERIFY2(window.createProjectAt(temp.path(),QStringLiteral("ConsoleRun"),&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    window.currentEditor()->setPlainText(QStringLiteral("real answer=42;display(answer);"));window.currentEditor()->document()->setModified(true);QVERIFY(window.currentDocument()->isModified());
    QVERIFY2(window.submitCommand(QStringLiteral("run main.p"),&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));QVERIFY(!window.currentDocument()->isModified());QVERIFY(window.resultsText().contains(QStringLiteral("42")));
    QFile saved(window.currentDocument()->path());QVERIFY(saved.open(QIODevice::ReadOnly));QVERIFY(saved.readAll().contains("answer=42"));
    QVERIFY(window.submitCommand(QStringLiteral("display(answer+1)"),&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QVERIFY(window.resultsText().contains(QStringLiteral("43")));
    QVERIFY(window.submitCommand(QStringLiteral("run \"main.p\";"),&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QCOMPARE(window.problemCount(),0);
    QVERIFY(!window.submitCommand(QStringLiteral("run(\"main.p\",1)"),&error));QVERIFY(error.contains(QStringLiteral("literal")));
}
void CommandTests::warningsUseMetadataAndHelpIsSearchable(){
    MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.submitCommand(QStringLiteral("warning(\"actual warning\");display(\"Warning: ordinary output\");"),&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);
    auto* console=window.findChild<CommandWindow*>();QVERIFY(console);const auto text=console->toPlainText();
    QVERIFY(at(console->document(),text.indexOf(QStringLiteral("Warning: actual warning"))).foreground().color().red()>150);
    QCOMPARE(at(console->document(),text.indexOf(QStringLiteral("Warning: ordinary output"))).foreground().color(),QColor(Qt::black));
    for(const auto command:{"help plot","help odesolve","help if","help(\"root\")","lookfor sensitivity"}){
        QVERIFY(window.submitCommand(QString::fromLatin1(command),&error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));
    }
    QVERIFY(window.resultsText().contains(QStringLiteral("Syntax:")));QVERIFY(window.resultsText().contains(QStringLiteral("LOOKFOR sensitivity")));QVERIFY(window.resultsText().contains(QStringLiteral("odeevents")));
}
PENTA_OFFSCREEN_TEST_MAIN(CommandTests)

#include "command_tests.moc"
