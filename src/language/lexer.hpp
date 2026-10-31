#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace penta::language {

enum class TokenKind {
    Keyword, Basis, Builtin, Identifier, Integer, Real, String, Comment,
    Operator, Punctuation, Error, EndOfFile
};

struct SourceSpan {
    std::size_t offset = 0; // UTF-8 byte offset, never Qt UTF-16 positions.
    std::size_t length = 0;
};

struct Token {
    TokenKind kind = TokenKind::Error;
    SourceSpan span;
};

struct LexDiagnostic {
    std::string code;
    std::string message;
    SourceSpan span;
};

struct LexerState {
    bool inBlockComment = false;
    std::size_t blockCommentStart = 0;
};

struct LexResult {
    std::vector<Token> tokens;
    std::vector<LexDiagnostic> diagnostics;
    LexerState state;
};

// One logical line (optionally including a terminal LF/CRLF). Token and
// diagnostic offsets include baseOffset. No EOF or unterminated-block diagnostic
// is emitted: the caller carries state to the next line. No Qt dependency.
[[nodiscard]] LexResult lexLine(std::string_view line, LexerState initial = {},
                                std::size_t baseOffset = 0);

// Shared line rules plus end-of-source block validation and one EOF token.
// Diagnostics are bounded to 128 per invocation; every input byte is consumed.
[[nodiscard]] LexResult lex(std::string_view source);

[[nodiscard]] std::string_view tokenKindName(TokenKind kind) noexcept;

} // namespace penta::language
