#include "pent_symbolic.hpp"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::symbolic {
namespace {
struct Loaded {void* handle;const penta_pent_symbolic_api_v1* api;};
std::mutex moduleMutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executableDirectory(){
#ifdef _WIN32
 std::vector<wchar_t> buffer(32768);const auto size=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));if(!size||size>=buffer.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate exact symbolic module directory.");return std::filesystem::path(std::wstring(buffer.data(),size)).parent_path();
#else
 std::vector<char> buffer(32768);const auto size=readlink("/proc/self/exe",buffer.data(),buffer.size());if(size<=0||static_cast<std::size_t>(size)>=buffer.size())throw Error(PENTA_NUMERIC_INTERNAL,"Cannot locate exact symbolic module directory.");return std::filesystem::path(std::string(buffer.data(),size)).parent_path();
#endif
}
struct Host {
 const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;
 static int cancelled(void* raw)noexcept{const auto& h=*static_cast<Host*>(raw);return h.cancellation&&h.cancellation->load(std::memory_order_relaxed);}
 static int account(void* raw,uint64_t amount,char* error,uint64_t capacity)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.charge)h.charge(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& e){h.failure=std::current_exception();if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),n);error[n]=0;}return 1;}catch(...){h.failure=std::current_exception();return 1;}}
};
}
PentModule::PentModule(std::filesystem::path directory){if(directory.empty())directory=executableDirectory()/"modules";if(!directory.is_absolute())throw Error(PENTA_NUMERIC_ARGUMENT,"Exact symbolic module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_science.dll";
#else
 path_=directory/"libpenta_science.so";
#endif
}
const penta_pent_symbolic_api_v1& PentModule::api()const{
 std::lock_guard lock(moduleMutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
 const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load penta_science or its dependencies for exact coefficients.");const auto query=reinterpret_cast<penta_pent_symbolic_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_science_pent_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
 const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(PENTA_NUMERIC_INTERNAL,"Cannot load penta_science or its dependencies for exact coefficients.");const auto query=reinterpret_cast<penta_pent_symbolic_query_function_v1>(dlsym(handle,"penta_science_pent_query_v1"));auto release=[&]{dlclose(handle);};
#endif
 const auto extension=query?query(PENTA_PENT_SYMBOLIC_ABI_V1):nullptr;if(!extension||extension->struct_size<sizeof(*extension)||extension->abi_version!=PENTA_PENT_SYMBOLIC_ABI_V1||!extension->execute){release();throw Error(PENTA_NUMERIC_VERSION,"penta_science does not provide the compatible exact Pentarnion coefficient capability.");}return *modules.emplace(path_,Loaded{handle,extension}).first->second.api;
}
Result PentModule::execute(uint32_t operation,const std::string& expression,const std::string& reals,
 const std::string& pentarnions,const std::string& gamma,const std::string& variable,
 const std::string& replacement,const std::map<std::string,double>& bindings,uint64_t maxWork,
 const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
 const std::array<const std::string*,6> strings{&expression,&reals,&pentarnions,&gamma,&variable,&replacement};for(const auto* text:strings)if(text->find('\0')!=std::string::npos||text->size()>32768)throw Error(PENTA_NUMERIC_ARGUMENT,"Exact symbolic text contains NUL or exceeds32768 bytes.");
 std::vector<const char*> bindingNames;std::vector<double> values;for(const auto& [name,value]:bindings){bindingNames.push_back(name.c_str());values.push_back(value);}std::vector<char> output(200000);char error[1024]{};
 penta_pent_symbolic_request_v1 request{};request.struct_size=sizeof(request);request.operation=operation;request.expression=expression.c_str();request.real_symbols=reals.c_str();request.pent_symbols=pentarnions.c_str();request.gamma=gamma.c_str();request.variable=variable.c_str();request.replacement=replacement.c_str();request.max_work=maxWork;request.binding_count=bindings.size();request.binding_names=bindingNames.data();request.binding_values=values.data();request.output=output.data();request.output_capacity=output.size();
 penta_pent_symbolic_report_v1 report{};report.struct_size=sizeof(report);Host context{cancellation,charge,{}};const penta_pent_symbolic_host_v1 host{sizeof(host),PENTA_PENT_SYMBOLIC_ABI_V1,&context,Host::cancelled,Host::account};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().execute(&host,&request,&report,error,sizeof(error));});
 if(context.failure)std::rethrow_exception(context.failure);
 if(status)throw Error(status,error[0]?error:"Exact symbolic capability failed.");
 if(!std::memchr(output.data(),0,output.size()))throw Error(PENTA_NUMERIC_VERSION,"Unterminated exact symbolic result.");
 Result result;result.text=output.data();std::copy(std::begin(report.coefficients),std::end(report.coefficients),result.coefficients.begin());result.gamma=report.gamma;result.workUnits=static_cast<std::size_t>(report.work_units);return result;
}
}
