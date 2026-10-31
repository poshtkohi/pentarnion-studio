#include "offscreen_test_bootstrap.hpp"
#include <QtTest>
#include "ide/raster_plot_canvas.hpp"
#include "ide/main_window.hpp"
#include "ide/document.hpp"
#include "ide/figure_manager.hpp"
#include "ide/desktop/desktop_shell.hpp"
#include "ide/workbook/workbook_document.hpp"
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QDir>
#include <QDockWidget>
#include <QOpenGLWidget>
#include <QPainter>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <algorithm>

using namespace penta;
class RasterPresentationTests:public QObject {
    Q_OBJECT
private slots:
    void startupHasNoOpenGLWidget();
    void staleWorkerFramesAreRejected();
    void sceneCaptureAndRasterizationUseCorrectThreads();
    void scriptPlotHasRasterPixelsAndRetainedData();
    void workbookActionsDoNotChangeHiddenSource();
};
void RasterPresentationTests::startupHasNoOpenGLWidget(){
    MainWindow window(false);QCOMPARE(window.findChildren<QOpenGLWidget*>().size(),0);
    QVERIFY(window.desktopShell());QVERIFY(window.findChild<QAction*>(QStringLiteral("interfaceRibbon")));
    QVERIFY(window.findChild<QAction*>(QStringLiteral("helpContext")));QVERIFY(window.findChild<QAction*>(QStringLiteral("dataOpenWorkbook")));
    QCOMPARE(window.figureManager()->figureCount(),0);QVERIFY(!window.isWorking());
    QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("copilotDock"))->isHidden());
    QVERIFY(window.findChild<QDockWidget*>(QStringLiteral("figuresDock"))->isHidden());
    const auto artifacts=qEnvironmentVariable("PENTA_GUI_REBUILD_ARTIFACTS");
    if(!artifacts.isEmpty()){
        QVERIFY(QApplication::platformName()==QStringLiteral("offscreen"));
        QVERIFY(QDir().mkpath(artifacts));window.resize(1100,720);window.show();QApplication::processEvents();
        QVERIFY(window.grab().save(QDir(artifacts).filePath(QStringLiteral("classic-offscreen.png"))));
        window.findChild<QAction*>(QStringLiteral("interfaceRibbon"))->trigger();QApplication::processEvents();
        QVERIFY(window.grab().save(QDir(artifacts).filePath(QStringLiteral("ribbon-offscreen.png"))));
    }
}
void RasterPresentationTests::staleWorkerFramesAreRejected(){
    RasterPlotCanvas canvas;canvas.resize(120,90);auto requested=std::make_shared<std::atomic<int>>(0);
    canvas.setSceneProvider([requested](QSize size,double){const int generation=++*requested;return RasterPlotTask{[size,generation]{
        if(generation==1)QThread::msleep(35);
        QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(generation==1?Qt::red:Qt::blue);return image;
    },std::make_shared<std::atomic_bool>(false),1024};});canvas.show();
    QTRY_VERIFY_WITH_TIMEOUT(requested->load()>0,2000);canvas.requestScene();
    QTRY_VERIFY_WITH_TIMEOUT(canvas.frameCurrent(),4000);QCOMPARE(canvas.cachedImage().pixelColor(10,10),QColor(Qt::blue));
}
void RasterPresentationTests::sceneCaptureAndRasterizationUseCorrectThreads(){
    RasterPlotCanvas canvas;canvas.resize(100,80);std::atomic_bool capturedOnGui{false},paintedOnWorker{false};
    canvas.setSceneProvider([&](QSize size,double){capturedOnGui=QThread::currentThread()==qApp->thread();return RasterPlotTask{[&,size]{paintedOnWorker=QThread::currentThread()!=qApp->thread();QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);return image;},{},1024};});
    canvas.show();QTRY_VERIFY_WITH_TIMEOUT(canvas.frameCurrent(),3000);QVERIFY(capturedOnGui);QVERIFY(paintedOnWorker);
}
void RasterPresentationTests::scriptPlotHasRasterPixelsAndRetainedData(){
    MainWindow window(false);window.resize(1000,700);window.show();QString error;
    QVERIFY2(window.submitCommand(QStringLiteral("pentarnion p=1+i+2*j; figure(1); plot2(\"i\",\"j\",p);"),&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),8000);QCOMPARE(window.problemCount(),0);QCOMPARE(window.figureManager()->figureCount(),1);
    auto* canvas=window.findChild<RasterPlotCanvas*>(QStringLiteral("figureCanvas"));QVERIFY(canvas);
    connect(canvas,&RasterPlotCanvas::frameReady,&window,[canvas]{if(!canvas->renderError().isEmpty())qWarning()<<canvas->renderError();});
    QTRY_VERIFY2_WITH_TIMEOUT(canvas->frameCurrent(),qPrintable(canvas->renderError()),3000);const auto frame=canvas->cachedImage();QVERIFY(!frame.isNull());
    int blue=0;for(int y=0;y<frame.height();++y)for(int x=0;x<frame.width();++x){const auto c=frame.pixelColor(x,y);if(c.blue()>c.red()+30&&c.blue()>c.green()+10)++blue;}
    QVERIFY(blue>20);QCOMPARE(window.findChildren<QOpenGLWidget*>().size(),0);
    const auto original=window.figureManager()->figureState(1);QVERIFY(original);QCOMPARE(original->vectors.size(),std::size_t(1));
    window.resize(1010,710);QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),8000);
    const auto after=window.figureManager()->figureState(1);QVERIFY(std::ranges::equal(after->vectors.front().allCoefficients(),original->vectors.front().allCoefficients()));
    QVERIFY2(window.submitCommand(QStringLiteral("figure(2);plot2(\"i\",\"j\",p);"),&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),3000);
    QTemporaryDir output;QVERIFY(output.isValid());
    QVERIFY2(window.figureManager()->exportFigure(1,output.filePath(QStringLiteral("hidden-figure.png")),&error),qPrintable(error));
    QVERIFY(!QImage(output.filePath(QStringLiteral("hidden-figure.png"))).isNull());
    QVERIFY2(window.figureManager()->exportFigure(1,output.filePath(QStringLiteral("hidden-figure.svg")),&error),qPrintable(error));
    const auto artifacts=qEnvironmentVariable("PENTA_GUI_REBUILD_ARTIFACTS");
    if(!artifacts.isEmpty())QVERIFY(QImage(output.filePath(QStringLiteral("hidden-figure.png"))).save(QDir(artifacts).filePath(QStringLiteral("vector-export-offscreen.png"))));
    QVERIFY2(window.submitCommand(QStringLiteral("close(\"all\");"),&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),3000);QCOMPARE(window.figureManager()->figureCount(),0);
}
void RasterPresentationTests::workbookActionsDoNotChangeHiddenSource(){
    QTemporaryDir temp;QVERIFY(temp.isValid());MainWindow window(false);QString error;
    QVERIFY2(window.createProjectAt(temp.path(),QStringLiteral("MixedDocuments"),&error),qPrintable(error));
    auto* source=window.currentDocument();QVERIFY(source);QFile csv(temp.filePath(QStringLiteral("data.csv")));QVERIFY(csv.open(QIODevice::WriteOnly));csv.write("a,b\n1,2\n");csv.close();
    QVERIFY2(window.openWorkbook(csv.fileName(),&error),qPrintable(error));auto* book=window.currentWorkbook();QVERIFY(book);QTRY_VERIFY_WITH_TIMEOUT(!book->isLoading(),3000);
    QVERIFY(!window.currentDocument());QVERIFY(!window.currentEditor());QVERIFY(book->setCell(1,0,QStringLiteral("42"),&error));QVERIFY(window.saveCurrentDocument(&error));
    QFile reread(csv.fileName());QVERIFY(reread.open(QIODevice::ReadOnly));QVERIFY(reread.readAll().contains("42"));
    auto* workspace=window.findChild<QTabWidget*>(QStringLiteral("workspaceTabs"));QVERIFY(workspace);workspace->setCurrentIndex(0);QCOMPARE(window.currentDocument(),source);
    QVERIFY(!source->isModified());
}
int main(int argc,char** argv){if(!penta::test::requireOffscreenTestPlatform(argc,argv))return 77;QApplication app(argc,argv);RasterPresentationTests tests;const int result=QTest::qExec(&tests,argc,argv);shutdownRasterPlotRenderer();return result;}
#include "raster_presentation_tests.moc"
