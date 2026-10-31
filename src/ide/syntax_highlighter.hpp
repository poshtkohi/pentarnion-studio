#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

namespace penta {

enum class SyntaxStyle {
    Keyword = 1,
    Basis,
    Builtin,
    Number,
    String,
    Comment,
    Operator
};

// Styling adapter only: all token and comment-state decisions come from the
// shared Qt-free lexer. No source text is modified or evaluated.
class SyntaxHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SyntaxHighlighter(QObject* parent = nullptr);
    static constexpr int styleProperty = QTextFormat::UserProperty + 101;
    static QTextCharFormat formatForStyle(SyntaxStyle style);

protected:
    void highlightBlock(const QString& text) override;
};

} // namespace penta
