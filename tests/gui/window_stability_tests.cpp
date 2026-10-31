#include "offscreen_test_bootstrap.hpp"
#include "ide/main_window.hpp"
#include "ide/document.hpp"
#include "ide/editor_widget.hpp"
#include "ide/figure_manager.hpp"
#include "ide/raster_plot_canvas.hpp"
#include "ide/desktop/collapsible_dock.hpp"
#include "ide/desktop/figure_docking.hpp"
#include "ide/desktop/studio_workspace_layout.hpp"

#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QOpenGLWidget>
#include <QPainter>
#include <QPointer>
#include <QSignalSpy>
#include <QSettings>
#include <QSurface>
#include <QTemporaryDir>
#include <QTest>
#include <QTextCursor>
#include <QWindow>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace penta;

namespace {
void replaceSource(EditorWidget* editor,const QString& source) {
    QTextCursor cursor(editor->document());cursor.select(QTextCursor::Document);cursor.insertText(source);
}
runtime::FigureAction select(int number) {
    runtime::FigureAction action;action.kind=runtime::FigureActionKind::Select;action.figure=number;
    action.name="Retained figure "+std::to_string(number);return action;
}
runtime::FigureAction plot(int number,int dimensions=3) {
    runtime::FigureAction action;action.kind=runtime::FigureActionKind::Plot;action.figure=number;
    action.dimensions=dimensions;action.axes={1,2,3};action.algebraId="A_gamma_v1";action.gamma=.75;
    action.vectors={{{1,1,2,.5,-.25},"p"},{{0,-1,.5,1,.25},"q"}};
    return action;
}
QList<RasterPlotCanvas*> visibleCanvases(QWidget& root) {
    QList<RasterPlotCanvas*> result;
    for(auto* canvas:root.findChildren<RasterPlotCanvas*>())if(canvas->isVisible())result.push_back(canvas);
    return result;
}
bool framesReady(QWidget& root) {
    const auto canvases=visibleCanvases(root);if(canvases.isEmpty())return false;
    for(const auto* canvas:canvases)if(!canvas->frameCurrent()||canvas->cachedImage().isNull())return false;
    return true;
}
}

// This suite deliberately cannot run against the interactive desktop. It uses
// tiny CPU scenes and Qt's offscreen backend; no native/GPU stress diagnostics.
class WindowStabilityTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void startupIsRasterAndHasNoOpenGLWidgets();
    void floatingFrameNormalizationWaitsForShowCompletion();
    void persistedFloatingPaneAndPinnedCopilotStartSafely();
    void tinyScriptKeepsWindowEditorDocumentAndPlotData();
    void dockAndRedockRetainTheSameCanvasAndCameraView();
    void closeAllAndReplotDiscardRetiredCanvases();
    void cachedRasterRequestsAndDestructionAreSafe();
    void cleanupTestCase();
};

void WindowStabilityTests::initTestCase() {
    if(QApplication::platformName()!=QStringLiteral("offscreen"))QSKIP("This suite is offscreen-only; desktop graphics testing is disabled.");
}

void WindowStabilityTests::startupIsRasterAndHasNoOpenGLWidgets() {
    MainWindow window(false,nullptr,true);window.configureAutosave(false);
    window.resize(800,600);window.show();QApplication::processEvents();
    QVERIFY(window.windowHandle());QCOMPARE(window.windowHandle()->surfaceType(),QSurface::RasterSurface);
    QCOMPARE(window.findChildren<QOpenGLWidget*>().size(),0);QCOMPARE(window.figureManager()->figureCount(),0);
    QVERIFY(!window.isWorking());QVERIFY(!window.windowIcon().isNull());
    QPointer<QWindow> handle=window.windowHandle();const WId identity=window.winId();
    window.resize(820,610);window.move(window.pos()+QPoint(2,2));QApplication::processEvents();
    QCOMPARE(window.windowHandle(),handle.data());QCOMPARE(window.winId(),identity);
    QCOMPARE(window.windowHandle()->surfaceType(),QSurface::RasterSurface);QVERIFY(window.close());
}

