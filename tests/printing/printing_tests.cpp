#include "ide/editor_widget.hpp"
#include "ide/source_printing.hpp"
#include "ide/syntax_highlighter.hpp"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPageLayout>
#include <QPageSize>
#include <QPrinter>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTest>

using namespace penta;
namespace {
QTextCharFormat formatAt(QTextDocument& document,int offset) {
    QTextCursor cursor(&document);cursor.setPosition(offset);cursor.setPosition(offset+1,QTextCursor::KeepAnchor);return cursor.charFormat();
}
void configurePdf(QPrinter& printer,const QString& path) {
    printer.setOutputFormat(QPrinter::PdfFormat);printer.setOutputFileName(path);
    printer.setResolution(144);printer.setColorMode(QPrinter::Color);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(15,15,15,15),QPageLayout::Millimeter);
}
}
class PrintingTests final:public QObject {
    Q_OBJECT
private slots:
    void snapshotRetainsExactSyntaxAndSource();
    void selectionRetainsPartialUnicodeAndBlankLines();
    void fullSourceHasNoClipboardFormattingFallback();
    void multipagePdfRangesAndLongLines();
};

void PrintingTests::snapshotRetainsExactSyntaxAndSource() {
    EditorWidget editor;
    const QString source=QString::fromUtf8("% λ source <tag> & quoted\n\nreal rate = 1.25;\npentarnion p = i + ell;\ndisplay(\"value <p>\", p);\n\treal x = sin(rate);\n");
    editor.setPlainText(source);editor.setFontPointSize(12);editor.syntaxHighlighter()->rehighlight();
    QTextCursor selected(editor.document());selected.setPosition(4);selected.setPosition(20,QTextCursor::KeepAnchor);editor.setTextCursor(selected);
    const int revision=editor.document()->revision(),undos=editor.document()->availableUndoSteps();
    const bool dirty=editor.document()->isModified();
    SourcePrintSettings settings;settings.font=editor.font();settings.textColor=Qt::black;settings.tabStopDistance=editor.tabStopDistance();settings.title=QString::fromUtf8("Résumé.p");
    QString error;auto snapshot=createSourcePrintSnapshot(*editor.document(),settings,&error);
    QVERIFY2(snapshot,qPrintable(error));QCOMPARE(snapshot->toPlainText(),source);
    QCOMPARE(snapshot->defaultFont(),editor.font());QCOMPARE(snapshot->defaultTextOption().tabStopDistance(),editor.tabStopDistance());
    QCOMPARE(snapshot->metaInformation(QTextDocument::DocumentTitle),settings.title);
    for(QTextBlock block=editor.document()->begin();block.isValid();block=block.next()) {
        for(const auto& run:block.layout()->formats()) {
            if(run.length<1)continue;
            const auto printed=formatAt(*snapshot,block.position()+run.start);
            if(run.format.foreground().style()!=Qt::NoBrush)QCOMPARE(printed.foreground().color(),run.format.foreground().color());
            if(run.format.hasProperty(QTextFormat::FontWeight))QCOMPARE(printed.fontWeight(),run.format.fontWeight());
            if(run.format.hasProperty(QTextFormat::FontItalic))QCOMPARE(printed.fontItalic(),run.format.fontItalic());
        }
    }
    QCOMPARE(formatAt(*snapshot,source.indexOf("real rate")).foreground().color(),QColor("#0000cc"));
    QCOMPARE(formatAt(*snapshot,source.indexOf("1.25")).foreground().color(),QColor("#7f007f"));
    QCOMPARE(formatAt(*snapshot,source.indexOf("\"value")).foreground().color(),QColor("#9c2020"));
    QCOMPARE(editor.document()->revision(),revision);QCOMPARE(editor.document()->availableUndoSteps(),undos);
    QCOMPARE(editor.document()->isModified(),dirty);QCOMPARE(editor.textCursor().selectionStart(),4);QCOMPARE(editor.textCursor().selectionEnd(),20);
    editor.setPlainText("real changed=2;");QCOMPARE(snapshot->toPlainText(),source);
}

