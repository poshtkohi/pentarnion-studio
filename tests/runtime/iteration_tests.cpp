#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace {
int checks=0,failures=0;
void expect(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
const std::string stopFirst=R"(
function integer stop_first(matrix x,record optimValues=iterationinfo(),string state="iter") {
    assert(size(x,1)==1);
    assert(optimValues.dimension==numel(x));
    if(state=="iter"){return 1;}
    return 0;
}
)";
void runtime(){using namespace penta::runtime;ExecutionOptions settings;settings.maxOperations=20000000;settings.dataDirectory=std::filesystem::temp_directory_path()/("penta-iteration-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(settings.dataDirectory);settings.odeCacheDirectory=settings.dataDirectory/".penta-cache"/"odes";
 auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},settings);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"OutputFcn language script");return result;};
 const std::string stopInit=R"(function integer init_stop(matrix x,record optimValues=iterationinfo(),string state="iter"){display("monitor:",state);assert(optimValues.dimension==numel(x));return state=="init";})";
 auto initCase=[&](const std::string& source){const auto result=run(stopInit+source);std::size_t initial=0,done=0;for(const auto& line:result.output){initial+=line.find("monitor:")!=std::string::npos&&line.find("init")!=std::string::npos;done+=line.find("monitor:")!=std::string::npos&&line.find("done")!=std::string::npos;}expect(initial==1&&done==1&&result.output.size()==2,"INIT stop delivers INIT and DONE exactly once");};
 initCase(R"(native function real f(real x){return x-3.0;}record r=lsqfit(f,0.0,options(OutputFcn=init_stop));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations==1);assert(r.residual_norm==3);assert(r.cost==4.5);)");
 initCase(R"(native function real f(real x){return(x-3.0)*(x-3.0);}record r=minimize(f,0.0,options(OutputFcn=init_stop));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations==1);assert(r.objective==9);)");
 initCase(R"(native function real f(real x){return(x-0.3)*(x-0.3);}record r=evolve(f,0.9,options(algorithm="jde",lower=-1,upper=1,population=8,OutputFcn=init_stop));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.generations==0);assert(r.evaluations==8);assert(r.objective>=0);assert(r.objective<=4);)");
 initCase(R"(native function real f(real x){return x*x-2.0;}record r=root(f,[0,2],options(OutputFcn=init_stop));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations==1);assert(r.x==0);assert(r.residual_norm==2);)");
 initCase(R"(native function [real a,real b] f(real x,real y){return x-1.0,y-2.0;}record r=rootsolve(f,[0,0],options(OutputFcn=init_stop));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations==1);assert(norm(r.x)==0);assert(r.residual_norm==2);)");
 run("record info=iterationinfo();assert(info.funccount==0);assert(info.objective_available==0);assert(info.event==\"none\");assert(info.state==\"init\");");
 run(stopFirst+R"(native function real objective(real x){return(x-3.0)*(x-3.0);}record r=minimize(objective,0.0,options(algorithm="nelder_mead",OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations<=3);)");
 run(stopFirst+R"(native function real residual(real x){return(x-3.0)*(x-3.0);}record r=lsqfit(residual,0.0,options(OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);)");
 run(stopFirst+R"(native function real model(real m,real b,real x){return m*x+b;}record r=curvefit(model,[0,0],[0,1,2],[1,3,5],options(OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);)");
 run(stopFirst+R"(ode linear(t,real y;real rate){y'=rate;}record r=odefit(linear,0.0,[0,1,2],[0;2;4],options(parameters=options(rate=0.5),OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);)");
 run(R"(function integer passive(matrix x,record optimValues=iterationinfo(),string state="iter"){return 0;}native function real f(real x){return(x-2.0)*(x-2.0);}record a=minimize(f,0.0);record b=minimize(f,0.0,options(OutputFcn=passive,OutputEvery=2));assert(a.evaluations==b.evaluations);assert(a.native_evaluations==b.native_evaluations);assert(a.objective==b.objective);record c=lsqnonlin(f,0.0,options(OutputFcn=passive,method="dogleg"));assert(c.converged);)");
 run(R"(function integer stop_done(matrix x,record optimValues=iterationinfo(),string state="iter"){return state=="done";}native function real f(real x){return(x-2.0)*(x-2.0);}record r=minimize(f,0.0,options(OutputFcn=stop_done));assert(r.converged);assert(r.status!="output_function_stop");)");
 run(R"(function integer captured_monitor(matrix x,string state,matrix data){assert(data(1)==1);figure(45);matrix n=1:numel(data);plot(n,data);return state=="iter";}matrix data=[1,2,3];callback observer=@(matrix x,record info,string state)=>captured_monitor(x,state,data);data(1)=100;native function real f(real x){return(x-2.0)*(x-2.0);}record r=minimize(f,0.0,options(OutputFcn=observer));assert(r.status=="output_function_stop");)");
 run(R"(function integer model_monitor(matrix x,record optimValues=iterationinfo(),string state="iter"){ode visual(t,real y;real a){y'=a;}if(state=="iter"){solution s=odesolve(visual,y=0.0,a=x(1),t0=0,t1=1,sample_step=0.5,method="adams");figure(44);plot(s.t,s.y);}return 0;}native function real f(real a){return(a-2.0)*(a-2.0);}record r=minimize(f,0.0,options(OutputFcn=model_monitor));assert(r.converged);)");
 run(stopFirst+R"(native function real scalar_residual(real x){return x*x-2.0;}record r=root(scalar_residual,[0,2],options(OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);assert(r.evaluations<=3);)");
 run(stopFirst+R"(native function [real a,real b] system_residual(real x,real y){return x-1.0,y-2.0;}record r=rootsolve(system_residual,[0,0],options(OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);)");
 for(const auto* algorithm:{"jde","ide","cmaes","ga"})run(stopFirst+"native function real objective(real x){return(x-0.3)*(x-0.3);}record r=evolve(objective,0.9,options(algorithm=\""+algorithm+"\",lower=-1,upper=1,population=8,max_generations=5,restarts=0,OutputFcn=stop_first));assert(r.status==\"output_function_stop\");assert(!r.converged);assert(r.generations==1);");
 run(stopFirst+R"(native function [real a,real b] objectives(real x){return x*x,(x-2.0)*(x-2.0);}record r=evolve(objectives,1.0,options(algorithm="nsga2",lower=0,upper=2,population=8,max_generations=5,OutputFcn=stop_first));assert(r.status=="output_function_stop");assert(!r.converged);)");
 const std::string plotSource=R"(
