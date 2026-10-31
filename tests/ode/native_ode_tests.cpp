#include "ode/native_ode.hpp"
#include "ode/native_internal.hpp"
#include "algebra/pentarnion.hpp"
#include "../algebra/basis_fixtures.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <iomanip>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#undef near
#endif

using namespace penta;
namespace {
int checks=0;
void require(bool yes,const std::string&message){++checks;if(!yes)throw std::runtime_error(message);}
void near(double a,double b,double tolerance,const std::string&message){require(std::abs(a-b)<=tolerance*(1+std::abs(b)),message+": "+std::to_string(a)+" vs "+std::to_string(b));}
template<class F>void fails(F f,const std::string&code){try{f();}catch(const ode::OdeError&e){require(e.code==code,"Expected "+code+", got "+e.code+": "+e.what());return;}throw std::runtime_error("Expected ODE failure "+code);}
language::OdeDefinition definition(const std::string&source){const auto result=language::analyse(source);if(!result.ok()){std::string message="ODE fixture failed analysis:";for(const auto&d:result.diagnostics)message+=" "+d.code+" "+d.message;throw std::runtime_error(message);}require(!result.program.statements.empty()&&result.program.statements[0].ode,"ODE fixture has typed definition");return *result.program.statements[0].ode;}
std::vector<double> coefficients(const algebra::Pentarnion&p){return {p.coefficients().begin(),p.coefficients().end()};}

void hashContract(){require(ode::detail::sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA256 empty known vector");require(ode::detail::sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 abc known vector");}

void analyticAndCache(const ode::NativeOptions&native){
    const auto model=definition("ode decay(t,real x;real a){x'=a*x;}");
    ode::SolveOptions options;options.t1=2;options.sampleStep=.1;options.rtol=1e-10;options.atol=1e-12;
    auto first=ode::solve(model,1,{2},{-.7},options,native);
    require(!first->stats.cacheHit,"First structural model is compiled");
    require(first->times.front()==0&&first->times.back()==2,"Exact interval endpoints");
    require(first->coordinateCount()==1&&first->stateNames[0]=="x","Solution coordinate schema");
    for(std::size_t i=0;i<first->times.size();++i)near(first->values[i],2*std::exp(-.7*first->times[i]),2e-8,"Adams exponential reference");
    options.method=ode::Method::Bdf;options.jacobian="finite_difference";options.rtol=1e-8;options.t0=1;options.t1=3;options.sampleStep=.17;
    auto second=ode::solve(model,0,{3},{-.2},options,native);
    require(second->stats.cacheHit&&second->stats.key==first->stats.key,"Values/gamma/times/tolerances/method/Jacobian selection excluded from key");
    for(std::size_t i=0;i<second->times.size();++i)near(second->values[i],3*std::exp(-.2*(second->times[i]-1)),2e-6,"BDF finite-difference exponential reference");
    const auto evaluated=ode::evaluateNative(model,.25,7,{4},{-2},native);near(evaluated.rhs[0],-8,1e-15,"Compiled RHS evaluation");near(evaluated.jacobian[0],-2,1e-15,"Compiled automatic Jacobian");
    const auto saturated=definition("ode saturated(t,real x){x'=tanh(x);}");
    for(double state:{-30.,30.}){const auto tail=ode::evaluateNative(saturated,1,0,{state},{},native);const double e=std::exp(-2*std::abs(state)),oracle=4*e/((1+e)*(1+e));require(tail.jacobian[0]>0&&std::abs(tail.jacobian[0]/oracle-1)<1e-14,"ODE tanh Jacobian preserves the independent saturated sech-squared tail");}
    options.jacobian="analytic";auto third=ode::solve(model,1,{1},{-2},options,native);require(third->stats.cacheHit&&third->stats.jacobian=="analytic"&&third->stats.jacobianEvaluations>0,"Analytic dense CVODE Jacobian is used");
}

void coupledAndSampling(const ode::NativeOptions&native){
    const auto model=definition("ode harmonic(t,real x,real y){x'=y;y'=-x;}");
    ode::SolveOptions options;options.t1=6.283185307179586;options.sampleStep=.07;options.rtol=1e-10;options.atol=1e-12;
    for(auto method:{ode::Method::Adams,ode::Method::Bdf,ode::Method::Erk,ode::Method::Esdirk}){
        options.method=method;auto solution=ode::solve(model,.5,{1,0},{},options,native);
        require(solution->stateWidths==std::vector<std::size_t>({1,1}),"Multiple state layout");
        for(std::size_t i=0;i<solution->times.size();++i){near(solution->values[i*2],std::cos(solution->times[i]),5e-8,"Harmonic x analytic reference");near(solution->values[i*2+1],-std::sin(solution->times[i]),5e-8,"Harmonic y analytic reference");}
    }
    const auto mixed=definition("ode mixed(t,real x,pentarnion p;real a){p'=a*p;x'=scalar(p)+sin(t);}");
    options.t1=1;options.sampleStep=.1;
    auto result=ode::solve(mixed,.5,{0,1,2,3,4,5},{0},options,native);
    require(result->coordinateCount()==6&&result->stateWidths==std::vector<std::size_t>({1,5}),"Mixed state packing");
    near(result->values[result->values.size()-6],2-std::cos(1.0),2e-8,"Mixed real integral");
    for(unsigned i=0;i<5;++i)near(result->values[result->values.size()-5+i],i+1,1e-13,"All five Pentarnion coordinates retained");
    options.t0=1;options.t1=2;options.sampleStep=1.0/3;options.maxSamples=4;
    result=ode::solve(model,1,{1,0},{},options,native);
    require(result->times.size()==4&&result->times.back()==2,"Rounded endpoint appears exactly once within exact sample budget");
    for(std::size_t i=1;i<result->times.size();++i)require(result->times[i]>result->times[i-1],"Sample times are strictly increasing");
}

void arkodeMethods(const ode::NativeOptions&native){
    const auto decay=definition("ode arkdecay(t,real x;real rate){x'=rate*x;}");
    ode::SolveOptions options;options.t0=.25;options.t1=1.37;options.sampleStep=.03;options.rtol=1e-10;options.atol=1e-12;
    std::string key;
    for(auto method:{ode::Method::Erk,ode::Method::Esdirk})for(const auto* mode:{"analytic","finite_difference"}){
        options.method=method;options.jacobian=mode;
        const auto solution=ode::solve(decay,.25,{2},{-.7},options,native);
        if(key.empty()){key=solution->stats.key;require(!solution->stats.cacheHit,"First ARKODE kernel compiles");}
        else require(solution->stats.cacheHit&&solution->stats.key==key,"ARKODE table/Jacobian mode reuses identical native kernel");
        require(solution->times.front()==options.t0&&solution->times.back()==options.t1,"ARKODE exact non-grid endpoints");
        require(solution->stats.steps>0&&solution->stats.rhsEvaluations>0,"ARKODE exposes actual solver work");
        require(solution->stats.method==(method==ode::Method::Erk?"erk":"esdirk"),"ARKODE records selected method");
        if(method==ode::Method::Erk)require(solution->stats.jacobian=="not_used"&&solution->stats.jacobianEvaluations==0,"Explicit RK does not construct/evaluate a Jacobian");
        else require(solution->stats.jacobian==mode&&solution->stats.jacobianEvaluations>0,"ESDIRK applies selected analytic or difference-quotient Jacobian");
        for(std::size_t i=0;i<solution->times.size();++i)near(solution->values[i],2*std::exp(-.7*(solution->times[i]-.25)),5e-8,"ARKODE exponential exact reference");
    }
    const auto timed=definition("ode arktime(t,real x){x'=cos(t);}");
    const auto pent=definition("ode arkpent(t,pentarnion p;real rate){p'=rate*p;}");
    for(auto method:{ode::Method::Erk,ode::Method::Esdirk}){
        options.method=method;options.jacobian="analytic";
        const auto solution=ode::solve(timed,1,{.3},{},options,native);
        double maximumError=0;std::size_t worst=0;for(std::size_t i=0;i<solution->times.size();++i){const double error=std::abs(solution->values[i]-(.3+std::sin(solution->times[i])-std::sin(.25)));if(error>maximumError){maximumError=error;worst=i;}}
        std::cout<<std::setprecision(17)<<"ARKODE forcing precision method="<<(method==ode::Method::Erk?"erk":"esdirk")<<" max_abs_error="<<maximumError<<" time="<<solution->times[worst]<<" computed="<<solution->values[worst]<<" exact="<<.3+std::sin(solution->times[worst])-std::sin(.25)<<'\n';
        for(std::size_t i=0;i<solution->times.size();++i)near(solution->values[i],.3+std::sin(solution->times[i])-std::sin(.25),5e-8,"ARKODE time-dependent forcing reference");
        require(!solution->denseSegments.empty()&&std::all_of(solution->denseSegments.begin(),solution->denseSegments.end(),[method](const auto& segment){return segment.degree==(method==ode::Method::Erk?4u:3u);}),"Retained ARKODE interpolant matches the selected table's q-1 degree");
        for(std::size_t i=0;i<solution->times.size();++i)near(ode::denseEvaluate(*solution,solution->times[i])[0],solution->values[i],2e-12,"Retained ARKODE polynomial reproduces native sample output");
        near(ode::denseEvaluate(*solution,.371)[0],.3+std::sin(.371)-std::sin(.25),5e-8,"ARKODE off-grid native dense forcing reference");
        auto withoutDense=options;withoutDense.denseOutput=false;const auto sampled=ode::solve(timed,1,{.3},{},withoutDense,native);require(sampled->denseSegments.empty(),"Disabling retained dense output leaves no polynomial buffers");
        for(std::size_t i=0;i<sampled->times.size();++i)near(sampled->values[i],.3+std::sin(sampled->times[i])-std::sin(.25),5e-8,"ARKODE sample accuracy is unchanged when retention is disabled");
        const std::vector<double>initial{1,-2,.3,4,-.5};
        const auto p=ode::solve(pent,.5,initial,{-1.25},options,native);
        require(p->stateWidths==std::vector<std::size_t>{5}&&p->coordinateCount()==5,"ARKODE retains complete Pentarnion state");
        for(std::size_t i=0;i<p->times.size();++i)for(std::size_t c=0;c<5;++c)near(p->values[5*i+c],initial[c]*std::exp(-1.25*(p->times[i]-.25)),5e-8,"ARKODE all five Pentarnion coordinates reference");
    }
    const auto stiff=definition("ode arkstiff(t,real x){x'=-1000*(x-cos(t))-sin(t);}");
    options.method=ode::Method::Esdirk;options.t0=0;options.t1=1;options.sampleStep=.037;
    for(const auto* mode:{"analytic","finite_difference"}){
        options.jacobian=mode;const auto solution=ode::solve(stiff,1,{1},{},options,native);
        for(std::size_t i=0;i<solution->times.size();++i)near(solution->values[i],std::cos(solution->times[i]),2e-7,"ESDIRK stiff tracking exact reference");
        require(solution->stats.jacobianEvaluations>0,"ESDIRK stiff reference uses implicit Jacobian");
    }
    const auto invalid=definition("ode arkdomain(t,real x){x'=log(x);}");
    for(auto method:{ode::Method::Erk,ode::Method::Esdirk}){
        options.method=method;options.jacobian="analytic";options.maxSteps=1;
        fails([&]{(void)ode::solve(decay,1,{1},{1},options,native);},"O1006");
        options.maxSteps=1000000;
        fails([&]{(void)ode::solve(invalid,1,{-1},{},options,native);},"O1005");
    }
    options.method=static_cast<ode::Method>(99);
    fails([&]{(void)ode::solve(decay,1,{1},{1},options,native);},"O1001");
    const auto slow=definition("ode arklimits(t,real x,real y;real w){x'=w*y;y'=-w*x;}");
    (void)ode::evaluateNative(slow,1,0,{1,0},{1},native);
    for(auto method:{ode::Method::Erk,ode::Method::Esdirk}){
        options={};options.method=method;options.t1=1e6;options.sampleStep=1e6;options.maxSteps=100000000;options.maximumWallSeconds=.001;
        fails([&]{(void)ode::solve(slow,1,{1,0},{10000},options,native);},"O1003");
        options.maximumWallSeconds=60;
        auto cancel=native;cancel.cancellation=std::make_shared<std::atomic_bool>(false);
        std::thread thread([flag=cancel.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(25));flag->store(true);});
        try{fails([&]{(void)ode::solve(slow,1,{1,0},{10000},options,cancel);},"O1004");}catch(...){thread.join();throw;}thread.join();
    }
}

