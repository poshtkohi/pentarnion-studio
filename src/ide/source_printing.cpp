#include "source_printing.hpp"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QPageRanges>
#include <QPrinter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTextOption>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <vector>

namespace penta {
namespace {
constexpr double referenceDpi=96;
constexpr double headerHeight=28,footerHeight=24;

bool fail(QString* error,const QString& message) {
    if(error)*error=message;
    return false;
}

struct PrintLayout {
    QImage metrics{1,1,QImage::Format_ARGB32_Premultiplied};
    std::unique_ptr<QTextDocument> document;
    QSizeF body;
    qreal xScale=1,yScale=1;
    int pages=0;

    bool prepare(const QTextDocument& snapshot,const QPrinter& printer,QString* error) {
        const QRectF paint=printer.pageRect(QPrinter::DevicePixel);
        const int dpiX=printer.logicalDpiX(),dpiY=printer.logicalDpiY();
        if(dpiX<=0||dpiY<=0||paint.width()<=0||paint.height()<=0)
            return fail(error,QStringLiteral("The selected paper has no usable printable area."));
        xScale=dpiX/referenceDpi;yScale=dpiY/referenceDpi;
        body=QSizeF(paint.width()/xScale,paint.height()/yScale-headerHeight-footerHeight);
        if(!std::isfinite(body.width())||!std::isfinite(body.height())||body.width()<32||body.height()<32)
            return fail(error,QStringLiteral("The printable area is too small for source text."));
        metrics.setDotsPerMeterX(qRound(referenceDpi/0.0254));metrics.setDotsPerMeterY(qRound(referenceDpi/0.0254));
        document.reset(snapshot.clone());document->setUndoRedoEnabled(false);document->setDocumentMargin(0);
        document->documentLayout()->setPaintDevice(&metrics);
        auto option=document->defaultTextOption();option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        const double columns=snapshot.property("sourcePrintTabColumns").toDouble();
        if(std::isfinite(columns)&&columns>0)
            option.setTabStopDistance(columns*QFontMetricsF(document->defaultFont(),&metrics).horizontalAdvance(QLatin1Char(' ')));
        document->setDefaultTextOption(option);document->setPageSize(body);
        pages=std::max(1,document->pageCount());return true;
    }
};
}

std::unique_ptr<QTextDocument> createSourcePrintSnapshot(
    const QTextDocument& source,const SourcePrintSettings& settings,QString* error) {
    if(error)error->clear();
    auto snapshot=std::unique_ptr<QTextDocument>(source.clone());
    snapshot->setUndoRedoEnabled(false);snapshot->setDefaultFont(settings.font);
    snapshot->setMetaInformation(QTextDocument::DocumentTitle,settings.title);
    auto option=source.defaultTextOption();option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    if(settings.tabStopDistance>0&&std::isfinite(settings.tabStopDistance))option.setTabStopDistance(settings.tabStopDistance);
    snapshot->setDefaultTextOption(option);snapshot->setPageSize(QSizeF(-1,-1));
    const QPaintDevice* device=source.documentLayout()?source.documentLayout()->paintDevice():nullptr;
    QImage sourceMetrics(1,1,QImage::Format_ARGB32_Premultiplied);
    if(settings.logicalDpi>0&&std::isfinite(settings.logicalDpi)){
        sourceMetrics.setDotsPerMeterX(qRound(settings.logicalDpi/0.0254));sourceMetrics.setDotsPerMeterY(qRound(settings.logicalDpi/0.0254));device=&sourceMetrics;
    }
    const qreal space=QFontMetricsF(settings.font,device).horizontalAdvance(QLatin1Char(' '));
    if(space>0)snapshot->setProperty("sourcePrintTabColumns",option.tabStopDistance()/space);
    // A cloned plain document keeps its underlying text formats, but syntax
    // highlighting lives in QTextLayout's extra ranges and must be materialized.
    QTextCursor all(snapshot.get());all.select(QTextCursor::Document);
    QTextCharFormat defaults;defaults.setForeground(settings.textColor);defaults.setFont(settings.font);
    all.mergeCharFormat(defaults);
    for(QTextBlock block=source.begin();block.isValid();block=block.next()){
        // Restore base source formatting first, then its displayed syntax runs.
        for(auto fragment=block.begin();!fragment.atEnd();++fragment){const auto text=fragment.fragment();if(!text.isValid())continue;
            QTextCursor run(snapshot.get());run.setPosition(text.position());run.setPosition(text.position()+text.length(),QTextCursor::KeepAnchor);
            run.mergeCharFormat(text.charFormat());
        }
        if(!block.layout())continue;
        for(const auto& range:block.layout()->formats()){
            const int start=std::clamp(range.start,0,static_cast<int>(block.text().size()));
            const int end=std::clamp(range.start+range.length,start,static_cast<int>(block.text().size()));
            if(start==end)continue;
            QTextCursor run(snapshot.get());run.setPosition(block.position()+start);
            run.setPosition(block.position()+end,QTextCursor::KeepAnchor);run.mergeCharFormat(range.format);
        }
    }
    snapshot->setModified(false);return snapshot;
}

std::unique_ptr<QTextDocument> createSourcePrintSnapshot(
    const QTextDocument& source,const QFont& font,const QString& title,QString* error) {
    SourcePrintSettings settings;settings.font=font;settings.title=title;
    return createSourcePrintSnapshot(source,settings,error);
}

std::unique_ptr<QTextDocument> createSourcePrintSelectionSnapshot(
    const QTextDocument& snapshot,int start,int end,QString* error) {
    if(error)error->clear();
    if(start<0||end<=start||end>snapshot.characterCount()-1){
        fail(error,QStringLiteral("There is no valid source selection to print."));return {};
    }
    QTextCursor selection(const_cast<QTextDocument*>(&snapshot));selection.setPosition(start);selection.setPosition(end,QTextCursor::KeepAnchor);
    auto result=std::make_unique<QTextDocument>();result->setUndoRedoEnabled(false);result->setDefaultFont(snapshot.defaultFont());
    result->setDefaultTextOption(snapshot.defaultTextOption());result->setMetaInformation(QTextDocument::DocumentTitle,snapshot.metaInformation(QTextDocument::DocumentTitle));
    result->setProperty("sourcePrintTabColumns",snapshot.property("sourcePrintTabColumns"));
    QTextCursor target(result.get());target.insertFragment(QTextDocumentFragment(selection));result->setModified(false);return result;
}

int sourcePrintPageCount(const QTextDocument& snapshot,const QPrinter& printer,QString* error) {
    if(error)error->clear();
    PrintLayout layout;return layout.prepare(snapshot,printer,error)?layout.pages:0;
}

bool printSourceSnapshot(const QTextDocument& snapshot,QPrinter& printer,QString* error) {
    if(error)error->clear();
    PrintLayout layout;if(!layout.prepare(snapshot,printer,error))return false;
    std::vector<int> pages;
    const auto ranges=printer.pageRanges();
    if(printer.printRange()==QPrinter::PageRange&&!ranges.isEmpty()){
        for(int page=1;page<=layout.pages;++page)if(ranges.contains(page))pages.push_back(page);
    }else{
        int first=1,last=layout.pages;
        if(printer.printRange()==QPrinter::PageRange){first=std::max(1,printer.fromPage());last=printer.toPage()>0?std::min(last,printer.toPage()):last;}
        for(int page=first;page<=last;++page)pages.push_back(page);
    }
    if(pages.empty())return fail(error,QStringLiteral("The selected page range is outside the source document."));
    if(printer.pageOrder()==QPrinter::LastPageFirst)std::reverse(pages.begin(),pages.end());
    const int copies=printer.supportsMultipleCopies()?1:std::clamp(printer.copyCount(),1,999);
    std::vector<int> order;
    if(printer.collateCopies())for(int copy=0;copy<copies;++copy)order.insert(order.end(),pages.begin(),pages.end());
    else for(int page:pages)for(int copy=0;copy<copies;++copy)order.push_back(page);
    QPainter painter;if(!painter.begin(&printer))return fail(error,QStringLiteral("The printer could not be opened. Check its destination and paper settings."));
    const QString title=snapshot.metaInformation(QTextDocument::DocumentTitle);
    bool okay=true;
    for(std::size_t index=0;index<order.size();++index){
        if(index!=0&&!printer.newPage()){okay=false;break;}
        painter.save();
        if(printer.fullPage())painter.translate(printer.pageRect(QPrinter::DevicePixel).topLeft());
        painter.scale(layout.xScale,layout.yScale);
        QFont header=snapshot.defaultFont();const qreal points=header.pointSizeF()>0?header.pointSizeF():10;
        header.setPixelSize(qRound(std::max(8.0,points*0.8)*referenceDpi/72));painter.setFont(header);painter.setPen(QColor(80,80,80));
        painter.drawText(QRectF(0,0,layout.body.width(),headerHeight-6),Qt::AlignLeft|Qt::AlignVCenter,
            QFontMetricsF(header).elidedText(title,Qt::ElideMiddle,static_cast<int>(std::floor(layout.body.width()))));
        painter.drawText(QRectF(0,headerHeight+layout.body.height()+4,layout.body.width(),footerHeight-4),Qt::AlignRight|Qt::AlignVCenter,
            QStringLiteral("%1 / %2").arg(order[index]).arg(layout.pages));
        painter.save();painter.setClipRect(QRectF(0,headerHeight,layout.body.width(),layout.body.height()));
        const qreal top=(order[index]-1)*layout.body.height();painter.translate(0,headerHeight-top);
        QAbstractTextDocumentLayout::PaintContext context;context.clip=QRectF(0,top,layout.body.width(),layout.body.height());
        layout.document->documentLayout()->draw(&painter,context);
        painter.restore();painter.restore();
    }
    const bool ended=painter.end();
    if(!okay||!ended||printer.printerState()==QPrinter::Error||printer.printerState()==QPrinter::Aborted)
        return fail(error,QStringLiteral("Printing did not finish. Check the printer or output destination."));
    return true;
}

} // namespace penta
