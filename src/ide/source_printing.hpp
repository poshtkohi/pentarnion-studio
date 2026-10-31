#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <memory>

class QPrinter;
class QTextDocument;

namespace penta {

struct SourcePrintSettings {
    QFont font;
    QColor textColor = Qt::black;
    qreal tabStopDistance = 0; // Zero uses the source document's tab setting.
    QString title;
    qreal logicalDpi = 0; // Zero infers the source paint device's font metrics.
};

// Copies all text and explicit/highlighter formatting, without HTML conversion
// or clipboard-size limits. The caller should flush its highlighter first.
std::unique_ptr<QTextDocument> createSourcePrintSnapshot(
    const QTextDocument& source, const SourcePrintSettings& settings, QString* error = nullptr);
std::unique_ptr<QTextDocument> createSourcePrintSnapshot(
    const QTextDocument& source, const QFont& font, const QString& title = {}, QString* error = nullptr);
std::unique_ptr<QTextDocument> createSourcePrintSelectionSnapshot(
    const QTextDocument& snapshot, int start, int end, QString* error = nullptr);

// Layout and printing use private clones, preserving the immutable snapshot.
int sourcePrintPageCount(const QTextDocument& snapshot, const QPrinter& printer, QString* error = nullptr);
bool printSourceSnapshot(const QTextDocument& snapshot, QPrinter& printer, QString* error = nullptr);

} // namespace penta