void retainedBudget(const ode::NativeOptions& native){
    const auto model=definition("ode retained(t,real x){x'=0;}");ode::SolveOptions options;options.t1=0;options.maxRetainedCoordinates=3;const auto point=ode::solve(model,1,{1},{},options,native);require(point->retainedCoordinateCount()==3,"Combined retention counts one state, one slope and one time separately from the state-only budget");
    options.maxRetainedCoordinates=2;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1006");options.maxRetainedCoordinates=0;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1001");options.maxRetainedCoordinates=32000001;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1001");
    options.t1=1;options.sampleStep=1;options.denseOutput=false;options.maxRetainedCoordinates=6;const auto sampled=ode::solve(model,1,{1},{},options,native);require(sampled->values.size()==2&&sampled->retainedCoordinateCount()==6,"Two states retain exactly six state/slope/time coordinates without dense storage");options.maxRetainedCoordinates=5;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1006");options.maxRetainedCoordinates=6;options.denseOutput=true;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1006");
    options.denseOutput=false;options.maxRetainedCoordinates=10;options.eventCount=1;options.eventTerminal={0};options.eventFunction=[](double time,const double*,double* value){value[0]=time-.5;};const auto event=ode::solve(model,1,{1},{},options,native);require(event->eventTimes.size()==1&&event->retainedCoordinateCount()==10,"Combined budget includes event time/state/index/direction buffers");options.maxRetainedCoordinates=9;fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1006");
}

