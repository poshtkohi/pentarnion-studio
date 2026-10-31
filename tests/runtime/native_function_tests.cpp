#include "native_functions/native_function.hpp"
#include "algebra/elementary.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace penta;
namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
std::shared_ptr<const language::FunctionDefinition> function(const std::string& source){const auto result=language::analyse(source);
    if(!result.ok()){for(const auto& d:result.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';throw std::runtime_error("Native fixture did not analyse.");}
    for(const auto& statement:result.program.statements)if(statement.function)return statement.function;
    throw std::runtime_error("Missing native fixture function.");}
native_functions::Value real(double value){native_functions::Value result;result.coefficients[0]=value;return result;}
native_functions::Value pent(const algebra::Pentarnion& value){native_functions::Value result;result.type=language::Type::Pentarnion;result.coefficients=value.coefficients();return result;}
bool near(const std::array<double,5>& a,const algebra::Pentarnion& b){for(std::size_t n=0;n<5;++n)if(std::abs(a[n]-b.coefficients()[n])>1e-12*(1+std::abs(b.coefficients()[n])))return false;return true;}
}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path(u8"penta-native-function-Δ")/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    native_functions::Options options;options.build.cacheDirectory=directory;int compiles=0;double compileSeconds=0;std::size_t operations=0;
    options.build.compilationFinished=[&](double seconds){++compiles;compileSeconds+=seconds;};options.chargeOperations=[&](std::size_t count){operations+=count;};
    try {
        auto cubic=function("native function real cubic(real x){real y=x*x;if(x>=0){return y*x+1.0;}else{return -y*x+1.0;}}");
        native_functions::validate(*cubic);expect(!std::filesystem::exists(directory),"validation never creates a native cache");
        auto first=native_functions::evaluate(*cubic,1,{real(2)},options);expect(first.value.coefficients[0]==9,"native locals/branches compute real result");expect(!first.cacheHit&&compiles==1,"first native invocation compiles once");
        auto second=native_functions::evaluate(*cubic,2,{real(-3)},options);expect(second.value.coefficients[0]==28,"cached function uses new arguments");expect(second.cacheHit&&second.key==first.key&&compiles==1,"arguments/gamma changes reuse structure");expect(operations>0&&compileSeconds>0,"native operations and compilation time reported separately");
        const algebra::Context context(1);const auto p=algebra::add(algebra::Pentarnion::scalar(context,1),algebra::Pentarnion::basis(context,1));
        const auto q=algebra::add(algebra::Pentarnion::basis(context,2),algebra::Pentarnion::basis(context,4));const auto r=algebra::Pentarnion::basis(context,3);
        auto grouping=function("native function pentarnion grouping(pentarnion a,pentarnion b,pentarnion c){return (a*b)*c-a*(b*c);}");
        auto grouped=native_functions::evaluate(*grouping,1,{pent(p),pent(q),pent(r)},options);
        const auto expected=algebra::associator(p,q,r);expect(near(grouped.value.coefficients,expected),"native expression preserves nonassociative grouping");
        for(const auto& name:{"sin","cos","exp","sinh","cosh"}){
            auto elementary=function(std::string("native function pentarnion f(pentarnion p){return ")+name+"(p);}");
            auto actual=native_functions::evaluate(*elementary,1,{pent(p)},options);
            const auto reference=std::string(name)=="sin"?algebra::sine(p):std::string(name)=="cos"?algebra::cosine(p):std::string(name)=="exp"?algebra::exponential(p):std::string(name)=="sinh"?algebra::hyperbolicSine(p):algebra::hyperbolicCosine(p);
            expect(near(actual.value.coefficients,reference),"native whole-Pentarnion elementary function agrees with paper-based core");
        }
        auto inverse=function("native function pentarnion inverse_power(pentarnion p){return p^(-1);}");
        expect(near(native_functions::evaluate(*inverse,1,{pent(p)},options).value.coefficients,algebra::inverse(p)),"native negative power uses published scalar inverse");
        auto changed=function("native function real cubic(real x){return x*x*x+2.0;}");auto newValue=native_functions::evaluate(*changed,1,{real(2)},options);
        expect(newValue.value.coefficients[0]==10&&newValue.key!=first.key,"structural function edits produce a new kernel");
        auto limited=options;limited.chargeOperations=[](std::size_t){throw std::runtime_error("operation-budget-probe");};
        bool budget=false;try{(void)native_functions::evaluate(*cubic,1,{real(2)},limited);}catch(const std::runtime_error& e){budget=std::string(e.what())=="operation-budget-probe";}
        expect(budget,"native C ABI returns host operation failures without losing their identity");
        auto cancelled=options;cancelled.build.cancellation=std::make_shared<std::atomic_bool>(true);bool stopped=false;
        try{(void)native_functions::evaluate(*cubic,1,{real(2)},cancelled);}catch(const ode::OdeError&){stopped=true;}expect(stopped,"cached native functions observe cancellation");
    }catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
