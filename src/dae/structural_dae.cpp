#include "structural_dae.hpp"
#include "runtime/lazy_module.hpp"
#include "runtime/thread_budget.hpp"
#include <cstring>
namespace penta::dae {
namespace {
struct Host {
    const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;std::exception_ptr failure;
    static int cancelled(void* raw){const auto& h=*static_cast<Host*>(raw);return h.cancellation&&h.cancellation->load();}
    static int account(void* raw,uint64_t work,char* error,uint64_t capacity)noexcept{auto& h=*static_cast<Host*>(raw);try{if(h.charge)h.charge(work);return 0;}catch(const std::exception& e){h.failure=std::current_exception();if(error&&capacity){const auto length=std::min<uint64_t>(capacity-1,std::strlen(e.what()));std::memcpy(error,e.what(),length);error[length]=0;}return 1;}catch(...){h.failure=std::current_exception();return 1;}}
};
const penta_dae_api_v3& api(const std::filesystem::path& path){const auto& a=runtime::detail::NativeModuleRegistry<penta_dae_api_v3>::get(path,"penta_dae_query_v3",PENTA_DAE_ABI_V3);if(!a.structure||!a.initialize)throw Error(5,"DAE module lacks structural/complete consistent-initial capability.");return a;}
}
StructuralModule::StructuralModule(std::filesystem::path directory):path_(runtime::detail::nativeModulePath(std::move(directory),"penta_dae")){}
StructuralResult StructuralModule::structure(const penta_dae_structure_request_v3& q,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    if(!q.dimension||q.dimension>32)throw Error(1,"Structural DAE analysis supports1..32 complete real coordinates.");
    StructuralResult result;result.equationOffsets.resize(q.dimension);result.variableOffsets.resize(q.dimension);result.matching.resize(q.dimension);result.algebraic.resize(q.dimension);auto& report=result.report;report.struct_size=sizeof(report);report.capacity=q.dimension;report.equation_offsets=result.equationOffsets.data();report.variable_offsets=result.variableOffsets.data();report.matching=result.matching.data();report.algebraic=result.algebraic.data();Host context{cancellation,charge,{}};const penta_dae_host_v1 host{sizeof(host),PENTA_DAE_ABI_V1,&context,Host::cancelled,Host::account};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api(path_).structure(&host,&q,&report,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(Host::cancelled(&context)?3:status,error[0]?error:"Native DAE structural analysis failed.");report.equation_offsets=report.variable_offsets=report.matching=nullptr;report.algebraic=nullptr;return result;
}
InitialResult StructuralModule::initialize(const penta_dae_initialize_request_v3& q,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
    if(!q.dimension||q.dimension>32)throw Error(1,"Consistent DAE initialization supports1..32 complete real coordinates.");
    InitialResult result;result.initial.resize(q.dimension);result.derivative.resize(q.dimension);result.differentialAcceleration.resize(q.dimension);auto& report=result.report;report.struct_size=sizeof(report);report.capacity=q.dimension;report.initial=result.initial.data();report.initial_derivative=result.derivative.data();report.differential_acceleration=result.differentialAcceleration.data();Host context{cancellation,charge,{}};const penta_dae_host_v1 host{sizeof(host),PENTA_DAE_ABI_V1,&context,Host::cancelled,Host::account};char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api(path_).initialize(&host,&q,&report,error,sizeof(error));});if(context.failure)std::rethrow_exception(context.failure);if(status)throw Error(Host::cancelled(&context)?3:status,error[0]?error:"Native complete DAE initialization failed.");report.initial=report.initial_derivative=report.differential_acceleration=nullptr;return result;
}
}