void normalizedProductAndJacobian(const ode::NativeOptions&native){
    const auto model=definition("ode ordered(t,pentarnion p;pentarnion q){p'=p*q;}");
    std::string key;
    for(const auto&fixture:basisFixtures){std::vector<double>p(5),q(5);p[fixture.left]=1;q[fixture.right]=1;const auto result=ode::evaluateNative(model,fixture.gamma,0,p,q,native);if(key.empty())key=result.key;require(result.key==key,"Gamma does not change structural kernel");for(unsigned c=0;c<5;++c)near(result.rhs[c],fixture.expected[c],5e-15,"Independent normalized basis product fixture");}
    const auto paper=definition("ode paper(t,pentarnion p;pentarnion A,pentarnion B,real eta){p'=A*p+(1-eta)*(B*(p*p))+eta*((B*p)*p);}");
    const std::vector<double>state{.2,-.4,.7,1.1,-.3},parameters{.2,.3,-.4,.1,.2,-.1,.5,.2,-.3,.7,.37};
    const auto result=ode::evaluateNative(paper,.5,0,state,parameters,native);
    const algebra::Context context(.5);const algebra::Pentarnion p(context,{.2,-.4,.7,1.1,-.3}),a(context,{.2,.3,-.4,.1,.2}),b(context,{-.1,.5,.2,-.3,.7});
    const auto expected=algebra::add(algebra::add(algebra::multiply(a,p),algebra::scale(algebra::multiply(b,algebra::multiply(p,p)),.63)),algebra::scale(algebra::multiply(algebra::multiply(b,p),p),.37));
    for(unsigned row=0;row<5;++row)near(result.rhs[row],expected.coefficients()[row],1e-14,"Paper ordered RHS agrees with independent core");
    for(unsigned column=0;column<5;++column){auto plus=state,minus=state;const double step=1e-6;plus[column]+=step;minus[column]-=step;const auto fplus=ode::evaluateNative(paper,.5,0,plus,parameters,native);const auto fminus=ode::evaluateNative(paper,.5,0,minus,parameters,native);for(unsigned row=0;row<5;++row)near(result.jacobian[row*5+column],(fplus.rhs[row]-fminus.rhs[row])/(2*step),2e-9,"Automatic real-coordinate Jacobian vs independent central difference");}
    const auto left=definition("ode grouping(t,pentarnion p;pentarnion b){p'=(b*p)*p;}");
    const auto right=definition("ode grouping(t,pentarnion p;pentarnion b){p'=b*(p*p);}");
    const auto l=ode::evaluateNative(left,.5,0,state,coefficients(b),native),r=ode::evaluateNative(right,.5,0,state,coefficients(b),native);
    require(l.key!=r.key,"Grouped ordered ASTs have different cache identities");
    double difference=0;for(unsigned c=0;c<5;++c)difference+=std::abs(l.rhs[c]-r.rhs[c]);require(difference>1e-3,"Nonassociative grouping changes compiled scientific result");
}

