#include "offscreen_test_bootstrap.hpp"
#include "editor_widget.hpp"
#include "find_replace_bar.hpp"
#include "main_window.hpp"
#include "document.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTest>
using namespace penta;
namespace {
struct Surface {
    QWidget window;EditorWidget* editor;FindReplaceBar* bar;
    explicit Surface(const QString& text){auto* layout=new QVBoxLayout(&window);editor=new EditorWidget(&window);bar=new FindReplaceBar(&window);layout->addWidget(editor,1);layout->addWidget(bar);editor->setPlainText(text);editor->document()->setModified(false);window.resize(1050,500);window.show();bar->openFor(editor);}
    QLineEdit* query()const{return bar->findChild<QLineEdit*>(QStringLiteral("findText"));}
    QLineEdit* replacement()const{return bar->findChild<QLineEdit*>(QStringLiteral("replaceText"));}
    QCheckBox* toggle(const char* name)const{return bar->findChild<QCheckBox*>(QString::fromLatin1(name));}
    void click(const char* name)const{if(auto* tool=bar->findChild<QToolButton*>(QString::fromLatin1(name)))tool->click();else if(auto* button=bar->findChild<QPushButton*>(QString::fromLatin1(name)))button->click();}
};
}
class FindReplaceTests final:public QObject {
    Q_OBJECT
private slots:
    void ctrlFIsOnePersistentNonmodalEditorBar();
    void liveMatchesNavigationAndEditing();
    void replacementsAreOneUndoStep();
    void regexGroupsZeroWidthAndInvalidPatterns();
    void tabSwitchMovesDecorationsAndPreservesQuery();
    void unicodeWholeWordsAndBoundedHighlighting();
};
void FindReplaceTests::ctrlFIsOnePersistentNonmodalEditorBar(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);window.configureAutosave(false);QString error;QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("FindProject"),&error),qPrintable(error));
    auto* editor=window.currentEditor();QVERIFY(editor);editor->setPlainText(QStringLiteral("real alpha=1;\nreal beta=alpha+2;\n% alpha\n"));editor->document()->setModified(false);window.resize(1250,800);window.show();window.activateWindow();editor->setFocus();QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* action=window.findChild<QAction*>(QStringLiteral("editFindReplace"));QVERIFY(action);QCOMPARE(action->shortcut(),QKeySequence::Find);
    QTest::keyClick(editor,Qt::Key_F,Qt::ControlModifier);auto* bar=window.findChild<FindReplaceBar*>();QVERIFY(bar);QTRY_VERIFY(bar->isVisible());QVERIFY(!bar->isWindow());QVERIFY(QApplication::activeModalWidget()==nullptr);QCOMPARE(window.findChildren<FindReplaceBar*>().size(),1);
    auto* query=bar->findChild<QLineEdit*>(QStringLiteral("findText"));QVERIFY(query);query->setText(QStringLiteral("alpha"));QCOMPARE(bar->matchCount(),3);QVERIFY(!editor->isReadOnly());QVERIFY(!editor->document()->isModified());QCOMPARE(editor->extraSelections().size(),4);
    action->trigger();QCOMPARE(window.findChild<FindReplaceBar*>(),bar);QCOMPARE(query->text(),QStringLiteral("alpha"));QTRY_VERIFY(query->hasFocus());QCOMPARE(QApplication::focusWidget(),query);QCOMPARE(query->selectedText(),QStringLiteral("alpha"));QVERIFY(QApplication::activeModalWidget()==nullptr);
    editor->setFocus();auto cursor=editor->textCursor();cursor.movePosition(QTextCursor::End);editor->setTextCursor(cursor);QTest::keyClicks(editor,"% alpha");QTRY_COMPARE(bar->matchCount(),4);QVERIFY(editor->document()->isModified());
    QTest::keyClick(editor,Qt::Key_Escape);QTRY_VERIFY(!bar->isVisible());QCOMPARE(editor->extraSelections().size(),1);action->trigger();QVERIFY(bar->isVisible());QCOMPARE(query->text(),QStringLiteral("alpha"));QCOMPARE(window.findChildren<FindReplaceBar*>().size(),1);
}
void FindReplaceTests::liveMatchesNavigationAndEditing(){
    Surface surface(QStringLiteral("alpha Alpha alphabet alpha"));surface.query()->setText(QStringLiteral("alpha"));QCOMPARE(surface.bar->matchCount(),4);surface.toggle("findWholeWord")->setChecked(true);QCOMPARE(surface.bar->matchCount(),3);surface.toggle("findCaseSensitive")->setChecked(true);QCOMPARE(surface.bar->matchCount(),2);
    surface.editor->moveCursor(QTextCursor::Start);surface.click("findNext");QCOMPARE(surface.editor->textCursor().selectionStart(),0);QCOMPARE(surface.editor->textCursor().selectedText(),QStringLiteral("alpha"));surface.click("findNext");QCOMPARE(surface.editor->textCursor().selectionStart(),21);surface.click("findNext");QCOMPARE(surface.editor->textCursor().selectionStart(),0);surface.click("findPrevious");QCOMPARE(surface.editor->textCursor().selectionStart(),21);
    QCOMPARE(surface.editor->extraSelections().size(),3);int current=0;for(const auto& selection:surface.editor->extraSelections())if(selection.format.background().color()==QColor(255,174,91))++current;QCOMPARE(current,1);
    surface.editor->setFocus();surface.editor->moveCursor(QTextCursor::End);QTest::keyClicks(surface.editor," alpha");QTRY_COMPARE(surface.bar->matchCount(),3);QCOMPARE(surface.editor->extraSelections().size(),4);QTest::keyClick(surface.editor,Qt::Key_F3);QCOMPARE(surface.editor->textCursor().selectionStart(),0);QTest::keyClick(surface.editor,Qt::Key_F3,Qt::ShiftModifier);QCOMPARE(surface.editor->textCursor().selectionStart(),27);
    surface.bar->closePanel();QCOMPARE(surface.editor->extraSelections().size(),1);QVERIFY(surface.editor->hasFocus());surface.bar->openFor(surface.editor);QCOMPARE(surface.bar->matchCount(),3);QVERIFY(surface.toggle("findCaseSensitive")->isChecked());
}
void FindReplaceTests::replacementsAreOneUndoStep(){
    const QString original=QStringLiteral("cat cat\ncat");Surface surface(original);surface.query()->setText(QStringLiteral("cat"));surface.replacement()->setText(QStringLiteral("dog"));surface.click("replaceAll");QCOMPARE(surface.editor->toPlainText(),QStringLiteral("dog dog\ndog"));QCOMPARE(surface.bar->matchCount(),0);surface.editor->undo();QCOMPARE(surface.editor->toPlainText(),original);QTRY_COMPARE(surface.bar->matchCount(),3);
    surface.editor->moveCursor(QTextCursor::Start);surface.click("findNext");surface.click("replaceOne");QCOMPARE(surface.editor->toPlainText(),QStringLiteral("dog cat\ncat"));surface.editor->undo();QCOMPARE(surface.editor->toPlainText(),original);
    surface.editor->setReadOnly(true);surface.bar->openFor(surface.editor);QVERIFY(!surface.bar->findChild<QPushButton*>(QStringLiteral("replaceAll"))->isEnabled());QVERIFY(surface.bar->findChild<QToolButton*>(QStringLiteral("findNext"))->isEnabled());
}
void FindReplaceTests::regexGroupsZeroWidthAndInvalidPatterns(){
    Surface surface(QStringLiteral("p12 q34 p56\n"));surface.toggle("findRegularExpression")->setChecked(true);surface.query()->setText(QStringLiteral("([pq])(\\d+)"));surface.replacement()->setText(QStringLiteral("\\2_\\1"));QCOMPARE(surface.bar->matchCount(),3);surface.click("replaceAll");QCOMPARE(surface.editor->toPlainText(),QStringLiteral("12_p 34_q 56_p\n"));surface.editor->undo();QCOMPARE(surface.editor->toPlainText(),QStringLiteral("p12 q34 p56\n"));
    surface.editor->setPlainText(QStringLiteral("aa"));surface.query()->setText(QStringLiteral("(?=a)"));surface.replacement()->setText(QStringLiteral("_"));QCOMPARE(surface.bar->matchCount(),2);surface.editor->moveCursor(QTextCursor::Start);surface.click("findNext");QCOMPARE(surface.editor->textCursor().position(),0);surface.click("findNext");QCOMPARE(surface.editor->textCursor().position(),1);surface.click("replaceAll");QCOMPARE(surface.editor->toPlainText(),QStringLiteral("_a_a"));surface.editor->undo();QCOMPARE(surface.editor->toPlainText(),QStringLiteral("aa"));
    surface.query()->setText(QStringLiteral("["));QCOMPARE(surface.bar->matchCount(),0);QVERIFY(surface.bar->findChild<QLabel*>(QStringLiteral("findStatus"))->text().contains(QStringLiteral("Invalid expression")));QVERIFY(!surface.editor->isReadOnly());QCOMPARE(surface.editor->extraSelections().size(),1);
}
void FindReplaceTests::tabSwitchMovesDecorationsAndPreservesQuery(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);window.configureAutosave(false);QString error;QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("TabFind"),&error),qPrintable(error));auto* first=window.currentEditor();first->setPlainText(QStringLiteral("alpha alpha"));window.show();auto* action=window.findChild<QAction*>(QStringLiteral("editFindReplace"));QVERIFY(action);action->trigger();auto* bar=window.findChild<FindReplaceBar*>();QVERIFY(bar);bar->findChild<QLineEdit*>(QStringLiteral("findText"))->setText(QStringLiteral("alpha"));QCOMPARE(bar->matchCount(),2);
    QVERIFY2(window.addSource(QStringLiteral("second.p"),&error),qPrintable(error));auto* second=window.currentEditor();QVERIFY(second!=first);second->setPlainText(QStringLiteral("alpha beta"));QTRY_COMPARE(bar->matchCount(),1);QCOMPARE(first->extraSelections().size(),1);QCOMPARE(second->extraSelections().size(),2);
    auto* tabs=window.findChild<QTabWidget*>(QStringLiteral("editorTabs"));QVERIFY(tabs);tabs->setCurrentIndex(0);QCOMPARE(bar->matchCount(),2);QCOMPARE(second->extraSelections().size(),1);QCOMPARE(first->extraSelections().size(),3);QCOMPARE(bar->findChild<QLineEdit*>(QStringLiteral("findText"))->text(),QStringLiteral("alpha"));
}
void FindReplaceTests::unicodeWholeWordsAndBoundedHighlighting(){
    Surface surface(QString::fromUtf8("α αβ α"));surface.toggle("findWholeWord")->setChecked(true);surface.query()->setText(QString::fromUtf8("α"));QCOMPARE(surface.bar->matchCount(),2);
    surface.toggle("findWholeWord")->setChecked(false);surface.editor->setPlainText(QString(10001,QLatin1Char('a')));surface.query()->setText(QStringLiteral("a"));QCOMPARE(surface.bar->matchCount(),10000);QVERIFY(!surface.bar->findChild<QPushButton*>(QStringLiteral("replaceAll"))->isEnabled());QVERIFY(surface.bar->findChild<QLabel*>(QStringLiteral("findStatus"))->text().contains(QStringLiteral("10,000")));surface.bar->closePanel();QCOMPARE(surface.editor->extraSelections().size(),1);
}
PENTA_OFFSCREEN_TEST_MAIN(FindReplaceTests)

#include "find_replace_tests.moc"
