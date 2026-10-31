#include "vector_export.hpp"
#include "native_export.hpp"
#include "runtime/module_inventory.hpp"
#include <algorithm>
#include <cstring>
#include <exception>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace penta::plotting {
std::vector<unsigned char> renderVector(const std::vector<VectorCommand>& commands,int width,int height,int dpi,bool pdf,const std::string& metadata,const std::filesystem::path& directory,const std::shared_ptr<std::atomic_bool>& cancellation){
    const auto module=runtime::loadModule("vector_plot",directory);
#ifdef _WIN32
    const auto handle=GetModuleHandleW(module.path.c_str());const auto query=reinterpret_cast<penta_vector_plot_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_vector_plot_query_v1")));
#else
    auto* handle=dlopen(module.path.c_str(),RTLD_NOW|RTLD_LOCAL);struct Release {void* h;~Release(){if(h)dlclose(h);}} release{handle};const auto query=handle?reinterpret_cast<penta_vector_plot_query_function_v1>(dlsym(handle,"penta_vector_plot_query_v1")):nullptr;
#endif
    const auto* api=query?query(PENTA_VECTOR_PLOT_ABI_V1):nullptr;if(!api||api->struct_size<sizeof(*api)||api->abi_version!=PENTA_VECTOR_PLOT_ABI_V1||!api->render)throw ExportError("The optional vector plot module has an incompatible ABI.");
    std::vector<penta_vector_command_v1> raw;raw.reserve(commands.size());for(const auto& command:commands){auto value=command.data;value.text=command.text.data();value.text_bytes=command.text.size();raw.push_back(value);}
    struct Host {const std::shared_ptr<std::atomic_bool>& cancellation;std::vector<unsigned char> bytes;std::exception_ptr failure;static int stopped(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load();}static int append(void* raw,const char* bytes,uint64_t count)noexcept{auto& self=*static_cast<Host*>(raw);try{if(count>134217728-self.bytes.size())throw ExportError("Vector export exceeds128MiB.");self.bytes.insert(self.bytes.end(),reinterpret_cast<const unsigned char*>(bytes),reinterpret_cast<const unsigned char*>(bytes)+count);return 0;}catch(...){self.failure=std::current_exception();return 1;}}} host{cancellation,{},{}};
    penta_vector_host_v1 bridge{sizeof(bridge),PENTA_VECTOR_PLOT_ABI_V1,&host,Host::stopped,Host::append};penta_vector_request_v1 request{sizeof(request),pdf?2u:1u,static_cast<uint32_t>(width),static_cast<uint32_t>(height),static_cast<uint32_t>(dpi),raw.size(),134217728,raw.data(),metadata.data(),metadata.size()};char error[2048]{};const auto status=api->render(&bridge,&request,error,sizeof(error));if(host.failure)std::rethrow_exception(host.failure);if(status)throw ExportError(error[0]?error:"Native vector plot export failed.");return std::move(host.bytes);
}
}