void functionsAndDomains(const ode::NativeOptions&native){
    const auto real=definition("ode elementary(t,real x;real z){x'=+sin(x)+cos(x)+tan(x)+exp(x)+log(x)+sqrt(x)+sinh(x)+cosh(x)+tanh(x)+sqrt(z);}");
    const double x=.7;auto result=ode::evaluateNative(real,.5,0,{x},{0},native);
    const double expected=std::cos(x)-std::sin(x)+1/std::pow(std::cos(x),2)+std::exp(x)+1/x+.5/std::sqrt(x)+std::cosh(x)+std::sinh(x)+1-std::pow(std::tanh(x),2);
    near(result.jacobian[0],expected,3e-15,"Real math AD rules and sqrt(zero constant) tangent");
    const auto polynomial=definition("ode polynomial(t,pentarnion p){p'=+p^3+conj(p)+pure(p)+scalar(p)+coeff(p,4)+norm2(p)+commutator(2,p)+associator(p,i,j);}");
    result=ode::evaluateNative(polynomial,1,0,{1,2,3,4,5},{},native);require(result.rhs.size()==5&&result.jacobian.size()==25,"Polynomial functions and scalar embedding compile");
    const auto bad=definition("ode domain(t,real x){x'=sqrt(x);}");fails([&]{(void)ode::evaluateNative(bad,1,0,{-1},{},native);},"O1005");fails([&]{(void)ode::evaluateNative(bad,1,0,{0},{},native);},"O1005");
    const auto zeroRoot=definition("ode zeroroot(t,real x){x'=sqrt(x*x);}");
    fails([&]{(void)ode::evaluateNative(zeroRoot,1,0,{0},{},native);},"O1005");
    const auto constantRoot=definition("ode constantroot(t,real x;real z){x'=sqrt(z);}");
    const auto constant=ode::evaluateNative(constantRoot,1,0,{0},{0},native);
    near(constant.rhs[0],0,0,"Zero parameter-only square root remains valid");
    near(constant.jacobian[0],0,0,"Zero parameter-only square root has zero state Jacobian");
    const auto divide=definition("ode denominator(t,real x;real d){x'=x/d;}");fails([&]{(void)ode::evaluateNative(divide,1,0,{1},{0},native);},"O1005");
    const auto overflow=definition("ode overflow(t,real x){x'=exp(x);}");fails([&]{(void)ode::evaluateNative(overflow,1,0,{10000},{},native);},"O1005");
}

