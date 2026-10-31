#pragma once
#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>
class QPainter;
class QRectF;

namespace penta {
// Native, bounded rendering of the documented LaTeX math subset. No external
// TeX/Python process is invoked. A null image means an error; empty text clears.
QImage renderMathText(const QString& text, const QFont& font, const QColor& color,
                      bool latex, QString* error = nullptr);
// Draw the same validated layout into a vector-capable painter.
bool paintMathText(QPainter& painter, const QRectF& target, const QString& text,
                   const QFont& font, const QColor& color, bool latex, QString* error = nullptr);
}
