#include "native_functions/native_callback.hpp"
#include "algebra/elementary.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
using namespace penta;
namespace {
int checks=0,failures=0;
void check(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool near(double a,double b,double tolerance=1e-10){return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));}
std::shared_ptr<const language::FunctionDefinition> function(const std::string& source){const auto analysis=language::analyse(source);for(const auto& diagnostic:analysis.diagnostics)if(diagnostic.severity==language::Severity::Error)std::cerr<<diagnostic.message<<'\n';if(!analysis.ok())throw std::runtime_error("Derivative fixture did not parse.");for(const auto& statement:analysis.program.statements)if(statement.function)return statement.function;throw std::runtime_error("Missing function.");}
template<class F>void rejects(F action,const std::string& label){try{action();check(false,label);}catch(const ode::OdeError&){check(true,label);}}
}
int main(){
    native_functions::Options options;options.build.cacheDirectory=std::filesystem::temp_directory_path()/("penta-native-derivative-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int compiles=0;std::size_t charged=0;options.build.compilationFinished=[&](double){++compiles;};options.chargeOperations=[&](std::size_t n){charged+=n;};
    try{
        const auto rosenbrock=function("function real objective(real x,real y,real a){real d=y-x*x;return a*d*d+(1.0-x)*(1.0-x);}");
        native_functions::validateCallback(*rosenbrock);check(!std::filesystem::exists(options.build.cacheDirectory),"Check does not compile or create cache");
        native_functions::Callback callback(*rosenbrock,options);native_functions::DerivativeRequest request;request.active={0,1};request.order=2;
        auto result=callback.evaluate(1,{1.2,1.4,100},request);check(near(result.values[0],.2),"native objective value");
        check(near(result.jacobian[0],19.6)&&near(result.jacobian[1],-8),"generated objective gradient");
        check(near(result.second[0],1170)&&near(result.second[1],-480)&&near(result.second[2],-480)&&near(result.second[3],200),"generated Hessian and symmetry");
        request.direction={.3,-.7};const auto product=callback.evaluate(1,{1.2,1.4,100},request);check(near(product.second[0],687)&&near(product.second[1],-284),"native Hessian-vector product without dense Hessian");
        request.order=1;const auto jvp=callback.evaluate(1,{1.2,1.4,100},request);check(near(jvp.jacobian[0],11.48)&&jvp.columns==1,"native gradient directional product");
        request.direction.clear();request.order=2;request.method=native_functions::DerivativeMethod::FiniteDifference;const auto fd=callback.evaluate(1,{1.2,1.4,100},request);bool agrees=true;for(std::size_t n=0;n<4;++n)agrees=agrees&&near(fd.second[n],result.second[n],2e-6);check(agrees,"Hessian agrees with independent native finite differences");
        request.direction={.3,-.7};const auto finiteProduct=callback.evaluate(1,{1.2,1.4,100},request);check(near(finiteProduct.second[0],687,2e-5)&&near(finiteProduct.second[1],-284,2e-5),"explicit finite-difference HVP orientation and scaling");request.direction.clear();
        request.order=1;request.active={2};request.method=native_functions::DerivativeMethod::Automatic;const auto parameter=callback.evaluate(1,{1.2,1.4,100},request);check(near(parameter.jacobian[0],.0016),"runtime parameter Jacobian");
        native_functions::Callback cached(*rosenbrock,options);check(cached.cacheHit()&&cached.key()==callback.key()&&compiles==1,"parameter and derivative mode changes reuse compiled structure");
        check(near(cached.evaluate(2,{1.2,1.4,200},request).jacobian[0],.0016),"cached runtime parameter changes");
        const auto system=function("function [real first,real second] residual(real x,real y){return x*x+y-3.0,x-y*y+1.0;}");
        native_functions::Callback vector(*system,options);request.active.clear();request.order=1;const auto jac=vector.evaluate(1,{2,3},request);check(jac.values==std::vector<double>({4,-6})&&jac.jacobian==std::vector<double>({4,1,1,-6}),"multiple-output native callback/Jacobian packing");
        request.direction={2,-1};const auto directional=vector.evaluate(1,{2,3},request);check(directional.jacobian==std::vector<double>({7,8}),"vector JVP");request.direction.clear();
        const algebra::Context context(1);const std::vector<double> pure{.2,.3,-.1,.4,.15};
        for(const std::string name:{"sin","cos","exp","sinh","cosh"}){
            const auto f=function("function pentarnion f(pentarnion p){return "+name+"(p);}");native_functions::Callback whole(*f,options);request.order=2;const auto ad=whole.evaluate(1,pure,request);
            algebra::Coefficients coefficients{};std::copy(pure.begin(),pure.end(),coefficients.begin());const algebra::Pentarnion p(context,coefficients);
            const auto reference=name=="sin"?algebra::sine(p):name=="cos"?algebra::cosine(p):name=="exp"?algebra::exponential(p):name=="sinh"?algebra::hyperbolicSine(p):algebra::hyperbolicCosine(p);
            bool same=true;for(std::size_t n=0;n<5;++n)same=same&&near(ad.values[n],reference.coefficients()[n]);check(same,"one-element "+name+" value agrees with published core");
            request.method=native_functions::DerivativeMethod::FiniteDifference;const auto numeric=whole.evaluate(1,pure,request);request.method=native_functions::DerivativeMethod::Automatic;bool derivatives=true;for(std::size_t n=0;n<ad.second.size();++n)derivatives=derivatives&&near(ad.second[n],numeric.second[n],4e-6);check(derivatives,"one-element "+name+" Hessian agrees with finite differences");
            const auto atReal=whole.evaluate(1,{.2,0,0,0,0},request);check(std::all_of(atReal.second.begin(),atReal.second.end(),[](double x){return std::isfinite(x);}),"one-element "+name+" derivatives are smooth at zero pure part");
        }
        const auto grouped=function("function pentarnion g(pentarnion a,pentarnion b,pentarnion c){return (a*b)*c-a*(b*c);}");native_functions::Callback algebraic(*grouped,options);const std::vector<double> args{1,.2,.3,.4,.5, 0,.5,.1,-.2,.3, .2,0,.2,.1,-.3};request.order=2;request.active={1,7};const auto grouping=algebraic.evaluate(1,args,request);request.method=native_functions::DerivativeMethod::FiniteDifference;const auto groupingFd=algebraic.evaluate(1,args,request);request.method=native_functions::DerivativeMethod::Automatic;bool preserved=false,correct=true;for(std::size_t n=0;n<grouping.values.size();++n)preserved=preserved||std::abs(grouping.values[n])>1e-6;for(std::size_t n=0;n<grouping.second.size();++n)correct=correct&&near(grouping.second[n],groupingFd.second[n],1e-6);check(preserved&&correct,"nonassociative mixed partials preserve explicit grouping");
        const auto abs=function("function real f(real x){return abs(x);}");native_functions::Callback nonsmooth(*abs,options);request.active.clear();request.order=1;rejects([&]{(void)nonsmooth.evaluate(1,{0},request);},"nondifferentiable abs derivative is diagnosed");request.method=native_functions::DerivativeMethod::FiniteDifference;check(near(nonsmooth.evaluate(1,{0},request).jacobian[0],0),"explicit finite differences do not claim analytic smoothness");request.method=native_functions::DerivativeMethod::Automatic;
        const auto branch=function("function real f(real x){if(x>0){return x*x;}return -x*x;}");native_functions::Callback branching(*branch,options);rejects([&]{(void)branching.evaluate(1,{0},request);},"state-dependent branch boundary diagnosed");
        const auto tail=function("function real f(real x){return erfc(x);}");native_functions::Callback gaussianTail(*tail,options);const auto tailResult=gaussianTail.evaluate(1,{8},request);check(tailResult.values[0]>0&&std::abs(tailResult.values[0]/std::erfc(8.)-1)<1e-14,"erfc native primal preserves a small Gaussian tail");
        const auto angle=function("function real f(real x){return acos(x);}");native_functions::Callback inverseCosine(*angle,options);const double nearOne=std::nextafter(1.,0.);const auto angleResult=inverseCosine.evaluate(1,{nearOne},request);check(std::abs(angleResult.values[0]/std::acos(nearOne)-1)<1e-14,"acos native primal avoids cancellation near one");
        const auto saturated=function("function real f(real x){return tanh(x);}");native_functions::Callback hyperbolicTangent(*saturated,options);const auto saturatedResult=hyperbolicTangent.evaluate(1,{30},request);const double tailDerivative=4*std::exp(-60.)/std::pow(1+std::exp(-60.),2);check(saturatedResult.jacobian[0]>0&&std::abs(saturatedResult.jacobian[0]/tailDerivative-1)<1e-14,"tanh derivative retains its small positive saturated tail");
        const auto scaled=function("function real f(real x){return 2.0*x;}");native_functions::Callback doubling(*scaled,options);request.method=native_functions::DerivativeMethod::FiniteDifference;request.direction={1e308};rejects([&]{(void)doubling.evaluate(1,{1},request);},"raw finite-difference JVP rejects nonfinite results");request.method=native_functions::DerivativeMethod::Automatic;request.direction.clear();
        auto cancel=options;cancel.build.cancellation=std::make_shared<std::atomic_bool>(true);native_functions::Callback cancelled(*rosenbrock,cancel);rejects([&]{(void)cancelled.evaluate(1,{1,1,100},request);},"cached native derivative observes cancellation");
        auto budget=options;budget.chargeOperations=[](std::size_t){throw std::runtime_error("derivative-budget");};native_functions::Callback limited(*rosenbrock,budget);bool propagated=false;try{(void)limited.evaluate(1,{1,1,100},request);}catch(const std::runtime_error&e){propagated=std::string(e.what())=="derivative-budget";}check(propagated,"host operation failure retains original exception identity");
        check(charged>0,"native derivative work charges operation budget");
    }catch(const std::exception&e){++failures;std::cerr<<e.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(options.build.cacheDirectory,ignored);
    std::cout<<checks<<" native derivative checks, "<<failures<<" failures\n";return failures?1:0;
}