void cacheValidationAndConcurrency(const ode::NativeOptions&native){
    const auto model=definition("ode cachetest(t,real x){x'=x^2+1;}");
    auto first=ode::evaluateNative(model,1,0,{2},{},native);require(!first.cacheHit,"Fresh cache test model");
    {std::ofstream file(native.cacheDirectory/first.key/"model.cpp",std::ios::app);file<<"corrupted source";}
    auto repaired=ode::evaluateNative(model,1,0,{2},{},native);require(!repaired.cacheHit&&repaired.key==first.key,"Source hash corruption triggers native regeneration");
#ifdef _WIN32
    const auto binary=native.cacheDirectory/first.key/"model.dll";
#else
    const auto binary=native.cacheDirectory/first.key/"model.so";
#endif
    {std::fstream file(binary,std::ios::in|std::ios::out|std::ios::binary);char byte=0;file.read(&byte,1);byte^=1;file.seekp(0);file.write(&byte,1);}
    repaired=ode::evaluateNative(model,1,0,{2},{},native);require(!repaired.cacheHit,"Binary hash corruption triggers recompilation before load");near(repaired.rhs[0],5,1e-15,"Repaired native cache result");
    const auto concurrent=definition("ode concurrent(t,real x){x'=x^3-2;}");
    auto a=std::async(std::launch::async,[&]{return ode::evaluateNative(concurrent,1,0,{2},{},native);});
    auto b=std::async(std::launch::async,[&]{return ode::evaluateNative(concurrent,.5,0,{3},{},native);});
    const auto av=a.get(),bv=b.get();require(av.key==bv.key&&av.cacheHit!=bv.cacheHit,"OS cache lock serializes simultaneous compiler requests");near(av.rhs[0],6,1e-15,"Concurrent first value");near(bv.rhs[0],25,1e-15,"Concurrent second value");
    auto unicode=native;unicode.cacheDirectory=native.cacheDirectory/std::filesystem::path(u8"cache-α-测试");
    auto unicodeResult=ode::evaluateNative(model,1,0,{2},{},unicode);near(unicodeResult.rhs[0],5,1e-15,"Native compilation/loading supports Unicode cache directories");
    auto relative=native;relative.cacheDirectory=native.cacheDirectory.lexically_relative(std::filesystem::current_path())/"relative";
    near(ode::evaluateNative(model,1,0,{2},{},relative).rhs[0],5,1e-15,"Relative cache directory resolves safely before compiler cwd changes");
}

