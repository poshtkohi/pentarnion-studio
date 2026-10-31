#pragma once
#include "language/frontend.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
namespace penta::exporting {
enum class Target { Python, Matlab };
struct Options {
    std::string sourceName="source.p";
    // The explicit Python adapter maps BDF to SciPy BDF, not CVODE. Unsupported
    // solver/event/sensitivity options remain errors even when this is enabled.
    bool allowPythonBdfAdapter=false;
    bool allowMatlabOde15sAdapter=false;
    bool allowScientificAdapters=false;
    // Optional host-supplied compiled target resources. exportProgram remains
    // a pure checked-AST transformation; the explicit export UI/CLI loads these.
    std::map<std::string,std::string> compiledRuntimeFiles;
};
struct File {std::string name,contents;};
struct SourceMapping {std::string file,source;std::size_t line=1;language::SourceSpan span;};
struct Result {
    std::vector<File> files;
    std::vector<language::Diagnostic> diagnostics;
    std::vector<SourceMapping> sourceMap;
    std::string compatibilityReport;
    [[nodiscard]] bool ok() const noexcept;
};
// No source is evaluated and no files/processes are written/launched. The caller
// chooses an output directory only after inspecting ok(). Failure returns no
// executable files; each unsupported AST node has an original byte span.
[[nodiscard]] Result exportProgram(const language::AnalysisResult&,Target,const Options& = {});
}
