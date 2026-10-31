#include "third_callback.hpp"
#include "third_callback_api.h"
#include "callback_generator.hpp"
#include "resident_cache.hpp"
#include "universal_native.hpp"
#include "ode/native_internal.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace penta::native_functions {
namespace {
struct Compiled {std::unique_ptr<ode::detail::Model> model;const penta_third_api_v1* api=nullptr;};
detail::ResidentCache<Compiled> cache;
struct Invocation {const Options* options;std::exception_ptr failure;};
int charge(void* raw,uint64_t count,char* error,uint64_t capacity){
    auto& invocation=*static_cast<Invocation*>(raw);
    try{
        if(invocation.options->build.cancellation&&invocation.options->build.cancellation->load())throw ode::OdeError("N2306","Native third derivative cancelled.");
        if(count>std::numeric_limits<std::size_t>::max())throw ode::OdeError("N2301","Native third-derivative work count exceeds the platform size limit.");
        if(invocation.options->chargeOperations)invocation.options->chargeOperations(static_cast<std::size_t>(count));
        return 0;
    }catch(const std::exception& e){invocation.failure=std::current_exception();if(error&&capacity){std::strncpy(error,e.what(),static_cast<std::size_t>(capacity-1));error[capacity-1]=0;}return -1;}
    catch(...){invocation.failure=std::current_exception();if(error&&capacity){std::strncpy(error,"Native third derivative interrupted.",static_cast<std::size_t>(capacity-1));error[capacity-1]=0;}return -1;}
}
}
struct ThirdCallback::State {
    std::shared_ptr<Compiled> compiled;Options options;
    std::vector<language::Type> outputs;
    std::vector<std::size_t> inputWidths,outputWidths;
    std::size_t inputs=0,outputCount=0;bool hit=false;language::SourceSpan span;std::string function;std::shared_ptr<const language::SourceOrigin> origin;
    ode::OdeError failure(const std::string& code,const std::string& message)const{ode::OdeError error(code,message,span);if(origin&&origin->source)error.frames.push_back({function,origin->identity,ode::detail::sha256(*origin->source),span,origin->source});return error;}
};
void validateThirdCallback(const language::FunctionDefinition& definition){if(definition.body.empty())throw ode::OdeError("N2301","A callable prototype has no native third-derivative body.",definition.span);(void)detail::generateThirdCallback(definition);}
std::string thirdFingerprint(const language::FunctionDefinition& definition){
    if(definition.body.empty())throw ode::OdeError("N2301","A callable prototype has no native third-derivative body.",definition.span);
    const auto generated=detail::generateThirdCallback(definition);return ode::detail::sha256(generated.normalized+generated.source);
}
ThirdCallback::ThirdCallback(const language::FunctionDefinition& definition,const Options& options){
    if(definition.body.empty())throw ode::OdeError("N2301","A callable prototype has no native third-derivative body.",definition.span);
    auto generated=detail::generateThirdCallback(definition);
    const auto directory=options.build.cacheDirectory.u8string();
    const auto identity=std::string(directory.begin(),directory.end())+":"+ode::detail::sha256(generated.normalized+generated.source);
    std::shared_ptr<Compiled> compiled;bool hit=false;
    auto resident=cache.lookupOrMakeRoom(identity);if(!resident.available)throw ode::OdeError("N2302","Native third-derivative session module limit reached (128 models are still referenced).",definition.span);compiled=std::move(resident.model);hit=bool(compiled);
    if(!compiled){
        auto candidate=std::make_shared<Compiled>();candidate->model=ode::detail::compile(generated,options.build);
        penta_third_query_v1 query=nullptr;
#ifdef _WIN32
        const auto symbol=GetProcAddress(static_cast<HMODULE>(candidate->model->library),"penta_callback_third_query_v1");
        static_assert(sizeof(query)==sizeof(symbol));std::memcpy(&query,&symbol,sizeof(query));
#else
        const auto symbol=dlsym(candidate->model->library,"penta_callback_third_query_v1");
        static_assert(sizeof(query)==sizeof(symbol));std::memcpy(&query,&symbol,sizeof(query));
#endif
        candidate->api=query?query(PENTA_THIRD_CALLBACK_ABI_V1):nullptr;
        if(!candidate->api||candidate->api->struct_size<sizeof(penta_third_api_v1)||candidate->api->abi_version!=PENTA_THIRD_CALLBACK_ABI_V1||!candidate->api->evaluate||candidate->api->inputs!=generated.parameters||candidate->api->outputs!=generated.coordinates)
            throw ode::OdeError("N2302","Generated module has no compatible third-derivative capability.",definition.span);
        auto admitted=cache.publish(identity,candidate);if(!admitted.first)throw ode::OdeError("N2302","Native third-derivative session module limit reached (128 models are still referenced).",definition.span);compiled=std::move(admitted.first);hit=!admitted.second||compiled->model->cacheHit;
    }
    state_=std::make_shared<State>();state_->compiled=std::move(compiled);state_->options=options;state_->inputs=generated.parameters;state_->outputCount=generated.coordinates;
    state_->outputs=definition.result==language::Type::Tuple?definition.results:std::vector<language::Type>{definition.result};state_->hit=hit;state_->span=definition.span;state_->function=definition.name;state_->origin=definition.origin;
    state_->inputWidths=callbackInputWidths(definition);state_->outputWidths=callbackOutputWidths(definition);
    if(options.build.cacheStatus)state_->hit=options.build.cacheStatus(state_->compiled->model->key,state_->hit);
}
std::size_t ThirdCallback::inputCount()const{return state_?state_->inputs:0;}
std::size_t ThirdCallback::outputCount()const{return state_?state_->outputCount:0;}
const std::vector<language::Type>& ThirdCallback::outputTypes()const{if(!state_)throw ode::OdeError("N2301","Uninitialized third-derivative callback.");return state_->outputs;}
const std::vector<std::size_t>& ThirdCallback::inputWidths()const{if(!state_)throw ode::OdeError("N2301","Uninitialized third-derivative callback.");return state_->inputWidths;}
const std::vector<std::size_t>& ThirdCallback::outputWidths()const{if(!state_)throw ode::OdeError("N2301","Uninitialized third-derivative callback.");return state_->outputWidths;}
const std::string& ThirdCallback::key()const{if(!state_)throw ode::OdeError("N2301","Uninitialized third-derivative callback.");return state_->compiled->model->key;}
bool ThirdCallback::cacheHit()const{return state_&&state_->hit;}
ThirdResult ThirdCallback::evaluate(double gamma,const std::vector<double>& input,const ThirdRequest& request)const{
    const auto span=state_?state_->span:language::SourceSpan{};
    if(!state_||input.size()!=state_->inputs||!std::isfinite(gamma)||gamma<0||gamma>1)throw ode::OdeError("N2301","Invalid third-derivative point or algebra parameter; A_gamma_v1 requires gamma in[0,1].",span);
    for(const auto value:input)if(!std::isfinite(value))throw ode::OdeError("N2301","Third-derivative point coordinates must be finite.",span);
    auto active=request.active;if(active.empty()){active.resize(input.size());std::iota(active.begin(),active.end(),0);}
    if(active.empty()||active.size()>128||request.u.size()!=active.size()||request.v.size()!=active.size()||request.w.size()!=active.size())throw ode::OdeError("N2301","All three directions must match the selected real-coordinate count.",span);
    std::set<std::size_t> unique;std::array<double,128> u{},v{},w{},values{},third{};std::array<unsigned char,128> dependent{};
    for(std::size_t at=0;at<active.size();++at){
        const auto index=active[at];if(index>=input.size()||!unique.insert(index).second)throw ode::OdeError("N2301","Third-derivative coordinates must be unique valid real input indices.",span);
        if(!std::isfinite(request.u[at])||!std::isfinite(request.v[at])||!std::isfinite(request.w[at]))throw ode::OdeError("N2301","Third-derivative directions must be finite.",span);
        u[index]=request.u[at];v[index]=request.v[at];w[index]=request.w[at];dependent[index]=1;
    }
    Invocation invocation{&state_->options,{}};penta_third_host_v1 host{sizeof(penta_third_host_v1),PENTA_THIRD_CALLBACK_ABI_V1,&invocation,charge};char error[2048]{};
    const auto status=state_->compiled->api->evaluate(gamma,input.data(),u.data(),v.data(),w.data(),dependent.data(),values.data(),third.data(),&host,error,sizeof(error));
    if(status){if(invocation.failure)std::rethrow_exception(invocation.failure);if(state_->options.build.cancellation&&state_->options.build.cancellation->load())throw ode::OdeError("N2306","Native third derivative cancelled.",span);throw state_->failure("N2305",error[0]?error:"Native third-derivative evaluation failed.");}
    ThirdResult result;result.values.assign(values.begin(),values.begin()+static_cast<std::ptrdiff_t>(state_->outputCount));result.third.assign(third.begin(),third.begin()+static_cast<std::ptrdiff_t>(state_->outputCount));result.columns=active.size();result.cacheHit=state_->hit;result.key=key();return result;
}
}
