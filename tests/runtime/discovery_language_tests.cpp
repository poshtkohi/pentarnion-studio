#include "runtime/interpreter.hpp"
#include "algebra/pentarnion.hpp"
#include "ode/native_ode.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
namespace {
int checks=0,failures=0;
void require(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void run(const std::string& source,const penta::runtime::ExecutionOptions& options,bool success=true,const char* code=""){
 auto r=penta::runtime::execute(source,{},options);require(r.ok()==success,"Discovery language accepted/rejected as expected.");
 if(r.ok()!=success){std::cerr<<source<<'\n';for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
 if(*code)require(!r.runtimeDiagnostics.empty()&&r.runtimeDiagnostics.front().code==code,"Discovery failure preserves its diagnostic code.");
}
}
int main(){try{
 const auto dir=std::filesystem::temp_directory_path()/("penta-discovery-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 penta::runtime::ExecutionOptions o;o.dataDirectory=dir;o.odeCacheDirectory=dir/"kernels";
 run("matrix d=pentdirections();assert(size(d)==[242,5]);assert(norm(sum(d.*d,2)-ones(242,1))<1e-12);",o);
 run("record a=searchinterval(-2,2,options(samples=7,method=\"uniform\",seed=42));record b=searchinterval(-2,2,options(samples=7,method=\"uniform\",seed=42));assert(a.values==b.values);record c=searchinterval(0,2,options(samples=3,method=\"linspace\"));assert(c.values==[0;1;2]);",o);
 run("ode F(t,pentarnion q,pentarnion v;pentarnion A,pentarnion B,real m){q'=v;v'=(A*q+B*(q*q))/m;}record p=discoverplan(F,zeros(1,10),options(parameters=options(A=searchset(pentdirections()),B=searchset(pentdirections()),m=1.0),positive_parameters=\"m\"));assert(p.runs==58564);assert(p.parameter_combinations==58564);assert(p.physical_coordinates==10);assert(p.within_budget==0);",o);
 const std::string oscillator="ode oscillator(t,real x,real v;real frequency){x'=v;v'=-frequency*x;}";
 run(oscillator+"record r=odeensemble(oscillator,[1,0;2,0],options(parameters=options(frequency=searchset([1,4])),threads=4,t1=7.0,sample_step=0.02,max_native_operations=10000000));assert(r.runs==4);assert(r.completed==4);assert(r.prepared_models==1);record a=discoverstatus(r,1);assert(a.run_id==1);assert(a.initial==[1,0]);record b=experimentrun(oscillator,r,3,options(rtol=1e-10,atol=1e-12));assert(b.cache_hit==1);assert(abs(b.values(end,1)-cos(14))<1e-7);",o);
 run("ode contract(t,real x){x'=-x;}record r=discover(contract,[1;2],options(t1=1.0,sample_step=0.1,checkpoint=\"resume.json\",threads=1));record s=discover(contract,[1;2],options(t1=1.0,sample_step=0.1,checkpoint=\"resume.json\",threads=4,resume=1));assert(s.restored==2);assert(r.states==s.states);record a=discoverstatus(r,2);assert(size(a.parameters)==[0,0]);",o);
 run("ode contract(t,real x){x'=-x;}record r=discover(contract,[1;2],options(t1=1.0,max_native_operations=1));",o,false,"R1005");
 run(oscillator+"record r=discover(oscillator,[1,0],options(parameters=options(frequency=0.0),positive_parameters=\"frequency\"));",o,false,"R2050");
 run("record r=searchinterval(0,1,options(samples=10));",o,false,"R2050");
 run("ode p(t,pentarnion q){q'=-q;}record r=discover(p,[1,2,3,4,5],options(gammas=[0,1],t1=0.1,sample_step=0.01));assert(r.runs==2);assert(r.states(1,:)==r.states(2,:));record s=experimentrun(p,r,1);assert(s.gamma==0);assert(s.coordinates==5);",o);
 run("ode growth(t,real x){x'=x;}record r=discover(growth,[1;2],options(t1=1.0,sample_step=0.1));record disabled=exportexperiment(growth,r,options(enabled=0,directory=\"disabled\",ids=[1]));assert(disabled.written==0);record saved=exportexperiment(growth,r,options(directory=\"selected\",ids=[2],kinds=\"series,norm\",width=640,height=480,maximum_images=2));assert(saved.written==2);record resumed=exportexperiment(growth,r,options(directory=\"selected\",ids=[2],kinds=\"series,norm\",width=640,height=480,maximum_images=2,resume=1));assert(resumed.restored==2&&resumed.written==0);record ranked=exportexperiment(growth,r,options(directory=\"ranked\",top_n=1,sort_by=\"maximum_norm\",width=640,height=480));assert(ranked.written==1);",o);
 require(!std::filesystem::exists(dir/"disabled"),"Disabled image export creates no directory.");
 require(std::filesystem::exists(dir/"selected/candidate-run-000002-series.png")&&std::filesystem::exists(dir/"selected/candidate-run-000002-norm.png"),"Selected run and selected kinds are saved natively.");
 require(!std::filesystem::exists(dir/"selected/candidate-run-000001-series.png")&&!std::filesystem::exists(dir/"selected/candidate-run-000002-trajectory2.png"),"Unselected runs and plot kinds are absent.");
 require(std::filesystem::exists(dir/"ranked/candidate-run-000002-series.png"),"Top-N ranking uses deterministic scores and IDs.");
 require(std::filesystem::exists(dir/"selected/candidate-exports.json"),"Independent versioned export ledger accompanies images.");
 {
  auto replayOptions=o;replayOptions.maxOperations=8000;
  const std::string source="ode fast(t,real x,real y){x'=-100*y;y'=100*x;}record e=discover(fast,[1,0],options(t1=1.0,sample_step=.1,rtol=1e-10,atol=1e-12));";
  run(source+"record selected=experimentrun(fast,e,1);assert(selected.native_operations>8000);",replayOptions);
  run(source+"record selected=experimentrun(fast,e,1,options(max_native_operations=1));",replayOptions,false,"R1005");
 }
 run("record p=provenance();assert(p.version==1);assert(p.scientific.precision_bits==53);assert(p.scientific.integer_bits==64);record r=researchrecord(\"test\",options(value=2.0),options(units=\"s\"));saveanalysis(\"research.json\",r);record restored=loadanalysis(\"research.json\",r);assert(restored.data.value==2.0);assert(restored.settings.units==\"s\");assert(restored.provenance.scientific.source_hash==p.scientific.source_hash);",o);
 {
  penta::runtime::InterpreterSession session;
  require(session.submit("pentarnion original=1+i;").ok(),"Existing console context is initialized.");
  require(session.submit("use_algebra(\"A_gamma\",gamma=0);clear original;pentarnion other=j;").status==penta::runtime::ExecutionStatus::RuntimeError,"Partial clear cannot change an existing algebra context.");
  require(session.submit("use_algebra(\"A_gamma\",gamma=0);clear;pentarnion original=1+j;").ok(),"A checked leading full clear allows an explicit context reset.");
  const auto failed=session.submit("use_algebra(\"A_gamma\",gamma=0.5);clear;pentarnion replacement=k;error(\"deliberate context rollback\");");
  require(failed.status==penta::runtime::ExecutionStatus::RuntimeError,"Failed context-changing submission reports its error.");
  require(session.submit("assert(norm2(original-(1+j))==0);").ok(),"A failed context reset restores previous values and context together.");
  require(session.submit("matrix components=[1,2];assert(components(2)==2);").ok(),"Preexisting components array identifiers remain compatible.");
 }
 // Independent ordered-algebra reference and exact ordered work identity.
 const auto analysis=penta::language::analyse("ode force(t,pentarnion q,pentarnion v,pentarnion W,pentarnion V,pentarnion C;pentarnion A,pentarnion B,real m){q'=v;v'=(A*q+B*(q*q))/m;W'=(A*q+B*(q*q))*v;V'=v*(A*q+B*(q*q));C'=(A*q+B*(q*q))*v-v*(A*q+B*(q*q));}");
 require(analysis.ok(),"Checked full-coordinate force/work model.");
 if(analysis.ok()){
  penta::ode::NativeOptions native;native.cacheDirectory=dir/"kernels";penta::ode::PreparedModel model(*analysis.program.statements.front().ode,native);
  std::vector<double> initial{.1,.2,-.1,.05,.15,.2,-.1,.25,.1,-.05};initial.resize(25,0);std::vector<double> parameters{.1,.2,-.1,0,.05,.03,-.1,.2,-.05,.1,1.7};
  using namespace penta::algebra;
  auto element=[](const std::vector<double>& v,std::size_t offset){return Pentarnion(Context(1),{v[offset],v[offset+1],v[offset+2],v[offset+3],v[offset+4]});};
  const auto q=element(initial,0),v=element(initial,5),A=element(parameters,0),B=element(parameters,5);const auto force=add(multiply(A,q),multiply(B,multiply(q,q)));const auto rhs=model.evaluate(1,0,initial,parameters,native).rhs;
  for(std::size_t k=0;k<5;++k)require(std::abs(rhs[k+5]-force.coefficients()[k]/1.7)<1e-14,"Compiled grouped force matches independent algebra.");
  penta::ode::SolveOptions s;s.t1=.2;s.sampleStep=.01;s.rtol=1e-11;s.atol=1e-13;const auto solved=model.solve(1,initial,parameters,s,native);std::vector<double> last(solved->values.end()-25,solved->values.end());
  const auto vt=element(last,5),W=element(last,10),V=element(last,15),C=element(last,20);const auto delta=scale(subtract(multiply(vt,vt),multiply(v,v)),1.7);
  const auto total=subtract(add(W,V),delta),corrected=subtract(W,scale(add(delta,C),.5));double error=0,correction=0;for(std::size_t k=0;k<5;++k){error+=std::abs(total.coefficients()[k])+std::abs(corrected.coefficients()[k]);correction+=std::abs(C.coefficients()[k]);}
  require(error<1e-8,"Ordered work includes the noncommuting correction.");require(correction>1e-5,"Noncommuting test genuinely exercises the commutator.");
 }
}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
std::cout<<checks<<" discovery language/work checks, "<<failures<<" failures\n";return failures?1:0;}
