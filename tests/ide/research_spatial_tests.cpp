#include "ide/research_explorer.hpp"
#include "ide/research_spatial_controls.hpp"
#include "plotting/field_animation.hpp"
#include <algorithm>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QTableWidget>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>
namespace {
void diagnostics(const penta::runtime::ExecutionResult& result){for(const auto& d:result.analysis.diagnostics)if(d.severity==penta::language::Severity::Error)qWarning()<<QString::fromStdString(d.code+": "+d.message);for(const auto& d:result.runtimeDiagnostics)qWarning()<<QString::fromStdString(d.code+": "+d.message);}
penta::runtime::ExecutionOptions execution(const QTemporaryDir& directory){penta::runtime::ExecutionOptions options;options.dataDirectory=directory.path().toStdWString();options.odeCacheDirectory=options.dataDirectory/"odes";options.maxOperations=20000000;return options;}
penta::research::Configuration small(penta::research::Mode mode){auto c=penta::research::defaultConfiguration(mode);c.t1=.1;c.sampleStep=.05;c.spatial.pde.nx=7;c.spatial.pde.ny=7;c.spatial.geometry.nu=7;c.spatial.geometry.nv=7;c.spatial.mechanics.timeStep=.02;return c;}
}
class ResearchSpatialTests:public QObject{
    Q_OBJECT
private slots:
    void implicitAlgebraFieldsAndMulticoordinateMechanics(){
        QTemporaryDir directory;const auto options=execution(directory);
        auto p=small(penta::research::Mode::Pde);p.algebras={{"H","quaternion"}};p.spatial.pde.integrator="implicit";p.spatial.pde.valueType="element<H>";p.spatial.pde.initial="amplitude*sin(wave*x/domain_x)*(1+H.i)";p.spatial.pde.reaction="-u";p.spatial.pde.component=3;p.rtol=1e-3;p.atol=1e-7;p.livePlots=true;
        penta::ResearchExplorer panel;panel.setConfiguration(p);QString error;const auto source=panel.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));QVERIFY(source.contains("pdeimplicit("));QVERIFY(source.contains("live_plot"));penta::runtime::InterpreterSession session;auto live=options;std::size_t updates=0;live.figureProgress=[&](const auto&){++updates;};const auto result=session.submit(source.toStdString(),{},live);if(!result.ok())diagnostics(result);QVERIFY(result.ok());QVERIFY(updates>0);panel.findChild<QPushButton*>("researchRun")->click();panel.acceptResult(result);QVERIFY(panel.resultFresh());const auto command=panel.exportCommand(directory.filePath("all-field-coordinates.csv"),&error);QVERIFY(command.contains("research_result.values"));QVERIFY(session.submit(command.toStdString(),{},options).ok());
        auto m=small(penta::research::Mode::Mechanics);m.spatial.mechanics.dimensions=2;m.spatial.mechanics.initialQ="[1.0;2.0]";m.spatial.mechanics.initialSecond="[0.0;0.0]";const auto ordinary=penta::research::generateScript(m)+"assert(size(research_result.q,2)==2);assert(abs(research_result.q(end,2)-2*cos(0.1))<1e-4);";const auto second=penta::runtime::execute(ordinary,{},options);if(!second.ok())diagnostics(second);QVERIFY(second.ok());
        m.spatial.mechanics.form="constrained";m.spatial.mechanics.method="idas";m.spatial.mechanics.expression="0.5*(v(1)*v(1)+v(2)*v(2))";m.spatial.mechanics.constraintExpression="q(1)*q(1)+q(2)*q(2)-1.0";m.spatial.mechanics.initialQ="[1.0;0.0]";m.spatial.mechanics.initialSecond="[0.0;1.0]";const auto third=penta::runtime::execute(penta::research::generateScript(m)+"assert(research_result.maximum_constraint_error<1e-5);",{},options);if(!third.ok())diagnostics(third);QVERIFY(third.ok());
    }
    void openingEditingAndCheckingNeverRunsScientificCode(){
        penta::ResearchExplorer panel;QSignalSpy run(&panel,&penta::ResearchExplorer::runRequested);for(auto mode:{penta::research::Mode::Pde,penta::research::Mode::Geometry,penta::research::Mode::Mechanics}){panel.setConfiguration(small(mode));QString error;const auto source=panel.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));QVERIFY(source.trimmed().endsWith(';'));QVERIFY(penta::language::analyse(source.toStdString()).ok());QVERIFY(panel.findChild<QPushButton*>("researchPreflight")->isEnabled());panel.findChild<QPushButton*>("researchGenerate")->click();}panel.findChild<QLineEdit*>("researchMechanicsExpression")->setText("-2*frequency*q");QCOMPARE(run.count(),0);
    }
    void spatialInputsRoundTripAndUseExplicitCoordinateMeanings(){
        penta::ResearchExplorer panel;auto c=small(penta::research::Mode::Geometry);c.gamma=.5;c.spatial.geometry.projection={3,4,5};c.spatial.geometry.coordinates[4]="amplitude*u*v";panel.setConfiguration(c);const auto saved=panel.configuration();QCOMPARE(saved.spatial.geometry.projection,c.spatial.geometry.projection);QCOMPARE(saved.spatial.geometry.coordinates,c.spatial.geometry.coordinates);QString error;const auto source=panel.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));QVERIFY(source.contains("projection=[3,4,5]"));QVERIFY(source.contains("*ell"));QVERIFY(source.contains("chartgeometry"));
        panel.setConfiguration(small(penta::research::Mode::Pde));panel.findChild<QComboBox*>("researchPdeDimension")->setCurrentText("2");panel.findChild<QComboBox*>("researchPdeValueType")->setCurrentText("pentarnion");QVERIFY(panel.configuration().spatial.pde.initial.find("sin(wave*y/domain_y)")!=std::string::npos);QVERIFY(panel.configuration().spatial.pde.initial.find("ell")!=std::string::npos);QCOMPARE(panel.findChild<QSpinBox*>("researchPdeComponent")->maximum(),4);
    }
    void mathematicalControlsRejectStatementsAndFileAccess(){
        for(auto mode:{penta::research::Mode::Pde,penta::research::Mode::Geometry,penta::research::Mode::Mechanics}){auto c=small(mode);if(mode==penta::research::Mode::Pde)c.spatial.pde.reaction="u;}display(999);native function real injected(real a){return a";else if(mode==penta::research::Mode::Geometry)c.spatial.geometry.coordinates[0]="readmatrix(\"not-requested.csv\")";else c.spatial.mechanics.expression="q;return q";bool rejected=false;try{(void)penta::research::generateScript(c);}catch(const penta::research::ScriptError&){rejected=true;}QVERIFY(rejected);}
        auto c=small(penta::research::Mode::Pde);c.parameters[0].name="domain_x";QVERIFY_EXCEPTION_THROWN(penta::research::generateScript(c),penta::research::ScriptError);c=small(penta::research::Mode::Geometry);c.spatial.geometry.projection={1,1,2};QVERIFY_EXCEPTION_THROWN(penta::research::generateScript(c),penta::research::ScriptError);
    }
    void nativeProgramsExecuteAndTheirExportsUseExactAcceptedData(){
        for(auto mode:{penta::research::Mode::Pde,penta::research::Mode::Geometry,penta::research::Mode::Mechanics}){
            QTemporaryDir directory;QVERIFY(directory.isValid());penta::ResearchExplorer panel;panel.setConfiguration(small(mode));QSignalSpy run(&panel,&penta::ResearchExplorer::runRequested);panel.findChild<QPushButton*>("researchRun")->click();QCOMPARE(run.count(),1);const auto source=run.takeFirst()[0].toString();penta::runtime::InterpreterSession session;const auto result=session.submit(source.toStdString(),{},execution(directory));if(!result.ok())diagnostics(result);QVERIFY(result.ok());QCOMPARE(QString::fromStdString(result.sourceHash),QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(),QCryptographicHash::Sha256).toHex()));panel.acceptResult(result);QVERIFY(panel.resultFresh());const auto file=directory.filePath("accepted.csv");QString error;const auto command=panel.exportCommand(file,&error);QVERIFY2(!command.isEmpty(),qPrintable(error));if(mode==penta::research::Mode::Geometry)QVERIFY(command.contains("research_mesh.vertices"));if(mode==penta::research::Mode::Mechanics)QVERIFY(command.contains("research_result.p"));const auto exported=session.submit(command.toStdString(),{},execution(directory));if(!exported.ok())diagnostics(exported);QVERIFY(exported.ok());QVERIFY(QFileInfo::exists(file));
        }
    }
    void twoDimensionalPentarnionFieldRetainsEveryCoefficient(){
        QTemporaryDir directory;auto c=small(penta::research::Mode::Pde);c.spatial.pde.dimension=2;c.spatial.pde.valueType="pentarnion";c.spatial.pde.initial="amplitude*sin(wave*x/domain_x)*sin(wave*y/domain_y)*(1+i+j+k+ell)";c.spatial.pde.component=4;const auto source=penta::research::generateScript(c)+"assert(size(pdecoeff(research_result,4),2)==49);assert(norm(pdecoeff(research_result,4)-pdecoeff(research_result,0),\"fro\")<1e-12);";const auto result=penta::runtime::execute(source,{},execution(directory));if(!result.ok())diagnostics(result);QVERIFY(result.ok());bool all=false;for(const auto& action:result.figureActions)if(action.animation)all=action.animation->components==5;QVERIFY(all);
    }
    void explicitPreflightDoesNotSolveOrPlot(){
        QTemporaryDir directory;for(auto mode:{penta::research::Mode::Pde,penta::research::Mode::Geometry,penta::research::Mode::Mechanics}){const auto source=penta::research::generateScript(small(mode),true);const auto result=penta::runtime::execute(source,{},execution(directory));if(!result.ok())diagnostics(result);QVERIFY(result.ok());QVERIFY(result.figureActions.empty());QVERIFY(std::none_of(result.variables.begin(),result.variables.end(),[](const auto& value){return value.name=="research_result"||value.name=="research_mesh";}));}
    }
    void parameterChangesReuseNativeBodiesAndInvalidateOldSnapshots(){
        QTemporaryDir directory;const auto options=execution(directory);penta::runtime::InterpreterSession session;penta::ResearchExplorer panel;auto c=small(penta::research::Mode::Geometry);panel.setConfiguration(c);QSignalSpy run(&panel,&penta::ResearchExplorer::runRequested);panel.findChild<QPushButton*>("researchRun")->click();const auto source=run.takeFirst()[0].toString();const auto first=session.submit(source.toStdString(),{},options);QVERIFY(first.ok());c.parameters[0].value="0.75";panel.setConfiguration(c);panel.acceptResult(first);QVERIFY(!panel.resultFresh());QString error;QVERIFY(panel.exportCommand(directory.filePath("stale.csv"),&error).isEmpty());const auto next=panel.generatedScript(&error);const auto second=session.submit(next.toStdString(),{},options);if(!second.ok())diagnostics(second);QVERIFY(second.ok());QCOMPARE(second.nativeCompilation.compilerAttempts,std::size_t(0));
    }
    void selectedLiteralSliderNeverExecutesAndReusesCachedBody(){
        QTemporaryDir directory;const auto options=execution(directory);penta::runtime::InterpreterSession session;penta::ResearchExplorer panel;auto c=small(penta::research::Mode::Geometry);c.parameters.push_back({"real","unused","1.0"});panel.setConfiguration(c);
        auto* slider=panel.findChild<QSlider*>("researchParameterSlider");auto* select=panel.findChild<QComboBox*>("researchSliderParameter");auto* table=panel.findChild<QTableWidget*>("researchParameters");auto* minimum=panel.findChild<QDoubleSpinBox*>("researchSliderMinimum");auto* maximum=panel.findChild<QDoubleSpinBox*>("researchSliderMaximum");
        QSignalSpy run(&panel,&penta::ResearchExplorer::runRequested),aux(&panel,&penta::ResearchExplorer::exportRequested);QVERIFY(slider->isEnabled());QCOMPARE(select->count(),table->rowCount());
        panel.findChild<QPushButton*>("researchRun")->click();const auto first=session.submit(run.takeFirst()[0].toString().toStdString(),{},options);QVERIFY(first.ok());panel.acceptResult(first);QVERIFY(panel.resultFresh());
        const auto unchanged=panel.configuration();slider->setValue(500);QVERIFY(!panel.resultFresh());QCOMPARE(run.count(),0);QCOMPARE(aux.count(),0);QCOMPARE(panel.configuration().parameters[0].value,std::string("2"));QCOMPARE(panel.configuration().spatial.geometry.coordinates,c.spatial.geometry.coordinates);QCOMPARE(panel.configuration().parameters[1].value,unchanged.parameters[1].value);
        QString error;const auto second=session.submit(panel.generatedScript(&error).toStdString(),{},options);QVERIFY(second.ok());QCOMPARE(second.nativeCompilation.compilerAttempts,std::size_t(0));
        const auto beforeRange=panel.configuration().parameters[0].value;minimum->setValue(5);QVERIFY(!slider->isEnabled());slider->setValue(700);QCOMPARE(panel.configuration().parameters[0].value,beforeRange);minimum->setValue(0);QVERIFY(slider->isEnabled());
        table->item(0,2)->setText("1+1");QVERIFY(!slider->isEnabled());slider->setValue(400);QCOMPARE(table->item(0,2)->text(),QString("1+1"));table->item(0,2)->setText("1e999");QVERIFY(!slider->isEnabled());table->item(0,2)->setText("1.2345678901234567");QVERIFY(slider->isEnabled());QCOMPARE(table->item(0,2)->text(),QString("1.2345678901234567"));
        table->item(0,0)->setText("pentarnion");QVERIFY(!slider->isEnabled());table->item(0,0)->setText("real");QVERIFY(slider->isEnabled());
        table->setCurrentCell(1,2);QCOMPARE(select->currentIndex(),1);select->setCurrentIndex(0);QCOMPARE(table->currentRow(),0);maximum->setValue(3);QVERIFY(slider->isEnabled());QCOMPARE(run.count(),0);QCOMPARE(aux.count(),0);
        for(auto mode:{penta::research::Mode::Ode,penta::research::Mode::Pde,penta::research::Mode::Mechanics}){panel.setConfiguration(small(mode));QVERIFY(slider->isEnabled());QCOMPARE(run.count(),0);}
    }
    void mechanicsTemplatesDistinguishVelocityAndCanonicalMomentum(){
        QTemporaryDir directory;penta::ResearchExplorer panel;panel.setConfiguration(small(penta::research::Mode::Mechanics));for(const auto* form:{"lagrangian","hamiltonian"}){panel.findChild<QComboBox*>("researchMechanicsForm")->setCurrentText(form);QString error;const auto source=panel.generatedScript(&error);QVERIFY2(!source.isEmpty(),qPrintable(error));const bool h=std::string(form)=="hamiltonian";QVERIFY(source.contains(h?"real p":"real v"));QVERIFY(source.contains(h?"midpoint":"rk4"));const auto result=penta::runtime::execute(source.toStdString(),{},execution(directory));if(!result.ok())diagnostics(result);QVERIFY(result.ok());}
    }
};
QTEST_MAIN(ResearchSpatialTests)
#include "research_spatial_tests.moc"
