#include "../cli_support.hpp"
#include "../distribution_activity.hpp"

namespace {
int checkFile(const std::vector<std::string>& arguments) {
    penta::cli::Options options;
    std::string error;
    if (!penta::cli::parseArguments(arguments, false, options, error))
        return penta::cli::inputFailure("penta-check", options, error);
    if (options.help) { penta::cli::help("penta-check", false); return 0; }
    if (options.version) { std::cout << "penta-check " << penta::cli::toolVersion << " (static validation)\n"; return 0; }
    std::string source;
    if (!penta::cli::loadSource(options.file, source, error))
        return penta::cli::inputFailure("penta-check", options, error);
    options.analysis.sourceDirectory=std::filesystem::absolute(penta::cli::utf8Path(options.file)).parent_path();
    {const auto identity=std::filesystem::absolute(penta::cli::utf8Path(options.file)).u8string();options.analysis.sourceIdentity.assign(identity.begin(),identity.end());}
    const auto result = penta::language::analyse(source, options.analysis);
    if (options.json) penta::cli::jsonAnalysis(result, options.file, source);
    else {
        penta::cli::textDiagnostics(result.diagnostics, options.file, source);
        if (result.ok()) std::cout << "No static errors. Language " << result.program.languageVersion
            << "; " << result.program.algebraId << " gamma=" << penta::cli::number(result.program.gamma) << ".\n";
    }
    return result.ok() ? 0 : 1;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try { penta::DistributionActivity activity;if(!activity.valid())throw std::runtime_error("Cannot register running work for safe installation updates.");return checkFile(penta::cli::arguments(argc, argv)); }
    catch (const std::exception& error) {
        std::cerr << "penta-check: input/operating-system failure: " << error.what() << '\n';
        return 2;
    }
}
