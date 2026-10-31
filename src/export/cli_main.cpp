#include "code_export.hpp"
#include "distribution_resources.hpp"
#include "../../apps/distribution_activity.hpp"
#include <chrono>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
std::filesystem::path path(const std::string& text){return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));}
int run(const std::vector<std::string>& arguments){
    using namespace penta::exporting;
    std::vector<std::string> positional;Target target=Target::Python;Options options;penta::language::AnalysisOptions analysis;
    for(std::size_t n=0;n<arguments.size();++n){
        const auto& arg=arguments[n];
        if(arg=="--help"||arg=="-h"){std::cout<<"penta-export --target python|matlab [--gamma NUMBER] [--python-bdf-adapter|--matlab-ode15s-adapter] [--scientific-adapters] source.p NEW_OUTPUT_DIRECTORY\nExports the checked supported subset, runtime, source map and compatibility report.\nUse --gamma for a project's default context when the source has no use_algebra directive.\nExplicit ODE mappings use SciPy BDF or MATLAB ode15s BDF with generated analytic Jacobians; their adaptive trajectories differ from SUNDIALS.\n";return 0;}
        if(arg=="--target"){if(++n==arguments.size())throw std::runtime_error("--target requires python or matlab");if(arguments[n]=="python")target=Target::Python;else if(arguments[n]=="matlab")target=Target::Matlab;else throw std::runtime_error("Unknown export target");}
        else if(arg=="--python-bdf-adapter")options.allowPythonBdfAdapter=true;
        else if(arg=="--matlab-ode15s-adapter")options.allowMatlabOde15sAdapter=true;
        else if(arg=="--scientific-adapters")options.allowScientificAdapters=true;
        else if(arg=="--gamma"){
            if(++n==arguments.size())throw std::runtime_error("--gamma requires a finite number in[0,1]");
            std::string_view literal(arguments[n]);if(literal.starts_with('+'))literal.remove_prefix(1);
            const auto converted=std::from_chars(literal.data(),literal.data()+literal.size(),analysis.gamma);
            if(converted.ec!=std::errc()||converted.ptr!=literal.data()+literal.size()||!std::isfinite(analysis.gamma)||analysis.gamma<0||analysis.gamma>1)throw std::runtime_error("--gamma requires a finite number in[0,1]");
        }
        else if(arg.starts_with("--"))throw std::runtime_error("Unknown option: "+arg);
        else positional.push_back(arg);
    }
    if(positional.size()!=2)throw std::runtime_error("Use penta-export --help for the two required paths");
    const auto input=std::filesystem::absolute(path(positional[0])),output=std::filesystem::absolute(path(positional[1]));
    if(input.extension()!=".p"||std::filesystem::file_size(input)>4*1024*1024)throw std::runtime_error("Input requires a .p source of at most4MiB");
    std::ifstream stream(input,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read input source");const std::string source{std::istreambuf_iterator<char>(stream),{}};
    analysis.sourceDirectory=input.parent_path();options.sourceName=positional[0];
    const auto checked=penta::language::analyse(source,analysis);
    std::string resourceError;if(!loadCompiledRuntimeFiles(options,target,resourceError))throw std::runtime_error(resourceError);
    const auto result=exportProgram(checked,target,options);
    if(!result.ok()){
        for(const auto& diagnostic:result.diagnostics){const auto location=penta::language::sourceLocation(source,diagnostic.span.offset);std::cerr<<positional[0]<<":"<<location.line<<":"<<location.column<<": "<<diagnostic.code<<": "<<diagnostic.message<<'\n';}return 1;
    }
    if(std::filesystem::symlink_status(output).type()!=std::filesystem::file_type::not_found)throw std::runtime_error("Choose a new output directory; exports never overwrite existing files");
    if(!std::filesystem::is_directory(output.parent_path()))throw std::runtime_error("Output parent directory does not exist");
    const auto staging=output.parent_path()/(".penta-export-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!std::filesystem::create_directory(staging))throw std::runtime_error("Cannot create private export staging directory");
    try{
        for(const auto& file:result.files){if(path(file.name).filename()!=path(file.name))throw std::runtime_error("Unsafe generated export file name");std::ofstream destination(staging/path(file.name),std::ios::binary);destination.write(file.contents.data(),static_cast<std::streamsize>(file.contents.size()));destination.close();if(!destination)throw std::runtime_error("Export write failed");}
        std::filesystem::rename(staging,output);
    }catch(...){for(const auto& file:result.files)if(path(file.name).filename()==path(file.name)){std::error_code ignored;std::filesystem::remove(staging/path(file.name),ignored);}std::error_code ignored;std::filesystem::remove(staging,ignored);throw;}
    std::cout<<"Exported checked supported subset to "<<positional[1]<<"\nReview compatibility.json before running the target script.\n";return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
    std::vector<std::string> arguments;for(int n=1;n<argc;++n){const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[n],-1,nullptr,0,nullptr,nullptr);if(!count)return 2;std::string text(static_cast<std::size_t>(count),0);WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[n],-1,text.data(),count,nullptr,nullptr);text.pop_back();arguments.push_back(std::move(text));}
#else
int main(int argc,char** argv){std::vector<std::string> arguments(argv+1,argv+argc);
#endif
    try{penta::DistributionActivity activity;if(!activity.valid())throw std::runtime_error("Cannot register running work for safe installation updates.");return run(arguments);}catch(const std::exception& error){std::cerr<<"penta-export: "<<error.what()<<'\n';return 2;}
}
