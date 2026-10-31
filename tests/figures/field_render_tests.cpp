#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/figure_manager.hpp"
#include "ide/raster_plot_canvas.hpp"
#include "plotting/native_export.hpp"
#include "runtime/interpreter.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtSvg/QSvgRenderer>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace penta;
namespace {
const char* source=R"PENTA(
matrix x=linspace(-1,1,13);matrix y=linspace(-1,1,13);
[matrix X,matrix Y]=meshgrid(x,y);matrix Z=X.*X+Y.*Y;
figure(1,"Name","Real fields and mathematical geometry","NumberTitle","off");
subplot(2,2,1);imagesc(x,y,Z);title("Real scalar field");xlabel("$x$");ylabel("$y$");
subplot(2,2,2);surf(x,y,Z);title("Surface: $x^2+y^2$");xlabel("$x$");ylabel("$y$");zlabel("$u$");
subplot(2,2,3);contour(x,y,Z,[0.3,0.7,1.2]);title("Scalar level sets");xlabel("$x$");ylabel("$y$");
matrix z=linspace(-1,1,9);matrix volume=zeros(117,13);
for(zz=1:9){for(yy=1:13){for(xx=1:13){volume((zz-1)*13+yy,xx)=x(xx)^2+y(yy)^2+z(zz)^2;}}}
subplot(2,2,4);isosurface(x,y,z,volume,0.7);title("Isosurface: $x^2+y^2+z^2=0.7$");xlabel("$x$");ylabel("$y$");zlabel("$z$");
)PENTA";
QString diagnostics(const runtime::ExecutionResult& result){QString text;for(const auto& d:result.analysis.diagnostics)text+=QString::fromStdString(d.code+": "+d.message)+"\n";for(const auto& d:result.runtimeDiagnostics)text+=QString::fromStdString(d.code+": "+d.message)+"\n";return text;}
runtime::FigureAction selectPanel(int index){runtime::FigureAction action;action.kind=runtime::FigureActionKind::Subplot;action.rows=action.columns=2;action.subplotIndex=index;return action;}
int colouredPixels(const QImage& image,const QRect& area){
    int result=0;
    for(int y=area.top();y<=area.bottom();++y)for(int x=area.left();x<=area.right();++x){
        const auto colour=image.pixelColor(x,y);
        if(colour.alpha()>128&&std::max({colour.red(),colour.green(),colour.blue()})-std::min({colour.red(),colour.green(),colour.blue()})>40)++result;
    }
    return result;
}
bool currentFrames(const QList<RasterPlotCanvas*>& canvases){
    return !canvases.empty()&&std::all_of(canvases.begin(),canvases.end(),[](const auto* canvas){return canvas->frameCurrent()&&!canvas->cachedImage().isNull()&&canvas->renderError().isEmpty();});
}
}

class FieldRenderTests final:public QObject {
    Q_OBJECT
private slots:
    void nativeLanguageGeometryRetainsMeshAndRejectsInvalidIndices();
    void nativeColorMappedFieldsAndSubplotsExport();
    void geometryExampleWithoutFieldsRendersAndExportsBothSubplots();
    void optionalMeshFieldsKeepDataTipsAndValidationAccurate();
    void cleanupTestCase(){shutdownRasterPlotRenderer();}
};

