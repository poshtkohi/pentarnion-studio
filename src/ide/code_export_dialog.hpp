#pragma once
#include "language/frontend.hpp"
#include <QString>
#include <filesystem>
#include <string>
class QWidget;
namespace penta::ide {
struct CodeExportOutcome {bool written=false;QString directory,summary;};
// Opens a bounded, checked export flow. Source is a saved/autosaved snapshot;
// this controller never modifies the editor or runs target code.
CodeExportOutcome showCodeExportDialog(QWidget* parent,const std::string& source,
    const language::AnalysisOptions&,const std::filesystem::path& sourcePath);
}
