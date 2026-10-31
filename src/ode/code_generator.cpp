#include "native_internal.hpp"
#include "ode/native_build_config.hpp"
#include "native_functions/universal_codegen.hpp"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>

namespace penta::ode::detail {
namespace {
using language::Type;using language::Expr;using language::ExprKind;
bool scalar(Type t){return t==Type::Real||t==Type::Integer;}
std::size_t width(Type t){if(t==Type::Real)return 1;if(t==Type::Pentarnion)return 5;throw OdeError("O1001","ODE variables must have real or pentarnion type.");}
struct Binding {Type type;std::string code;};
class Generator {
    const language::OdeDefinition& definition;
    std::map<std::string,Binding> bindings;
    std::size_t nodes=0;
public:
    Generated result;
    explicit Generator(const language::OdeDefinition&d):definition(d){}
    [[noreturn]] void invalid(const Expr&e,const std::string&m){throw OdeError("O1001",m,e.span);}
    std::string promoted(const Expr&e,std::string code){return e.type==Type::Pentarnion?code:"lift<S>("+code+")";}
    unsigned literal(const language::ExprPtr&e,unsigned maximum){
        unsigned n=0;if(!e||e->kind!=ExprKind::IntegerLiteral)throw OdeError("O1001","Native ODE power/coeff needs a nonnegative integer literal.",e?e->span:language::SourceSpan{});
        const auto parsed=std::from_chars(e->text.data(),e->text.data()+e->text.size(),n);
        if(parsed.ec!=std::errc{}||parsed.ptr!=e->text.data()+e->text.size()||n>maximum)invalid(*e,"Native ODE literal index/exponent is outside its supported range.");
        return n;
    }
    std::string expression(const language::ExprPtr&ptr,std::size_t depth=0){
        if(!ptr)throw OdeError("O1001","Missing ODE derivative expression.");
        const Expr&e=*ptr;
        if(++nodes>20000||depth>128)invalid(e,"Native ODE expression exceeds the node/depth limit.");
        if(!scalar(e.type)&&e.type!=Type::Pentarnion)invalid(e,"Unsupported native ODE expression type.");
        result.normalized+=std::to_string(static_cast<int>(e.kind))+":"+std::to_string(static_cast<int>(e.type))+":"+std::to_string(e.text.size())+":"+e.text+"[";
        auto child=[&](std::size_t i){if(i>=e.children.size())invalid(e,"Malformed ODE expression.");return expression(e.children[i],depth+1);};
        auto arity=[&](std::size_t n){if(e.children.size()!=n)invalid(e,"Malformed ODE expression arity.");};
        std::string code;
        switch(e.kind){
        case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:{
            arity(0);double number=0;const auto parsed=std::from_chars(e.text.data(),e.text.data()+e.text.size(),number,std::chars_format::general);
            if(parsed.ec!=std::errc{}||parsed.ptr!=e.text.data()+e.text.size()||!std::isfinite(number))invalid(e,"ODE numeric literal must be finite binary64.");
            std::ostringstream out;out.imbue(std::locale::classic());out<<std::hexfloat<<number;code="S("+out.str()+"L)";break;
        }
        case ExprKind::Reference:{
            arity(0);const auto it=bindings.find(e.text);
            if(it!=bindings.end()){if(it->second.type!=e.type)invalid(e,"ODE reference type does not match its declaration.");code=it->second.code;}
            else {const std::map<std::string,unsigned> bases{{"i",1},{"j",2},{"k",3},{"ell",4}};const auto b=bases.find(e.text);
                if(b==bases.end()||e.type!=Type::Pentarnion)invalid(e,"ODE references must be declared states, parameters or time.");
                code="basis<S>("+std::to_string(b->second)+")";}
            break;
        }
        case ExprKind::Group:arity(1);code="("+child(0)+")";break;
        case ExprKind::Unary:arity(1);if(e.text!="+"&&e.text!="-")invalid(e,"Unsupported native ODE unary operator.");code="("+(e.text=="-"?std::string("-"):std::string{})+child(0)+")";break;
        case ExprKind::Binary:{
            arity(2);const std::string a=child(0),b=child(1);
            const auto at=e.children[0]->type,bt=e.children[1]->type;
            if(e.text=="+"||e.text=="-")code="("+(e.type==Type::Pentarnion?promoted(*e.children[0],a):a)+e.text+(e.type==Type::Pentarnion?promoted(*e.children[1],b):b)+")";
            else if(e.text=="*")code=at==Type::Pentarnion&&bt==Type::Pentarnion?"product("+a+","+b+",gamma)":"("+a+"*"+b+")";
            else if(e.text=="/"){if(!scalar(bt))invalid(e,"Native ODE division requires a real scalar denominator.");code="divide("+a+","+b+")";}
            else if(e.text=="^"){const auto exponent=literal(e.children[1],64);code=at==Type::Pentarnion?"power("+a+","+std::to_string(exponent)+",gamma)":"integerPower("+a+","+std::to_string(exponent)+")";}
            else invalid(e,"Unsupported native ODE binary operator.");
            break;
        }
        case ExprKind::PentConstructor:case ExprKind::Call:{
            const std::string name=e.kind==ExprKind::PentConstructor?"pent":e.text;
            if(name=="pent"){
                arity(5);code="pent<S>(";for(unsigned i=0;i<5;++i){if(!scalar(e.children[i]->type))invalid(e,"pent constructor coefficients must be real.");if(i)code+=",";code+=child(i);}code+=")";
            }else if(name=="sin"||name=="cos"||name=="tan"||name=="exp"||name=="log"||name=="sqrt"||name=="sinh"||name=="cosh"||name=="tanh"){
                arity(1);if(!scalar(e.children[0]->type))invalid(e,"Native ODE elementary functions currently require real arguments.");code="math_"+name+"("+child(0)+")";
            }else if(name=="conj"||name=="scalar"||name=="pure"||name=="norm2"){
                arity(1);const auto a=promoted(*e.children[0],child(0));
                code=name=="scalar"?"("+a+").c[0]":(name=="conj"?"conjugate":name)+"("+a+")";
            }else if(name=="coeff"){
                arity(2);code="("+promoted(*e.children[0],child(0))+").c["+std::to_string(literal(e.children[1],4))+"]";
                // Record the literal in the normalized tree too.
                (void)child(1);
            }else if(name=="commutator"||name=="associator"){
                arity(name=="commutator"?2:3);code=name+"(";
                for(std::size_t i=0;i<e.children.size();++i){if(i)code+=",";code+=promoted(*e.children[i],child(i));}code+=",gamma)";
            }else invalid(e,"Unsupported function in native ODE: "+name+".");
            break;
        }
        default:invalid(e,"Unsupported expression in native ODE.");
        }
        result.normalized+="]";return code;
    }
    Generated run(){
        if(definition.states.empty()||definition.states.size()>64||definition.parameters.size()>128||definition.derivatives.size()!=definition.states.size())throw OdeError("O1001","ODE must have 1..64 states and exactly one derivative per state.",definition.span);
        auto add=[&](const language::OdeVariable&v,const std::string&code){if(v.name.empty()||bindings.contains(v.name)||v.name=="i"||v.name=="j"||v.name=="k"||v.name=="ell")throw OdeError("O1001","Duplicate or reserved ODE variable name.",v.span);bindings.emplace(v.name,Binding{v.type,code});};
        if(definition.timeName.empty())throw OdeError("O1001","ODE requires an explicit time variable.",definition.span);
        bindings.emplace(definition.timeName,Binding{Type::Real,"S(time)"});
        std::string setup;
        auto variables=[&](const std::vector<language::OdeVariable>&vars,bool state){
            std::size_t offset=0,index=0;
            for(const auto&v:vars){const auto n=width(v.type);const std::string name=(state?"s":"p")+std::to_string(index++);add(v,name);
                if(n==1)setup+="const S "+name+"="+(state?"y":"parameters")+"["+std::to_string(offset)+"];\n";
                else {setup+="const Pent<S> "+name+"=pent<S>(";for(std::size_t c=0;c<5;++c){if(c)setup+=",";setup+=(state?"y":"parameters")+std::string("[")+std::to_string(offset+c)+"]";}setup+=");\n";}
                result.normalized+=(state?"state:":"parameter:")+std::to_string(v.name.size())+":"+v.name+":"+std::to_string(static_cast<int>(v.type))+";";offset+=n;
            }return offset;
        };
        result.coordinates=variables(definition.states,true);result.parameters=variables(definition.parameters,false);
        if(result.coordinates>64||result.parameters>128)throw OdeError("O1001","Native ODE supports at most 64 state and 128 parameter coordinates.",definition.span);
        std::string body=setup;std::size_t offset=0;
        for(std::size_t i=0;i<definition.derivatives.size();++i){const auto&e=definition.derivatives[i];std::string code=expression(e);
            const auto n=width(definition.states[i].type);
            if(n==1){if(!scalar(e->type))invalid(*e,"Real-state derivative must be real.");body+="out["+std::to_string(offset)+"]="+code+";\n";}
            else{code=promoted(*e,code);body+="const auto derivative"+std::to_string(i)+"="+code+";\n";for(std::size_t c=0;c<5;++c)body+="out["+std::to_string(offset+c)+"]=derivative"+std::to_string(i)+".c["+std::to_string(c)+"];\n";}
            offset+=n;
        }
        const std::string header(embeddedNativeSupport);
        if(header.empty()||header.size()>1024*1024)throw OdeError("O1002","Invalid embedded native ODE support source.");
        const std::string dimension=std::to_string(result.coordinates),parameters=std::to_string(result.parameters);
        result.source=header+"\nusing namespace penta_generated;\nconstexpr std::size_t N="+dimension+", P="+parameters+";\n"
            "template<class S>void model(double time,double gamma,const S*y,const S*parameters,S*out){\n"+body+"}\n"
            "#ifdef _WIN32\n#define MODEL_EXPORT extern \"C\" __declspec(dllexport)\n#else\n#define MODEL_EXPORT extern \"C\" __attribute__((visibility(\"default\")))\n#endif\n"
            "MODEL_EXPORT unsigned penta_model_abi(){return 1;}\nMODEL_EXPORT unsigned penta_model_coordinates(){return N;}\nMODEL_EXPORT unsigned penta_model_parameters(){return P;}\n"
            "MODEL_EXPORT int penta_model_evaluate(double t,double g,const double*y,const double*p,double*out,double*jac,char*error,std::size_t capacity){try{"
            "if(jac){std::array<Dual<N>,N>a{},r{};std::array<Dual<N>,P>b{};for(std::size_t i=0;i<N;++i){a[i]=Dual<N>(y[i]);a[i].d[i]=1;a[i].stateDependent=true;}for(std::size_t i=0;i<P;++i)b[i]=Dual<N>(p[i]);model(t,g,a.data(),b.data(),r.data());for(std::size_t i=0;i<N;++i){out[i]=binary64(r[i].v);for(std::size_t j=0;j<N;++j)jac[i*N+j]=binary64(r[i].d[j]);}}"
            "else{std::array<long double,N>a{},r{};std::array<long double,P>b{};for(std::size_t i=0;i<N;++i)a[i]=y[i];for(std::size_t i=0;i<P;++i)b[i]=p[i];model(t,g,a.data(),b.data(),r.data());for(std::size_t i=0;i<N;++i)out[i]=binary64(r[i]);}return 0;}catch(const std::exception&e){if(error&&capacity){std::strncpy(error,e.what(),capacity-1);error[capacity-1]=0;}return -1;}catch(...){if(error&&capacity){std::strncpy(error,\"Unknown native ODE evaluation error.\",capacity-1);error[capacity-1]=0;}return -1;}}\n";
        return std::move(result);
    }
};
}
Generated generate(const language::OdeDefinition&d){if(native_functions::usesUniversalCoordinates(d))return native_functions::detail::generateUniversalOde(d);return Generator(d).run();}
} // namespace penta::ode::detail
