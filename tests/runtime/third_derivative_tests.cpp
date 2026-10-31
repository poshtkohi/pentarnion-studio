#include "native_functions/third_callback.hpp"
#include "native_functions/native_callback.hpp"
#include "runtime/thread_budget.hpp"
#include "third_reference.hpp"
#ifndef PENTA_THIRD_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
namespace {
using penta::native_functions::ThirdCallback;using penta::native_functions::ThirdRequest;
namespace reference=penta::test::third_reference;
int checks=0,failures=0;
void expect(bool yes,const std::string& message){++checks;if(!yes){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
bool near(double actual,long double expected,double tolerance=2e-12){return std::abs(static_cast<long double>(actual)-expected)<=tolerance*(1+std::abs(expected));}
std::shared_ptr<const penta::language::FunctionDefinition> definition(const std::string& source){auto analysis=penta::language::analyse(source);if(!analysis.ok()){for(const auto& diagnostic:analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';throw std::runtime_error("Third-derivative test source did not analyse.");}for(const auto& statement:analysis.program.statements)if(statement.function)return statement.function;throw std::runtime_error("Missing test function.");}
template<class F>void rejects(F action,const char* code,const char* message){bool rejected=false;try{action();}catch(const penta::ode::OdeError& e){rejected=e.code==code;}expect(rejected,message);}
void native(){
    penta::native_functions::Options options;options.build.cacheDirectory=std::filesystem::temp_directory_path()/("penta-third-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::size_t charged=0;options.chargeOperations=[&](std::size_t count){charged+=count;};
    const auto polynomial=definition("native function real polynomial(real x,real y,real gain){return gain*x*x*x*y*y+sin(x)+exp(y);}");ThirdCallback f(*polynomial,options);
    ThirdRequest request;request.active={0,1};request.u={.7,-.2};request.v={-.3,.5};request.w={1.1,.9};
    const auto result=f.evaluate(.5,{.6,-.4,2},request);const auto expected=2*reference::xCubedYSquared(.6,-.4,{.7L,-.2L},{-.3L,.5L},{1.1L,.9L})-std::cos(.6L)*.7L*-.3L*1.1L+std::exp(-.4L)*-.2L*.5L*.9L;
    expect(near(result.third[0],expected),"Mixed polynomial/sine/exponential D3 agrees with an independently differentiated expression.");expect(charged>0&&result.nativeEvaluations==1&&result.threadsUsed==1,"A trilinear derivative uses one charged native evaluation.");
    const auto key=f.key();ThirdCallback repeated(*polynomial,options);expect(repeated.cacheHit()&&repeated.key()==key,"Repeated unchanged bodies reuse the AD3 shared module.");auto changed=request;changed.u={.2,.3};const auto changedParameters=repeated.evaluate(1,{.2,.3,4},changed);expect(changedParameters.key==key,"Point, directions, gamma and fixed parameter values do not change AD3 structural cache identity.");
    auto symmetric=request;std::swap(symmetric.u,symmetric.w);expect(near(f.evaluate(.5,{.6,-.4,2},symmetric).third[0],expected),"The real-coordinate trilinear form is symmetric in its directions.");
    penta::native_functions::Callback old(*polynomial,options);expect(old.key()!=key,"AD3 has a separate versioned cache from existing AD1/AD2.");penta::native_functions::DerivativeRequest ad2;ad2.active={0,1};ad2.order=2;ad2.direction=request.v;
    const double spacing=2e-5;std::vector<double> plus{.6,-.4,2},minus=plus;for(unsigned n=0;n<2;++n){plus[n]+=spacing*request.w[n];minus[n]-=spacing*request.w[n];}const auto hp=old.evaluate(.5,plus,ad2),hm=old.evaluate(.5,minus,ad2);double difference=0;for(unsigned n=0;n<2;++n)difference+=request.u[n]*(hp.second[n]-hm.second[n])/(2*spacing);expect(near(result.third[0],difference,2e-8),"Independent central differentiation of the old analytic Hessian-vector product validates D3.");
    const char* names[]{"sin","cos","exp","expm1","log","log1p","log10","log2","sqrt","sinh","cosh","tanh","tan","asin","acos","atan","asinh","atanh","erf","erfc","abs","floor","ceil","round"};
    auto intrinsicSource=[&](unsigned first,unsigned last){std::string text="native function [";for(unsigned n=first;n<last;++n){if(n!=first)text+=",";text+="real output"+std::to_string(n);}text+="] smooth"+std::to_string(first)+"(real x){return ";for(unsigned n=first;n<last;++n){if(n!=first)text+=",";text+=std::string(names[n])+"(x)";}return text+";}";};
    const auto intrinsic=definition(intrinsicSource(0,12));ThirdCallback all(*intrinsic,options);const auto moreIntrinsic=definition(intrinsicSource(12,24));ThirdCallback more(*moreIntrinsic,options);ThirdRequest scalar;scalar.u={.7};scalar.v={-.3};scalar.w={1.1};
    const auto intrinsicResult=all.evaluate(1,{.3},scalar),moreResult=more.evaluate(1,{.3},scalar);for(unsigned n=0;n<24;++n)expect(near(n<12?intrinsicResult.third[n]:moreResult.third[n-12],reference::intrinsic(names[n],.3L)*.7L*-.3L*1.1L),std::string("Generated intrinsic third derivative: ")+names[n]);
    const auto twoInput=definition("native function [real angle,real length,real inverse_hyperbolic] joint(real x,real y){return atan2(y,x),hypot(x,y),acosh(y+2);}");ThirdCallback joint(*twoInput,options);
    ThirdRequest two;two.u={.7,-.2};two.v={-.3,.5};two.w={1.1,.9};for(const double x:{.4,1e-8,1e-100,0.}){const auto actual=joint.evaluate(1,{x,1},two);expect(near(actual.third[0],reference::atan2ThirdXY({x,1},{.7L,-.2L},{-.3L,.5L},{1.1L,.9L})),"atan2 uses a stable pivot near the vertical axis.");expect(near(actual.third[1],reference::hypotThird({x,1},{.7L,-.2L},{-.3L,.5L},{1.1L,.9L})),"hypot trilinear derivative agrees with radial geometry.");expect(near(actual.third[2],reference::intrinsic("acosh",3)*-.2L*.5L*.9L),"acosh third derivative is analytic.");}
    const auto whole=definition("native function [pentarnion a,pentarnion b,pentarnion c,pentarnion d,pentarnion e] whole(pentarnion p){return sin(p),cos(p),exp(p),sinh(p),cosh(p);}");ThirdCallback pf(*whole,options);ThirdRequest pd;pd.u={.3,.7,-.4,.2,-.1};pd.v={-.2,.5,.8,-.3,.4};pd.w={.6,-.1,.2,.9,-.7};
    const reference::Coefficients pu{.3L,.7L,-.4L,.2L,-.1L},pv{-.2L,.5L,.8L,-.3L,.4L},pw{.6L,-.1L,.2L,.9L,-.7L};const char* wholeNames[]{"sin","cos","exp","sinh","cosh"};
    for(const double gamma:{0.,.5,1.}){const auto actual=pf.evaluate(gamma,{.2,0,0,0,0},pd);for(unsigned op=0;op<5;++op){const auto exact=reference::wholeAtReal(wholeNames[op],.2L,pu,pv,pw);for(unsigned c=0;c<5;++c)expect(near(actual.third[5*op+c],exact[c]),std::string("Whole Pentarnion third tensor at its smooth real axis: ")+wholeNames[op]);}}
    ThirdRequest pure;pure.u=pure.v=pure.w={0,1,0,0,0};const auto exponentialAtZero=pf.evaluate(1,{0,0,0,0,0},pure);expect(near(exponentialAtZero.third[11],-1),"Whole exp has D3exp(0)[i,i,i]=-i; the radial origin is not discarded.");
    const auto groups=definition("native function [pentarnion left,pentarnion right] grouped(pentarnion a,pentarnion b,pentarnion c){return (a*b)*c,a*(b*c);}");ThirdCallback grouped(*groups,options);ThirdRequest g;g.u.assign(15,0);g.v=g.w=g.u;g.u[1]=1;g.v[7]=1;g.w[14]=1;
    for(const double gamma:{0.,.5,1.}){const auto actual=grouped.evaluate(gamma,std::vector<double>(15,.1),g);const auto left=reference::orderedBasisThird(gamma,false),right=reference::orderedBasisThird(gamma,true);for(unsigned c=0;c<5;++c){expect(near(actual.third[c],left[c]),"Left-grouped mixed Pentarnion product retains its published multiplication table.");expect(near(actual.third[5+c],right[c]),"Right-grouped mixed Pentarnion product retains nonassociativity.");}}
    auto bad=request;bad.u.pop_back();rejects([&]{(void)f.evaluate(1,{.6,-.4,2},bad);},"N2301","Mismatched directions are rejected.");bad=request;bad.active={0,0};rejects([&]{(void)f.evaluate(1,{.6,-.4,2},bad);},"N2301","Repeated coordinate selections are rejected.");bad=request;bad.u[0]=std::numeric_limits<double>::infinity();rejects([&]{(void)f.evaluate(1,{.6,-.4,2},bad);},"N2301","Nonfinite directions are rejected.");rejects([&]{(void)f.evaluate(1,{std::numeric_limits<double>::quiet_NaN(),-.4,2},request);},"N2301","Nonfinite points are rejected.");
    rejects([&]{(void)joint.evaluate(1,{0,0},two);},"N2305","Nonsmooth angle/norm origin cannot be silently differentiated.");rejects([&]{(void)all.evaluate(1,{0},scalar);},"N2305","Logarithmic and absolute-value domain boundaries fail explicitly.");
    const auto branching=definition("native function real branch(real x){if(x>0){return x*x*x;}else{return -x*x*x;}}");ThirdCallback branch(*branching,options);expect(near(branch.evaluate(1,{2},scalar).third[0],6*.7L*-.3L*1.1L),"Smooth branch interior supports third derivatives.");rejects([&]{(void)branch.evaluate(1,{0},scalar);},"N2305","State-dependent branch boundaries fail conservatively.");
    auto cancelledOptions=options;cancelledOptions.build.cancellation=std::make_shared<std::atomic_bool>(true);ThirdCallback cancelled(*polynomial,cancelledOptions);rejects([&]{(void)cancelled.evaluate(1,{.6,-.4,2},request);},"N2306","Cancellation is checked even on a loaded module.");auto quotaOptions=options;quotaOptions.chargeOperations=[](std::size_t){throw penta::ode::OdeError("TEST_QUOTA","Budget exhausted.");};ThirdCallback quota(*polynomial,quotaOptions);rejects([&]{(void)quota.evaluate(1,{.6,-.4,2},request);},"TEST_QUOTA","Original host quota errors cross the generated boundary intact.");
    auto concurrentOptions=options;concurrentOptions.chargeOperations={};ThirdCallback concurrent(*polynomial,concurrentOptions);auto invoke=[&]{penta::native_functions::ThirdResult output;penta::runtime::ThreadBudget::instance().withReservation([&]{output=concurrent.evaluate(.5,{.6,-.4,2},request);});return output.third;};auto first=std::async(std::launch::async,invoke),second=std::async(std::launch::async,invoke);expect(first.get()==second.get(),"Concurrent callers use local seeds/output buffers and the shared CPU reservation.");
    const auto shifted=definition("\n\n native function real polynomial(real x, real y, real gain) { % layout only\nreturn gain*x*x*x*y*y + sin(x) + exp(y); }");expect(penta::native_functions::thirdFingerprint(*shifted)==penta::native_functions::thirdFingerprint(*polynomial),"Formatting/source locations do not change AD3 structural fingerprints.");
    auto prototype=*polynomial;prototype.body.clear();rejects([&]{penta::native_functions::validateThirdCallback(prototype);},"N2301","Bodyless prototypes fail before native preparation.");
    rejects([&]{ThirdCallback invalid(prototype,options);},"N2301","A bodyless constructor never compiles a throwing stub.");
    auto declarations=*definition("native function real declarations(real x){real local_value=x;return local_value;}");const auto local=declarations.body.front(),returned=declarations.body.back();declarations.body.clear();for(unsigned n=0;n<257;++n){auto statement=local;statement.name="local"+std::to_string(n);declarations.body.push_back(statement);}declarations.body.push_back(returned);rejects([&]{penta::native_functions::validateThirdCallback(declarations);},"N1001","AD3 has a bounded local-jet declaration budget.");
    rejects([&]{(void)f.evaluate(-.1,{.6,-.4,2},request);},"N2301","AD3 gamma validation matches the immutable A_gamma_v1 context.");
}
#ifndef PENTA_THIRD_NATIVE_ONLY
void language(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-third-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));options.odeCacheDirectory=options.dataDirectory/"odes";std::filesystem::create_directories(options.dataDirectory);
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);expect(result.ok()==good,source);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}return result;};
    const std::string source="native function real cubic(real x,real gain){return gain*x*x*x;}record d=thirdderivative(@cubic,2,1,2,3,options(parameters=options(gain=4)));assert(abs(d.third(1)-144)<1e-10);assert(d.values(1)==32);assert(d.method==\"analytic_hyperdual3\");assert(d.native_evaluations==1);";run(source);const auto cached=run(source);if(cached.ok())expect(cached.nativeCompilation.compilerAttempts==0,"Repeated language requests reuse compiled third-derivative modules.");
    run("native function pentarnion f(pentarnion p){return exp(p);}record d=thirdderivative(@f,0*i,i,i,i);assert(abs(d.third(2)+1)<1e-12);assert(d.output_coordinates==5);record realaxis=thirdderivative(@f,0*i,1,1,1);assert(abs(realaxis.third(1)-1)<1e-12);");
    run("native function real f(real x,real y){return x*x*y;}record d=thirdderivative(@f,[2,3],[1,0],[1,0],[0,1]);assert(abs(d.third(1)-2)<1e-12);");
    const std::string batch="native function real first_third_body(real x){return exp(x)+x*x*x;}native function real second_third_body(real x){return sin(x)+x*x*x*x;}record a=thirdderivative(@first_third_body,0.2,1,1,1);record b=thirdderivative(@second_third_body,0.4,1,1,1);assert(a.cache_key!=b.cache_key);assert(abs(a.third(1)-exp(0.2)-6)<1e-10);assert(abs(b.third(1)+cos(0.4)-24*0.4)<1e-10);";
    const auto prepared=run(batch);if(prepared.ok())expect(prepared.nativeCompilation.translationUnits>=2&&prepared.nativeCompilation.compilerAttempts>=2,"Two explicit AD3 bodies participate in the bounded parallel compiler batch.");
    const auto reused=run(batch);if(reused.ok())expect(reused.nativeCompilation.compilerAttempts==0,"Repeated multiple AD3 callbacks perform no new compiler attempts.");
    run("native function real f(real x){return x*x*x;}record d=thirdderivative(@f,1,[1,2],1,1);",false);
    run("native function real f(real x){return x*x*x;}record d=thirdderivative(@f,1,1,1,1,options(method=\"finite_difference\"));",false);
    penta::language::AnalysisOptions old;old.languageVersion="0.1";expect(!penta::language::analyse(source,old).ok(),"Third-derivative extension does not change language0.1.");
}
#endif
}
int main(){try{native();
#ifndef PENTA_THIRD_NATIVE_ONLY
language();
#endif
}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}std::cout<<checks<<" native third-derivative checks, "<<failures<<" failures\n";return failures?1:0;}
