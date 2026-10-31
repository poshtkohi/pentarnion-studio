#include "native_function.hpp"
#include "resident_cache.hpp"
#include "algebra/elementary.hpp"
#include "ode/native_internal.hpp"
#include "ode/native_build_config.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
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
using language::Type;using language::Expr;using language::ExprKind;using language::Statement;using language::StatementKind;
using P=algebra::Pentarnion;
bool scalar(Type t){return t==Type::Real||t==Type::Integer;}
std::size_t width(Type t){if(t==Type::Real)return 1;if(t==Type::Pentarnion)return 5;throw ode::OdeError("N1001","Native function parameters/results currently require real or pentarnion type.");}
struct Host {std::uint32_t abi=1;void* context=nullptr;
    int(*charge)(void*,std::size_t,char*,std::size_t)=nullptr;
    int(*math)(void*,std::uint32_t,const double*,std::int64_t,double*,char*,std::size_t)=nullptr;};
using Entry=int(*)(double,const double*,double*,const Host*,char*,std::size_t);
struct Invocation {double gamma;const Options* options;std::exception_ptr failure;};
void errorText(char* out,std::size_t capacity,const char* text){if(out&&capacity){std::strncpy(out,text,capacity-1);out[capacity-1]=0;}}
int charge(void* context,std::size_t count,char* error,std::size_t capacity){try{auto& call=*static_cast<Invocation*>(context);
    if(call.options->build.cancellation&&call.options->build.cancellation->load())throw ode::OdeError("N1006","Native function execution cancelled.");
    if(call.options->chargeOperations)call.options->chargeOperations(count);
    return 0;
}catch(const std::exception& failure){static_cast<Invocation*>(context)->failure=std::current_exception();errorText(error,capacity,failure.what());return -1;}catch(...){static_cast<Invocation*>(context)->failure=std::current_exception();errorText(error,capacity,"Native function was interrupted.");return -1;}}
int elementary(void* context,std::uint32_t operation,const double* input,std::int64_t auxiliary,double* output,char* error,std::size_t capacity){try{
    const auto& call=*static_cast<Invocation*>(context);algebra::Coefficients coefficients{};std::copy_n(input,5,coefficients.begin());const P value(algebra::Context(call.gamma),coefficients);
    P result;
    switch(operation){case 1:result=algebra::sine(value);break;case 2:result=algebra::cosine(value);break;case 3:result=algebra::exponential(value);break;
    case 4:result=algebra::hyperbolicSine(value);break;case 5:result=algebra::hyperbolicCosine(value);break;case 6:result=algebra::power(value,auxiliary);break;
    case 7:result=algebra::inverse(value);break;default:throw std::runtime_error("Invalid native elementary operation.");}
    std::copy(result.coefficients().begin(),result.coefficients().end(),output);return 0;
}catch(const std::exception& failure){errorText(error,capacity,failure.what());return -1;}catch(...){errorText(error,capacity,"Native elementary function failed.");return -1;}}
struct Binding {Type type;std::string code;};
class Generator {
    const language::FunctionDefinition& definition;std::map<std::string,Binding> bindings;std::size_t serial=0,nodes=0;
    [[noreturn]] void invalid(const std::string& message,language::SourceSpan span={})const{throw ode::OdeError("N1001",message,span.length?span:definition.span);}
    std::string promote(const Expr& value,const std::string& code)const{return value.type==Type::Pentarnion?code:"lift<S>("+code+")";}
    std::int64_t literal(const language::ExprPtr& value,std::int64_t maximum) {
        if(!value)invalid("Missing native integer literal.");
        if(value->kind==ExprKind::Group)return literal(value->children.at(0),maximum);
        if(value->kind==ExprKind::Unary&&(value->text=="+"||value->text=="-")){auto n=literal(value->children.at(0),maximum);return value->text=="-"?-n:n;}
        std::int64_t n=0;if(value->kind!=ExprKind::IntegerLiteral)invalid("Native power/coeff arguments currently require integer literals.",value->span);
        const auto result=std::from_chars(value->text.data(),value->text.data()+value->text.size(),n);
        if(result.ec!=std::errc()||result.ptr!=value->text.data()+value->text.size()||n<0||n>maximum)invalid("Native integer literal exceeds its supported bound.",value->span);
        return n;
    }
    std::string expr(const language::ExprPtr& pointer,std::size_t depth=0){
        if(!pointer)invalid("Missing native expression.");
        const auto& value=*pointer;
        if(++nodes>20000||depth>128)invalid("Native function expression node/depth limit exceeded.",value.span);
        if(!scalar(value.type)&&value.type!=Type::Pentarnion)invalid("Native expressions currently require real, integer-literal or Pentarnion scalar values.",value.span);
        result.normalized+=std::to_string(static_cast<int>(value.kind))+":"+std::to_string(static_cast<int>(value.type))+":"+std::to_string(value.text.size())+":"+value.text+"[";
        auto child=[&](std::size_t n){if(n>=value.children.size())invalid("Invalid native expression arity.",value.span);return expr(value.children[n],depth+1);};std::string code;
        switch(value.kind){
        case ExprKind::IntegerLiteral:{std::int64_t number=0;const auto parsed=std::from_chars(value.text.data(),value.text.data()+value.text.size(),number);
            if(parsed.ec!=std::errc()||parsed.ptr!=value.text.data()+value.text.size())invalid("Native integer literal is outside int64.",value.span);
            code="S(std::int64_t("+std::to_string(number)+"))";break;}
        case ExprKind::RealLiteral:{double number=0;const auto parsed=std::from_chars(value.text.data(),value.text.data()+value.text.size(),number,std::chars_format::general);
            if(parsed.ec!=std::errc()||parsed.ptr!=value.text.data()+value.text.size()||!std::isfinite(number))invalid("Native literal must be finite binary64.",value.span);
            std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::hexfloat<<number;code="S("+stream.str()+"L)";break;}
        case ExprKind::Reference:{const auto it=bindings.find(value.text);if(it!=bindings.end())code=it->second.code;
            else {const std::map<std::string,int> basis{{"i",1},{"j",2},{"k",3},{"ell",4}};const auto found=basis.find(value.text);if(found==basis.end())invalid("Native functions cannot capture variables or call unsupported references.",value.span);code="basis<S>("+std::to_string(found->second)+")";}break;}
        case ExprKind::Group:code="("+child(0)+")";break;
        case ExprKind::Unary:if(value.text!="+"&&value.text!="-"&&value.text!="!"&&value.text!="~")invalid("Unsupported native unary operation.",value.span);
            if(value.text=="-"&&value.children.at(0)->kind==ExprKind::IntegerLiteral&&value.children[0]->text=="9223372036854775808")code="S(std::numeric_limits<std::int64_t>::min())";
            else code="("+(value.text=="~"?std::string("!"):value.text)+child(0)+")";
            break;
        case ExprKind::Binary:{const auto a=child(0),b=child(1);const auto& left=*value.children[0];const auto& right=*value.children[1];const auto& op=value.text;
            if(value.type==Type::Integer&&(op=="+"||op=="-"||op=="*"||op=="^"))invalid("Native functions currently require real arithmetic for computed numeric expressions; use real literals or real parameters.",value.span);
            if(op=="+"||op=="-")code="("+(value.type==Type::Pentarnion?promote(left,a):a)+op+(value.type==Type::Pentarnion?promote(right,b):b)+")";
            else if(op=="*")code=left.type==Type::Pentarnion&&right.type==Type::Pentarnion?"product("+a+","+b+",gamma)":"("+a+"*"+b+")";
            else if(op=="/"){if(!scalar(right.type))invalid("Native division currently requires a real scalar denominator.",value.span);code="divide("+a+","+b+")";}
            else if(op=="^"){const auto exponent=literal(value.children[1],100000);code=left.type==Type::Pentarnion?"host_math("+a+",6,"+std::to_string(exponent)+",host)":"checked(std::pow("+a+",S("+std::to_string(exponent)+")))";}
            else if(op=="=="||op=="!="||op=="~="){code=(left.type==Type::Pentarnion||right.type==Type::Pentarnion)?"pent_equal("+promote(left,a)+","+promote(right,b)+")":"("+a+"=="+b+")";if(op!="==")code="(!"+code+")";}
            else if(op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||")code="("+a+op+b+")";
            else invalid("Unsupported native binary operator.",value.span);
            break;}
        case ExprKind::PentConstructor:case ExprKind::Call:{const auto& name=value.kind==ExprKind::PentConstructor?std::string("pent"):value.text;
            if(name=="pent"){if(value.children.size()!=5)invalid("Native pent requires five scalar coefficients.",value.span);code="pent<S>(";for(std::size_t n=0;n<5;++n){if(!scalar(value.children[n]->type))invalid("Native pent coefficients must be real.",value.span);if(n)code+=",";code+=child(n);}code+=")";}
            else if(name=="coeff"){const auto index=literal(value.children.at(1),4);if(index<0)invalid("Coefficient index must be 0..4.",value.span);code="("+promote(*value.children[0],child(0))+").c["+std::to_string(index)+"]";}
            else if(name=="scalar"||name=="pure"||name=="conj"||name=="norm2"||name=="inv"){
                const auto a=promote(*value.children.at(0),child(0));code=name=="scalar"?"("+a+").c[0]":name=="inv"?"host_math("+a+",7,0,host)":(name=="conj"?"conjugate":name)+"("+a+")";}
            else if(name=="commutator"||name=="associator"){code=name+"(";for(std::size_t n=0;n<value.children.size();++n){if(n)code+=",";code+=promote(*value.children[n],child(n));}code+=",gamma)";}
            else {const std::map<std::string,int> whole{{"sin",1},{"cos",2},{"exp",3},{"sinh",4},{"cosh",5}};
                if(value.children.size()==1&&value.children[0]->type==Type::Pentarnion){const auto found=whole.find(name);if(found==whole.end())invalid("This native elementary function has no whole-Pentarnion definition.",value.span);code="host_math("+child(0)+","+std::to_string(found->second)+",0,host)";}
                else {const std::vector<std::string> unary{"sin","cos","exp","sinh","cosh","tan","asin","acos","atan","tanh","asinh","acosh","atanh","log","log10","log2","sqrt","abs","floor","ceil","round","expm1","log1p","erf","erfc"};
                    if(std::find(unary.begin(),unary.end(),name)!=unary.end()&&value.children.size()==1&&scalar(value.children[0]->type))code="checked(std::"+name+"("+child(0)+"))";
                    else if((name=="hypot"||name=="atan2")&&value.children.size()==2&&scalar(value.children[0]->type)&&scalar(value.children[1]->type))code="checked(std::"+name+"("+child(0)+","+child(1)+"))";
                    else invalid("Unsupported call in native function: "+name+".",value.span);}}
            break;}
        default:invalid("Native functions currently support scalar expression trees, not arrays, callbacks, imports or records.",value.span);}
        result.normalized+="]";return code;
    }
    std::string statements(const std::vector<Statement>& body){const auto original=bindings;std::string code;
        for(const auto& statement:body){if(++nodes>20000)invalid("Native statement limit exceeded.",statement.span);result.normalized+="stmt:"+std::to_string(static_cast<int>(statement.kind))+";";
            code+="host_charge(host,1);\n";
            switch(statement.kind){
            case StatementKind::Declaration:{if(statement.declaredType!=Type::Real&&statement.declaredType!=Type::Pentarnion)invalid("Native local declarations currently require real or pentarnion type.",statement.span);
                const auto name="v"+std::to_string(serial++);const auto value=expr(statement.expression);code+=(statement.declaredType==Type::Real?"S ":"Pent<S> ")+name+"="+(statement.declaredType==Type::Pentarnion?promote(*statement.expression,value):value)+";\n";bindings.insert_or_assign(statement.name,Binding{statement.declaredType,name});break;}
            case StatementKind::Assignment:{const auto found=bindings.find(statement.name);if(found==bindings.end())invalid("Native assignment target was not declared.",statement.span);const auto value=expr(statement.expression);code+=found->second.code+"="+(found->second.type==Type::Pentarnion?promote(*statement.expression,value):value)+";\n";break;}
            case StatementKind::If:{const auto condition=expr(statement.expression);code+="if("+condition+"){\n"+statements(statement.body)+"}\n";if(!statement.alternative.empty())code+="else{\n"+statements(statement.alternative)+"}\n";break;}
            case StatementKind::Return:{if(!statement.expression)invalid("Native scalar functions must return a value.",statement.span);const auto value=expr(statement.expression);code+="return "+(definition.result==Type::Pentarnion?promote(*statement.expression,value):value)+";\n";break;}
            default:invalid("Native scalar functions support local declarations, assignment, if/else and return. Loops/IO remain interpreted.",statement.span);}
        }bindings=original;return code;}
public:
    ode::detail::Generated result;
    explicit Generator(const language::FunctionDefinition& function):definition(function){}
    ode::detail::Generated run(){result.normalized="PENTA_NATIVE_FUNCTION_1;";result.coordinates=width(definition.result);
        if(definition.results.size()>1)invalid("Native functions currently return one real/Pentarnion scalar.");
        if(definition.parameters.size()>64)invalid("Native function parameter limit exceeded.");
        std::string setup;
        for(const auto& parameter:definition.parameters){const auto count=width(parameter.type);const auto name="a"+std::to_string(serial++);bindings.emplace(parameter.name,Binding{parameter.type,name});
            setup+=(count==1?"S ":"Pent<S> ")+name+"=";
            if(count==1)setup+="S(parameters["+std::to_string(result.parameters)+"])";
            else{setup+="pent<S>(";for(std::size_t n=0;n<5;++n){if(n)setup+=",";setup+="S(parameters["+std::to_string(result.parameters+n)+"])";}setup+=")";}setup+=";\n";result.parameters+=count;result.normalized+="param:"+std::to_string(static_cast<int>(parameter.type))+";";}
        if(result.parameters>128)invalid("Native function supports at most 128 real parameter coordinates.");
        const auto body=statements(definition.body);result.normalized+="result:"+std::to_string(static_cast<int>(definition.result));
        result.source=std::string(ode::detail::embeddedNativeSupport)+R"FN(
#include <cstdint>
using namespace penta_generated;
struct FunctionHost {std::uint32_t abi;void* context;int(*charge)(void*,std::size_t,char*,std::size_t);int(*math)(void*,std::uint32_t,const double*,std::int64_t,double*,char*,std::size_t);};
inline void host_charge(const FunctionHost* h,std::size_t n){char e[1024]{};if(!h||h->abi!=1||!h->charge||h->charge(h->context,n,e,sizeof(e)))throw std::domain_error(e[0]?e:"Invalid native function host ABI.");}
template<class S>Pent<S> host_math(const Pent<S>& p,std::uint32_t op,std::int64_t aux,const FunctionHost* h){double a[5],b[5];char e[1024]{};for(unsigned n=0;n<5;++n)a[n]=binary64(p.c[n]);if(!h||h->abi!=1||!h->math||h->math(h->context,op,a,aux,b,e,sizeof(e)))throw std::domain_error(e[0]?e:"Native elementary callback failed.");Pent<S> r;for(unsigned n=0;n<5;++n)r.c[n]=S(b[n]);return r;}

#ifdef _WIN32
#define FN_EXPORT extern "C" __declspec(dllexport)
#else
#define FN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
)FN";
#ifndef PENTA_DISTRIBUTION_BUILD
        result.source+="template<class S>bool pent_equal(const Pent<S>& a,const Pent<S>& b){for(unsigned n=0;n<5;++n)if(a.c[n]!=b.c[n])return false;return true;}\n";
#endif
        result.source+="using S=long double;\n"+std::string(definition.result==Type::Real?"S":"Pent<S>")+" function_body(double gamma,const double*parameters,const FunctionHost*host){\n"+setup+body+"throw std::domain_error(\"Native function reached its end without returning.\");}\n";
        result.source+="FN_EXPORT unsigned penta_model_abi(){return 1;}\nFN_EXPORT unsigned penta_model_coordinates(){return "+std::to_string(result.coordinates)+";}\nFN_EXPORT unsigned penta_model_parameters(){return "+std::to_string(result.parameters)+";}\n";
        result.source+="FN_EXPORT int penta_function_evaluate(double gamma,const double* parameters,double* out,const FunctionHost* host,char* error,std::size_t capacity){try{host_charge(host,"+std::to_string(nodes)+");const auto value=function_body(gamma,parameters,host);";
        if(definition.result==Type::Real)result.source+="out[0]=binary64(value);";else result.source+="for(unsigned n=0;n<5;++n)out[n]=binary64(value.c[n]);";
        result.source+="return 0;}catch(const std::exception& e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}}\nFN_EXPORT int penta_model_evaluate(double,double,const double*,const double*,double*,double*,char*,std::size_t){return -1;}\n";return result;}
};
struct Cached {std::unique_ptr<ode::detail::Model> model;Entry entry=nullptr;};
detail::ResidentCache<Cached> cache;
}
void validate(const language::FunctionDefinition& definition){(void)Generator(definition).run();}
std::string functionFingerprint(const language::FunctionDefinition& definition){
    if(definition.body.empty())throw ode::OdeError("N1001","A function prototype has no native body to prepare.",definition.span);
    const auto generated=Generator(definition).run();return ode::detail::sha256(generated.normalized+generated.source);
}
std::pair<std::string,bool> prepare(const language::FunctionDefinition& definition,const Options& options){
    auto model=ode::detail::compile(Generator(definition).run(),options.build);
    return {model->key,model->cacheHit};
}
Result evaluate(const language::FunctionDefinition& definition,double gamma,const std::vector<Value>& arguments,const Options& options){
    if(arguments.size()!=definition.parameters.size())throw ode::OdeError("N1001","Native function argument count mismatch.",definition.span);
    auto generated=Generator(definition).run();std::vector<double> flat;flat.reserve(generated.parameters);
    for(std::size_t n=0;n<arguments.size();++n){if(arguments[n].type!=definition.parameters[n].type)throw ode::OdeError("N1001","Native function argument type mismatch.",definition.span);
        const auto count=width(arguments[n].type);for(std::size_t c=0;c<count;++c){const auto value=arguments[n].coefficients[c];if(!std::isfinite(value))throw ode::OdeError("N1001","Native function inputs must be finite.",definition.span);flat.push_back(value);}}
    if(!std::isfinite(gamma)||std::abs(gamma)>1e6)throw ode::OdeError("N1001","Invalid native function algebra parameter.",definition.span);
    const auto directory=options.build.cacheDirectory.u8string();const auto identity=std::string(directory.begin(),directory.end())+":"+ode::detail::sha256(generated.normalized+generated.source);
    std::shared_ptr<Cached> compiled;bool memoryHit=false;
    auto resident=cache.lookupOrMakeRoom(identity);if(!resident.available)throw ode::OdeError("N1002","The session native function module limit was reached (128 models are still referenced).",definition.span);compiled=std::move(resident.model);memoryHit=bool(compiled);
    if(!compiled){auto candidate=std::make_shared<Cached>();candidate->model=ode::detail::compile(generated,options.build);
#ifdef _WIN32
        const auto symbol=GetProcAddress(static_cast<HMODULE>(candidate->model->library),"penta_function_evaluate");
        static_assert(sizeof(symbol)==sizeof(candidate->entry));std::memcpy(&candidate->entry,&symbol,sizeof(symbol));
#else
        candidate->entry=reinterpret_cast<Entry>(dlsym(candidate->model->library,"penta_function_evaluate"));
#endif
        if(!candidate->entry)throw ode::OdeError("N1002","Native function export is missing.",definition.span);
        auto admitted=cache.publish(identity,candidate);if(!admitted.first)throw ode::OdeError("N1002","The session native function module limit was reached (128 models are still referenced).",definition.span);compiled=std::move(admitted.first);memoryHit=!admitted.second;
    }
    Invocation invocation{gamma,&options,{}};Host host{1,&invocation,charge,elementary};Result result;result.value.type=definition.result;result.key=compiled->model->key;result.cacheHit=memoryHit||compiled->model->cacheHit;
    if(options.build.cacheStatus)result.cacheHit=options.build.cacheStatus(result.key,result.cacheHit);
    std::array<char,1024> error{};
    if(compiled->entry(gamma,flat.data(),result.value.coefficients.data(),&host,error.data(),error.size())){
        if(invocation.failure)std::rethrow_exception(invocation.failure);
        if(options.build.cancellation&&options.build.cancellation->load())throw ode::OdeError("N1006","Native function execution cancelled.",definition.span);
        throw ode::OdeError("N1005",error[0]?error.data():"Native function evaluation failed.",definition.span);}
    return result;
}
}