void WindowStabilityTests::floatingFrameNormalizationWaitsForShowCompletion() {
    QMainWindow host;auto* dock=new QDockWidget(QStringLiteral("Retained figure"),&host);
    auto* content=new QLabel(QStringLiteral("Scientific view"),dock);dock->setWidget(content);
    auto* title=new QLabel(QStringLiteral("Compact dock header"),dock);dock->setTitleBarWidget(title);
    host.addDockWidget(Qt::RightDockWidgetArea,dock);dock->setFloating(true);dock->hide();
    desktop::FigureDockController controller(content,QStringLiteral("Figures"),QStringLiteral("deferredDockControl"));
    controller.setDockContainer(dock);QCOMPARE(dock->titleBarWidget(),title);
    // Qt can send Show while its saved dock layout is still being traversed.
    // Frame/title replacement must wait until that Show has completely returned.
    dock->show();QCOMPARE(dock->titleBarWidget(),title);
    QTRY_VERIFY_WITH_TIMEOUT(!dock->titleBarWidget(),1000);
    QCOMPARE(dock->windowFlags()&Qt::WindowType_Mask,Qt::WindowFlags(Qt::Window));
    QVERIFY(dock->windowFlags()&Qt::WindowMinimizeButtonHint);QVERIFY(dock->windowFlags()&Qt::WindowMaximizeButtonHint);
    QCOMPARE(dock->widget(),content);QVERIFY(controller.dock());QCOMPARE(dock->titleBarWidget(),title);
}

void WindowStabilityTests::persistedFloatingPaneAndPinnedCopilotStartSafely() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    const QSettings original(QSettings::IniFormat,QSettings::UserScope,QStringLiteral("Pentarnion"),QStringLiteral("Workbench"));
    const auto previousRoot=QFileInfo(QFileInfo(original.fileName()).absolutePath()).absolutePath();
    struct SettingsPathRestore {
        QString root;
        ~SettingsPathRestore(){QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root);}
    } restore{previousRoot};
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());
    QSettings saved(QSettings::IniFormat,QSettings::UserScope,QStringLiteral("Pentarnion"),QStringLiteral("Workbench"));
    QCOMPARE(QDir::cleanPath(saved.fileName()),QDir(temporary.path()).filePath(QStringLiteral("Pentarnion/Workbench.ini")));
    {
        MainWindow first(false,nullptr,false);first.configureAutosave(false);first.resize(800,600);first.show();
        auto* layout=first.findChild<desktop::StudioWorkspaceLayout*>();QVERIFY(layout);
        auto* figures=first.findChild<QDockWidget*>(QStringLiteral("figuresDock"));QVERIFY(figures);
        auto* copilot=dynamic_cast<desktop::CollapsibleDockController*>(first.findChild<QObject*>(QStringLiteral("copilotDockController")));QVERIFY(copilot);
        copilot->setAutoHide(false);copilot->setPanelVisible(true);
        figures->setFloating(true);QApplication::processEvents();figures->hide();QApplication::processEvents();
        saved.setValue(QStringLiteral("window/geometry"),first.saveGeometry());
        saved.setValue(QStringLiteral("window/outerLayoutV2"),first.saveState(2));
        saved.setValue(QStringLiteral("workspace/layoutV2"),layout->saveWorkingState());
        saved.setValue(QStringLiteral("interface/docks/copilotDockController"),copilot->saveState());
        saved.setValue(QStringLiteral("interface/docks/figuresDockDockController"),QByteArrayLiteral("{\"schema\":3,\"autoHide\":false,\"collapsed\":false,\"visible\":false,\"expandedWidth\":420,\"expandedHeight\":300}"));
        saved.setValue(QStringLiteral("interface/mode"),QStringLiteral("ribbon"));
        saved.setValue(QStringLiteral("autosave/enabled"),false);saved.sync();QVERIFY(first.close());
    }
    MainWindow restored(true,nullptr,false);restored.configureAutosave(false);restored.show();QApplication::processEvents();QTest::qWait(100);
    auto* layout=restored.findChild<desktop::StudioWorkspaceLayout*>();QVERIFY(layout);
    auto* copilot=restored.findChild<QDockWidget*>(QStringLiteral("copilotDock"));QVERIFY(copilot);
    auto* controller=dynamic_cast<desktop::CollapsibleDockController*>(restored.findChild<QObject*>(QStringLiteral("copilotDockController")));QVERIFY(controller);
    QVERIFY(!controller->autoHide());QVERIFY(controller->panelVisible());QVERIFY(!controller->drawerOpen());QVERIFY(copilot->isVisible());
    QCOMPARE(copilot->parentWidget(),&restored);QCOMPARE(restored.findChildren<QDockWidget*>(QString(),Qt::FindDirectChildrenOnly).size(),1);
    for(auto* pane:layout->workingDocks()){
        QCOMPARE(pane->parentWidget(),layout->workingHost());
        for(int i=0;i<restored.layout()->count();++i)QVERIFY(restored.layout()->itemAt(i)->widget()!=pane);
    }
    auto* figures=restored.findChild<QDockWidget*>(QStringLiteral("figuresDock"));QVERIFY(figures&&figures->isFloating());QVERIFY(figures->isHidden());
    QCOMPARE(restored.figureManager()->figureCount(),0);QVERIFY(!restored.isWorking());QCOMPARE(restored.currentDocument(),nullptr);
    QVERIFY(restored.close());
}

