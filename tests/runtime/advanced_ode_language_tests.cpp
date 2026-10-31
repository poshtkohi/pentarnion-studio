#include "runtime/interpreter.hpp"
#include "runtime/data_io.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace {int checks=0,failures=0;void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}}
int main(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-advanced-ode-runtime-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directory(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache"/"odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);if(good&&!result.ok()){for(const auto& error:result.analysis.diagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';for(const auto& error:result.runtimeDiagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';}expect(result.ok()==good,source.substr(0,100));return result;};
    const std::string model="ode decay(t,real y;real rate){y'=-rate*y;}";
    run(model+"solution s=odesolve(decay,y=1.0,rate=0.7,t0=0,t1=2,sample_step=0.5,rtol=1e-10,atol=1e-12,sensitivities=\"all\");matrix S=odesens(s);assert(size(S,2)==2);assert(abs(S(end,1)+2*exp(-1.4))<1e-7);assert(abs(S(end,2)-exp(-1.4))<1e-7);assert(abs(deval(s,.137)-exp(-.7*.137))<1e-7);record R=odestats(s);assert(R.sensitivity_count==2&&R.dense_segments>0);solution trimmed=odewindow(s,.6,1.7);assert(trimmed.t(1)==1&&trimmed.t(end)==1.5);assert(size(odesens(trimmed),1)==2);");
    for(const auto* method:{"erk","esdirk"}){
        run(std::string("ode unparameterized(t,real y){y'=-y*y;}solution s=odesolve(unparameterized,y=1.0,t0=0,t1=1,sample_step=.5,method=\"")+method+"\",sensitivities=\"initial\",rtol=1e-9,atol=1e-11);matrix S=odesens(s);assert(abs(s.y(end)-.5)<1e-7);assert(abs(S(end)-.25)<1e-6);");
        run(model+"solution s=odesolve(decay,y=1.0,rate=0.7,t0=0,t1=2,sample_step=0.5,method=\""+method+"\",rtol=1e-9,atol=1e-11,sensitivities=\"all\");matrix S=odesens(s);assert(abs(S(end,1)+2*exp(-1.4))<1e-6);assert(abs(S(end,2)-exp(-1.4))<1e-6);");
        run(model+"native function real terminal(real t,real y,real rate){return 0.0*y;}native function real running(real t,real y,real rate){return y*y;}record adj=odeadjoint(decay,1.0,options(parameters=options(rate=0.7),objective=terminal,running_cost=running,t0=0,t1=2,sample_step=.5,method=\""+method+"\",rtol=1e-9,atol=1e-11));real exact=(1-exp(-2.8))/1.4;assert(abs(adj.running_cost-exact)<1e-6);assert(abs(adj.objective-exact)<1e-6);assert(abs(adj.initial_gradient(1)-2*exact)<1e-5);assert(abs(adj.parameter_gradient(1)-(4*0.7*exp(-2.8)-(1-exp(-2.8)))/(2*0.7*0.7))<1e-5);");
    }
    run(model+"native function real event(real t,real y,real rate){return y-.5;}solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=4,sample_step=.4,events=event,event_direction=-1,event_terminal=1,rtol=1e-10,atol=1e-12);record E=odeevents(s);assert(abs(E.t(1)-log(2)/.7)<1e-7);assert(E.index(1)==1&&E.direction(1)==-1&&E.terminal);assert(abs(s.y(end)-.5)<1e-8);record R=odestats(s);assert(R.event_evaluations>0&&R.terminal_event);");
    run(model+"solution s=odesolve(decay,y=exp(-1.4),rate=.7,t0=2,t1=0,sample_step=.5,rtol=1e-10,atol=1e-12);assert(abs(s.y(end)-1)<1e-7);assert(abs(deval(s,.137)-exp(-.7*.137))<1e-7);solution w=odewindow(s,1.7,.3);assert(w.t(1)==1.5&&w.t(end)==.5);");
    run(model+"solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=1,output_times=[0,.13,.47,1],mass=[2.0],max_step=.1,max_order=4);assert(length(s.t)==4&&s.t(2)==.13);assert(abs(s.y(end)-exp(-.35))<1e-6);");
    run(model+"solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=1,sensitivity_parameters=[1],sensitivity_initial=[2.0],sensitivity_method=\"staggered\",rtol=1e-10,atol=1e-12);matrix S=odesens(s);assert(abs(S(end)-exp(-.7))<1e-7);");
    run("ode coupled(t,real x,real y;real rate){x'=-rate*x;y'=rate*y;}solution s=odesolve(coupled,x=1.0,y=2.0,rate=.7,t0=0,t1=1,sample_step=.25,sensitivities=\"all\",rtol=1e-10,atol=1e-12);matrix S=odesens(s);assert(size(S,2)==6);assert(abs(S(end,1)+exp(-.7))<1e-7);assert(abs(S(end,2)-2*exp(.7))<1e-6);matrix A=odesens(s,2);matrix B=odesens(s,3);assert(abs(A(end,1)-exp(-.7))<1e-7);assert(abs(B(end,2)-exp(.7))<1e-7);");
    for(const auto& optionsText:{"events=2","event_direction=0","sensitivities=\"typo\"","sensitivity_method=\"bad\"","mass=[0]","sensitivity_parameters=[2]","sensitivity_initial=[1]","output_times=[0,.7,.4,1]","max_order=6"})run(model+"solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=1,"+optionsText+");",false);
    run(model+"native function pentarnion bad(real t,real y,real rate){return i*y;}solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=1,events=bad);",false);
    run(model+"native function real wrong(real y){return y;}solution s=odesolve(decay,y=1.0,rate=.7,t0=0,t1=1,events=wrong);",false);
    // Native dense polynomials, integrated sensitivities, backwards order and
    // event crossing metadata survive data-only persistence.
    penta::runtime::InterpreterSession session;
    auto saved=session.runFile(model+"native function real hit(real t,real y,real rate){return y-.5;}solution s=odesolve(decay,y=exp(-1.4),rate=.7,t0=2,t1=0,sample_step=.25,sensitivities=\"all\",events=hit,event_direction=-1,rtol=1e-10,atol=1e-12);save(\"advanced.pws\",\"s\");",{},options);expect(saved.ok(),"Save native advanced ODE solution");
    expect(session.submit("clear;load(\"advanced.pws\");",{},options).ok(),"Restore native advanced ODE solution");auto restored=session.submit("assert(abs(deval(s,.137)-exp(-.7*.137))<1e-7);matrix S=odesens(s);assert(size(S,1)==9&&size(S,2)==2);record E=odeevents(s);assert(length(E.t)==1&&E.direction(1)==-1);",{},options);if(!restored.ok())for(const auto& error:restored.runtimeDiagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';expect(restored.ok(),"Restored dense/sensitivity/event/backward data remain usable");
#ifdef PENTA_SOURCE_DIR
    for(const auto* name:{"main.p","adjoint.p","mass_and_times.p"}){const auto path=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/advanced-odes"/name;std::ifstream stream(path,std::ios::binary);const std::string example((std::istreambuf_iterator<char>(stream)),{});expect(!example.empty(),"advanced-odes example exists");const auto result=run(example);if(std::string(name)=="main.p"){std::size_t plots=0;for(const auto& action:result.figureActions)if(action.kind==penta::runtime::FigureActionKind::Plot)++plots;expect(plots==4,"Advanced ODE example retains four real plot data sets");}}
#endif
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" advanced ODE language checks, "<<failures<<" failures\n";return failures?1:0;
}
