#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace penta::runtime::dataio {
struct NumericTable {
    std::size_t rows=0, columns=0;
    std::vector<double> values; // Row-major; no implicit missing-value substitution.
};
struct Options {
    std::size_t maxElements=100000;
    std::size_t maxFileBytes=64*1024*1024;
    std::size_t maxUncompressedBytes=64*1024*1024;
    std::shared_ptr<std::atomic_bool> cancellation;
};
class Error : public std::runtime_error { using std::runtime_error::runtime_error; };

[[nodiscard]] std::string readBytes(const std::filesystem::path& path,const Options& options={});
void writeBytesAtomic(const std::filesystem::path& path,std::string_view bytes,const Options& options={});

// Numeric CSV and .xlsx tables only. Empty sheet selects the first worksheet.
// Values must be finite and the table rectangular, without headers or blanks.
// Cached numeric formula values may be read; formulas are never evaluated.
[[nodiscard]] NumericTable readMatrix(const std::filesystem::path& path,
    const std::string& sheet={},const Options& options={});

// Parse an already captured data snapshot without rereading a mutable file.
// formatPath supplies the .csv/.xlsx extension only; no file is opened.
[[nodiscard]] NumericTable readMatrixBytes(std::string_view bytes,
    const std::filesystem::path& formatPath,const std::string& sheet={},const Options& options={});

// Writes a new single-sheet XLSX workbook, or native CSV. Does not append to a
// workbook. The original target is preserved until successful validation/commit.
// A nonempty sheet option is valid only for .xlsx; default output sheet is Sheet1.
void writeMatrix(const std::filesystem::path& path,const NumericTable& table,
    const std::string& sheet={},const Options& options={});
}