function integer live_monitor(matrix x,record optimValues=iterationinfo(),string state="iter") {
 if(optimValues.objective_available){
   figure(41,"Name","Live progress");
   plot(optimValues.history(:,2),optimValues.history(:,3));
   title("Objective evaluations");
 }
 if(state=="iter"&&optimValues.funccount>=150){return 1;}
 return 0;
}
native function real objective(real x){return sin(3.0*x)+0.01*x*x;}
figure(40);plot([0,1],[0,1]);
record result=minimize(objective,0.1,options(algorithm="crs2",lower=-5,upper=5,
   max_evaluations=500,ftol=0,ftol_abs=0,xtol=0,xtol_abs=0,
   OutputFcn=live_monitor,OutputEvery=1));
assert(result.status=="output_function_stop");assert(result.evaluations==150);
figure(42);plot([0,1],[1,0]);
)";
 const auto headless=run(plotSource);expect(headless.figureActions.size()<20,"headless result retains latest bounded observer frame");bool before=false,after=false;for(const auto& action:headless.figureActions){before=before||action.figure==40;after=after||action.figure==42;}expect(before&&after,"ordinary figures surrounding OutputFcn are preserved");
 std::size_t frames=0,actions=0;settings.figureProgress=[&](const std::vector<FigureAction>& batch){++frames;actions+=batch.size();expect(batch.size()<20,"each streamed frame stays bounded");};const auto streamed=run(plotSource);expect(frames>128&&actions>256,"long live iteration stream avoids action accumulation");expect(streamed.figureActions.size()<10,"final result excludes already streamed frames");settings.figureProgress={};
 for(const bool initialPlot:{false,true}){
  const std::string source=R"(function integer setup_monitor(matrix x,record optimValues=iterationinfo(),string state="iter"){
   if(state=="init"){figure();subplot(1,2,2);title("INIT-only title");)"+std::string(initialPlot?"plot([0,1],[0,0]);":"")+R"(}
   if(state=="iter"){plot([0,1],[x(1),x(1)+1]);}
   return state=="iter"&&optimValues.funccount>=12;
  }
  native function real f(real x){return(x-3.0)*(x-3.0);}
  figure(40);plot([0,1],[4,4]);
  record r=minimize(f,0.0,options(algorithm="nelder_mead",OutputFcn=setup_monitor));
  assert(r.status=="output_function_stop");
  figure(42);plot([0,1],[2,2]);)";
  const auto result=run(source);std::size_t newFigures=0,subplotSelections=0,titles=0,observerPlots=0;bool inObserver=false,selectedSubplot=false,hasTitle=false,contextCorrect=true,ordinaryPrefix=false,ordinarySuffix=false;
  for(const auto& action:result.figureActions){
   if(action.kind==FigureActionKind::Select){if(action.figure==40)ordinaryPrefix=true;if(action.figure==42)ordinarySuffix=true;inObserver=false;}
   if(action.kind==FigureActionKind::SelectNew){++newFigures;inObserver=true;}
   if(inObserver&&action.kind==FigureActionKind::Subplot){++subplotSelections;selectedSubplot=action.rows==1&&action.columns==2&&action.subplotIndex==2;}
   if(inObserver&&action.kind==FigureActionKind::SetTitle){++titles;hasTitle=action.text=="INIT-only title";}
   if(inObserver&&action.kind==FigureActionKind::Plot){++observerPlots;contextCorrect=contextCorrect&&selectedSubplot&&hasTitle;}
  }
  expect(newFigures==1&&subplotSelections==1&&titles==1,"headless compaction retains INIT-only anonymous figure/subplot/title once");
  expect(observerPlots==(initialPlot?2u:1u)&&contextCorrect,"deferred latest plot replays in retained INIT context and preserves initial plot");
  expect(ordinaryPrefix&&ordinarySuffix&&result.figureActions.size()<20,"INIT retention preserves ordinary surrounding figures and bounded actions");
 }
 run(R"(function integer invalid_monitor(matrix x,record optimValues=iterationinfo(),string state="iter"){return 2;}native function real f(real x){return x*x;}record r=minimize(f,1.0,options(OutputFcn=invalid_monitor));)",false);
 run(R"(function integer bad(real x){return 0;}native function real f(real x){return x*x;}record r=minimize(f,1.0,options(OutputFcn=bad));)",false);
 run(R"(native function real f(real x){return x*x;}record r=minimize(f,1.0,options(OutputEvery=2));)",false);
 run(R"(native function real f(real x){return x*x;}record r=minimize(f,1.0,options(OutputFcn=3));)",false);
 const auto recursive=run(R"(native function real f(real x){return x*x;}function integer recursive_monitor(matrix x,record optimValues=iterationinfo(),string state="iter"){record nested=minimize(f,1.0);return 0;}record r=minimize(f,1.0,options(OutputFcn=recursive_monitor));)",false);bool recursionDiagnosed=false;for(const auto& diagnostic:recursive.runtimeDiagnostics)recursionDiagnosed=recursionDiagnosed||diagnostic.code=="R2050";expect(recursionDiagnosed,"recursive solver specifically rejected by observer guard");
 const auto broken=run(R"(native function real f(real x){return x*x;}function integer broken(matrix x,record optimValues=iterationinfo(),string state="iter"){error("observer failed");return 0;}record r=minimize(f,1.0,options(OutputFcn=broken));)",false);bool originalError=false;for(const auto& diagnostic:broken.runtimeDiagnostics)originalError=originalError||diagnostic.message.find("observer failed")!=std::string::npos;expect(originalError,"original observer error survives native unwind");
#ifdef PENTA_SOURCE_DIR
 const auto path=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/iteration-callbacks/main.p";std::ifstream input(path,std::ios::binary);const std::string example((std::istreambuf_iterator<char>(input)),{});expect(!example.empty(),"OutputFcn example exists");run(example);
#endif
}
}
int main(){runtime();std::cout<<checks<<" OutputFcn checks, "<<failures<<" failures\n";return failures?1:0;}
