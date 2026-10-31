#include "offscreen_test_bootstrap.hpp"
#include "ide/editor_widget.hpp"
#include "ide/source_assistance.hpp"
#include "ide/feature_browser.hpp"
#include "runtime/module_inventory.hpp"
#include "runtime/interpreter.hpp"
#include <QCompleter>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTreeWidget>
#include <future>

using namespace penta;
class SourceAssistanceTests final:public QObject {
    Q_OBJECT
private slots:
    void checkedNamesAndScopes(){
        EditorWidget editor;SourceAssistance assistance(&editor);
        const QString source=QString::fromUtf8("% Unicode λ 🙂\nreal input=2;record cfg=options(rate=3);\nfunction real square(real value){real local=value*value;return local;}\nreal output=square(input);\n");
        editor.setPlainText(source);const auto bytes=source.toUtf8();const auto analysis=language::analyse(std::string_view(bytes.constData(),bytes.size()));QVERIFY(analysis.ok());assistance.updateAnalysis(analysis,bytes,QStringLiteral("test.p"));
        editor.moveCursor(QTextCursor::End);QVERIFY(assistance.hasCurrentAnalysis());QVERIFY(assistance.completions(QStringLiteral("sq")).contains(QStringLiteral("square")));QVERIFY(assistance.completions(QStringLiteral("cfg.")).contains(QStringLiteral("cfg.rate")));QVERIFY(!assistance.completions(QStringLiteral("loc")).contains(QStringLiteral("local")));
        auto cursor=editor.textCursor();cursor.setPosition(source.indexOf(QStringLiteral("return local"))+9);editor.setTextCursor(cursor);QVERIFY(assistance.completions(QStringLiteral("loc")).contains(QStringLiteral("local")));QVERIFY(assistance.inspectionAt(cursor.position()).contains(QStringLiteral("real")));
        cursor.setPosition(source.lastIndexOf(QStringLiteral("square(input)"))+2);editor.setTextCursor(cursor);assistance.jumpToDefinition();QCOMPARE(editor.textCursor().selectedText(),QStringLiteral("square"));QVERIFY(editor.textCursor().position()<source.lastIndexOf(QStringLiteral("square(input)")));
        QCOMPARE(editor.toPlainText(),source);
    }
    void staleInspectionsRequestCheckWithoutMoving(){
        EditorWidget editor;SourceAssistance assistance(&editor);editor.setPlainText(QStringLiteral("real value=2;display(value);"));const auto bytes=editor.toPlainText().toUtf8();assistance.updateAnalysis(language::analyse(bytes.toStdString()),bytes,QStringLiteral("test.p"));editor.moveCursor(QTextCursor::End);editor.insertPlainText(QStringLiteral("\n"));QVERIFY(!assistance.hasCurrentAnalysis());
        auto cursor=editor.textCursor();cursor.setPosition(24);editor.setTextCursor(cursor);const auto position=cursor.position();QSignalSpy requested(&assistance,&SourceAssistance::analysisRequested);assistance.jumpToDefinition();QCOMPARE(requested.size(),1);QCOMPARE(editor.textCursor().position(),position);QVERIFY(!assistance.inspectionAt(position).contains(QStringLiteral("F12: go to declaration")));
    }
    void builtinCompletionEditsOnlyTheWord(){
        EditorWidget editor;SourceAssistance assistance(&editor);editor.resize(640,300);editor.show();editor.setFocus();editor.setPlainText(QStringLiteral("displ"));editor.moveCursor(QTextCursor::End);QTest::keyClick(&editor,Qt::Key_Space,Qt::ControlModifier);auto* completer=assistance.findChild<QCompleter*>();QVERIFY(completer);QCOMPARE(completer->completionPrefix(),QStringLiteral("displ"));QVERIFY(completer->completionCount()>=1);QTest::keyClick(&editor,Qt::Key_Return);QCOMPARE(editor.toPlainText(),QStringLiteral("display"));
    }
    void moduleInventoryDoesNotLoadAndIsThreadSafe(){
        const auto before=runtime::moduleInventory();QVERIFY(before.size()>=20);const auto find=[](const auto& values,const std::string& name){for(const auto& item:values)if(item.name==name)return item;throw std::runtime_error("missing inventory name");};
        const auto initial=find(before,"numerics");QVERIFY(initial.available);const auto inspected=find(runtime::moduleInventory(),"numerics");QCOMPARE(inspected.loaded,initial.loaded);
        const auto loaded=runtime::loadModule("numerics");QVERIFY(loaded.loaded);QCOMPARE(loaded.abi,std::uint32_t(0x10000));QCOMPARE(loaded.status,std::string("loaded"));
        auto left=std::async(std::launch::async,[]{return runtime::moduleInventory();});auto right=std::async(std::launch::async,[]{return runtime::loadModule("numerics");});QVERIFY(find(left.get(),"numerics").loaded);QVERIFY(right.get().loaded);QVERIFY_EXCEPTION_THROWN(runtime::loadModule("../numerics"),std::runtime_error);
        const auto languageResult=runtime::execute("record n=module_info(\"numerics\");assert(n.available);assert(n.loaded);assert(n.abi==65536);record all=module_info();assert(all.numerics.loaded);");QVERIFY(languageResult.ok());
    }
    void rulesAreReadableAndVersioned(){
#ifdef PENTA_SOURCE_DIR
        FeatureBrowser browser(QString::fromUtf8(PENTA_SOURCE_DIR),[]{return false;});QVERIFY(browser.ruleCount()>30);auto* info=browser.findChild<QLabel*>(QStringLiteral("loadedSpecificationInfo"));QVERIFY(info);QVERIFY(info->text().contains(QStringLiteral("SHA256")));QVERIFY(info->text().contains(QStringLiteral("0.2")));auto* text=browser.findChild<QPlainTextEdit*>(QStringLiteral("featureRuleText"));QVERIFY(text&&text->isReadOnly());auto* modules=browser.findChild<QTreeWidget*>(QStringLiteral("nativeModulesTree"));QVERIFY(modules&&modules->topLevelItemCount()>=20);
#endif
    }
};
PENTA_OFFSCREEN_TEST_MAIN(SourceAssistanceTests)
#include "source_assistance_tests.moc"
