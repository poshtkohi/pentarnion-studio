#include "native_callback.hpp"
#include "callback_support.hpp"
#include "resident_cache.hpp"
#include "callback_generator.hpp"
#include "third_support.hpp"
#include "universal_codegen.hpp"
#include "ode/native_internal.hpp"
#include "ode/native_build_config.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
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
bool scalar(Type t){return t==Type::Real||t==Type::Integer;}
std::size_t width(Type t){if(t==Type::Real)return 1;if(t==Type::Pentarnion)return 5;throw ode::OdeError("N2001","Compiled callback parameters/results require real or Pentarnion scalars.");}
struct CallbackHost{std::uint32_t abi=1;void* context=nullptr;int(*charge)(void*,std::size_t,char*,std::size_t)=nullptr;};
using CallbackEntry=int(*)(double,const double*,const double*,const double*,const unsigned char*,double*,double*,double*,const CallbackHost*,char*,std::size_t);
struct Invocation{const Options* options;std::exception_ptr failure;};
int callbackCharge(void* context,std::size_t count,char* error,std::size_t capacity){
    auto& call=*static_cast<Invocation*>(context);
    try{if(call.options->build.cancellation&&call.options->build.cancellation->load())throw ode::OdeError("N2006","Native callback cancelled.");if(call.options->chargeOperations)call.options->chargeOperations(count);return 0;}
    catch(const std::exception& e){call.failure=std::current_exception();if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}
    catch(...){call.failure=std::current_exception();if(error&&capacity){std::strncpy(error,"Native callback interrupted.",capacity-1);error[capacity-1]=0;}return -1;}
}
struct Binding {Type type;std::string code;};
class CallbackGenerator {
    const language::FunctionDefinition& definition;std::map<std::string,Binding> bindings;std::size_t serial=0,nodes=0,declarations=0;bool thirdOrder=false;std::vector<Type> outputs;
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
        if(++nodes>(thirdOrder?4096u:20000u)||depth>128)invalid("Native function expression node/depth limit exceeded.",value.span);
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
            else if(op=="^"){const auto exponent=literal(value.children[1],100000);code=left.type==Type::Pentarnion?"cb_pent_power("+a+","+std::to_string(exponent)+",gamma)":"cb_ipow("+a+","+std::to_string(exponent)+")";}
            else if(op=="=="||op=="!="||op=="~="){code=(left.type==Type::Pentarnion||right.type==Type::Pentarnion)?"pent_equal("+promote(left,a)+","+promote(right,b)+")":"("+a+"=="+b+")";if(op!="==")code="(!"+code+")";}
            else if(op=="<"||op=="<="||op==">"||op==">="||op=="&&"||op=="||")code="("+a+op+b+")";
            else invalid("Unsupported native binary operator.",value.span);
            break;}
        case ExprKind::PentConstructor:case ExprKind::Call:{const auto& name=value.kind==ExprKind::PentConstructor?std::string("pent"):value.text;
            if(name=="pent"){if(value.children.size()!=5)invalid("Native pent requires five scalar coefficients.",value.span);code="pent<S>(";for(std::size_t n=0;n<5;++n){if(!scalar(value.children[n]->type))invalid("Native pent coefficients must be real.",value.span);if(n)code+=",";code+=child(n);}code+=")";}
            else if(name=="coeff"){const auto index=literal(value.children.at(1),4);if(index<0)invalid("Coefficient index must be 0..4.",value.span);code="("+promote(*value.children[0],child(0))+").c["+std::to_string(index)+"]";}
            else if(name=="scalar"||name=="pure"||name=="conj"||name=="norm2"||name=="inv"){
                const auto a=promote(*value.children.at(0),child(0));code=name=="scalar"?"("+a+").c[0]":name=="inv"?"cb_inverse("+a+")":(name=="conj"?"conjugate":name)+"("+a+")";}
            else if(name=="commutator"||name=="associator"){code=name+"(";for(std::size_t n=0;n<value.children.size();++n){if(n)code+=",";code+=promote(*value.children[n],child(n));}code+=",gamma)";}
            else {const std::map<std::string,int> whole{{"sin",1},{"cos",2},{"exp",3},{"sinh",4},{"cosh",5}};
                if(value.children.size()==1&&value.children[0]->type==Type::Pentarnion){const auto found=whole.find(name);if(found==whole.end())invalid("This native elementary function has no whole-Pentarnion definition.",value.span);code="cb_whole("+child(0)+","+std::to_string(found->second)+")";}
                else {const std::vector<std::string> unary{"sin","cos","exp","sinh","cosh","tan","asin","acos","atan","tanh","asinh","acosh","atanh","log","log10","log2","sqrt","abs","floor","ceil","round","expm1","log1p","erf","erfc","sign"};
                    if(std::find(unary.begin(),unary.end(),name)!=unary.end()&&value.children.size()==1&&scalar(value.children[0]->type))code="cb_"+name+"("+child(0)+")";
                    else if((name=="hypot"||name=="atan2")&&value.children.size()==2&&scalar(value.children[0]->type)&&scalar(value.children[1]->type))code="cb_"+name+"("+child(0)+","+child(1)+")";
                    else invalid("Unsupported call in native function: "+name+".",value.span);}}
            break;}
        default:invalid("Native functions currently support scalar expression trees, not arrays, callbacks, imports or records.",value.span);}
        result.normalized+="]";return code;
    }
    std::string returned(const Statement& statement){
        if(!statement.expression)invalid("Native callbacks must return numeric values.",statement.span);
        std::vector<language::ExprPtr> expressions;
        if(definition.result==Type::Tuple){auto tuple=statement.expression;while(tuple->kind==ExprKind::Group&&tuple->children.size()==1)tuple=tuple->children[0];if(tuple->kind!=ExprKind::TupleLiteral)invalid("Native multiple outputs require an explicit returned tuple.",statement.span);expressions=tuple->children;}
        else expressions.push_back(statement.expression);
        if(expressions.size()!=outputs.size())invalid("Native callback result arity mismatch.",statement.span);
        std::string code="{std::array<S,"+std::to_string(result.coordinates)+"> out{};\n";std::size_t offset=0;
        for(std::size_t n=0;n<outputs.size();++n){const auto source=expr(expressions[n]);const auto name="r"+std::to_string(serial++);code+="const auto "+name+"="+(outputs[n]==Type::Pentarnion?promote(*expressions[n],source):source)+";\n";
            if(outputs[n]==Type::Real)code+="out["+std::to_string(offset++)+"]=S("+name+");\n";
            else for(unsigned c=0;c<5;++c)code+="out["+std::to_string(offset++)+"]="+name+".c["+std::to_string(c)+"];\n";
        }return code+"return out;}\n";
    }
    std::string statements(const std::vector<Statement>& body){const auto original=bindings;std::string code;
        for(const auto& statement:body){if(++nodes>(thirdOrder?4096u:20000u))invalid("Native statement limit exceeded.",statement.span);result.normalized+="stmt:"+std::to_string(static_cast<int>(statement.kind))+";";
            code+="host_charge(host,1);\n";
            switch(statement.kind){
            case StatementKind::Declaration:{if(statement.declaredType!=Type::Real&&statement.declaredType!=Type::Pentarnion)invalid("Native local declarations currently require real or pentarnion type.",statement.span);
                if(thirdOrder&&++declarations>256)invalid("Third-derivative callbacks are bounded to256 local scalar declarations.",statement.span);
                const auto name="v"+std::to_string(serial++);const auto value=expr(statement.expression);code+=(statement.declaredType==Type::Real?"S ":"Pent<S> ")+name+"="+(statement.declaredType==Type::Pentarnion?promote(*statement.expression,value):value)+";\n";bindings.insert_or_assign(statement.name,Binding{statement.declaredType,name});break;}
            case StatementKind::Assignment:{const auto found=bindings.find(statement.name);if(found==bindings.end())invalid("Native assignment target was not declared.",statement.span);const auto value=expr(statement.expression);code+=found->second.code+"="+(found->second.type==Type::Pentarnion?promote(*statement.expression,value):value)+";\n";break;}
            case StatementKind::If:{const auto condition=expr(statement.expression);code+="if("+condition+"){\n"+statements(statement.body)+"}\n";if(!statement.alternative.empty())code+="else{\n"+statements(statement.alternative)+"}\n";break;}
            case StatementKind::Return:{code+=returned(statement);break;}
            default:invalid("Native scalar functions support local declarations, assignment, if/else and return. Loops/IO remain interpreted.",statement.span);}
        }bindings=original;return code;}
