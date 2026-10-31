#include "offscreen_test_bootstrap.hpp"
#include "main_window.hpp"
#include "document.hpp"
#include "editor_widget.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QPointer>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

using namespace penta;
namespace {
QByteArray bytes(const QString& path){QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();}
QTreeWidgetItem* sourceItem(QTreeWidget* tree,const QString& relative){
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if(!(*it)->data(0,Qt::UserRole+1).toBool()&&(*it)->data(0,Qt::UserRole).toString()==relative)return *it;
    return nullptr;
}
void selectSource(MainWindow& window,const QString& relative){
    auto* tree=window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
    auto* item=sourceItem(tree,relative);QVERIFY(item);
    for(auto* parent=item->parent();parent;parent=parent->parent())parent->setExpanded(true);
    tree->setCurrentItem(item);tree->scrollToItem(item);tree->setFocus();
}
QLineEdit* renameEditor(MainWindow& window){return window.findChild<QLineEdit*>(QStringLiteral("projectSourceRenameEditor"));}
}

class InlineRenameTests final:public QObject {
    Q_OBJECT
private slots:
    void inlineCommitPreservesDirtyDocumentAndManifest();
    void f2AndEscapeLeaveDiskAndManifestUnchanged();
    void collisionRejectsWithoutDialogOrDataLoss();
    void pathsRejectAndExtensionFreeNamesRetainSourceExtension();
    void foldersAndProjectCannotBeRenamedAsSources();
};

void InlineRenameTests::inlineCommitPreservesDirtyDocumentAndManifest(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("Inline rename"),&error),qPrintable(error));
    QVERIFY2(window.addSource(QStringLiteral("models/before.p"),&error),qPrintable(error));
    auto* document=window.currentDocument();auto* editor=window.currentEditor();const QString oldPath=document->path();
    editor->selectAll();editor->insertPlainText(QStringLiteral("real retained=42;\n"));QVERIFY(document->isModified());
    window.show();selectSource(window,QStringLiteral("models/before.p"));
    auto* rename=window.findChild<QAction*>(QStringLiteral("projectRenameSource"));QVERIFY(rename&&rename->isEnabled());rename->trigger();
    QTRY_VERIFY(renameEditor(window));QPointer<QLineEdit> edit=renameEditor(window);QVERIFY(edit->isVisible());
    QCOMPARE(edit->text(),QStringLiteral("before.p"));QCOMPARE(edit->selectedText(),QStringLiteral("before"));
    QVERIFY(QApplication::activeModalWidget()==nullptr);
    QTest::keyClicks(edit,QStringLiteral("after"));QCOMPARE(edit->text(),QStringLiteral("after.p"));QTest::keyClick(edit,Qt::Key_Return);
    QTRY_VERIFY(window.activeProject()->sources.contains(QStringLiteral("models/after.p")));
    QTRY_VERIFY(edit.isNull());QCOMPARE(window.currentDocument(),document);QCOMPARE(window.currentEditor(),editor);
    QCOMPARE(document->text(),QStringLiteral("real retained=42;\n"));QVERIFY(document->isModified());
    QVERIFY(!QFileInfo::exists(oldPath));QVERIFY(QFileInfo::exists(document->path()));
    QVERIFY(bytes(window.activeProject()->manifestPath).contains("models/after.p"));
    auto* tree=window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
    QCOMPARE(tree->currentItem()->data(0,Qt::UserRole).toString(),QStringLiteral("models/after.p"));
    QVERIFY(window.findChild<QTabWidget*>(QStringLiteral("editorTabs"))->tabText(1).startsWith(QStringLiteral("after.p")));
    QVERIFY2(window.saveCurrentDocument(&error),qPrintable(error));QCOMPARE(bytes(document->path()),QByteArray("real retained=42;\n"));
    QVERIFY(window.close());
}

void InlineRenameTests::f2AndEscapeLeaveDiskAndManifestUnchanged(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Cancel rename"),&error));
    const QString path=window.currentDocument()->path();const auto source=bytes(path),manifest=bytes(window.activeProject()->manifestPath);
    window.show();QVERIFY(QTest::qWaitForWindowActive(&window));selectSource(window,QStringLiteral("main.p"));auto* tree=window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));
    QTest::keyClick(tree,Qt::Key_F2);QTRY_VERIFY(renameEditor(window));QPointer<QLineEdit> edit=renameEditor(window);
    QTest::keyClicks(edit,QStringLiteral("cancelled"));QTest::keyClick(edit,Qt::Key_Escape);QTRY_VERIFY(edit.isNull());
    QCOMPARE(bytes(path),source);QCOMPARE(bytes(window.activeProject()->manifestPath),manifest);
    QCOMPARE(window.activeProject()->sources,QStringList{QStringLiteral("main.p")});
    QCOMPARE(tree->currentItem()->text(0),QStringLiteral("main.p"));QVERIFY(QApplication::activeModalWidget()==nullptr);
    QVERIFY(window.close());
}

