#include "../cli_support.hpp"
#include "../distribution_activity.hpp"
#include "runtime/interpreter.hpp"

namespace {
int runFile(const std::vector<std::string>& arguments) {
    penta::cli::Options options;
    std::string error;
    if (!penta::cli::parseArguments(arguments, true, options, error))
        return penta::cli::inputFailure("penta-run", options, error);
    if (options.help) { penta::cli::help("penta-run", true); return 0; }
    if (options.version) { std::cout << "penta-run " << penta::cli::toolVersion << " (bounded interpreter)\n"; return 0; }
    std::string source;
    if (!penta::cli::loadSource(options.file, source, error))
        return penta::cli::inputFailure("penta-run", options, error);
    options.analysis.sourceDirectory=std::filesystem::absolute(penta::cli::utf8Path(options.file)).parent_path();
    {const auto identity=std::filesystem::absolute(penta::cli::utf8Path(options.file)).u8string();options.analysis.sourceIdentity.assign(identity.begin(),identity.end());}
    penta::runtime::ExecutionOptions execution;
    execution.allowFigures = options.headlessFigures;
    execution.nativeFigureExport = options.headlessFigures;
    execution.odeCacheDirectory = std::filesystem::absolute(penta::cli::utf8Path(options.file)).parent_path()/".penta-cache"/"odes";
    execution.dataDirectory = std::filesystem::absolute(penta::cli::utf8Path(options.file)).parent_path();
    const auto result = penta::runtime::execute(source, options.analysis, execution);
    for (const auto& line : result.output) std::cout << line << '\n';
    std::cout.flush();
    penta::cli::textDiagnostics(result.analysis.diagnostics, options.file, source);
    penta::cli::textDiagnostics(result.runtimeDiagnostics, options.file, source);
    return result.ok() ? 0 : 1;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try { penta::DistributionActivity activity;if(!activity.valid())throw std::runtime_error("Cannot register running work for safe installation updates.");return runFile(penta::cli::arguments(argc, argv)); }
    catch (const std::exception& error) {
        std::cerr << "penta-run: input/operating-system failure: " << error.what() << '\n';
        return 2;
    }
}