void WindowStabilityTests::tinyScriptKeepsWindowEditorDocumentAndPlotData() {
    QTemporaryDir temporary;QVERIFY(temporary.isValid());
    MainWindow window(false,nullptr,true);window.configureAutosave(false);QString error;
    QVERIFY2(window.createProjectAt(temporary.path(),QStringLiteral("Raster script lifecycle"),&error),qPrintable(error));
    const QString source=QStringLiteral("use_algebra(\"A_gamma\",gamma=1);\npentarnion p=1+i+2*j+k/2;\nfigure(1,\"Name\",\"Tiny vectors\");plot3(\"i\",\"j\",\"k\",p);\n");
    replaceSource(window.currentEditor(),source);
    QPointer<Document> document=window.currentDocument();QPointer<EditorWidget> editor=window.currentEditor();
    window.resize(900,640);window.show();QApplication::processEvents();
    QPointer<QWindow> handle=window.windowHandle();const WId identity=window.winId();
    const auto icon=window.windowIcon().pixmap(32,32).toImage();QSignalSpy finished(&window,&MainWindow::workFinished);
    QVERIFY2(window.runCurrentDocument(&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isWorking(),10000);QVERIFY(!finished.isEmpty());
    QVERIFY2(window.problemCount()==0,qPrintable(window.resultsText()));
    QTRY_VERIFY_WITH_TIMEOUT(framesReady(window),5000);
    const auto state=window.figureManager()->figureState(1);QVERIFY(state);QCOMPARE(state->vectors.size(),std::size_t(1));
    const auto coefficients=state->vectors.front().coefficients;QCOMPARE(state->name,QStringLiteral("Tiny vectors"));
    window.resize(940,670);QTRY_VERIFY_WITH_TIMEOUT(framesReady(window),5000);
    window.resize(900,640);QTRY_VERIFY_WITH_TIMEOUT(framesReady(window),5000);
    QCOMPARE(window.currentDocument(),document.data());QCOMPARE(window.currentEditor(),editor.data());
    QCOMPARE(window.currentDocument()->text(),source);QVERIFY(!window.currentDocument()->isModified());
    QCOMPARE(window.figureManager()->figureState(1)->vectors.front().coefficients,coefficients);
    QCOMPARE(window.windowHandle(),handle.data());QCOMPARE(window.winId(),identity);
    QCOMPARE(window.windowHandle()->surfaceType(),QSurface::RasterSurface);
    QCOMPARE(window.windowIcon().pixmap(32,32).toImage(),icon);QCOMPARE(window.findChildren<QOpenGLWidget*>().size(),0);
    QVERIFY(window.close());
}

void WindowStabilityTests::dockAndRedockRetainTheSameCanvasAndCameraView() {
    FigureManager manager(nullptr,true);manager.resize(620,480);manager.show();
    QSignalSpy errors(&manager,&FigureManager::error);manager.apply({select(1),plot(1)});
    QTRY_VERIFY_WITH_TIMEOUT(framesReady(manager),5000);QCOMPARE(errors.size(),0);
    QPointer<RasterPlotCanvas> canvas=visibleCanvases(manager).front();const auto originalSize=canvas->size();
    QString error;QVERIFY2(manager.zoomPlot(1,1,1.25,&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),5000);
    const QImage cameraView=canvas->grabFramebuffer();QVERIFY(!cameraView.isNull());
    const auto coefficients=manager.figureState(1)->vectors.front().coefficients;
    QVERIFY2(manager.undockFigure(1,&error),qPrintable(error));QApplication::processEvents();
    QVERIFY(canvas);QVERIFY(!manager.figureState(1)->docked);
    QVERIFY2(manager.dockFigure(1,&error),qPrintable(error));QApplication::processEvents();
    QVERIFY(canvas);QVERIFY(manager.figureState(1)->docked);
    QTRY_COMPARE_WITH_TIMEOUT(canvas->size(),originalSize,5000);QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),5000);
    QCOMPARE(canvas->grabFramebuffer(),cameraView);QCOMPARE(manager.figureState(1)->vectors.front().coefficients,coefficients);
    QCOMPARE(manager.findChildren<QOpenGLWidget*>().size(),0);QCOMPARE(errors.size(),0);
}

