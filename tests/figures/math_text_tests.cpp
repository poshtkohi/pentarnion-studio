#include "../gui/offscreen_test_bootstrap.hpp"
#include "ide/math_text_renderer.hpp"
#include <QtTest>
#include <QDir>
#include <QPainter>

using namespace penta;
class MathTextTests final:public QObject {
 Q_OBJECT
private slots:
 void validMathAndPlainText() {
    const QStringList labels{QStringLiteral("Vector addition"),QStringLiteral("$p+q$"),
      QStringLiteral("$\\alpha_i^2+\\beta_j$"),QStringLiteral("$\\frac{1}{2}+\\sqrt{x^2+y^2}$"),
      QStringLiteral("$\\sum_{n=0}^{N} n^2$"),QStringLiteral("$p\\star q$"),
      QStringLiteral("Projection: $p_i, p_j, p_k$")};
    for(const auto& text:labels) {
      QString error;const auto image=renderMathText(text,QFont(QStringLiteral("Arial"),16),Qt::black,true,&error);
      QVERIFY2(!image.isNull(),qPrintable(text+QStringLiteral(": ")+error));
      bool ink=false;for(int y=0;y<image.height()&&!ink;++y)for(int x=0;x<image.width();++x)if(qAlpha(image.pixel(x,y))) {ink=true;break;}
      QVERIFY(ink);
    }
    QString error;
    QVERIFY(!renderMathText(QStringLiteral("\\not_a_supported_command"),QFont{},Qt::black,false,&error).isNull());
 }
 void vectorTypographyFitsAcrossOutputDpi(){
    const QRectF target(80,70,400,100);QRect reference;
    for(const int dpi:{96,150,300}){QImage output(600,250,QImage::Format_ARGB32_Premultiplied);output.fill(Qt::white);output.setDotsPerMeterX(static_cast<int>(dpi/0.0254));output.setDotsPerMeterY(static_cast<int>(dpi/0.0254));QPainter painter(&output);QString error;QVERIFY2(paintMathText(painter,target,QStringLiteral("$\\alpha(t)=\\sin(t)+\\frac{1}{2}$"),QFont("Arial",14),Qt::black,true,&error),qPrintable(error));painter.end();QRect ink;for(int y=0;y<output.height();++y)for(int x=0;x<output.width();++x)if(qGray(output.pixel(x,y))<220)ink=ink.united(QRect(x,y,1,1));QVERIFY(!ink.isEmpty());QVERIFY2(target.adjusted(-1,-1,1,1).contains(QRectF(ink)),"Vector mathematical text must fit inside its target at screen/PDF DPI.");if(dpi==96)reference=ink;else{QVERIFY(std::abs(ink.width()-reference.width())<25);QVERIFY(std::abs(ink.height()-reference.height())<25);}}
 }
 void invalidOrExcessiveMathIsRejected() {
    QString error;
    QVERIFY(renderMathText(QStringLiteral("$\\notacommandxyz{a}$"),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(!error.isEmpty());
    QVERIFY(renderMathText(QString(4097,QLatin1Char('a')),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(renderMathText(QString(65,QLatin1Char('{'))+QStringLiteral("x")+QString(65,QLatin1Char('}')),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(renderMathText(QStringLiteral("x")+QStringLiteral("^x").repeated(129),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(renderMathText(QString(64,QLatin1Char('}'))+QString(128,QLatin1Char('{'))+QStringLiteral("x"),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("Unmatched closing")));
    QVERIFY(renderMathText(QStringLiteral("$\\fontsize{1e300}{1e300} x$"),QFont{},Qt::black,true,&error).isNull());
    QVERIFY(error.contains(QStringLiteral("Unsupported LaTeX")));
    QVERIFY(renderMathText(QString(1000,QLatin1Char('W')),QFont{},Qt::black,false,&error).isNull());
 }
 void emptyLabelsClearAndUnicodeWorks() {
    QString error;const auto empty=renderMathText({},QFont{},Qt::black,true,&error);
    QCOMPARE(empty.size(),QSize(1,1));QCOMPARE(qAlpha(empty.pixel(0,0)),0);
    QVERIFY(!renderMathText(QString::fromUtf8("Susanne Pumplün"),QFont{},Qt::black,true,&error).isNull());
    const QString folder=qEnvironmentVariable("PENTA_TEST_ARTIFACT_DIR");
    if(!folder.isEmpty()) {QVERIFY(QDir().mkpath(folder));QVERIFY(renderMathText(QStringLiteral("$p\\star q=2+\\sqrt{2}i+2j+2\\sqrt{2}k+\\sqrt{2}\\ell$"),QFont{},Qt::black,true,&error).save(QDir(folder).filePath(QStringLiteral("latex-caption.png"))));}
 }
};
PENTA_OFFSCREEN_TEST_MAIN(MathTextTests)
#include "math_text_tests.moc"