void FieldRenderTests::nativeLanguageGeometryRetainsMeshAndRejectsInvalidIndices(){
    const auto result=runtime::execute(source);QVERIFY2(result.ok(),qPrintable(diagnostics(result)));
    FigureManager manager(nullptr,false);QSignalSpy errors(&manager,&FigureManager::error);manager.apply(result.figureActions);QCOMPARE(errors.size(),0);QCOMPARE(manager.figureCount(),1);
    const auto figure=manager.figureState(1);QVERIFY(figure);QCOMPARE(figure->rows,2);QCOMPARE(figure->columns,2);QCOMPARE(figure->subplots.size(),std::size_t(4));
    for(const auto& panel:figure->subplots){QVERIFY(panel.mesh);QVERIFY(!panel.mesh->points.empty());QCOMPARE(panel.mesh->scalars.size(),panel.mesh->points.size());QVERIFY(panel.curves.empty());QVERIFY(panel.vectors.empty());QVERIFY(panel.axes==(std::array<int,3>{-1,-1,-1}));for(const auto& face:panel.mesh->triangles)for(const auto id:face)QVERIFY(id<panel.mesh->points.size());for(const auto& edge:panel.mesh->segments)for(const auto id:edge)QVERIFY(id<panel.mesh->points.size());}
    const auto& image=figure->subplots[0];const auto& surface=figure->subplots[1];const auto& contours=figure->subplots[2];const auto& iso=figure->subplots[3];
    QCOMPARE(image.dimensions,2);QCOMPARE(surface.dimensions,3);QCOMPARE(contours.dimensions,2);QCOMPARE(iso.dimensions,3);QCOMPARE(image.mesh->points.size(),std::size_t(169));QCOMPARE(surface.mesh->points.size(),std::size_t(169));QCOMPARE(surface.mesh->triangles.size(),std::size_t(288));
    QCOMPARE(image.mesh->points.front()[2],0.0);QCOMPARE(image.mesh->scalars.front(),2.0);QCOMPARE(surface.mesh->points.front()[2],2.0);QVERIFY(!contours.mesh->segments.empty());QVERIFY(!iso.mesh->triangles.empty());for(const double scalar:iso.mesh->scalars)QVERIFY(std::abs(scalar-.7)<1e-14);
    const auto plot=std::find_if(result.figureActions.begin(),result.figureActions.end(),[](const auto& a){return a.mesh&&a.mesh->label=="surf";});QVERIFY(plot!=result.figureActions.end());manager.apply({selectPanel(2)});const auto before=manager.figureState(1);QVERIFY(before&&before->mesh);
    auto invalid=*plot;invalid.mesh->triangles[0][2]=static_cast<std::uint32_t>(invalid.mesh->points.size());manager.apply({invalid});QCOMPARE(errors.size(),1);QVERIFY(manager.figureState(1)->mesh->points==before->mesh->points);QVERIFY(manager.figureState(1)->mesh->triangles==before->mesh->triangles);
    invalid=*plot;invalid.mesh->segments.push_back({0,static_cast<std::uint32_t>(invalid.mesh->points.size())});manager.apply({invalid});QCOMPARE(errors.size(),2);QVERIFY(manager.figureState(1)->mesh->triangles==before->mesh->triangles);
    invalid=*plot;invalid.mesh->scalars.pop_back();manager.apply({invalid});QCOMPARE(errors.size(),3);QVERIFY(manager.figureState(1)->mesh->scalars==before->mesh->scalars);
    invalid=*plot;invalid.mesh->points[0][0]=std::numeric_limits<double>::quiet_NaN();manager.apply({invalid});QCOMPARE(errors.size(),4);QVERIFY(manager.figureState(1)->mesh->points==before->mesh->points);
    invalid=*plot;invalid.mesh->scalars[0]=std::numeric_limits<double>::infinity();manager.apply({invalid});QCOMPARE(errors.size(),5);QVERIFY(manager.figureState(1)->mesh->scalars==before->mesh->scalars);
}

void FieldRenderTests::nativeColorMappedFieldsAndSubplotsExport() {
    QSKIP("Historical native/GPU visual acceptance is disabled; use the bounded offscreen raster presentation and depth suites.");
}

void FieldRenderTests::geometryExampleWithoutFieldsRendersAndExportsBothSubplots(){
    const auto path=QFINDTESTDATA("../../examples/real-geometry/primitives-and-intersections.p");QVERIFY(!path.isEmpty());
    QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();
    const auto result=runtime::execute(std::string_view(bytes.constData(),static_cast<std::size_t>(bytes.size())));
    QVERIFY2(result.ok(),qPrintable(diagnostics(result)));
    const auto meshActions=std::count_if(result.figureActions.begin(),result.figureActions.end(),[](const auto& action){return action.mesh.has_value();});
    QCOMPARE(meshActions,2);
    for(const auto& action:result.figureActions)if(action.mesh){QVERIFY(!action.mesh->points.empty());QVERIFY(!action.mesh->triangles.empty());QVERIFY(action.mesh->scalars.empty());}
    FigureManager manager(nullptr,true);manager.resize(640,320);manager.show();QSignalSpy errors(&manager,&FigureManager::error);
    manager.apply(result.figureActions);QVERIFY2(errors.empty(),errors.empty()?"":qPrintable(errors.first().first().toString()));
    const auto state=manager.figureState(1);QVERIFY(state);QCOMPARE(state->rows,1);QCOMPARE(state->columns,2);QCOMPARE(state->subplots.size(),std::size_t(2));
    for(const auto& panel:state->subplots){QCOMPARE(panel.dimensions,3);QVERIFY(panel.mesh);QVERIFY(panel.mesh->scalars.empty());QVERIFY(panel.style.equalUnits);}
    const auto canvases=manager.findChildren<RasterPlotCanvas*>();QCOMPARE(canvases.size(),2);
    QTRY_VERIFY_WITH_TIMEOUT(currentFrames(canvases),5000);
    for(const auto* canvas:canvases)QVERIFY(colouredPixels(canvas->cachedImage(),canvas->cachedImage().rect())>100);
    QTemporaryDir output;QVERIFY(output.isValid());QString error;
    runtime::FigureExportOptions options;options.widthCm=12;options.heightCm=6;options.dpi=75;
    const auto png=output.filePath(QStringLiteral("geometry.png")),svg=output.filePath(QStringLiteral("geometry.svg"));
    QVERIFY2(manager.exportFigure(1,png,options,&error),qPrintable(error));QImage exported(png);QVERIFY(!exported.isNull());
    const QRect left(0,0,exported.width()/2,exported.height()),right(exported.width()/2,0,exported.width()-exported.width()/2,exported.height());
    QVERIFY(colouredPixels(exported,left)>100);QVERIFY(colouredPixels(exported,right)>100);
    QVERIFY2(manager.exportFigure(1,svg,options,&error),qPrintable(error));QFile vectorFile(svg);QVERIFY(vectorFile.open(QIODevice::ReadOnly));const auto vectorData=vectorFile.readAll();
    QSvgRenderer renderer(vectorData);QVERIFY(renderer.isValid());QVERIFY(!vectorData.contains("<image"));
    QImage vectorPixels(exported.size(),QImage::Format_ARGB32_Premultiplied);vectorPixels.fill(Qt::white);{QPainter painter(&vectorPixels);renderer.render(&painter);}
    QVERIFY(colouredPixels(vectorPixels,left)>100);QVERIFY(colouredPixels(vectorPixels,right)>100);
    const auto artifacts=qEnvironmentVariable("PENTA_GUI_REBUILD_ARTIFACTS");
    if(!artifacts.isEmpty()){
        QVERIFY2(QDir().mkpath(artifacts),"Cannot create the bounded GUI artifact directory.");
        QVERIFY2(exported.save(QDir(artifacts).filePath(QStringLiteral("real-geometry.png"))),"Cannot save the geometry PNG artifact.");
        QFile artifact(QDir(artifacts).filePath(QStringLiteral("real-geometry.svg")));QVERIFY2(artifact.open(QIODevice::WriteOnly|QIODevice::Truncate),qPrintable(artifact.errorString()));
        QCOMPARE(artifact.write(vectorData),qint64(vectorData.size()));QVERIFY2(artifact.flush(),qPrintable(artifact.errorString()));
    }
    // The native headless exporter consumes the same interpreter actions and
    // must accept geometry without inventing a per-vertex scalar field.
    plotting::NativeFigureScene scene;scene.ingest(result.figureActions);plotting::PngOptions nativeOptions;nativeOptions.width=480;nativeOptions.height=240;
    const auto nativePath=output.filePath(QStringLiteral("native-geometry.png"));
    try{scene.exportCurrentPng(std::filesystem::path(nativePath.toStdU16String()),nativeOptions,1);}
    catch(const std::exception& exception){QFAIL(exception.what());}
    QImage nativePixels(nativePath);QVERIFY(!nativePixels.isNull());
    QVERIFY(colouredPixels(nativePixels,QRect(0,0,240,240))>100);QVERIFY(colouredPixels(nativePixels,QRect(240,0,240,240))>100);
}