void InlineRenameTests::collisionRejectsWithoutDialogOrDataLoss(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Collision rename"),&error));
    QVERIFY(window.addSource(QStringLiteral("taken.p"),&error));
    const QString mainPath=QDir(window.activeProject()->rootPath).filePath(QStringLiteral("main.p"));
    const QString takenPath=window.currentDocument()->path();const auto mainBytes=bytes(mainPath),takenBytes=bytes(takenPath),manifest=bytes(window.activeProject()->manifestPath);
    QVERIFY(window.openSourcePath(mainPath,&error));auto* document=window.currentDocument();
    window.show();selectSource(window,QStringLiteral("main.p"));window.findChild<QAction*>(QStringLiteral("projectRenameSource"))->trigger();
    QTRY_VERIFY(renameEditor(window));auto* edit=renameEditor(window);edit->setText(QStringLiteral("taken.p"));QTest::keyClick(edit,Qt::Key_Return);
    QTRY_VERIFY(window.statusBar()->currentMessage().startsWith(QStringLiteral("Rename failed:")));
    QVERIFY(QApplication::activeModalWidget()==nullptr);QCOMPARE(bytes(mainPath),mainBytes);QCOMPARE(bytes(takenPath),takenBytes);
    QCOMPARE(bytes(window.activeProject()->manifestPath),manifest);QCOMPARE(window.currentDocument(),document);QCOMPARE(document->path(),mainPath);
    QVERIFY(sourceItem(window.findChild<QTreeWidget*>(QStringLiteral("projectTree")),QStringLiteral("main.p")));
    QVERIFY(window.close());
}

void InlineRenameTests::pathsRejectAndExtensionFreeNamesRetainSourceExtension(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Filename rename"),&error));
    const QString path=window.currentDocument()->path();const auto manifest=bytes(window.activeProject()->manifestPath);
    window.show();selectSource(window,QStringLiteral("main.p"));auto* rename=window.findChild<QAction*>(QStringLiteral("projectRenameSource"));rename->trigger();
    QTRY_VERIFY(renameEditor(window));auto* edit=renameEditor(window);edit->setText(QStringLiteral("../outside.p"));QTest::keyClick(edit,Qt::Key_Return);
    QTRY_VERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("without path separators")));
    QCOMPARE(bytes(window.activeProject()->manifestPath),manifest);QVERIFY(QFileInfo::exists(path));
    QVERIFY(QApplication::activeModalWidget()==nullptr);QTRY_VERIFY(!renameEditor(window));rename->trigger();QTRY_VERIFY(renameEditor(window));
    edit=renameEditor(window);edit->setText(QStringLiteral("retained_extension"));QTest::keyClick(edit,Qt::Key_Return);
    QTRY_VERIFY(window.activeProject()->sources.contains(QStringLiteral("retained_extension.p")));
    QVERIFY(window.currentDocument()->path().endsWith(QStringLiteral("retained_extension.p")));QVERIFY(!QFileInfo::exists(path));
    QVERIFY(window.close());
}

void InlineRenameTests::foldersAndProjectCannotBeRenamedAsSources(){
    QTemporaryDir temporary;MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Source only"),&error));
    QVERIFY(window.addSource(QStringLiteral("folder/main.p"),&error));window.show();
    auto* tree=window.findChild<QTreeWidget*>(QStringLiteral("projectTree"));auto* root=tree->topLevelItem(0);QVERIFY(root);
    tree->setCurrentItem(root);QVERIFY(!(root->flags()&Qt::ItemIsEditable));
    auto* rename=window.findChild<QAction*>(QStringLiteral("projectRenameSource"));QVERIFY(!rename->isEnabled());
    auto* folder=sourceItem(tree,QStringLiteral("folder/main.p"))->parent();QVERIFY(!(folder->flags()&Qt::ItemIsEditable));
    tree->setCurrentItem(folder);QVERIFY(!rename->isEnabled());tree->setFocus();QTest::keyClick(tree,Qt::Key_F2);
    QVERIFY(!renameEditor(window));QVERIFY(QApplication::activeModalWidget()==nullptr);QVERIFY(window.close());
}

PENTA_OFFSCREEN_TEST_MAIN(InlineRenameTests)
#include "inline_rename_tests.moc"