void limitsAndCancellation(const ode::NativeOptions&native){
    const auto model=definition("ode limits(t,real x;real a){x'=a*x;}");ode::SolveOptions options;options.t1=1;
    fails([&]{(void)ode::solve(model,1,{1},{},options,native);},"O1001");
    options.sampleStep=1e-9;fails([&]{(void)ode::solve(model,1,{1},{1},options,native);},"O1006");
    options.sampleStep=.1;options.maxSteps=1;fails([&]{(void)ode::solve(model,1,{1},{1},options,native);},"O1006");
    auto cancelled=native;cancelled.cancellation=std::make_shared<std::atomic_bool>(true);fails([&]{(void)ode::solve(model,1,{1},{1},options,cancelled);},"O1004");
    const auto compiling=definition("ode cancelcompile(t,real x){x'=x^4+2;}");cancelled.cancellation->store(false);
    std::vector<double> cancelledCompilations;
    cancelled.compilationFinished=[&](double seconds){cancelledCompilations.push_back(seconds);};
    std::thread cancellation([flag=cancelled.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(50));flag->store(true);});
    try{fails([&]{(void)ode::evaluateNative(compiling,1,0,{1},{},cancelled);},"O1004");}catch(...){cancellation.join();throw;}cancellation.join();
    require(cancelledCompilations.size()==1&&cancelledCompilations[0]>0,"Cancelled compiler invocation reports its duration exactly once");
    const auto slow=definition("ode slow(t,real x,real y;real w){x'=w*y;y'=-w*x;}");
    (void)ode::evaluateNative(slow,1,0,{1,0},{1},native);
    options={};options.t1=1e6;options.sampleStep=1e6;options.maxSteps=100000000;options.maximumWallSeconds=.001;
    fails([&]{(void)ode::solve(slow,1,{1,0},{10000},options,native);},"O1003");
    options.maximumWallSeconds=60;cancelled.cancellation->store(false);std::thread integrationCancel([flag=cancelled.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(25));flag->store(true);});
    try{fails([&]{(void)ode::solve(slow,1,{1,0},{10000},options,cancelled);},"O1004");}catch(...){integrationCancel.join();throw;}integrationCancel.join();
}

void compilationTiming(const ode::NativeOptions&native){
    const auto model=definition("ode compilertiming(t,real x){x'=x^5+3;}");
    auto timed=native;std::vector<double> durations;
    timed.compilationFinished=[&](double seconds){durations.push_back(seconds);throw std::runtime_error("Observer exception must not affect native compilation.");};
    const auto first=ode::evaluateNative(model,1,0,{1},{},timed);
    require(!first.cacheHit&&durations.size()==1&&durations[0]>0,"Successful compiler invocation reports positive duration once and contains callback exceptions");
    const auto second=ode::evaluateNative(model,.5,0,{2},{},timed);
    require(second.cacheHit&&second.key==first.key&&durations.size()==1,"Timing callback does not enter cache identity and is not called on cache hit");
    const auto timeout=definition("ode compilationtimeout(t,real x){x'=x^6+3;}");
    timed.compileTimeoutSeconds=.001;
    fails([&]{(void)ode::evaluateNative(timeout,1,0,{1},{},timed);},"O1003");
    require(durations.size()==2&&durations.back()>0,"Timed-out compiler invocation reports its duration exactly once");
}

