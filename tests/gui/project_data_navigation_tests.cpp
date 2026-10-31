#include "offscreen_test_bootstrap.hpp"
#include "ide/main_window.hpp"
#include "ide/document.hpp"
#include "ide/workbook/workbook_document.hpp"
#include "ide/desktop/project_file_icons.hpp"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QtTest>
#include <filesystem>

using namespace penta;
namespace {
bool writeBytes(const QString& path,const QByteArray& bytes){
    QFile file(path);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
}
QByteArray readBytes(const QString& path){
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return {};
    return file.readAll();
}
QTreeWidgetItem* itemAt(QTreeWidget* tree,const QString& relative){
    for(QTreeWidgetItemIterator it(tree);*it;++it)
        if((*it)->data(0,Qt::UserRole).toString()==relative)return *it;
    return nullptr;
}
QAction* action(MainWindow& window,const QString& name){return window.findChild<QAction*>(name);}
}

class ProjectDataNavigationTests final:public QObject {
    Q_OBJECT
private slots:
    void projectShowsTypedDataAndSkipsGeneratedCaches();
    void doubleClickCsvOpensDataWithoutChangingSource();
    void folderAndSelectionStateSurviveRefresh();
    void unregisteredSourcesCannotUseSourceActions();
    void directoryLinksAreNotTraversed();
};

void ProjectDataNavigationTests::projectShowsTypedDataAndSkipsGeneratedCaches(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("Data navigation"),&error),qPrintable(error));window.configureAutosave(false);
    const QDir project(window.activeProject()->rootPath);
    for(const auto& folder:{"data/nested","output",".user-data","empty-user-folder",".penta-cache/odes/generated/deeper",".penta-native/callbacks",".git/objects"})QVERIFY(project.mkpath(QString::fromLatin1(folder)));
    QVERIFY(writeBytes(project.filePath("data/input.csv"),"x,y\n1,2\n"));
    QVERIFY(writeBytes(project.filePath("data/nested/table.tsv"),"a\tb\n3\t4\n"));
    QVERIFY(writeBytes(project.filePath(".user-data/visible.csv"),"1,2\n"));
    QVERIFY(writeBytes(project.filePath("output/result.csv"),"5,6\n"));
    QVERIFY(writeBytes(project.filePath(".penta-cache/odes/generated/deeper/hidden.csv"),"not user data"));
    QVERIFY(writeBytes(project.filePath(".penta-native/callbacks/hidden.tsv"),"not user data"));
    QVERIFY(writeBytes(project.filePath(".git/objects/hidden.csv"),"not user data"));
    workbook::WorkbookDocument book;QVERIFY2(book.saveAs(project.filePath("data/book.xlsx"),&error),qPrintable(error));
    QVERIFY2(window.addSource(QStringLiteral("refresh.p"),&error),qPrintable(error));
    auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);
    for(const auto& path:{"data/input.csv","data/nested/table.tsv","data/book.xlsx",".user-data/visible.csv","output/result.csv","empty-user-folder","main.p","refresh.p"})QVERIFY2(itemAt(tree,QString::fromLatin1(path)),path);
    QVERIFY(!itemAt(tree,QStringLiteral(".penta-cache")));QVERIFY(!itemAt(tree,QStringLiteral(".penta-native")));QVERIFY(!itemAt(tree,QStringLiteral(".git")));
    QVERIFY(itemAt(tree,"empty-user-folder")->data(0,Qt::UserRole+1).toBool());
    QVERIFY(!itemAt(tree,"data/input.csv")->flags().testFlag(Qt::ItemIsEditable));
    QVERIFY(itemAt(tree,"main.p")->flags().testFlag(Qt::ItemIsEditable));
    QCOMPARE(tree->topLevelItem(0)->icon(0).cacheKey(),desktop::projectKindIcon(desktop::ProjectIconKind::Project).cacheKey());
    QCOMPARE(itemAt(tree,"main.p")->icon(0).cacheKey(),desktop::projectKindIcon(desktop::ProjectIconKind::Model).cacheKey());
    QCOMPARE(itemAt(tree,"data/input.csv")->icon(0).cacheKey(),desktop::projectKindIcon(desktop::ProjectIconKind::Csv).cacheKey());
    QCOMPARE(itemAt(tree,"data/book.xlsx")->icon(0).cacheKey(),desktop::projectKindIcon(desktop::ProjectIconKind::Spreadsheet).cacheKey());
    QVERIFY(!tree->property("pentarnionProjectTreeLimited").toBool());QVERIFY(window.close());
}

