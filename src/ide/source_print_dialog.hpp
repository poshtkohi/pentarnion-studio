#pragma once
#include <QDialog>
#include <memory>
class QPrinter;
class QTextDocument;
namespace penta {
// A Qt preview and print settings in one surface; no Win32 generic print UI.
class SourcePrintDialog final : public QDialog {
public:
    SourcePrintDialog(QPrinter& printer,std::unique_ptr<QTextDocument> source,
                      std::unique_ptr<QTextDocument> selection,QWidget* parent=nullptr);
};
}
