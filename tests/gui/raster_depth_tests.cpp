#include "ide/raster_depth_painter.hpp"
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
using penta::RasterPrimitive;
RasterPrimitive triangle(QColor color,std::vector<double> depths) {
    RasterPrimitive result;result.kind=1;result.color=color;
    result.points=QPolygonF{QPointF(2,2),QPointF(30,2),QPointF(2,30)};
    result.vertexDepths=std::move(depths);return result;
}
RasterPrimitive rectangle(QColor color,double depth) {
    RasterPrimitive result;result.kind=1;result.color=color;result.depth=depth;
    result.points=QPolygonF{QPointF(0,0),QPointF(32,0),QPointF(32,32),QPointF(0,32)};return result;
}
QImage draw(const std::vector<RasterPrimitive>& values,QString* error=nullptr) {
    return penta::rasterizePlotGeometry(QSize(32,32),values,QRectF(0,0,32,32),{},error);
}
bool closeColor(QColor actual,QColor expected,int tolerance=1) {
    return std::abs(actual.red()-expected.red())<=tolerance && std::abs(actual.green()-expected.green())<=tolerance &&
           std::abs(actual.blue()-expected.blue())<=tolerance && std::abs(actual.alpha()-expected.alpha())<=tolerance;
}
}

class RasterDepthTests final:public QObject {
    Q_OBJECT
private slots:
    void intersectingTrianglesUseDepthAtEachPixel();
    void translucentLayersAreSortedAtEachPixel();
    void opaqueGeometryOccludesTransparentLayers();
    void roundLinesAndDisksHaveDepthAndAntialiasing();
    void fractionalClipIsAntialiased();
    void concavePolygonsRetainTheirShape();
    void cancellationAndQuotaFailuresAreExplicitAndCheap();
    void malformedGeometryDoesNotProduceAnImage();
};

void RasterDepthTests::intersectingTrianglesUseDepthAtEachPixel() {
    std::vector<RasterPrimitive> values{triangle(Qt::red,{.1,.9,.1}),triangle(Qt::blue,{.5,.5,.5})};
    QString error;const auto first=draw(values,&error);QVERIFY2(!first.isNull(),qPrintable(error));
    QCOMPARE(first.pixelColor(5,5),QColor(Qt::red));
    QCOMPARE(first.pixelColor(22,5),QColor(Qt::blue));
    std::reverse(values.begin(),values.end());const auto second=draw(values,&error);
    QVERIFY2(!second.isNull(),qPrintable(error));QVERIFY(first==second);
    QCOMPARE(first.pixelColor(31,31).alpha(),0);
}

void RasterDepthTests::translucentLayersAreSortedAtEachPixel() {
    std::vector<RasterPrimitive> values{rectangle(Qt::blue,.9),rectangle(QColor(255,0,0,128),.2),rectangle(QColor(0,255,0,128),.5)};
    QString error;const auto first=draw(values,&error);QVERIFY2(!first.isNull(),qPrintable(error));
    QVERIFY(closeColor(first.pixelColor(8,10),QColor(128,64,63,255)));
    std::reverse(values.begin(),values.end());const auto second=draw(values,&error);QVERIFY(first==second);
    // The two translucent planes cross inside the same triangle footprint.
    values={rectangle(Qt::blue,.95),triangle(QColor(255,0,0,128),{.1,.9,.1}),triangle(QColor(0,255,0,128),{.5,.5,.5})};
    const auto crossing=draw(values,&error);QVERIFY2(!crossing.isNull(),qPrintable(error));
    QVERIFY(crossing.pixelColor(5,5).red()>crossing.pixelColor(5,5).green());
    QVERIFY(crossing.pixelColor(22,5).green()>crossing.pixelColor(22,5).red());
}

void RasterDepthTests::opaqueGeometryOccludesTransparentLayers() {
    std::vector<RasterPrimitive> values{rectangle(QColor(255,0,0,128),.7),rectangle(Qt::blue,.2),rectangle(QColor(0,255,0,128),.5)};
    const auto first=draw(values);QVERIFY(!first.isNull());QCOMPARE(first.pixelColor(9,11),QColor(Qt::blue));
    std::reverse(values.begin(),values.end());QVERIFY(first==draw(values));
    // One transparent layer in front is retained while layers behind are removed.
    values.push_back(rectangle(QColor(255,0,0,128),.1));
    QVERIFY(closeColor(draw(values).pixelColor(9,11),QColor(128,0,127,255)));
}