public:
    ode::detail::Generated result;
    explicit CallbackGenerator(const language::FunctionDefinition& function):definition(function){}
    ode::detail::Generated run(bool third=false){
        if(usesUniversalCoordinates(definition))return detail::generateUniversalCallback(definition,third);
        thirdOrder=third;
        result.normalized=third?"PENTA_NATIVE_CALLBACK_AD3_DIRECTIONAL_V1;":"PENTA_NATIVE_CALLBACK_AD2_1;";
        if(!definition.captureNames.empty())invalid("Compiled solver callbacks cannot implicitly capture values; pass fixed runtime parameters explicitly.");
        outputs=definition.result==Type::Tuple?definition.results:std::vector<Type>{definition.result};
        if(outputs.empty()||outputs.size()>16)invalid("A compiled callback requires1..16 numeric results.");
        for(const auto type:outputs)result.coordinates+=width(type);
        if(result.coordinates>128||definition.parameters.size()>64)invalid("Compiled callback output/parameter budget exceeded.");
        std::string setup;
        for(const auto& parameter:definition.parameters){const auto count=width(parameter.type);const auto name="a"+std::to_string(serial++);bindings.emplace(parameter.name,Binding{parameter.type,name});setup+=(count==1?"S ":"Pent<S> ")+name+"=";
            if(count==1)setup+="parameters["+std::to_string(result.parameters)+"]";
            else{setup+="pent<S>(";for(std::size_t n=0;n<5;++n){if(n)setup+=",";setup+="parameters["+std::to_string(result.parameters+n)+"]";}setup+=")";}setup+=";\n";result.parameters+=count;result.normalized+="param:"+std::to_string(static_cast<int>(parameter.type))+";";}
        if(!result.parameters||result.parameters>128)invalid("Compiled callbacks require1..128 real input coordinates.");
        const auto body=statements(definition.body);for(const auto type:outputs)result.normalized+="out:"+std::to_string(static_cast<int>(type))+";";
#ifdef PENTA_DISTRIBUTION_BUILD
        result.source=std::string(ode::detail::embeddedNativeSupport);
#else
        result.source=std::string(ode::detail::embeddedNativeSupport)+std::string(detail::callbackSupport)+(third?std::string(detail::thirdSupport):std::string());
#endif
        result.source+=R"CB(
#include <cstdint>
using namespace penta_generated;
struct CallbackHost{std::uint32_t abi;void* context;int(*charge)(void*,std::size_t,char*,std::size_t);};
inline void host_charge(const CallbackHost*h,std::size_t n){char e[1024]{};if(!h||h->abi!=1||!h->charge||h->charge(h->context,n,e,sizeof(e)))throw std::domain_error(e[0]?e:"Invalid native callback host ABI.");}
#ifdef _WIN32
#define CB_EXPORT extern "C" __declspec(dllexport)
#else
#define CB_EXPORT extern "C" __attribute__((visibility("default")))
#endif
)CB";
        result.source+="template<class S>std::array<S,"+std::to_string(result.coordinates)+"> callback_body(double gamma,const S*parameters,const CallbackHost*host){\n"+setup+body+"throw std::domain_error(\"Compiled callback reached its end without returning.\");}\n";
        result.source+="CB_EXPORT unsigned penta_model_abi(){return 1;}\nCB_EXPORT unsigned penta_model_coordinates(){return "+std::to_string(result.coordinates)+";}\nCB_EXPORT unsigned penta_model_parameters(){return "+std::to_string(result.parameters)+";}\n";
        if(third)result.source+=detail::thirdEntry(result.parameters,result.coordinates,nodes);
        else result.source+="CB_EXPORT int penta_callback_evaluate(double gamma,const double*input,const double*sx,const double*sy,const unsigned char*dependent,double*out,double*first,double*mixed,const CallbackHost*host,char*error,std::size_t capacity){try{host_charge(host,"+std::to_string(nodes)+");std::array<Jet2,"+std::to_string(result.parameters)+"> inputJets;for(unsigned n=0;n<"+std::to_string(result.parameters)+";++n){inputJets[n]=Jet2(input[n]);inputJets[n].x=sx?sx[n]:0;inputJets[n].y=sy?sy[n]:0;inputJets[n].dependent=dependent&&dependent[n];}const auto result=callback_body(gamma,inputJets.data(),host);for(unsigned n=0;n<"+std::to_string(result.coordinates)+";++n){out[n]=binary64(result[n].v);if(first)first[n]=binary64(result[n].x);if(mixed)mixed[n]=binary64(result[n].xy);}return 0;}catch(const std::exception&e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}}\nCB_EXPORT int penta_model_evaluate(double,double,const double*,const double*,double*,double*,char*,std::size_t){return -1;}\n";
        return result;
    }
};
struct CachedCallback{std::unique_ptr<ode::detail::Model>model;CallbackEntry entry=nullptr;};
detail::ResidentCache<CachedCallback> callbackCache;
}
namespace detail {ode::detail::Generated generateThirdCallback(const language::FunctionDefinition& definition){return CallbackGenerator(definition).run(true);}}
struct Callback::State{std::shared_ptr<CachedCallback> compiled;Options options;std::vector<Type> outputs;std::vector<std::size_t> inputWidths,outputWidths;std::size_t inputs=0,outputCount=0;bool hit=false;language::SourceSpan span;std::string function;std::shared_ptr<const language::SourceOrigin> origin;ode::OdeError failure(const std::string& code,const std::string& message)const{ode::OdeError error(code,message,span);if(origin&&origin->source)error.frames.push_back({function,origin->identity,ode::detail::sha256(*origin->source),span,origin->source});return error;}};
void validateCallback(const language::FunctionDefinition& function){(void)CallbackGenerator(function).run();}
std::string callbackFingerprint(const language::FunctionDefinition& function){
    if(function.body.empty())throw ode::OdeError("N2001","A callable prototype has no native body to prepare.",function.span);
    const auto generated=CallbackGenerator(function).run();return ode::detail::sha256(generated.normalized+generated.source);
}
Callback::Callback(const language::FunctionDefinition& function,const Options& options){
    auto generated=CallbackGenerator(function).run();const auto directory=options.build.cacheDirectory.u8string();const auto identity=std::string(directory.begin(),directory.end())+":"+ode::detail::sha256(generated.normalized+generated.source);std::shared_ptr<CachedCallback> compiled;bool hit=false;
    auto resident=callbackCache.lookupOrMakeRoom(identity);if(!resident.available)throw ode::OdeError("N2002","Native callback session module limit reached (128 models are still referenced).",function.span);compiled=std::move(resident.model);hit=bool(compiled);
    if(!compiled){auto candidate=std::make_shared<CachedCallback>();candidate->model=ode::detail::compile(generated,options.build);
#ifdef _WIN32
        const auto symbol=GetProcAddress(static_cast<HMODULE>(candidate->model->library),"penta_callback_evaluate");static_assert(sizeof(symbol)==sizeof(candidate->entry));std::memcpy(&candidate->entry,&symbol,sizeof(symbol));
#else
        candidate->entry=reinterpret_cast<CallbackEntry>(dlsym(candidate->model->library,"penta_callback_evaluate"));
#endif
        if(!candidate->entry)throw ode::OdeError("N2002","Native callback export is missing.",function.span);
        auto admitted=callbackCache.publish(identity,candidate);if(!admitted.first)throw ode::OdeError("N2002","Native callback session module limit reached (128 models are still referenced).",function.span);compiled=std::move(admitted.first);hit=!admitted.second||compiled->model->cacheHit;
    }
    state_=std::make_shared<State>();state_->compiled=std::move(compiled);state_->options=options;state_->inputs=generated.parameters;state_->outputCount=generated.coordinates;state_->outputs=function.result==Type::Tuple?function.results:std::vector<Type>{function.result};state_->hit=hit;state_->span=function.span;state_->function=function.name;state_->origin=function.origin;
    state_->inputWidths=callbackInputWidths(function);state_->outputWidths=callbackOutputWidths(function);
    if(options.build.cacheStatus)state_->hit=options.build.cacheStatus(state_->compiled->model->key,state_->hit);
}
std::size_t Callback::inputCount()const{return state_?state_->inputs:0;}std::size_t Callback::outputCount()const{return state_?state_->outputCount:0;}
const std::vector<Type>& Callback::outputTypes()const{if(!state_)throw ode::OdeError("N2001","Uninitialized native callback.");return state_->outputs;}
const std::vector<std::size_t>& Callback::inputWidths()const{if(!state_)throw ode::OdeError("N2001","Uninitialized native callback.");return state_->inputWidths;}
const std::vector<std::size_t>& Callback::outputWidths()const{if(!state_)throw ode::OdeError("N2001","Uninitialized native callback.");return state_->outputWidths;}
const std::string& Callback::key()const{if(!state_)throw ode::OdeError("N2001","Uninitialized native callback.");return state_->compiled->model->key;}
bool Callback::cacheHit()const{return state_&&state_->hit;}
void Callback::evaluateValues(double gamma,const std::vector<double>& input,std::span<double> output)const{
    if(!state_||input.size()!=state_->inputs||output.size()!=state_->outputCount||!output.data()||!std::isfinite(gamma)||std::abs(gamma)>1e6)
        throw ode::OdeError("N2001","Invalid compiled callback value input or output layout.",state_?state_->span:language::SourceSpan{});
    for(const auto value:input)if(!std::isfinite(value))throw ode::OdeError("N2001","Compiled callback inputs must be finite.",state_->span);
    // Generated callbacks have at most128 output coordinates. Use bounded stack
    // storage so even a late domain/host failure cannot partly overwrite output.
    std::array<double,128> candidate{};
    Invocation invocation{&state_->options,{}};CallbackHost host{1,&invocation,callbackCharge};char error[2048]{};
    const auto status=state_->compiled->entry(gamma,input.data(),nullptr,nullptr,nullptr,candidate.data(),nullptr,nullptr,&host,error,sizeof(error));
    if(status){if(invocation.failure)std::rethrow_exception(invocation.failure);if(state_->options.build.cancellation&&state_->options.build.cancellation->load())throw ode::OdeError("N2006","Native callback cancelled.",state_->span);throw state_->failure("N2005",error[0]?error:"Compiled callback evaluation failed.");}
    std::copy_n(candidate.data(),output.size(),output.data());
}
DerivativeResult Callback::evaluate(double gamma,const std::vector<double>& input,const DerivativeRequest& request)const{
    if(!state_||input.size()!=state_->inputs||!std::isfinite(gamma)||std::abs(gamma)>1e6||request.order>2||request.requestedThreads>64)
        throw ode::OdeError("N2001","Invalid compiled callback input or derivative request.",state_?state_->span:language::SourceSpan{});
    for(const auto value:input)if(!std::isfinite(value))throw ode::OdeError("N2001","Compiled callback inputs must be finite.",state_->span);
    auto active=request.active;if(active.empty()){active.resize(input.size());std::iota(active.begin(),active.end(),0);}
    std::set<std::size_t> unique;for(const auto index:active)if(index>=input.size()||!unique.insert(index).second)throw ode::OdeError("N2001","Derivative coordinates must be unique valid real input indices.",state_->span);
    const auto n=active.size(),m=state_->outputCount;
    const bool directional=!request.direction.empty();
    if(directional&&request.direction.size()!=n)throw ode::OdeError("N2001","Derivative direction must match selected coordinate count.",state_->span);
    for(const auto value:request.direction)if(!std::isfinite(value))throw ode::OdeError("N2001","Derivative direction must be finite.",state_->span);
    if(!std::isfinite(request.step)||request.step<0||(request.order==2&&!directional&&m*n*n>1000000))throw ode::OdeError("N2001","Derivative output/step exceeds its supported budget.",state_->span);
    DerivativeResult result;result.cacheHit=state_->hit;result.key=key();result.columns=request.order==1&&directional?1:n;
    auto invoke=[&](const std::vector<double>& point,const std::vector<double>* sx,const std::vector<double>* sy,const std::vector<unsigned char>* dependent,std::vector<double>* first,std::vector<double>* mixed){
        std::vector<double> values(m);if(first)first->resize(m);if(mixed)mixed->resize(m);
        Invocation invocation{&state_->options,{}};CallbackHost host{1,&invocation,callbackCharge};char error[2048]{};
        const auto status=state_->compiled->entry(gamma,point.data(),sx?sx->data():nullptr,sy?sy->data():nullptr,dependent?dependent->data():nullptr,values.data(),first?first->data():nullptr,mixed?mixed->data():nullptr,&host,error,sizeof(error));
        ++result.nativeEvaluations;
        if(status){if(invocation.failure)std::rethrow_exception(invocation.failure);if(state_->options.build.cancellation&&state_->options.build.cancellation->load())throw ode::OdeError("N2006","Native callback cancelled.",state_->span);throw state_->failure("N2005",error[0]?error:"Compiled callback evaluation failed.");}
        return values;
    };
    result.values=invoke(input,nullptr,nullptr,nullptr,nullptr,nullptr);
    if(request.order==0)return result;
    if(request.method==DerivativeMethod::Automatic){
        std::vector<unsigned char> dependent(input.size(),0);for(const auto index:active)dependent[index]=1;
        std::vector<double> sx(input.size(),0),sy(input.size(),0),first,mixed;
        result.jacobian.resize(m*result.columns);
        if(request.order==1&&directional){for(std::size_t j=0;j<n;++j)sx[active[j]]=request.direction[j];invoke(input,&sx,nullptr,&dependent,&first,nullptr);result.jacobian=std::move(first);return result;}
        for(std::size_t i=0;i<n;++i){sx[active[i]]=1;invoke(input,&sx,nullptr,&dependent,&first,nullptr);for(std::size_t o=0;o<m;++o)result.jacobian[o*n+i]=first[o];sx[active[i]]=0;}
        if(request.order==2){result.second.resize(m*n*(directional?1:n));
            if(directional){for(std::size_t j=0;j<n;++j)sy[active[j]]=request.direction[j];for(std::size_t i=0;i<n;++i){sx[active[i]]=1;invoke(input,&sx,&sy,&dependent,nullptr,&mixed);for(std::size_t o=0;o<m;++o)result.second[o*n+i]=mixed[o];sx[active[i]]=0;}}
            else for(std::size_t i=0;i<n;++i){sx[active[i]]=1;for(std::size_t j=i;j<n;++j){sy[active[j]]=1;invoke(input,&sx,&sy,&dependent,nullptr,&mixed);for(std::size_t o=0;o<m;++o){result.second[(o*n+i)*n+j]=mixed[o];result.second[(o*n+j)*n+i]=mixed[o];}sy[active[j]]=0;}sx[active[i]]=0;}
        }return result;
    }
    const auto defaultStep=request.order==2?std::pow(std::numeric_limits<double>::epsilon(),.25):std::cbrt(std::numeric_limits<double>::epsilon());
    const double relative=request.step>0?request.step:defaultStep;
    auto shifted=[&](const std::vector<double>& point,std::size_t coordinate,double delta){auto changed=point;changed[coordinate]+=delta;if(!std::isfinite(changed[coordinate])||changed[coordinate]==point[coordinate])throw ode::OdeError("N2001","Finite difference step cannot advance its input coordinate.",state_->span);return changed;};
    auto firstDerivatives=[&](const std::vector<double>& point){std::vector<double> jac(m*n);for(std::size_t i=0;i<n;++i){const auto coordinate=active[i];const double h=relative*std::max(1.,std::abs(point[coordinate]));const auto up=invoke(shifted(point,coordinate,h),nullptr,nullptr,nullptr,nullptr,nullptr),down=invoke(shifted(point,coordinate,-h),nullptr,nullptr,nullptr,nullptr,nullptr);for(std::size_t o=0;o<m;++o)jac[o*n+i]=(up[o]-down[o])/(2*h);}return jac;};
    result.jacobian=firstDerivatives(input);
    if(request.order==1&&directional){std::vector<double> product(m,0);for(std::size_t o=0;o<m;++o)for(std::size_t i=0;i<n;++i)product[o]+=result.jacobian[o*n+i]*request.direction[i];for(const auto value:product)if(!std::isfinite(value))throw ode::OdeError("N2005","Finite-difference directional derivative exceeds the finite real domain.",state_->span);result.jacobian=std::move(product);return result;}
    if(request.order==2){result.second.resize(m*n*(directional?1:n));
        if(directional){double length=0,pointScale=1;for(const auto value:request.direction)length=std::hypot(length,value);for(const auto coordinate:active)pointScale=std::max(pointScale,std::abs(input[coordinate]));if(length==0)return result;const double h=relative*pointScale/length;if(!std::isfinite(h)||h==0)throw ode::OdeError("N2001","Finite difference directional spacing is not representable.",state_->span);auto up=input,down=input;for(std::size_t j=0;j<n;++j){up[active[j]]+=h*request.direction[j];down[active[j]]-=h*request.direction[j];if(!std::isfinite(up[active[j]])||!std::isfinite(down[active[j]])||(request.direction[j]!=0&&(up[active[j]]==input[active[j]]||down[active[j]]==input[active[j]])))throw ode::OdeError("N2001","Finite difference directional step cannot advance its input coordinate.",state_->span);}const auto upper=firstDerivatives(up),lower=firstDerivatives(down);for(std::size_t k=0;k<m*n;++k)result.second[k]=(upper[k]-lower[k])/(2*h);}
        else for(std::size_t i=0;i<n;++i){const auto ci=active[i];const double hi=relative*std::max(1.,std::abs(input[ci]));for(std::size_t j=i;j<n;++j){const auto cj=active[j];const double hj=relative*std::max(1.,std::abs(input[cj]));std::vector<double> derivative(m);if(i==j){const auto up=invoke(shifted(input,ci,hi),nullptr,nullptr,nullptr,nullptr,nullptr),down=invoke(shifted(input,ci,-hi),nullptr,nullptr,nullptr,nullptr,nullptr);for(std::size_t o=0;o<m;++o)derivative[o]=(up[o]-2*result.values[o]+down[o])/(hi*hi);}else{const auto pp=invoke(shifted(shifted(input,ci,hi),cj,hj),nullptr,nullptr,nullptr,nullptr,nullptr),pm=invoke(shifted(shifted(input,ci,hi),cj,-hj),nullptr,nullptr,nullptr,nullptr,nullptr),mp=invoke(shifted(shifted(input,ci,-hi),cj,hj),nullptr,nullptr,nullptr,nullptr,nullptr),mm=invoke(shifted(shifted(input,ci,-hi),cj,-hj),nullptr,nullptr,nullptr,nullptr,nullptr);for(std::size_t o=0;o<m;++o)derivative[o]=(pp[o]-pm[o]-mp[o]+mm[o])/(4*hi*hj);}for(std::size_t o=0;o<m;++o){result.second[(o*n+i)*n+j]=derivative[o];result.second[(o*n+j)*n+i]=derivative[o];}}}
    }
    for(const auto value:result.jacobian)if(!std::isfinite(value))throw ode::OdeError("N2005","Finite difference derivative is not finite.",state_->span);
    for(const auto value:result.second)if(!std::isfinite(value))throw ode::OdeError("N2005","Finite difference second derivative is not finite.",state_->span);
    return result;
}
}

