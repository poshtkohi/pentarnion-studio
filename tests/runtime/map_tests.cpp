#include "maps/map_module.hpp"
#ifndef PENTA_MAP_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
bool close(double a,double b,double tolerance=1e-11){return std::abs(a-b)<tolerance*(1+std::abs(b));}
struct Context {int kind=0;double rate=3.7;std::shared_ptr<std::atomic_bool> cancellation;};
int evaluate(void* raw,const double* x,uint64_t n,double* next,double* jac,char* error,uint64_t capacity){
    auto& context=*static_cast<Context*>(raw);
    if(context.kind==4){if(capacity){const std::string text="independent map callback failure";const auto size=std::min<uint64_t>(capacity-1,text.size());std::copy_n(text.data(),size,error);error[size]=0;}return 1;}
    if(context.kind==5){next[0]=std::numeric_limits<double>::quiet_NaN();return 0;}
    for(uint64_t row=0;row<n;++row){next[row]=context.kind==1?2*x[row]:context.kind==2?.5*x[row]+static_cast<double>(row+1):context.rate*x[row]*(1-x[row]);if(jac)for(uint64_t col=0;col<n;++col)jac[row*n+col]=row==col?(context.kind==1?2:context.kind==2?.5:context.rate*(1-2*x[row])):0;}
    if(context.cancellation)context.cancellation->store(true);
    return 0;
}
penta_maps_request_v1 request(Context& context,const std::vector<double>& initial){penta_maps_request_v1 r{};r.struct_size=sizeof(r);r.dimension=initial.size();r.iterations=5;r.stride=1;r.final_jacobian=1;r.max_evaluations=1000;r.maximum_wall_seconds=60;r.initial=initial.data();r.callback_context=&context;r.evaluate=evaluate;return r;}
template<class F>void rejects(F task,int status,const std::string& label){try{task();expect(false,label);}catch(const penta::maps::Error& error){expect(error.status==status,label+": "+error.what());}}
void kernels(){
    using namespace penta::maps;Module module;expect(!module.loaded(),"maps module initially unloaded");Context context;std::vector<double> initial{.2};auto r=request(context,initial);const auto result=module.iterate(r);expect(module.loaded(),"first map use lazily loads module");
    double independent=.2;for(std::size_t n=0;n<=5;++n){expect(close(result.states[n],independent),"native logistic orbit independent recurrence step"+std::to_string(n));expect(result.indices[n]==n,"zero-based map iteration index");independent=3.7*independent*(1-independent);}
    expect(result.report.iterations==5&&result.report.samples==6&&result.report.evaluations==6&&result.report.jacobian_evaluations==1,"iteration/value/Jacobian counts");expect(close(result.jacobian[0],3.7*(1-2*result.solution[0])),"final-state generated Jacobian layout");
    r.iterations=7;r.transient=2;r.stride=2;const auto sampled=module.iterate(r);expect(sampled.indices==std::vector<double>({2,4,6,7}),"transient/stride retain final off-stride sample");expect(sampled.states.size()==4&&sampled.report.iterations==7,"transients are computed rather than skipped");
    r=request(context,initial);r.iterations=0;const auto zero=module.iterate(r);expect(zero.states==initial&&zero.solution==initial&&zero.report.evaluations==1,"zero-iteration identity and final Jacobian");r.final_jacobian=0;const auto noJac=module.iterate(r);expect(noJac.report.evaluations==0&&noJac.jacobian.empty(),"disabled Jacobian has no hidden evaluation");
    context.kind=1;initial[0]=.75;r=request(context,initial);r.iterations=10;r.escape_radius=2;const auto escaped=module.iterate(r);expect(escaped.report.escaped&&escaped.report.escape_iteration==2&&escaped.report.iterations==2&&escaped.solution[0]==3,"finite-radius escape records stopping iteration");expect(escaped.indices==std::vector<double>({0,1,2})&&escaped.jacobian.empty(),"escape samples retained and no undefined final Jacobian");r.stride=3;expect(module.iterate(r).indices==std::vector<double>({0,2}),"off-stride terminal escape sample retained");r.transient=3;expect(module.iterate(r).states.empty(),"escape before transient returns no misleading retained samples");
    context.kind=2;initial.assign(5,0);r=request(context,initial);r.iterations=20;const auto pent=module.iterate(r);for(std::size_t n=0;n<5;++n)expect(close(pent.solution[n],2*static_cast<double>(n+1)*(1-std::pow(.5,20))),"five-coordinate affine map");for(std::size_t row=0;row<5;++row)for(std::size_t col=0;col<5;++col)expect(pent.jacobian[row*5+col]==(row==col?.5:0),"output-major real-coordinate Jacobian");
    r.max_evaluations=1;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_BUDGET,"evaluation exhaustion is a bounded diagnostic");r=request(context,initial);r.maximum_wall_seconds=1e-12;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_BUDGET,"wall-time budget");r=request(context,initial);r.transient=6;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_ARGUMENT,"invalid transient");r=request(context,initial);r.stride=0;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_ARGUMENT,"zero stride");r=request(context,initial);r.iterations=1000001;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_ARGUMENT,"iteration hard bound");
    r=request(context,initial);r.escape_radius=-1;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_ARGUMENT,"negative escape radius");context.kind=4;r=request(context,initial);rejects([&]{(void)module.iterate(r);},PENTA_MAPS_CALLBACK,"callback failure diagnosis");context.kind=5;rejects([&]{(void)module.iterate(r);},PENTA_MAPS_DOMAIN,"nonfinite map output rejected");
    context.kind=0;initial={.2};r=request(context,initial);auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{(void)module.iterate(r,cancellation);},PENTA_MAPS_CANCELLED,"pre-cancelled module remains unloaded/safe");cancellation->store(false);context.cancellation=cancellation;rejects([&]{(void)module.iterate(r,cancellation);},PENTA_MAPS_CANCELLED,"active map cancellation");context.cancellation.reset();cancellation->store(false);expect(module.iterate(r,cancellation).report.samples==6,"module reusable after cancellation");
    struct BudgetFailure:std::exception{};bool preserved=false;try{(void)module.iterate(r,{},[](std::size_t){throw BudgetFailure{};});}catch(const BudgetFailure&){preserved=true;}expect(preserved,"host budget exception identity preserved over ABI");
    const auto missingPath=std::filesystem::temp_directory_path()/("penta-missing-map-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(missingPath);Module missing(missingPath);rejects([&]{(void)missing.iterate(r);},PENTA_MAPS_INTERNAL,"missing optional map module diagnosed");std::filesystem::remove(missingPath);rejects([&]{Module relative("relative");},PENTA_MAPS_ARGUMENT,"relative module path rejected");
}
#ifndef PENTA_MAP_KERNEL_ONLY
void runtime(){
    using namespace penta::runtime;ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-map-source-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"runtime map program "+source.substr(0,75));return result;};
    const auto first=run("native function real logistic(real x,real r){return r*x*(1-x);}record m=mapiterate(logistic,0.2,options(parameters=options(r=3.7),iterations=5));assert(m.samples==6);assert(m.n(1)==0);assert(abs(m.states(2)-0.592)<1e-12);assert(abs(m.jacobian(1)-3.7*(1-2*m.x))<1e-10);assert(m.threads_used==1);display(m.cache_key);record q=mapiterate(logistic,0.1,options(parameters=options(r=3.5),iterations=3,transient=1));assert(q.cache_hit);assert(q.cache_key==m.cache_key);record pairs=returnmap(m,options(lag=2));assert(pairs.samples==4);assert(pairs.source_index(1)==0&&pairs.target_index(1)==2);assert(pairs.x(1)==m.states(1)&&pairs.y(1)==m.states(3));record fd=mapiterate(logistic,0.2,options(parameters=options(r=3.7),iterations=5,jacobian=\"finite_difference\"));assert(norm(fd.jacobian-m.jacobian,\"fro\")<1e-6);");
    const auto edited=run("native function real logistic(real x,real r){return r*x*(1-x)+0.001;}record m=mapiterate(logistic,0.2,options(parameters=options(r=3.7),iterations=5));display(m.cache_key);");expect(first.output.size()==1&&edited.output.size()==1&&first.output[0]!=edited.output[0],"map body edit invalidates compiled structure cache");
    run("threads(3);native function pentarnion affine(pentarnion p,pentarnion a){return 0.5*p+a;}record m=mapiterate(affine,0.0,options(parameters=options(a=1.0+j),iterations=10,threads=2));assert(abs(coeff(m.x,0)-2*(1-1/1024))<1e-12);assert(abs(coeff(m.x,2)-2*(1-1/1024))<1e-12);assert(size(m.states,2)==5);assert(size(m.orbit,2)==1);assert(m.jacobian==0.5*eye(5));assert(threads()==3);record pairs=returnmap(m,options(component=3));assert(pairs.samples==10);assert(pairs.x(1)==0&&pairs.y(1)==1);");
    run("native function [real first,pentarnion second] mixed(real x,pentarnion p){return x+1,0.5*p;}record m=mapiterate(mixed,(0.0,i),options(iterations=2));assert(size(m.states,2)==6);assert(m.states(3,1)==2);assert(m.states(3,3)==0.25);");
    run("native function real logistic(real x,real r){return r*x*(1-x);}record d=mapdiagram(logistic,0.2,\"r\",[2.0,3.0],options(parameters=options(r=1.0),iterations=3,transient=1));assert(d.samples==6);assert(d.parameter(1)==2&&d.parameter(4)==3);assert(abs(d.states(1)-0.32)<1e-12);assert(abs(d.states(4)-0.48)<1e-12);assert(d.scan_index(1)==1&&d.scan_index(4)==2);assert(size(d.final_states,1)==2);assert(d.escaped==0);");
    run("native function real growing(real x){return 2*x;}record m=mapiterate(growing,0.75,options(iterations=10,escape_radius=2.0));assert(m.escaped==1&&m.escape_iteration==2);assert(m.x==3);assert(numel(m.jacobian)==0);");
    run("matrix samples=[1+i;2+3*i;4+5*i];record pairs=returnmap(samples,options(component=2));assert(pairs.x==[1;3]);assert(pairs.y==[3;5]);");
    std::string grouping="native function pentarnion leftmap(pentarnion p,pentarnion a){return a*(p*p);}native function pentarnion rightmap(pentarnion p,pentarnion a){return (a*p)*p;}pentarnion p0=1+i+j+ell;pentarnion a=i-j+ell;pentarnion expected=a*(p0*p0);record m=mapiterate(leftmap,p0,options(parameters=options(a=a),iterations=1,jacobian=\"none\"));record r=mapiterate(rightmap,p0,options(parameters=options(a=a),iterations=1,jacobian=\"none\"));";for(int c=0;c<5;++c)grouping+="assert(abs(coeff(m.x-expected,"+std::to_string(c)+"))<1e-11);";grouping+="assert(norm(m.states-r.states,\"fro\")>1e-5);";run(grouping);
    run("native function real F(real x){return x;}record m=mapiterate(F,0.0,options(typo=1));",false);
    run("native function real F(real x){return x;}record m=mapiterate(F,0.0,options(jacobian=\"symbolic\"));",false);
    run("native function real F(real x){return x;}record m=mapiterate(F,0.0,options(iterations=1,transient=2));",false);
    run("native function real F(real x){return x;}record m=mapiterate(F,0.0,options(iterations=10,max_evaluations=1));",false);
    run("native function real F(real x){return x;}record m=mapdiagram(F,0.0,\"x\",[1,2]);",false);
    run("native function pentarnion F(pentarnion p){return p;}record m=mapiterate(F,[i,j]);",false);
    run("native function [real a,real b] F(real x){return x,x;}record m=mapiterate(F,0.0);",false);
    run("function real F(real x){matrix A=[x];return sum(A);}record m=mapiterate(F,0.0);",false);
    run("record p=returnmap([1,2],options(lag=2));",false);
    run("record p=returnmap([1,2],options(typo=1));",false);
    run("native function real F(real x,real r){return r*x;}record d=mapdiagram(F,0.1,\"r\",[1,2],options(parameters=options(r=1.0),iterations=3));record pairs=returnmap(d);",false);
#ifdef PENTA_SOURCE_DIR
    const auto source=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/discrete-maps/main.p";std::ifstream stream(source,std::ios::binary);const std::string example((std::istreambuf_iterator<char>(stream)),{});expect(!example.empty(),"discrete-maps example exists");auto exampleOptions=options;exampleOptions.maxOperations=20000000;const auto exampleResult=execute(example,{},exampleOptions);if(!exampleResult.ok()){for(const auto& d:exampleResult.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:exampleResult.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(exampleResult.ok(),"complete discrete-maps example runs");std::size_t plots=0,points=0;for(const auto& action:exampleResult.figureActions)if(action.kind==FigureActionKind::Plot){++plots;for(const auto& curve:action.curves)points+=curve.points.size();}expect(plots==4&&points>1000,"map orbit/return/diagram/Pentarnion phase plot samples retained");
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){kernels();
#ifndef PENTA_MAP_KERNEL_ONLY
runtime();
#endif
std::cout<<checks<<" map checks, "<<failures<<" failures\n";return failures?1:0;}