#ifdef _WIN32
int startupProbe(const char* readyName) {
    STARTUPINFOW startup{};startup.cb=sizeof(startup);GetStartupInfoW(&startup);
    std::cout<<"startup-probe flags="<<startup.dwFlags<<" show="<<startup.wShowWindow
        <<" console="<<(GetConsoleWindow()!=nullptr)<<" stdin_type="<<GetFileType(GetStdHandle(STD_INPUT_HANDLE))<<std::endl;
    if(GetConsoleWindow()!=nullptr || !(startup.dwFlags&STARTF_USESHOWWINDOW) ||
        startup.wShowWindow!=SW_HIDE || !(startup.dwFlags&STARTF_FORCEOFFFEEDBACK))return 71;
    const auto input=GetStdHandle(STD_INPUT_HANDLE);
    if(input==nullptr || input==INVALID_HANDLE_VALUE || GetFileType(input)!=FILE_TYPE_CHAR)return 72;
    char byte=0;DWORD count=1;
    if(!ReadFile(input,&byte,1,&count,nullptr) || count!=0)return 73;
    WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
    type.lpszClassName=L"PentaNativeCompilerStartupProbe";
    if(!RegisterClassW(&type))return 75;
    const auto window=CreateWindowExW(WS_EX_APPWINDOW,type.lpszClassName,L"Native compiler visibility probe",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,300,100,nullptr,nullptr,type.hInstance,nullptr);
    if(!window)return 76;
    ShowWindow(window,SW_SHOWDEFAULT);
    const bool visible=IsWindowVisible(window)!=FALSE;
    DestroyWindow(window);UnregisterClassW(type.lpszClassName,type.hInstance);
    if(visible)return 77;
    const auto ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,std::filesystem::path(readyName).c_str());
    if(!ready)return 74;
    const bool signalled=SetEvent(ready)!=FALSE;CloseHandle(ready);
    if(!signalled)return 74;
    Sleep(250); // Parent checks pipe EOF while this process is still alive.
    std::cout<<"headless-startup: no console, hidden window, NUL stdin, restricted handles\n";
    return 0;
}

void compilerStartupIsolation(const ode::NativeOptions& native) {
    wchar_t executable[32768];const auto length=GetModuleFileNameW(nullptr,executable,32768);
    require(length>0&&length<32768,"Locate startup probe executable");
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
    const auto markerName=L"Local\\PentaCompilerSentinel-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto marker=CreateEventW(nullptr,TRUE,FALSE,markerName.c_str());
    require(marker!=nullptr,"Create compiler probe readiness event");
    HANDLE read=nullptr,write=nullptr;
    if(!CreatePipe(&read,&write,&attributes,0)){CloseHandle(marker);throw std::runtime_error("Cannot create inheritance sentinel pipe");}
    if(!SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)){CloseHandle(read);CloseHandle(write);CloseHandle(marker);throw std::runtime_error("Cannot isolate sentinel pipe reader");}
    try {
        const std::vector<std::filesystem::path> arguments{
            "--compiler-startup-probe",std::filesystem::path(markerName)};
        auto child=std::async(std::launch::async,[&]{return ode::detail::runCompilerProcess(std::filesystem::path(executable),arguments,
            std::filesystem::current_path(),native);});
        const bool ready=WaitForSingleObject(marker,5000)==WAIT_OBJECT_0;
        CloseHandle(write);write=nullptr;
        DWORD available=0;
        const bool inherited=PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr)!=FALSE || GetLastError()!=ERROR_BROKEN_PIPE;
        const auto output=child.get();
        require(output.find("headless-startup: no console")!=std::string::npos,
            "Native compiler startup has no console, suppresses window display and supplies NUL input");
        require(ready&&!inherited,"Compiler child does not inherit unrelated parent pipe handles");
    } catch(...) {CloseHandle(read);if(write)CloseHandle(write);CloseHandle(marker);throw;}
    CloseHandle(read);CloseHandle(marker);
}

