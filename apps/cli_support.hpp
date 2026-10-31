#pragma once
#include "language/frontend.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace penta::cli {
inline constexpr std::string_view toolVersion = "0.2";
inline constexpr std::size_t maximumFileBytes = 4 * 1024 * 1024;
struct Options {
    language::AnalysisOptions analysis;
    std::string file;
    bool json = false;
    bool help = false;
    bool version = false;
    bool headlessFigures = false;
};

inline std::filesystem::path utf8Path(std::string_view text) {
    std::u8string encoded;
    encoded.reserve(text.size());
    for (const unsigned char byte : text) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}
#ifdef _WIN32
inline std::vector<std::string> arguments(int count, wchar_t** values) {
    std::vector<std::string> result;
    for (int index = 0; index < count; ++index) {
        const auto utf8 = std::filesystem::path(values[index]).u8string();
        result.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    return result;
}
#else
inline std::vector<std::string> arguments(int count, char** values) {
    std::vector<std::string> result;
    for (int index = 0; index < count; ++index) result.emplace_back(values[index]);
    return result;
}
#endif

inline std::string jsonString(std::string_view text) {
    static constexpr char hexadecimal[] = "0123456789abcdef";
    std::string result = "\"";
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        switch (byte) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        default:
            if (byte >= 0x80) {
                const std::size_t length = byte >= 0xc2 && byte <= 0xdf ? 2 :
                    byte >= 0xe0 && byte <= 0xef ? 3 : byte >= 0xf0 && byte <= 0xf4 ? 4 : 0;
                bool valid = length != 0 && length <= text.size() - index;
                for (std::size_t extra = 1; valid && extra < length; ++extra) {
                    const auto continuation = static_cast<unsigned char>(text[index + extra]);
                    valid = continuation >= 0x80 && continuation <= 0xbf;
                }
                if (valid) {
                    const auto second = static_cast<unsigned char>(text[index + 1]);
                    valid = !((byte == 0xe0 && second < 0xa0) || (byte == 0xed && second >= 0xa0) ||
                              (byte == 0xf0 && second < 0x90) || (byte == 0xf4 && second >= 0x90));
                }
                if (valid) { result.append(text.substr(index, length)); index += length - 1; }
                else {
                    result += "\\u00";
                    result += hexadecimal[byte >> 4];
                    result += hexadecimal[byte & 15];
                }
            } else if (byte < 0x20) {
                result += "\\u00";
                result += hexadecimal[byte >> 4];
                result += hexadecimal[byte & 15];
            } else result += static_cast<char>(byte);
        }
    }
    result += '"';
    return result;
}
inline std::string number(double value) {
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                        std::chars_format::general, 17);
    return converted.ec == std::errc() ? std::string(buffer.data(), converted.ptr) : "null";
}

inline bool parseArguments(const std::vector<std::string>& args, bool runner,
                           Options& options, std::string& error) {
    bool pathsOnly = false;
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& argument = args[index];
        auto optionValue = [&](std::string_view option, std::string& value) {
            if (argument == option) {
                if (index + 1 == args.size()) { error = "Missing value for " + std::string(option) + "."; return false; }
                value = args[++index];
            } else value = argument.substr(option.size() + 1);
            if (value.empty()) { error = "Missing value for " + std::string(option) + "."; return false; }
            return true;
        };
        if (!pathsOnly && argument == "--") { pathsOnly = true; continue; }
        if (!pathsOnly && (argument == "--help" || argument == "-h")) { options.help = true; continue; }
        if (!pathsOnly && argument == "--version") { options.version = true; continue; }
        if (!pathsOnly && argument == "--headless-figures") {
            if(!runner){error="--headless-figures is an execution option for penta-run.";return false;}
            options.headlessFigures=true;continue;
        }
        if (!pathsOnly && argument == "--json") {
            if (runner) { error = "--json is provided by penta-check; penta-run prints display output."; return false; }
            options.json = true; continue;
        }
        if (!pathsOnly && (argument == "--gamma" || argument.starts_with("--gamma="))) {
            std::string value;
            if (!optionValue("--gamma", value)) return false;
            std::string_view literal(value);
            if (literal.starts_with('+')) literal.remove_prefix(1);
            double gamma = -1;
            const auto converted = std::from_chars(literal.data(), literal.data() + literal.size(), gamma,
                                                  std::chars_format::general);
            if (literal.empty() || converted.ec != std::errc() || converted.ptr != literal.data() + literal.size() ||
                !std::isfinite(gamma) || gamma < 0 || gamma > 1) {
                error = "--gamma requires one finite numeric literal in [0,1]."; return false;
            }
            options.analysis.gamma = gamma;
            continue;
        }
        if (!pathsOnly && (argument == "--language-version" || argument.starts_with("--language-version="))) {
            std::string value;
            if (!optionValue("--language-version", value)) return false;
            if (value != "0.1" && value != "0.2") { error = "--language-version must be 0.1 or 0.2."; return false; }
            options.analysis.languageVersion = value;
            continue;
        }
        if (!pathsOnly && !argument.empty() && argument.front() == '-') {
            error = "Unknown option: " + argument + ". Use -- before a filename starting with '-'."; return false;
        }
        if (!options.file.empty()) { error = "Provide exactly one source file."; return false; }
        options.file = argument;
    }
    if (!options.help && !options.version && options.file.empty()) { error = "A source filename is required."; return false; }
    return true;
}

