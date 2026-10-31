#include "offscreen_test_bootstrap.hpp"
#include "ide/map_explorer.hpp"
#include "language/frontend.hpp"
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
using namespace penta;
class MapExplorerTests final:public QObject {
 Q_OBJECT
private slots:
 void realScriptAndRunSignal(){MapExplorer explorer;QString error;const auto source=explorer.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));QVERIFY(source.contains(QStringLiteral("native function real mapexplorer_fn(real x,real r)")));QVERIFY(source.contains(QStringLiteral("r*x*(1-x)")));QVERIFY(source.contains(QStringLiteral("clear mapexplorer_fn mapexplorer_result;")));QVERIFY(language::analyse(source.toStdString()).ok());QSignalSpy spy(&explorer,&MapExplorer::runRequested);explorer.findChild<QPushButton*>(QStringLiteral("mapRun"))->click();QCOMPARE(spy.size(),1);QCOMPARE(spy.takeFirst().front().toString(),source);QVERIFY(!explorer.isWindow()||explorer.parent()==nullptr);}
 void pentarnionAndProjection(){MapExplorer explorer;explorer.findChild<QComboBox*>(QStringLiteral("mapValueType"))->setCurrentIndex(1);QString error;auto source=explorer.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));QVERIFY(source.contains(QStringLiteral("native function pentarnion")));QVERIFY(source.contains(QStringLiteral("pentarnion a,pentarnion b")));QVERIFY(source.contains(QStringLiteral("coeff(mapexplorer_result.orbit,3)")));QVERIFY(source.contains(QStringLiteral("All five original coefficients")));explorer.findChild<QComboBox*>(QStringLiteral("mapProjectionY"))->setCurrentIndex(1);QVERIFY(explorer.generatedScript(&error).isEmpty());QVERIFY(error.contains(QStringLiteral("distinct")));}
 void rejectInjectedStatements(){MapExplorer explorer;auto* expression=explorer.findChild<QLineEdit*>(QStringLiteral("mapExpression"));QString error;expression->setText(QStringLiteral("x; real injected=0.0; return x"));QVERIFY(explorer.generatedScript(&error).isEmpty());QVERIFY(!error.isEmpty());expression->setText(QStringLiteral("x"));auto* initial=explorer.findChild<QLineEdit*>(QStringLiteral("mapInitial"));initial->setText(QStringLiteral("0.2; real injected=0.0"));QVERIFY(explorer.generatedScript(&error).isEmpty());initial->setText(QStringLiteral("0.2"));auto* table=explorer.findChild<QTableWidget*>(QStringLiteral("mapParameters"));table->item(0,1)->setText(QStringLiteral("r); clear all;"));QVERIFY(explorer.generatedScript(&error).isEmpty());}
 void budgetValidationAndReset(){MapExplorer explorer;auto* iterations=explorer.findChild<QSpinBox*>(QStringLiteral("mapIterations"));auto* transient=explorer.findChild<QSpinBox*>(QStringLiteral("mapTransient"));iterations->setValue(10);transient->setValue(11);QString error;QVERIFY(explorer.generatedScript(&error).isEmpty());explorer.resetDefaults();QCOMPARE(iterations->value(),500);QCOMPARE(transient->value(),100);QVERIFY2(!explorer.generatedScript(&error).isEmpty(),qPrintable(error));explorer.findChild<QComboBox*>(QStringLiteral("mapValueType"))->setCurrentIndex(1);iterations->setValue(1000000);QVERIFY(explorer.generatedScript(&error).isEmpty());}
 void exportRetainsAllCoordinatesAndEscapesPath(){MapExplorer explorer;QString error;const auto command=explorer.exportCommand(QStringLiteral("C:\\data\\a\"b.csv"),&error);QVERIFY(error.isEmpty());QVERIFY(command.contains(QStringLiteral("mapexplorer_result.n,mapexplorer_result.states")));QVERIFY(command.contains(QStringLiteral("C:\\\\data\\\\a\\\"b.csv")));QVERIFY(explorer.exportCommand(QString(),&error).isEmpty());}
 void previewIsNonmodal(){MapExplorer explorer;explorer.findChild<QPushButton*>(QStringLiteral("mapGenerateScript"))->click();auto* preview=explorer.findChild<QPlainTextEdit*>(QStringLiteral("mapScriptPreview"));QVERIFY(preview->isReadOnly());QVERIFY(preview->toPlainText().contains(QStringLiteral("mapiterate")));QVERIFY(QApplication::activeModalWidget()==nullptr);}
};
PENTA_OFFSCREEN_TEST_MAIN(MapExplorerTests)
#include "map_explorer_tests.moc"