void RasterDepthTests::roundLinesAndDisksHaveDepthAndAntialiasing() {
    RasterPrimitive line;line.color=Qt::red;line.width=2;
    line.points=QPolygonF{QPointF(3,8.5),QPointF(29,8.5)};line.vertexDepths={.2,.8};
    RasterPrimitive near;near.kind=2;near.color=Qt::green;near.width=5;near.depth=.1;near.points=QPolygonF{QPointF(12,8.5)};
    auto far=near;far.color=Qt::blue;far.width=3;far.depth=.9;far.points=QPolygonF{QPointF(24,8.5)};
    const auto image=draw({far,line,near});QVERIFY(!image.isNull());
    QCOMPARE(image.pixelColor(12,8),QColor(Qt::green));QCOMPARE(image.pixelColor(24,8),QColor(Qt::red));
    QVERIFY(image.pixelColor(18,7).alpha()>0);QVERIFY(image.pixelColor(18,7).alpha()<255);
    QCOMPARE(image.pixelColor(18,6).alpha(),0);
    QVERIFY(image.pixelColor(2,8).alpha()>0); // Rounded first endpoint.
}

void RasterDepthTests::fractionalClipIsAntialiased() {
    QString error;const auto image=penta::rasterizePlotGeometry(QSize(32,32),{rectangle(Qt::red,.5)},QRectF(8.5,8.5,8,8),{},&error);
    QVERIFY2(!image.isNull(),qPrintable(error));QCOMPARE(image.pixelColor(7,9).alpha(),0);
    QCOMPARE(image.pixelColor(9,9),QColor(Qt::red));
    QVERIFY(std::abs(image.pixelColor(8,8).alpha()-64)<=1);
    QCOMPARE(image.pixelColor(17,9).alpha(),0);
}

void RasterDepthTests::concavePolygonsRetainTheirShape() {
    RasterPrimitive shape;shape.kind=1;shape.color=Qt::magenta;shape.depth=.3;
    shape.points=QPolygonF{QPointF(2,2),QPointF(28,2),QPointF(28,9),QPointF(9,9),QPointF(9,28),QPointF(2,28)};
    QString error;const auto image=draw({shape},&error);QVERIFY2(!image.isNull(),qPrintable(error));
    QCOMPARE(image.pixelColor(5,20),QColor(Qt::magenta));QCOMPARE(image.pixelColor(20,5),QColor(Qt::magenta));
    QCOMPARE(image.pixelColor(20,20).alpha(),0);
}

void RasterDepthTests::cancellationAndQuotaFailuresAreExplicitAndCheap() {
    QString error;auto cancel=std::make_shared<std::atomic_bool>(true);
    QVERIFY(penta::rasterizePlotGeometry(QSize(32,32),{rectangle(Qt::red,.5)},QRectF(0,0,32,32),cancel,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("cancelled")));
    penta::RasterDepthBudget budget;budget.maximumOperations=100;
    QVERIFY(penta::rasterizePlotGeometry(QSize(32,32),{rectangle(Qt::red,.5)},QRectF(0,0,32,32),{},budget,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("operation")));
    budget={};budget.maximumBytes=1024;
    QVERIFY(penta::rasterizePlotGeometry(QSize(32,32),{},QRectF(0,0,32,32),{},budget,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("storage")));
    budget={};budget.maximumBytes=400000;
    std::vector<RasterPrimitive> layers(10,rectangle(QColor(255,0,0,100),.4));
    QVERIFY(penta::rasterizePlotGeometry(QSize(32,32),layers,QRectF(0,0,32,32),{},budget,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("fragments")));
    budget={};budget.maximumPixels=100;
    QVERIFY(penta::rasterizePlotGeometry(QSize(32,32),{},QRectF(0,0,32,32),{},budget,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("pixel")));
    QVERIFY(penta::rasterizePlotGeometry(QSize(16001,1),{},QRectF(0,0,1,1),{},&error).isNull());
}

void RasterDepthTests::malformedGeometryDoesNotProduceAnImage() {
    QString error;auto value=triangle(Qt::red,{.2,.2});QVERIFY(draw({value},&error).isNull());
    QVERIFY(error.contains(QStringLiteral("depth")));
    value=triangle(Qt::red,{.2,.2,.2});value.points[1].setX(std::numeric_limits<double>::quiet_NaN());
    QVERIFY(draw({value},&error).isNull());QVERIFY(error.contains(QStringLiteral("non-finite")));
    value=rectangle(Qt::red,.2);value.points=QPolygonF{QPointF(2,2),QPointF(28,28),QPointF(28,2),QPointF(2,28)};
    QVERIFY(draw({value},&error).isNull());QVERIFY(error.contains(QStringLiteral("self-intersecting")));
    value=rectangle(Qt::red,.2);value.kind=5;QVERIFY(draw({value},&error).isNull());
}

QTEST_APPLESS_MAIN(RasterDepthTests)
#include "raster_depth_tests.moc"