void FieldRenderTests::optionalMeshFieldsKeepDataTipsAndValidationAccurate(){
    const auto result=runtime::execute(R"PENTA(record P=meshplane([0,1],[0,1],options(nx=3,ny=3));
figure(1);subplot(1,2,1);meshplot(P,options(projection=[1,2]));
subplot(1,2,2);meshplot(P,options(projection=[1,2],values=linspace(-1,1,9)));)PENTA");
    QVERIFY2(result.ok(),qPrintable(diagnostics(result)));
    FigureManager manager(nullptr,true);manager.resize(640,300);manager.show();QSignalSpy errors(&manager,&FigureManager::error);manager.apply(result.figureActions);
    QCOMPARE(errors.size(),0);const auto state=manager.figureState(1);QVERIFY(state);QCOMPARE(state->subplots.size(),std::size_t(2));
    QVERIFY(state->subplots[0].mesh->scalars.empty());QCOMPARE(state->subplots[1].mesh->scalars.size(),std::size_t(9));
    const auto canvases=manager.findChildren<RasterPlotCanvas*>();QCOMPARE(canvases.size(),2);QTRY_VERIFY_WITH_TIMEOUT(currentFrames(canvases),5000);
    for(int index=1;index<=2;++index){
        auto* panel=manager.findChild<QWidget*>(QStringLiteral("subplot%1").arg(index));QVERIFY(panel);auto* canvas=panel->findChild<RasterPlotCanvas*>();QVERIFY(canvas);
        const auto point=manager.pickPlotPoint(1,index,QPointF(canvas->width()/2.0,canvas->height()/2.0),400);QVERIFY(point);
        if(index==1)QVERIFY(!point->scalarValue.has_value());
        else{QVERIFY(point->scalarValue.has_value());QCOMPARE(*point->scalarValue,state->subplots[1].mesh->scalars[point->index-1]);}
    }
    auto plot=std::find_if(result.figureActions.begin(),result.figureActions.end(),[](const auto& action){return action.mesh&&!action.mesh->scalars.empty();});QVERIFY(plot!=result.figureActions.end());
    auto invalid=*plot;invalid.mesh->scalars.pop_back();manager.apply({invalid});QCOMPARE(errors.size(),1);
    QCOMPARE(manager.figureState(1)->subplots[1].mesh->scalars,state->subplots[1].mesh->scalars);
    const auto bad=runtime::execute("record P=meshplane([0,1],[0,1],options(nx=3,ny=3));meshplot(P,options(values=[1,2]));");
    QVERIFY(!bad.ok());QVERIFY(diagnostics(bad).contains(QStringLiteral("one real value per vertex")));
}

PENTA_OFFSCREEN_TEST_MAIN(FieldRenderTests)

#include "field_render_tests.moc"