inline void help(std::string_view tool, bool runner) {
    std::cout << "Usage: " << tool << " [options] [--] source.p\n\n";
    if (runner) std::cout << "Run explicitly selected Pentarnion source after full syntax/type checking.\n"
                             "display(...) writes values; execution requires language 0.2.\n";
    else std::cout << "Check syntax/types without executing source or producing compiled code.\n"
                      "Validation does not prove numerical invertibility or division solvability.\n";
    std::cout << "\nOptions:\n"
                 "  --gamma NUMBER          Default A_gamma_v1 gamma in [0,1] (default: 1).\n"
                 "  --language-version V    0.1 or 0.2 (default: 0.2).\n";
    if (!runner) std::cout << "  --json                  Write structured analysis/diagnostics to stdout.\n";
    if (runner) std::cout << "  --headless-figures      Collect native plot data and allow explicit PNG/SVG/PDF exports.\n";
    std::cout << "  --help, -h              Show this help.\n"
                 "  --version               Show tool version.\n"
                 "  --                      End option parsing.\n\n"
                 "Source limit: 4 MiB. Files are independent scopes; no project metadata is changed.\n"
                 "Exit status: 0 success, 1 source/runtime errors, 2 usage or input failure.\n";
}

inline bool loadSource(std::string_view filename, std::string& source, std::string& error) {
    const auto path = utf8Path(filename);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        error = "Cannot read a regular source file: " + std::string(filename);
        if (ec) error += " (" + ec.message() + ")";
        return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) { error = "Cannot inspect source size: " + std::string(filename); return false; }
    if (size > maximumFileBytes) { error = "Source exceeds the 4 MiB input limit: " + std::string(filename); return false; }
    std::ifstream file(path, std::ios::binary);
    if (!file) { error = "Cannot open source: " + std::string(filename); return false; }
    source.clear();
    source.reserve(static_cast<std::size_t>(size));
    std::array<char, 8192> buffer{};
    while (file) {
        const auto available = std::min(buffer.size(), maximumFileBytes + 1 - source.size());
        file.read(buffer.data(), static_cast<std::streamsize>(available));
        source.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
        if (source.size() > maximumFileBytes) { error = "Source grew beyond the 4 MiB input limit."; return false; }
        if (file.bad() || (file.fail() && !file.eof())) { error = "Failed while reading source: " + std::string(filename); return false; }
    }
    return true;
}

inline void textDiagnostics(const std::vector<language::Diagnostic>& diagnostics,
                            std::string_view file, std::string_view source) {
    for (const auto& item : diagnostics) {
        const auto location = language::sourceLocation(source, item.span.offset);
        std::cerr << file << ':' << location.line << ':' << location.column << ": "
                  << (item.severity == language::Severity::Error ? "error " : "warning ")
                  << item.code << ": " << item.message << '\n';
        for(const auto& frame:item.frames)if(frame.source){const auto at=language::sourceLocation(*frame.source,frame.span.offset);std::cerr<<"  at "<<frame.function<<" ("<<frame.sourceIdentity<<':'<<at.line<<':'<<at.column<<")\n";}
    }
}
inline void jsonAnalysis(const language::AnalysisResult& result,
                         std::string_view file, std::string_view source) {
    std::cout << "{\"tool\":\"penta-check\",\"version\":\"" << toolVersion
              << "\",\"file\":" << jsonString(file)
              << ",\"language_version\":" << jsonString(result.program.languageVersion)
              << ",\"algebra\":{\"id\":" << jsonString(result.program.algebraId)
              << ",\"gamma\":" << number(result.program.gamma)
              << "},\"ok\":" << (result.ok() ? "true" : "false")
              << ",\"cancelled\":" << (result.cancelled ? "true" : "false") << ",\"diagnostics\":[";
    bool first = true;
    for (const auto& item : result.diagnostics) {
        if (!first) std::cout << ',';
        first = false;
        const auto location = language::sourceLocation(source, item.span.offset);
        std::cout << "{\"severity\":" << jsonString(item.severity == language::Severity::Error ? "error" : "warning")
                  << ",\"code\":" << jsonString(item.code) << ",\"message\":" << jsonString(item.message)
                  << ",\"file\":" << jsonString(file)
                  << ",\"line\":" << location.line << ",\"column\":" << location.column
                  << ",\"offset\":" << item.span.offset << ",\"length\":" << item.span.length << '}';
    }
    std::cout << "]}\n";
}
inline int inputFailure(std::string_view tool, const Options& options, std::string_view message) {
    if (options.json) std::cout << "{\"tool\":" << jsonString(tool) << ",\"version\":" << jsonString(toolVersion)
        << ",\"ok\":false,\"input_error\":" << jsonString(message) << ",\"diagnostics\":[]}\n";
    else std::cerr << tool << ": " << message << '\n';
    return 2;
}
} // namespace penta::cli
