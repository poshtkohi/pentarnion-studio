#include "syntax_highlighter.hpp"

#include "language/lexer.hpp"

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <algorithm>
#include <optional>
#include <vector>

namespace penta {
namespace {
std::optional<SyntaxStyle> styleForKind(language::TokenKind kind) {
    using language::TokenKind;
    switch (kind) {
    case TokenKind::Keyword: return SyntaxStyle::Keyword;
    case TokenKind::Basis: return SyntaxStyle::Basis;
    case TokenKind::Builtin: return SyntaxStyle::Builtin;
    case TokenKind::Integer:
    case TokenKind::Real: return SyntaxStyle::Number;
    case TokenKind::String: return SyntaxStyle::String;
    case TokenKind::Comment: return SyntaxStyle::Comment;
    case TokenKind::Operator:
    case TokenKind::Punctuation: return SyntaxStyle::Operator;
    default: return std::nullopt;
    }
}

// A QString stores UTF-16; the language service deliberately reports UTF-8 byte
// offsets. Build this map once per block so emojis before later tokens do not
// shift their formatting. Interior UTF-8 byte offsets map to the scalar start.
std::vector<int> utf16Positions(const QByteArray& utf8) {
    std::vector<int> positions(static_cast<std::size_t>(utf8.size()) + 1);
    qsizetype byte = 0;
    int utf16 = 0;
    while (byte < utf8.size()) {
        const auto first = static_cast<unsigned char>(utf8.at(byte));
        int length = 1;
        if ((first & 0xe0u) == 0xc0u) length = 2;
        else if ((first & 0xf0u) == 0xe0u) length = 3;
        else if ((first & 0xf8u) == 0xf0u) length = 4;
        length = std::min(length, static_cast<int>(utf8.size() - byte));
        for (int index = 0; index < length; ++index)
            positions[static_cast<std::size_t>(byte + index)] = utf16;
        byte += length;
        utf16 += length == 4 ? 2 : 1;
        positions[static_cast<std::size_t>(byte)] = utf16;
    }
    return positions;
}
}

SyntaxHighlighter::SyntaxHighlighter(QObject* parent) : QSyntaxHighlighter(parent) {}

QTextCharFormat SyntaxHighlighter::formatForStyle(SyntaxStyle style) {
    QTextCharFormat format;
    format.setProperty(styleProperty, static_cast<int>(style));
    switch (style) {
    case SyntaxStyle::Keyword:
        format.setForeground(QColor(QStringLiteral("#0000cc")));
        format.setFontWeight(QFont::Bold);
        break;
    case SyntaxStyle::Basis:
        format.setForeground(QColor(QStringLiteral("#006080")));
        format.setFontWeight(QFont::DemiBold);
        break;
    case SyntaxStyle::Builtin:
        format.setForeground(QColor(QStringLiteral("#006666")));
        break;
    case SyntaxStyle::Number:
        format.setForeground(QColor(QStringLiteral("#7f007f")));
        break;
    case SyntaxStyle::String:
        format.setForeground(QColor(QStringLiteral("#9c2020")));
        break;
    case SyntaxStyle::Comment:
        format.setForeground(QColor(QStringLiteral("#008000")));
        break;
    case SyntaxStyle::Operator:
        format.setForeground(QColor(QStringLiteral("#404040")));
        break;
    }
    return format;
}

void SyntaxHighlighter::highlightBlock(const QString& text) {
    const QByteArray utf8 = text.toUtf8();
    language::LexerState initial;
    initial.inBlockComment = previousBlockState() == 1;
    const auto result = language::lexLine(
        std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())), initial);
    setCurrentBlockState(result.state.inBlockComment ? 1 : 0);
    const auto positions = utf16Positions(utf8);
    for (const auto& token : result.tokens) {
        const auto style = styleForKind(token.kind);
        if (!style || token.span.offset > static_cast<std::size_t>(utf8.size())
                || token.span.length > static_cast<std::size_t>(utf8.size()) - token.span.offset)
            continue;
        const int start = positions[token.span.offset];
        const int end = positions[token.span.offset + token.span.length];
        if (end > start) setFormat(start, end - start, formatForStyle(*style));
    }
}

} // namespace penta