void PrintingTests::selectionRetainsPartialUnicodeAndBlankLines() {
    EditorWidget editor;const QString source=QString::fromUtf8("\n% αβ example\n\nreal amount=2.5;\n\n");
    editor.setPlainText(source);editor.syntaxHighlighter()->rehighlight();
    QString error;auto full=createSourcePrintSnapshot(*editor.document(),editor.font(),"selection.p",&error);QVERIFY(full);
    const int start=source.indexOf(QString::fromUtf8("α")),end=source.indexOf("amount")+3;
    auto part=createSourcePrintSelectionSnapshot(*full,start,end,&error);QVERIFY2(part,qPrintable(error));
    QCOMPARE(part->toPlainText(),source.mid(start,end-start));
    QCOMPARE(formatAt(*part,0).foreground().color(),formatAt(*full,start).foreground().color());
    auto all=createSourcePrintSelectionSnapshot(*full,0,source.size(),&error);QVERIFY(all);QCOMPARE(all->toPlainText(),source);
    QVERIFY(!createSourcePrintSelectionSnapshot(*full,-1,2,&error));QVERIFY(!error.isEmpty());
    QVERIFY(!createSourcePrintSelectionSnapshot(*full,2,2,&error));
    QVERIFY(!createSourcePrintSelectionSnapshot(*full,0,source.size()+1,&error));
}

void PrintingTests::fullSourceHasNoClipboardFormattingFallback() {
    EditorWidget editor;const QString source=QString("real a=1; pentarnion p=i+j; % colored\n").repeated(12000);
    editor.setPlainText(source);editor.syntaxHighlighter()->rehighlight();
    QString error;auto snapshot=createSourcePrintSnapshot(*editor.document(),editor.font(),"large.p",&error);
    QVERIFY2(snapshot,qPrintable(error));QCOMPARE(snapshot->toPlainText(),source);
    QCOMPARE(formatAt(*snapshot,source.lastIndexOf("real")).foreground().color(),QColor("#0000cc"));
    QCOMPARE(formatAt(*snapshot,source.lastIndexOf("colored")).foreground().color(),QColor("#008000"));
}

void PrintingTests::multipagePdfRangesAndLongLines() {
    EditorWidget editor;editor.setFontPointSize(12);
    QString source=QString::fromUtf8("% Printed syntax colors — λ\nreal rate=0.7;\npentarnion p=1+i+j+k+ell;\ndisplay(\"source <p> & colors\",p);\n\n");
    for(int line=1;line<=180;++line)source+=QString("real row_%1 = 1.25; %% row %1\n").arg(line,4,10,QChar('0'));
    source+="% "+QString("long_text_").repeated(75)+"LONG_LINE_END\n";
    source+="real FINAL_SOURCE_MARKER = 42;\n";
    editor.setPlainText(source);editor.syntaxHighlighter()->rehighlight();
    QString error;auto snapshot=createSourcePrintSnapshot(*editor.document(),editor.font(),"printed-source.p",&error);QVERIFY(snapshot);
    const auto originalText=snapshot->toPlainText();const int revision=snapshot->revision();
    QTemporaryDir temporary;
    const QString artifacts=qEnvironmentVariable("PENTA_QA_ARTIFACT_DIR");
    const QString directory=artifacts.isEmpty()?temporary.path():artifacts;QVERIFY(QDir().mkpath(directory));
    QPrinter printer(QPrinter::HighResolution);configurePdf(printer,QDir(directory).filePath("colored-source.pdf"));
    const int pages=sourcePrintPageCount(*snapshot,printer,&error);QVERIFY2(pages>=3,qPrintable(error));
    QVERIFY2(printSourceSnapshot(*snapshot,printer,&error),qPrintable(error));
    QFile full(printer.outputFileName());QVERIFY(full.open(QIODevice::ReadOnly));QVERIFY(full.readAll().startsWith("%PDF-"));
    QPrinter range(QPrinter::HighResolution);configurePdf(range,QDir(directory).filePath("colored-source-page2.pdf"));
    range.setPrintRange(QPrinter::PageRange);range.setFromTo(2,2);
    QVERIFY2(printSourceSnapshot(*snapshot,range,&error),qPrintable(error));
    QPrinter invalid(QPrinter::HighResolution);configurePdf(invalid,temporary.filePath("should-not-exist.pdf"));
    invalid.setPrintRange(QPrinter::PageRange);invalid.setFromTo(pages+1,pages+1);
    QVERIFY(!printSourceSnapshot(*snapshot,invalid,&error));QVERIFY(!QFile::exists(invalid.outputFileName()));
    QCOMPARE(snapshot->toPlainText(),originalText);QCOMPARE(snapshot->revision(),revision);
    qInfo("Printed source has %d pages; all text, colors and the immutable snapshot are retained.",pages);
}

int main(int argc,char** argv){QApplication app(argc,argv);PrintingTests tests;return QTest::qExec(&tests,argc,argv);}
#include "printing_tests.moc"
