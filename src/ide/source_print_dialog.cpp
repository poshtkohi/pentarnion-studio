#include "source_print_dialog.hpp"
#include "source_printing.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageSetupDialog>
#include <QPageRanges>
#include <QPrintPreviewWidget>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace penta {
SourcePrintDialog::SourcePrintDialog(QPrinter& printer,std::unique_ptr<QTextDocument> source,
    std::unique_ptr<QTextDocument> selection,QWidget* parent):QDialog(parent) {
    setObjectName(QStringLiteral("sourcePrintDialog"));resize(1180,820);
    setWindowTitle(tr("Print source — %1").arg(source->metaInformation(QTextDocument::DocumentTitle)));
    auto* full=source.release();full->setObjectName(QStringLiteral("sourcePrintSnapshot"));full->setParent(this);
    auto* part=selection.release();if(part){part->setObjectName(QStringLiteral("sourcePrintSelection"));part->setParent(this);}
    auto* outer=new QHBoxLayout(this);auto* settings=new QVBoxLayout;auto* form=new QFormLayout;
    auto* destinations=new QComboBox(this);destinations->setObjectName(QStringLiteral("sourcePrintDestination"));destinations->setMinimumWidth(230);
    destinations->addItem(tr("Save as PDF"),QString());
    for(const auto& info:QPrinterInfo::availablePrinters())destinations->addItem(info.printerName(),info.printerName());
    const int current=destinations->findData(printer.printerName());
    if(printer.outputFormat()==QPrinter::NativeFormat&&current>0)destinations->setCurrentIndex(current);
    form->addRow(tr("Destination:"),destinations);
    auto* orientation=new QComboBox(this);orientation->addItems({tr("Portrait"),tr("Landscape")});
    orientation->setCurrentIndex(printer.pageLayout().orientation()==QPageLayout::Landscape?1:0);form->addRow(tr("Orientation:"),orientation);
    auto* range=new QComboBox(this);range->setObjectName(QStringLiteral("sourcePrintRange"));
    range->addItem(tr("All pages"));range->addItem(tr("Pages"));if(part)range->addItem(tr("Selected source"));form->addRow(tr("Print:"),range);
    auto* pages=new QLineEdit(this);pages->setObjectName(QStringLiteral("sourcePrintPages"));pages->setPlaceholderText(tr("e.g. 1-3, 5"));pages->setEnabled(false);form->addRow(tr("Pages:"),pages);
    auto* copies=new QSpinBox(this);copies->setObjectName(QStringLiteral("sourcePrintCopies"));copies->setRange(1,999);copies->setValue(qBound(1,printer.copyCount(),999));form->addRow(tr("Copies:"),copies);
    auto* collate=new QCheckBox(tr("Collate copies"),this);collate->setChecked(printer.collateCopies());form->addRow(collate);
    auto* colour=new QCheckBox(tr("Print in colour"),this);colour->setChecked(printer.colorMode()==QPrinter::Color);form->addRow(colour);
    auto* reverse=new QCheckBox(tr("Last page first"),this);reverse->setChecked(printer.pageOrder()==QPrinter::LastPageFirst);form->addRow(reverse);
    auto* paper=new QPushButton(tr("Paper and margins…"),this);paper->setObjectName(QStringLiteral("sourcePrintPageSetup"));form->addRow(paper);
    settings->addLayout(form);auto* count=new QLabel(this);count->setObjectName(QStringLiteral("sourcePrintPageCount"));settings->addWidget(count);
    auto* error=new QLabel(this);error->setObjectName(QStringLiteral("sourcePrintError"));error->setWordWrap(true);error->setMaximumWidth(260);settings->addWidget(error);settings->addStretch();
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);auto* print=buttons->addButton(tr("Print"),QDialogButtonBox::AcceptRole);
    print->setObjectName(QStringLiteral("sourcePrintSubmit"));settings->addWidget(buttons);outer->addLayout(settings);
    auto* right=new QVBoxLayout;auto* toolbar=new QToolBar(this);auto* preview=new QPrintPreviewWidget(&printer,this);preview->setObjectName(QStringLiteral("sourcePrintPreviewWidget"));
    auto* previous=toolbar->addAction(tr("Previous"));auto* next=toolbar->addAction(tr("Next"));
    connect(previous,&QAction::triggered,preview,[preview]{preview->setCurrentPage(qMax(1,preview->currentPage()-1));});
    connect(next,&QAction::triggered,preview,[preview]{preview->setCurrentPage(qMin(preview->pageCount(),preview->currentPage()+1));});
    connect(toolbar->addAction(tr("Zoom in")),&QAction::triggered,preview,[preview]{preview->zoomIn();});
    connect(toolbar->addAction(tr("Zoom out")),&QAction::triggered,preview,[preview]{preview->zoomOut();});
    connect(toolbar->addAction(tr("Fit page")),&QAction::triggered,preview,&QPrintPreviewWidget::fitInView);
    connect(toolbar->addAction(tr("Fit width")),&QAction::triggered,preview,&QPrintPreviewWidget::fitToWidth);
    right->addWidget(toolbar);right->addWidget(preview,1);outer->addLayout(right,1);
    auto document=[full,part,range](){return range->currentIndex()==2&&part?part:full;};
    auto configure=[&printer,destinations,orientation,colour,reverse,copies,collate] {
        if(destinations->currentIndex()==0)printer.setOutputFormat(QPrinter::PdfFormat);
        else {printer.setPrinterName(destinations->currentData().toString());printer.setOutputFormat(QPrinter::NativeFormat);}
        printer.setPageOrientation(orientation->currentIndex()?QPageLayout::Landscape:QPageLayout::Portrait);
        printer.setColorMode(colour->isChecked()?QPrinter::Color:QPrinter::GrayScale);
        printer.setPageOrder(reverse->isChecked()?QPrinter::LastPageFirst:QPrinter::FirstPageFirst);
        printer.setCopyCount(copies->value());printer.setCollateCopies(collate->isChecked());
    };
    connect(preview,&QPrintPreviewWidget::paintRequested,this,[&printer,document,error](QPrinter* target) {
        printer.setPrintRange(QPrinter::AllPages);printer.setPageRanges({});
        // Preview renders each page once, independently of the requested output copies/order.
        const int copiesValue=printer.copyCount();const auto order=printer.pageOrder();printer.setCopyCount(1);printer.setPageOrder(QPrinter::FirstPageFirst);
        QString message;const bool ok=printSourceSnapshot(*document(),*target,&message);error->setText(ok?QString():message);
        printer.setCopyCount(copiesValue);printer.setPageOrder(order);
    });
    connect(preview,&QPrintPreviewWidget::previewChanged,this,[preview,count]{count->setText(tr("Page %1 of %2").arg(preview->currentPage()).arg(preview->pageCount()));});
    auto refresh=[preview]{preview->updatePreview();};
    connect(destinations,&QComboBox::currentIndexChanged,this,[configure,refresh,print,destinations]{configure();print->setText(destinations->currentIndex()?tr("Print"):tr("Save PDF…"));refresh();});
    connect(orientation,&QComboBox::currentIndexChanged,this,[configure,refresh]{configure();refresh();});
    connect(colour,&QCheckBox::toggled,this,[configure,refresh]{configure();refresh();});
    connect(range,&QComboBox::currentIndexChanged,this,[pages,range,refresh]{pages->setEnabled(range->currentIndex()==1);refresh();});
    connect(paper,&QPushButton::clicked,this,[this,&printer,configure,refresh,orientation] {
        configure();QPageSetupDialog dialog(&printer,this);if(dialog.exec()==QDialog::Accepted){orientation->setCurrentIndex(printer.pageLayout().orientation()==QPageLayout::Landscape?1:0);refresh();}
    });
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(print,&QPushButton::clicked,this,[this,&printer,configure,document,range,pages,error] {
        configure();printer.setPrintRange(QPrinter::AllPages);printer.setPageRanges({});QString message;
        if(range->currentIndex()==1){const auto requested=QPageRanges::fromString(pages->text());
            const int total=sourcePrintPageCount(*document(),printer,&message);
            if(requested.isEmpty()||requested.lastPage()>total){error->setText(tr("Enter page numbers within 1–%1, for example 1-3, 5.").arg(total));return;}
            printer.setPageRanges(requested);printer.setPrintRange(QPrinter::PageRange);
        }
        if(printer.outputFormat()==QPrinter::PdfFormat){
            QString path=QFileDialog::getSaveFileName(this,tr("Save printed source"),printer.docName()+QStringLiteral(".pdf"),tr("PDF document (*.pdf)"));
            if(path.isEmpty())return;
            if(QFileInfo(path).suffix().isEmpty())path+=QStringLiteral(".pdf");
            // Render to a private sibling-temporary destination, then commit atomically.
            QTemporaryDir temporary;if(!temporary.isValid()){error->setText(tr("Cannot prepare a temporary PDF."));return;}
            const QString original=printer.outputFileName();printer.setOutputFileName(temporary.filePath(QStringLiteral("source.pdf")));
            const bool okay=printSourceSnapshot(*document(),printer,&message);const QString rendered=printer.outputFileName();printer.setOutputFileName(original);
            if(!okay){error->setText(message);return;}QFile input(rendered);QSaveFile output(path);
            if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)){error->setText(tr("Cannot write the selected PDF destination."));return;}
            while(!input.atEnd()){const auto bytes=input.read(65536);if(bytes.isEmpty()&&input.error()!=QFile::NoError){output.cancelWriting();error->setText(input.errorString());return;}
                if(output.write(bytes)!=bytes.size()){output.cancelWriting();error->setText(output.errorString());return;}}
            if(!output.commit()){error->setText(output.errorString());return;}
        }else if(!printSourceSnapshot(*document(),printer,&message)){error->setText(message);return;}
        accept();
    });
    configure();print->setText(destinations->currentIndex()?tr("Print"):tr("Save PDF…"));QTimer::singleShot(0,preview,&QPrintPreviewWidget::updatePreview);
}
}