void ProjectDataNavigationTests::doubleClickCsvOpensDataWithoutChangingSource(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("Open scientific data"),&error),qPrintable(error));window.configureAutosave(false);
    const QDir project(window.activeProject()->rootPath);QVERIFY(project.mkpath("data"));
    const auto csv=project.filePath("data/input.csv");const QByteArray csvBytes("a,b\n11,22\n");QVERIFY(writeBytes(csv,csvBytes));
    QVERIFY2(window.addSource(QStringLiteral("refresh.p"),&error),qPrintable(error));
    QHash<QString,QByteArray> sources;for(const auto& relative:window.activeProject()->sources)sources.insert(relative,readBytes(project.filePath(relative)));
    const auto registered=window.activeProject()->sources;QPointer<Document> source=window.currentDocument();QVERIFY(source);const auto sourceText=source->text();const int sourceTabs=window.documentCount();
    auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);auto* item=itemAt(tree,"data/input.csv");QVERIFY(item);tree->setCurrentItem(item);
    QVERIFY(action(window,"projectRunSource"));QVERIFY(action(window,"projectRenameSource"));
    QVERIFY(!action(window,"projectRunSource")->isEnabled());QVERIFY(!action(window,"projectRenameSource")->isEnabled());
    tree->itemDoubleClicked(item,0);
    auto* document=window.currentWorkbook();QVERIFY(document);QSignalSpy loaded(document,&workbook::WorkbookDocument::loadingFinished);
    QTRY_VERIFY_WITH_TIMEOUT(!document->isLoading(),5000);QVERIFY(document->statusText().contains("input.csv"));
    QCOMPARE(QFileInfo(document->path()).canonicalFilePath(),QFileInfo(csv).canonicalFilePath());QCOMPARE(document->cellText(1,0),QStringLiteral("11"));
    QVERIFY(window.currentDocument()==nullptr);QVERIFY(window.currentEditor()==nullptr);QCOMPARE(window.documentCount(),sourceTabs);
    QVERIFY(!action(window,"runFile")->isEnabled());QVERIFY(!action(window,"projectRunSource")->isEnabled());QVERIFY(!action(window,"projectRenameSource")->isEnabled());
    QVERIFY(source);QCOMPARE(source->text(),sourceText);QVERIFY(!source->isModified());QCOMPARE(window.activeProject()->sources,registered);
    for(auto it=sources.cbegin();it!=sources.cend();++it)QCOMPARE(readBytes(project.filePath(it.key())),it.value());
    QCOMPARE(readBytes(csv),csvBytes);QVERIFY(window.close());
}

void ProjectDataNavigationTests::folderAndSelectionStateSurviveRefresh(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Folder state"),&error));window.configureAutosave(false);const QDir project(window.activeProject()->rootPath);
    QVERIFY(project.mkpath("data/nested"));QVERIFY(project.mkpath("user-empty"));QVERIFY(project.mkpath(".penta-cache/odes"));QVERIFY(writeBytes(project.filePath("data/nested/input.csv"),"1,2\n"));QVERIFY(window.addSource("first.p",&error));
    auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);auto* data=itemAt(tree,"data");auto* nested=itemAt(tree,"data/nested");QVERIFY(data&&nested);QVERIFY(!data->isExpanded());QVERIFY(!nested->isExpanded());QVERIFY(!itemAt(tree,"user-empty")->isExpanded());
    data->setExpanded(true);nested->setExpanded(true);tree->setCurrentItem(itemAt(tree,"data/nested/input.csv"));
    QVERIFY2(window.addSource("second.p",&error),qPrintable(error));QVERIFY(itemAt(tree,"data")->isExpanded());QVERIFY(itemAt(tree,"data/nested")->isExpanded());QVERIFY(!itemAt(tree,"user-empty")->isExpanded());QVERIFY(!itemAt(tree,".penta-cache"));
    QCOMPARE(tree->currentItem()->data(0,Qt::UserRole).toString(),QStringLiteral("data/nested/input.csv"));
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Another project"),&error));QVERIFY(tree->topLevelItem(0)->isExpanded());QVERIFY(!itemAt(tree,"data"));QVERIFY(window.close());
}

void ProjectDataNavigationTests::unregisteredSourcesCannotUseSourceActions(){
    QTemporaryDir temporary;QVERIFY(temporary.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("Registered sources"),&error));window.configureAutosave(false);const QDir project(window.activeProject()->rootPath);QVERIFY(writeBytes(project.filePath("loose.p"),"display(99);\n"));QVERIFY(window.addSource("registered.p",&error));
    auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);QVERIFY(!itemAt(tree,"loose.p"));
    auto* fake=new QTreeWidgetItem(tree->topLevelItem(0),{QStringLiteral("loose.p")});fake->setData(0,Qt::UserRole,QStringLiteral("loose.p"));fake->setData(0,Qt::UserRole+1,false);tree->setCurrentItem(fake);
    QVERIFY(!action(window,"projectRunSource")->isEnabled());QVERIFY(!action(window,"projectRenameSource")->isEnabled());
    tree->setCurrentItem(itemAt(tree,"registered.p"));QVERIFY(action(window,"projectRunSource")->isEnabled());QVERIFY(action(window,"projectRenameSource")->isEnabled());QVERIFY(window.close());
}

void ProjectDataNavigationTests::directoryLinksAreNotTraversed(){
    QTemporaryDir temporary,outside;QVERIFY(temporary.isValid()&&outside.isValid());MainWindow window(false,nullptr,false);QString error;
    QVERIFY(window.createProjectAt(temporary.path(),QStringLiteral("No directory links"),&error));window.configureAutosave(false);QVERIFY(writeBytes(outside.filePath("outside.csv"),"1,2\n"));
    const auto linked=QDir(window.activeProject()->rootPath).filePath("linked-data");std::error_code result;
    std::filesystem::create_directory_symlink(std::filesystem::path(outside.path().toStdU16String()),std::filesystem::path(linked.toStdU16String()),result);
    if(result)QSKIP("Temporary directory links are unavailable on this platform/account.");
    QVERIFY(window.addSource("refresh.p",&error));auto* tree=window.findChild<QTreeWidget*>("projectTree");QVERIFY(tree);QVERIFY(!itemAt(tree,"linked-data"));QVERIFY(!itemAt(tree,"linked-data/outside.csv"));QCOMPARE(readBytes(outside.filePath("outside.csv")),QByteArray("1,2\n"));QVERIFY(window.close());
}

PENTA_OFFSCREEN_TEST_MAIN(ProjectDataNavigationTests)
#include "project_data_navigation_tests.moc"