void guiProcessWithoutStandardInput(const ode::NativeOptions&native){
    struct RestoreInput {
        HANDLE original=GetStdHandle(STD_INPUT_HANDLE);
        ~RestoreInput(){SetStdHandle(STD_INPUT_HANDLE,original);}
    } restore;
    require(SetStdHandle(STD_INPUT_HANDLE,nullptr)!=FALSE,"Simulate GUI process without a console stdin");
    const auto model=definition("ode guinostdin(t,real x){x'=x^7+4;}");
    const auto result=ode::evaluateNative(model,1,0,{1},{},native);
    require(!result.cacheHit,"GUI null-stdin regression invokes a fresh native compilation");
    near(result.rhs[0],5,1e-15,"Actual native compilation succeeds without console stdin");
}

void crossProcessCache(const ode::NativeOptions&native){
    wchar_t buffer[32768];const auto length=GetModuleFileNameW(nullptr,buffer,32768);require(length>0&&length<32768,"Locate native test executable for process-lock test");
    std::wstring command=L"\""+std::wstring(buffer,length)+L"\" --cache-child \""+native.cacheDirectory.wstring()+L"\"";
    PROCESS_INFORMATION children[2]{};
    for(auto&child:children){STARTUPINFOW start{};start.cb=sizeof(start);auto arguments=command;require(CreateProcessW(buffer,arguments.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&child)!=FALSE,"Launch simultaneous native cache process");CloseHandle(child.hThread);}
    for(auto&child:children){const auto status=WaitForSingleObject(child.hProcess,30000);if(status!=WAIT_OBJECT_0)TerminateProcess(child.hProcess,1);DWORD exit=1;GetExitCodeProcess(child.hProcess,&exit);CloseHandle(child.hProcess);require(status==WAIT_OBJECT_0&&exit==0,"Simultaneous native cache child succeeds");}
    std::string a,b;std::ifstream(native.cacheDirectory/("child-"+std::to_string(children[0].dwProcessId)+".txt"))>>a;std::ifstream(native.cacheDirectory/("child-"+std::to_string(children[1].dwProcessId)+".txt"))>>b;
    require((a=="hit"&&b=="miss")||(a=="miss"&&b=="hit"),"Cross-process cache lock compiles exactly once");
}
#endif
}
int main(int argc,char**argv){
#ifdef _WIN32
    if(argc==3&&std::string(argv[1])=="--compiler-startup-probe")try{return startupProbe(argv[2]);}catch(...){return 78;}
    if(argc==3&&std::string(argv[1])=="--cache-child")try{ode::NativeOptions native;native.cacheDirectory=argv[2];auto model=definition("ode crossprocess(t,real x){x'=x^2+3;}");const auto result=ode::evaluateNative(model,1,0,{2},{},native);if(result.rhs[0]!=7)return 2;std::ofstream(native.cacheDirectory/("child-"+std::to_string(GetCurrentProcessId())+".txt"))<<(result.cacheHit?"hit":"miss");return 0;}catch(...){return 3;}
#else
    (void)argc;(void)argv;
#endif
    const auto root=std::filesystem::absolute(std::filesystem::path("ode-native-test-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
    try{ode::NativeOptions native;native.cacheDirectory=root;
        hashContract();analyticAndCache(native);coupledAndSampling(native);arkodeMethods(native);retainedBudget(native);normalizedProductAndJacobian(native);functionsAndDomains(native);cacheValidationAndConcurrency(native);limitsAndCancellation(native);compilationTiming(native);
#ifdef _WIN32
        compilerStartupIsolation(native);
        guiProcessWithoutStandardInput(native);
        crossProcessCache(native);
#endif
        std::cout<<"Native ODE tests: "<<checks<<" checks passed\n";std::error_code ignored;std::filesystem::remove_all(root,ignored);return 0;
    }catch(const std::exception&e){std::cerr<<"Native ODE tests failed after "<<checks<<" checks: "<<e.what()<<"\nCache retained at "<<root<<'\n';return 1;}
}