void WindowStabilityTests::closeAllAndReplotDiscardRetiredCanvases() {
    MainWindow window(false,nullptr,true);window.configureAutosave(false);window.resize(900,640);window.show();
    auto* manager=window.figureManager();auto* dock=window.findChild<QDockWidget*>(QStringLiteral("figuresDock"));QVERIFY(dock);
    dock->show();manager->apply({select(1),plot(1,2),select(2),plot(2,3)});QString error;
    QVERIFY2(manager->tileStudio(1,2,&error),qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(framesReady(window),5000);
    const auto retired=manager->findChildren<RasterPlotCanvas*>();QCOMPARE(retired.size(),2);
    QPointer<RasterPlotCanvas> first=retired[0],second=retired[1];
    first->requestScene();second->requestScene();
    runtime::FigureAction close;close.kind=runtime::FigureActionKind::CloseAll;manager->apply({close});
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QApplication::processEvents();
    QCOMPARE(manager->figureCount(),0);QVERIFY(first.isNull());QVERIFY(second.isNull());
    manager->apply({select(3),plot(3,2)});QTRY_VERIFY_WITH_TIMEOUT(framesReady(window),5000);
    QCOMPARE(manager->figureCount(),1);QVERIFY(manager->containsFigure(3));QCOMPARE(manager->figureState(3)->vectors.size(),std::size_t(2));
    QCOMPARE(window.findChildren<QOpenGLWidget*>().size(),0);QCOMPARE(window.windowHandle()->surfaceType(),QSurface::RasterSurface);
    QVERIFY(window.close());
}

void WindowStabilityTests::cachedRasterRequestsAndDestructionAreSafe() {
    auto* canvas=new RasterPlotCanvas;canvas->resize(120,90);
    QColor color(Qt::red);auto starts=std::make_shared<std::atomic_int>(0);
    canvas->setSceneProvider([&](QSize pixels,double){
        const QColor snapshot=color;RasterPlotTask task;task.cancelled=std::make_shared<std::atomic_bool>(false);
        task.estimatedBytes=static_cast<std::size_t>(pixels.width()*pixels.height()*4);
        task.paint=[snapshot,pixels,starts]{
            starts->fetch_add(1);std::this_thread::sleep_for(std::chrono::milliseconds(10));
            QImage image(pixels,QImage::Format_ARGB32_Premultiplied);image.fill(snapshot);return image;
        };return task;
    });
    canvas->show();QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),3000);
    QCOMPARE(canvas->cachedImage().pixelColor(10,10),QColor(Qt::red));
    color=Qt::green;canvas->requestScene();color=Qt::blue;canvas->requestScene();
    QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),3000);QCOMPARE(canvas->cachedImage().pixelColor(10,10),QColor(Qt::blue));
    canvas->resize(140,100);QTRY_VERIFY_WITH_TIMEOUT(canvas->frameCurrent(),3000);
    const auto ratio=canvas->devicePixelRatioF();
    QCOMPARE(canvas->cachedImage().size(),QSize(static_cast<int>(std::ceil(140*ratio)),static_cast<int>(std::ceil(100*ratio))));
    canvas->requestScene();QApplication::processEvents();QPointer<RasterPlotCanvas> retired=canvas;delete canvas;
    QVERIFY(retired.isNull());QApplication::processEvents();QTest::qWait(20);
    QTRY_COMPARE_WITH_TIMEOUT(RasterPlotCanvas::pendingSceneCount(),std::size_t(0),3000);
}

void WindowStabilityTests::cleanupTestCase(){shutdownRasterPlotRenderer();}

int main(int argc,char* argv[]) {
    if(!penta::test::requireOffscreenTestPlatform(argc,argv))return 77;
    QApplication application(argc,argv);WindowStabilityTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "window_stability_tests.moc"
