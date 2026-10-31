#include "runtime/interpreter.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace penta;
namespace {
int failures = 0, checks = 0;
void expect(bool condition, const std::string& label) {
    ++checks;
    if (!condition) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
bool code(const runtime::ExecutionResult& r, const std::string& wanted) {
    for (const auto& d : r.analysis.diagnostics) if (d.code == wanted) return true;
    for (const auto& d : r.runtimeDiagnostics) if (d.code == wanted) return true;
    return false;
}
void showErrors(const runtime::ExecutionResult& r) {
    for (const auto& d : r.analysis.diagnostics) std::cerr << d.code << ": " << d.message << '\n';
    for (const auto& d : r.runtimeDiagnostics) std::cerr << d.code << ": " << d.message << '\n';
}
void output(const std::string& source, const std::vector<std::string>& expected) {
    const auto r = runtime::execute(source);
    if (!r.ok()) showErrors(r);
    expect(r.ok(), "execution succeeds: " + source);
    if (r.output != expected) {
        for (const auto& line : r.output) std::cerr << "actual: " << line << '\n';
    }
    expect(r.output == expected, "display output: " + source);
}

double realVariable(const runtime::ExecutionResult& result,const std::string& name) {
    for(const auto& variable:result.variables)if(variable.name==name && variable.type=="real")return std::stod(variable.value);
    return std::numeric_limits<double>::quiet_NaN();
}
void timerChecks() {
    {
        runtime::detail::TimerState timer;
        using Clock=runtime::detail::TimerState::Clock;
        const Clock::time_point origin{};
        double compilationTotal=5; // Builds before tic must not be subtracted.
        timer.restart(origin,compilationTotal);
        compilationTotal+=3;
        expect(timer.elapsed(origin+std::chrono::seconds(10),compilationTotal)==7,"timer subtracts only compilation after tic");
        const auto previous=timer;
        timer.restart(origin+std::chrono::seconds(11),compilationTotal);
        compilationTotal+=2; // A cancelled request still spent this time compiling.
        timer=previous;
        expect(timer.elapsed(origin+std::chrono::seconds(15),compilationTotal)==10,
            "restoring an earlier timer still excludes compiler time accrued during cancellation");
        timer.restart(origin+std::chrono::seconds(20),compilationTotal);
        compilationTotal+=4;
        expect(timer.elapsed(origin+std::chrono::seconds(30),compilationTotal)==6,"tic restarts its clock and compilation baseline together");
        timer.restart(origin,compilationTotal);
        compilationTotal+=1e-9;
        expect(timer.elapsed(origin,compilationTotal)==0,"timer clamps negative floating-point residue to zero");
        timer.reset();expect(!timer.start,"reset clears timer state independently of cumulative compilation history");
    }
    expect(code(runtime::execute("toc;"),"R1811"),"toc without tic reports the missing timer");
    expect(code(runtime::execute("real elapsed=toc();"),"R1811"),"captured toc also requires a running timer");
    auto result=runtime::execute("tic;real first=toc();real second=toc();toc;toc();real composed=toc()+1;");
    expect(result.ok() && result.output.size()==2 && result.output[0].starts_with("Elapsed time is ") && result.output[0].ends_with(" seconds."),
        "standalone toc forms print elapsed wall time");
    expect(realVariable(result,"first")>=0 && realVariable(result,"second")>=realVariable(result,"first") &&
        realVariable(result,"composed")>=1,"captured toc is finite nonnegative monotonic and composes as real arithmetic");
    result=runtime::execute("tic();real elapsed=toc();");
    expect(result.ok() && result.output.empty(),"assigned toc call does not print its own result");
    runtime::InterpreterSession session;
    expect(session.submit("tic").ok(),"console starts the timer using optional final semicolon");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    result=session.submit("real before=toc();");const auto before=realVariable(result,"before");
    expect(result.ok() && before>=0,"console captures a timer started by an earlier command");
    expect(session.submit("clear all;clc;").ok(),"clearing console and variables succeeds with a running timer");
    result=session.submit("real after=toc();");
    expect(result.ok() && realVariable(result,"after")>=before,"clear and clc preserve the timer without restarting it");
    expect(session.submit("tic();real restarted=toc();").ok(),"tic restarts an already running default timer");
    expect(!session.runFile("real broken=i;").ok() && session.submit("real kept=toc();").ok(),"a static checking error never resets or measures the running timer");
    expect(session.runFile("real file_value=1;").ok() && code(session.submit("toc;"),"R1811"),"fresh Run File resets previous console timer");
    expect(session.runFile("tic;real file_elapsed=toc();").ok() && session.submit("toc;").ok(),"a timer explicitly started by a file remains available to its console workspace");
    session.reset();
    expect(code(session.submit("toc;"),"R1811"),"Reset Workspace clears the timer");
    runtime::ExecutionOptions cancelled;cancelled.cancellation=std::make_shared<std::atomic_bool>(true);
    result=session.submit("tic;",{},cancelled);
    expect(result.status==runtime::ExecutionStatus::Cancelled && code(session.submit("toc;"),"R1811"),"cancelled request cannot create a timer");
}

void nativeOdeIntegrationChecks() {
    using runtime::FigureActionKind;
    const auto cache=std::filesystem::temp_directory_path()/(
        "pentarnion-runtime-ode-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(cache);
    runtime::ExecutionOptions execution;execution.odeCacheDirectory=cache;
    const std::string model="ode decay(t,pentarnion p;real a){p'=-a*p;} ";
    auto result=runtime::execute(model,{},execution);
    expect(result.ok() && std::filesystem::is_empty(cache),"defining a native model never compiles or integrates it");
    result=runtime::execute(model+"solution s=odesolve(decay,p=i,a=1,t0=0,t1=1);display(s.unknown);",{},execution);
    expect(result.status==runtime::ExecutionStatus::StaticError && std::filesystem::is_empty(cache),"whole-source static errors prevent native compilation/cache writes");
    const std::string solve="solution s=odesolve(decay,p=1+i,a=.2,t0=0,t1=1,sample_step=.1,method=\"bdf\",jacobian=\"analytic\",rtol=1e-9,atol=1e-11);";
    runtime::InterpreterSession compilationTimer;
    const auto wallStart=std::chrono::steady_clock::now();
    result=compilationTimer.runFile("tic;"+model+solve+"real measured=toc();display(s);subplot(1,2,1);plot(s.t,coeff(s.p,0));subplot(1,2,2);plot3(\"i\",\"j\",\"ell\",s.p);",{},execution);
    const auto wallSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wallStart).count();
    if(!result.ok())showErrors(result);
    expect(result.ok() && result.figureActions.size()==4,"native solve feeds time and Pentarnion trajectory subplots");
    expect(result.output.size()==1 && result.output[0].find("native cache miss")!=std::string::npos &&
        result.output[0].find("jacobian=analytic")!=std::string::npos,"solution display identifies first native compilation and generated Jacobian mode");
    expect(realVariable(result,"measured")>=0 && realVariable(result,"measured")+.01<wallSeconds,
        "first cache-miss toc excludes compiler-process time while native execution remains measured");
    expect(compilationTimer.submit("tic;").ok(),"timer restarts after a compiler exclusion");
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    expect(realVariable(compilationTimer.submit("real restarted_after_compile=toc();"),"restarted_after_compile")>=.001,
        "restart does not retain the previous compilation subtraction");
    std::string firstStatus=result.output.empty()?"":result.output[0];
    if(result.figureActions.size()==4) {
        const auto& a=result.figureActions;
        expect(a[0].kind==FigureActionKind::Subplot && a[0].rows==1 && a[0].columns==2 && a[0].subplotIndex==1 &&
            a[2].subplotIndex==2,"subplot actions preserve one-based row-major selection");
        expect(a[1].kind==FigureActionKind::Plot && a[1].axes[0]==-1 && a[1].vectors.empty() && a[1].curves.size()==1,
            "time curve has generic axes and sampled points rather than arrows");
        if(a[1].curves.size()==1) {
            const auto& points=a[1].curves[0].points;
            expect(points.size()==11 && points.front()[0]==0 && points.front()[1]==1 && std::abs(points.back()[0]-1)<1e-12 &&
                std::abs(points.back()[1]-std::exp(-.2))<1e-6,"compiled native decay agrees with independent exponential at stored sample times");
        }
        expect(a[3].dimensions==3 && a[3].axes==std::array<int,3>{{1,2,4}} && a[3].curves.size()==1,
            "trajectory projection preserves canonical i/j/ell axes");
        if(a[3].curves.size()==1) expect(std::abs(a[3].curves[0].points.back()[0]-std::exp(-.2))<1e-6 &&
            a[3].curves[0].points.back()[1]==0 && a[3].curves[0].points.back()[2]==0,"trajectory uses actual sampled five-coefficient state data");
    }
    result=runtime::execute(model+"solution s=odesolve(decay,p=2+j,a=.3,t0=0,t1=.5,sample_step=.05,method=\"adams\",jacobian=\"finite_difference\",rtol=1e-7,atol=1e-9);display(s);",{},execution);
    if(!result.ok())showErrors(result);
    expect(result.ok() && result.output.size()==1 && result.output[0].find("native cache hit")!=std::string::npos &&
        result.output[0].find("jacobian=finite_difference")!=std::string::npos,
        "parameter/initial/interval/sampling/method/tolerance/Jacobian edits reuse compiled native kernel");
    const auto keyFrom=[](const std::string& status){const auto start=status.find("key=");return start==std::string::npos?"":status.substr(start+4,status.find(',',start)-start-4);};
    const auto firstKey=keyFrom(firstStatus);
    expect(!firstKey.empty() && result.output.size()==1 && keyFrom(result.output[0])==firstKey,"cache identity is structural rather than source-file settings");
    for(const auto* method:{"erk","esdirk"}) {
        result=runtime::execute(model+"solution s=odesolve(decay,p=2+j,a=.3,t0=0,t1=.5,sample_step=.05,method=\""+method+"\",jacobian=\"analytic\",rtol=1e-9,atol=1e-11);display(s);plot(s.t,coeff(s.p,2));",{},execution);
        if(!result.ok())showErrors(result);
        expect(result.ok()&&result.output.size()==1&&result.output[0].find(std::string("method=")+method)!=std::string::npos,
            "new ARKODE method is accepted and exposed by the language runtime");
        expect(result.output.size()==1&&result.output[0].find("native cache hit")!=std::string::npos&&keyFrom(result.output[0])==firstKey,
            "ARKODE selection reuses the existing CVODE compiled Pentarnion kernel");
        expect(result.figureActions.size()==1&&std::abs(result.figureActions[0].curves[0].points.back()[1]-std::exp(-.15))<2e-7,
            "ARKODE sampled Pentarnion output feeds actual component plots");
        expect(result.output.size()==1&&result.output[0].find(std::string("jacobian=")+(std::string(method)=="erk"?"not_used":"analytic"))!=std::string::npos,
            "ARKODE summary distinguishes explicit no-Jacobian work from implicit analytic Jacobian");
    }
    result=runtime::execute("ode decay(t,pentarnion p;real a){p'=-a*p+p*0;}"+solve+"display(s);",{},execution);
    expect(result.ok() && result.output.size()==1 && result.output[0].find("native cache miss")!=std::string::npos &&
        keyFrom(result.output[0])!=firstKey,"changing the ordered RHS structure recompiles a distinct kernel");
    result=runtime::execute("ode oscillator(t,real x,real y){x'=y;y'=-x;}solution s=odesolve(oscillator,x=1,y=0,t0=0,t1=.1,sample_step=.1);plot(s.t,s.x);",{},execution);
    expect(result.ok() && result.figureActions.size()==1 && std::abs(result.figureActions[0].curves[0].points.back()[1]-std::cos(.1))<1e-6,
        "multiple real ODE states bind and expose named real series");
    runtime::InterpreterSession session;
    expect(session.submit(model,{},execution).ok(),"console retains native model definitions");
    result=session.submit(solve,{},execution);
    expect(result.ok(),"console solves a previously defined model");
    result=session.submit("plot(s.t,coeff(s.p,1));",{},execution);
    expect(result.ok() && result.figureActions.size()==1,"console retains typed solution schema and sampled data");
    expect(session.submit("clear decay;",{},execution).ok() && session.submit("display(s);",{},execution).ok(),
        "solution remains valid after clearing its model binding");
    expect(!session.submit("solution other=odesolve(decay,p=i,a=1,t0=0,t1=1);",{},execution).ok(),"cleared model cannot be solved again");
    result=runtime::execute(model+"real dt=-.1;solution s=odesolve(decay,p=i,a=1,t0=0,t1=1,sample_step=dt);",{},execution);
    expect(result.status==runtime::ExecutionStatus::RuntimeError && result.figureActions.empty(),"dynamic invalid solve settings fail without interpreter fallback");
    result=runtime::execute("integer index=3;subplot(1,2,index);",{},execution);
    expect(code(result,"R1012") && result.figureActions.empty(),"dynamic subplot index is checked before action emission");
    runtime::ExecutionOptions disabled=execution;disabled.allowFigures=false;
    result=runtime::execute("subplot(1,1,1);",{},disabled);
    expect(code(result,"R1010"),"headless CLI explicitly rejects subplot controls");
    runtime::ExecutionOptions cancelled=execution;cancelled.cancellation=std::make_shared<std::atomic_bool>(true);
    result=runtime::execute(model+solve,{},cancelled);
    expect(result.status==runtime::ExecutionStatus::Cancelled && result.figureActions.empty(),"pre-cancelled native solve never runs");
    runtime::ExecutionOptions solveBudget=execution;solveBudget.maxOdeSolves=1;
    result=runtime::execute(model+solve+"solution second=odesolve(decay,p=i,a=1,t0=0,t1=.1);",{},solveBudget);
    expect(code(result,"R1005") && std::any_of(result.variables.begin(),result.variables.end(),[](const auto& x){return x.name=="s";}),
        "per-request native solve budget preserves earlier completed solution");
    runtime::InterpreterSession retained;
    runtime::ExecutionOptions sampleBudget=execution;sampleBudget.maxSolutionCoordinates=55;
    expect(retained.submit(model+solve,{},sampleBudget).ok(),"workspace accepts exactly55 retained state coordinates without charging time samples");
    expect(retained.submit("solution alias=s;",{},sampleBudget).ok(),"solution aliases share retained-coordinate budget");
    const std::string replace="s=odesolve(decay,p=2*i,a=.2,t0=0,t1=1,sample_step=.1);";
    result=retained.submit(replace,{},sampleBudget);
    expect(code(result,"R1005"),"replacement cannot evict an old solution still retained by another alias");
    result=retained.submit("plot(s.t,coeff(s.p,1));",{},sampleBudget);
    expect(result.ok() && result.figureActions[0].curves[0].points.front()[1]==1,"failed solution assignment preserves its previous numerical data");
    expect(retained.submit("clear alias;",{},sampleBudget).ok() && retained.submit(replace,{},sampleBudget).ok(),
        "replacement excludes the overwritten target when no alias retains its previous data");
    result=retained.submit("plot(s.t,coeff(s.p,1));",{},sampleBudget);
    expect(result.ok() && result.figureActions[0].curves[0].points.front()[1]==2,"successful replacement exposes its new native initial state");
    runtime::ExecutionOptions pointBudget=execution;pointBudget.maxPlotPoints=10;
    const std::string sixSamples=model+"solution s=odesolve(decay,p=i,a=.2,t0=0,t1=.5,sample_step=.1);";
    result=runtime::execute(sixSamples+"figure(1);plot(s.t,coeff(s.p,1));plot2(\"i\",\"j\",s.p);",{},pointBudget);
    expect(code(result,"R1005") && result.figureActions.size()==2 && result.figureActions[1].curves.size()==1 &&
        result.figureActions[1].curves[0].points.size()==6,"XY and trajectory plots share one aggregate request point budget");
    expect(std::any_of(result.variables.begin(),result.variables.end(),[](const auto& x){return x.name=="s";}),
        "point budget failure preserves earlier completed solution workspace");
    result=runtime::execute(sixSamples+"figure(1);plot3(\"i\",\"j\",\"k\",s.p,s.p);",{},pointBudget);
    expect(code(result,"R1005") && result.figureActions.size()==1,"multi-curve plot is not partially published when its later curve exceeds request budget");
    pointBudget.maxPlotPoints=0;
    result=runtime::execute("plot2(\"i\",\"j\",i,j);",{},pointBudget);
    expect(result.ok() && result.figureActions[0].vectors.size()==2 && result.figureActions[0].curves.empty(),
        "sampled-point budget does not charge legacy vector arrows");
    expect(retained.submit("tic;",{},execution).ok(),"timer cancellation test starts previous timer");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const double previousElapsed=realVariable(retained.submit("real old_elapsed=toc();",{},execution),"old_elapsed");
    runtime::ExecutionOptions stop=execution;stop.cancellation=std::make_shared<std::atomic_bool>(false);
    std::thread stopper([flag=stop.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(20));flag->store(true);});
    result=retained.submit("tic;display(\"timer restarted\");solution cancelled_solution=odesolve(decay,p=i,a=.2,t0=0,t1=8999.99,sample_step=.01);",{},stop);
    stopper.join();
    expect(result.status==runtime::ExecutionStatus::Cancelled && !result.output.empty() && result.output[0]=="timer restarted",
        "native cancellation occurs after the new tic has executed");
    result=retained.submit("real restored=toc();",{},execution);
    expect(result.ok() && realVariable(result,"restored")>=previousElapsed,"cancellation restores the previous timer along with workspace state");
}

void figureLayoutChecks() {
    using runtime::FigureActionKind;
    auto result = runtime::execute(R"(use_algebra("A_gamma",gamma=.5);
        figure("Name","Phase α","NumberTitle","off"); figure(3,"NumberTitle","ON","NAME","");
        figure(3); undock(3); dock(3); tilefigures(); tilefigures("desktop",2,3);
        title("Phase $\\alpha^2$"); xlabel("$i$","Interpreter","none"); ylabel("j"); zlabel("k");
        caption("α 😀 \"quote\"\nnext","interpreter","LaTeX");)");
    if (!result.ok()) showErrors(result);
    expect(result.ok() && result.figureActions.size() == 12, "figure layout and annotation request executes in source order");
    if (result.figureActions.size() == 12) {
        const auto& a = result.figureActions;
        expect(a[0].kind == FigureActionKind::SelectNew && a[0].name == "Phase α" && a[0].numberTitle == false,
            "new figure emits name and NumberTitle properties");
        expect(a[1].kind == FigureActionKind::Select && a[1].figure == 3 && a[1].name == "" && a[1].numberTitle == true,
            "existing figure emits explicit empty name and enabled NumberTitle");
        expect(a[2].kind == FigureActionKind::Select && a[2].figure == 3 && !a[2].name && !a[2].numberTitle,
            "ordinary select emits no property reset");
        expect(a[3].kind == FigureActionKind::Undock && a[3].figure == 3 &&
            a[4].kind == FigureActionKind::Dock && a[4].figure == 3, "docking events preserve identifiers and order");
        expect(a[5].kind == FigureActionKind::TileStudio && a[5].rows == 0 && a[5].columns == 0 &&
            a[6].kind == FigureActionKind::TileDesktop && a[6].rows == 2 && a[6].columns == 3,
            "automatic and explicit grids stay distinct without renderer state");
        expect(a[7].kind == FigureActionKind::SetTitle && a[7].text == "Phase $\\alpha^2$" && a[7].latex &&
            a[8].kind == FigureActionKind::SetXLabel && a[8].text == "$i$" && !a[8].latex &&
            a[9].kind == FigureActionKind::SetYLabel && a[10].kind == FigureActionKind::SetZLabel &&
            a[11].kind == FigureActionKind::SetCaption && a[11].text == "α 😀 \"quote\"\nnext" && a[11].latex,
            "annotation events retain decoded Unicode/math text and per-label interpreter mode");
        expect(a[7].figure == 0 && a[7].algebraId == "A_gamma_v1" && a[7].gamma == .5,
            "annotations target GUI current figure and retain algebra context");
    }
    result = runtime::execute(R"(integer rows=2; integer cols=3;tilefigures("studio",rows+1,cols-1);)");
    expect(result.ok() && result.figureActions.size() == 1 && result.figureActions[0].rows == 3 &&
        result.figureActions[0].columns == 2, "integer grid expressions evaluate at runtime");
    result = runtime::execute(R"(integer n=64;tilefigures("desktop",n,1);)");
    expect(result.ok() && result.figureActions[0].rows == 64, "maximum 64-cell grid remains executable");
    for (const auto* source : {R"(integer n=0;tilefigures("studio",n,1);)",
         R"(integer n=65;tilefigures("desktop",1,n);)",R"(integer n=9;tilefigures("studio",8,n);)",
         R"(integer n=9223372036854775807;tilefigures("studio",n,n);)"}) {
        result = runtime::execute(source);
        expect(code(result,"R1012") && result.figureActions.empty(), "dynamic grid bounds reject complete offending action safely");
    }
    for (const auto* source : {"integer n=0;dock(n);", "integer n=65;undock(n);"}) {
        result = runtime::execute(source);
        expect(code(result,"R1011") && result.figureActions.empty(), "dynamic docking identifier bounds remain enforced");
    }
    runtime::ExecutionOptions disabled; disabled.allowFigures = false;
    for (const auto* source : {"dock(1);","undock(1);","tilefigures();",R"(tilefigures("desktop");)",
         R"(figure("Name","name");)",R"(title("x");)",R"(xlabel("x");)",R"(ylabel("x");)",R"(zlabel("x");)",R"(caption("x");)"}) {
        result = runtime::execute(source,{},disabled);
        expect(code(result,"R1010") && result.figureActions.empty(), "CLI-disabled figure mode covers every new command");
    }
    result = runtime::execute(R"(integer n=0;dock(n);)",{},disabled);
    expect(code(result,"R1010") && !code(result,"R1011"), "disabled mode is diagnosed before dynamic argument evaluation");
    runtime::ExecutionOptions budget; budget.maxFigureActions = 2;
    result = runtime::execute(R"(dock(1);title("ok");tilefigures();)",{},budget);
    expect(code(result,"R1005") && result.figureActions.size() == 2 && result.figureActions[1].kind == FigureActionKind::SetTitle,
        "docking/text/layout events share one ordered action budget");
    result = runtime::execute(R"(figure(1);title("ok");integer n=0;tilefigures("studio",n,1);)");
    expect(code(result,"R1012") && result.figureActions.size() == 2, "runtime layout error retains only earlier complete actions");
    result = runtime::execute(R"(figure(1);title("ok");tilefigures("studio",8,9);)");
    expect(result.status == runtime::ExecutionStatus::StaticError && result.figureActions.empty(),
        "static invalid grid prevents all preceding figure effects");
    runtime::ExecutionOptions cancelled;
    cancelled.cancellation = std::make_shared<std::atomic_bool>(true);
    result = runtime::execute(R"(figure("Name","x");dock(1);title("x");tilefigures();)",{},cancelled);
    expect(result.status == runtime::ExecutionStatus::Cancelled && result.figureActions.empty(),
        "cancelled layout requests emit no events");
    runtime::InterpreterSession session;
    result = session.submit(R"(title("Command $i$");undock(1);tilefigures("desktop"))");
    expect(result.ok() && result.figureActions.size() == 3, "console convenience semicolon supports new figure commands");
}

void figureAndFunctionChecks() {
    using runtime::FigureActionKind;
    auto result = runtime::execute("use_algebra(\"A_gamma\",gamma=.5); pentarnion p=pent([1,2,3,4,5]); "
        "figure(); figure(3); plot2(\"1\",\"ell\",p); clf; close(3); close(\"all\");");
    expect(result.ok() && result.figureActions.size() == 6, "ordered figure command stream succeeds");
    if (result.figureActions.size() == 6) {
        const auto& actions = result.figureActions;
        expect(actions[0].kind == FigureActionKind::SelectNew && actions[1].kind == FigureActionKind::Select &&
            actions[1].figure == 3 && actions[2].kind == FigureActionKind::Plot && actions[3].kind == FigureActionKind::Clear &&
            actions[4].kind == FigureActionKind::Close && actions[4].figure == 3 && actions[5].kind == FigureActionKind::CloseAll,
            "new/select/plot/clear/close order is preserved");
        expect(actions[2].dimensions == 2 && actions[2].axes[0] == 0 && actions[2].axes[1] == 4,
            "selected canonical axes survive runtime without projection");
        expect(actions[2].vectors.size() == 1 && actions[2].vectors[0].coefficients == std::array<double,5>{{1,2,3,4,5}} &&
            actions[2].vectors[0].label == "p", "plot payload preserves all five unscaled coefficients and reference label");
        expect(actions[2].gamma == .5 && actions[2].algebraId == "A_gamma_v1", "immutable algebra context accompanies plot payload");
    }
    result = runtime::execute("plot3(\"i\",\"j\",\"k\",i,j,k);");
    expect(result.ok() && result.figureActions.size() == 1 && result.figureActions[0].dimensions == 3 &&
        result.figureActions[0].vectors.size() == 3, "3D plot emits three typed vectors");
    result = runtime::execute("figure(1); plot2(\"i\",\"i\",i);");
    expect(result.status == runtime::ExecutionStatus::StaticError && result.figureActions.empty(), "whole-file validation precedes every figure action");
    result = runtime::execute("figure(2);plot2(\"i\",\"j\",i);integer zero=0;real bad=1/zero;close(\"all\");");
    expect(code(result,"R1001") && result.figureActions.size() == 2, "ordinary runtime failure preserves earlier figure actions only");
    runtime::ExecutionOptions disabled;
    disabled.allowFigures = false;
    for (const auto* source : {"figure();", "plot2(\"i\",\"j\",i);", "clf;", "close(1);", "close(\"all\");"}) {
        result = runtime::execute(source, {}, disabled);
        expect(code(result,"R1010") && result.figureActions.empty(), "disabled execution mode diagnoses figure command instead of ignoring it");
    }
    runtime::ExecutionOptions budget;
    budget.maxFigureActions = 2;
    result = runtime::execute("figure(1);clf;close(1);", {}, budget);
    expect(code(result,"R1005") && result.figureActions.size() == 2, "figure action budget preserves already-emitted events");
    budget = {}; budget.maxPlotVectors = 1;
    result = runtime::execute("plot2(\"i\",\"j\",i,j);", {}, budget);
    expect(code(result,"R1005") && result.figureActions.empty(), "per-plot vector budget rejects the complete offending plot");
    result = runtime::execute("plot2(\"i\",\"j\",i);plot2(\"i\",\"j\",j);", {}, budget);
    expect(result.ok() && result.figureActions.size() == 2, "vector budget is per plot rather than silently global");
    result = runtime::execute("integer n=65;figure(n);");
    expect(code(result,"R1011") && result.figureActions.empty(), "dynamic figure identifiers are range checked");
    std::string actions;
    for (int index = 0; index < 257; ++index) actions += "figure(1);";
    result = runtime::execute(actions);
    expect(code(result,"R1005") && result.figureActions.size() == 256, "hard figure action ceiling bounds output data");
    std::string vectors = "plot2(\"i\",\"j\"";
    for (int index = 0; index < 256; ++index) vectors += ",i";
    result = runtime::execute(vectors + ");");
    expect(result.ok() && result.figureActions[0].vectors.size() == 256, "maximum valid plot size remains executable");
    result = runtime::execute(vectors + ",i);");
    expect(code(result,"E1703") && result.figureActions.empty(), "static vector ceiling prevents oversized payloads");
    {
        runtime::InterpreterSession session;
        expect(session.submit("integer n=7;").ok(), "cancel regression initial workspace");
        std::string busy = "integer n=99;figure(1);dock(1);title(\"temporary\");tilefigures();display(";
        for (int index = 0; index < 20000; ++index) {
            if (index) busy += ',';
            busy += "i^9223372036854775807";
        }
        busy += ");";
        runtime::ExecutionOptions cancellable;
        cancellable.cancellation = std::make_shared<std::atomic_bool>(false);
        std::thread stop([flag=cancellable.cancellation] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            flag->store(true, std::memory_order_relaxed);
        });
        result = session.runFile(busy, {}, cancellable);
        stop.join();
        expect(result.status == runtime::ExecutionStatus::Cancelled && result.figureActions.empty(), "cancelled requests return no figure effects");
        expect(session.submit("n").output == std::vector<std::string>{"7"}, "cancelled plotting request restores previous workspace");
        expect(session.submit("figure(4)").figureActions.size() == 1, "figure selection is emitted by console session");
        expect(session.submit("clear all").figureActions.empty(), "clear variables does not close or clear plots");
        expect(session.submit("clc").figureActions.empty(), "clc does not close or clear plots");
        session.reset();
        result = session.submit("plot2(\"i\",\"j\",i)");
        expect(result.ok() && result.figureActions.size() == 1 && result.figureActions[0].kind == FigureActionKind::Plot,
            "workspace reset emits no implicit figure reset or selection");
    }
    output("display(sin(0),cos(0),exp(0),sinh(0),cosh(0));", {"0 1 1 0 1"});
    const double a = 1.0, radius = 2.0;
    struct FunctionExpected { const char* name; double scalar, factor; };
    const FunctionExpected functions[]{
        {"sin",std::sin(a)*std::cosh(radius),std::cos(a)*std::sinh(radius)/radius},
        {"cos",std::cos(a)*std::cosh(radius),-std::sin(a)*std::sinh(radius)/radius},
        {"exp",std::exp(a)*std::cos(radius),std::exp(a)*std::sin(radius)/radius},
        {"sinh",std::sinh(a)*std::cos(radius),std::cosh(a)*std::sin(radius)/radius},
        {"cosh",std::cosh(a)*std::cos(radius),std::sinh(a)*std::sin(radius)/radius}
    };
    for (const auto& function : functions) {
        result = runtime::execute("pentarnion p=1+i+j+k+ell;plot2(\"1\",\"i\"," + std::string(function.name) + "(p));");
        expect(result.ok() && result.figureActions.size() == 1, "Pentarnion elementary overload can feed plotting");
        if (result.figureActions.size() == 1) {
            const auto& coefficients = result.figureActions[0].vectors[0].coefficients;
            expect(std::abs(coefficients[0] - function.scalar) < 1e-13, "elementary scalar component follows one-element formula");
            for (std::size_t index = 1; index < 5; ++index)
                expect(std::abs(coefficients[index] - function.factor) < 1e-13, "elementary pure coefficients share quadratic-subalgebra factor");
        }
    }
    output("display(tan(0),asin(0),acos(1),atan(0),tanh(0),asinh(0),acosh(1),atanh(0),log(1),log10(100),log2(8),abs(-2),floor(1.7),ceil(1.2),round(2.5),round(-2.5));",
           {"0 0 0 0 0 0 0 0 0 2 3 2 1 2 3 -3"});
    result = runtime::execute("display(atan2(1,0));");
    expect(result.ok() && std::abs(std::stod(result.output.front()) - std::acos(-1.0)/2) < 1e-14, "atan2 argument order and radians follow std convention");
    for (const auto* source : {"display(asin(2));", "display(acos(-2));", "display(acosh(.5));", "display(atanh(1));",
        "display(atanh(-1));", "display(log(0));", "display(log10(-1));", "display(log2(0));", "display(exp(1000));", "display(cosh(1000));"}) {
        result = runtime::execute(source);
        expect(code(result,"R1001") && result.output.empty(), "real function domain/overflow failures are explicit");
    }
    result = runtime::execute("display(tan(i));");
    expect(result.status == runtime::ExecutionStatus::StaticError && code(result,"E1204"), "real-only functions do not reinterpret Pentarnion coefficients");
    result = runtime::execute("display(cosh(pent([1000,0,0,0,0])));");
    expect(code(result,"R1001") && !result.runtimeDiagnostics.empty() && result.runtimeDiagnostics[0].span.offset == 8,
        "elementary AlgebraError retains the precise call span");
}
}
int main() {
    timerChecks();
    nativeOdeIntegrationChecks();
    figureLayoutChecks();
    figureAndFunctionChecks();
    output("real x=1/2; display(x);", {"0.5"});
    output("integer x=2; x=x*3+4; display(\"x\",x);", {"x 10"});
    output("display(\"\",1); display(\"a\\nb\");", {" 1", "a\nb"});
    output("pentarnion p=1+i; p=p+j; display(p);", {"1 + i + j"});
    output("display(i*i); display(i*j);", {"-1", "0.707106781186547*k + 0.707106781186547*ell"});
    output("display((i*j)*j); display(i*(j*j));", {"0.5*k - 0.5*ell", "-i"});
    output("display((i*inv(j))*j);", {"-0.5*k + 0.5*ell"});
    output("display(conj(1+i)); display(scalar(2+i)); display(pure(2+i));", {"1 - i", "2", "i"});
    output("display(coeff(1+2*i,1)); display(norm2(1+2*i));", {"2", "5"});
    output("display(commutator(i,i)); display(associator(i,j,j));", {"0", "i + 0.5*k - 0.5*ell"});
    output("display(i^0); display(i^2); display(i^-1); display(-i^2);", {"1", "-1", "-i", "1"});
    output("display(2^3^2); display(2^-2); display(2\\8);", {"512", "0.25", "4"});
    output("integer n=-9223372036854775808; display(n);", {"-9223372036854775808"});
    output("integer n=-0009223372036854775808; display(n);", {"-9223372036854775808"});
    output("integer range r=0:1:9223372036854775807; display(r);", {"0:1:9223372036854775807"});
    output("real range r=1:1:3; display(r);", {"1:1:3"});
    output(";use_algebra(\"A_gamma\",gamma=0);display(i*j);display(ell*ell);", {"k", "-1"});
    output("real d=1e-320; pentarnion p=pent([1e-320,0,0,0,0]); display(p/d);", {"1"});
    {
        auto r = runtime::execute("pentarnion x=i/j; display(norm2(x*j-i));");
        expect(r.ok(), "equation right division succeeds");
        expect(!r.output.empty() && std::stod(r.output.front()) < 1e-26, "right quotient solves x*j=i");
    }
    {
        auto r = runtime::execute("display(\"no execution\"); real x=i;");
        expect(r.status == runtime::ExecutionStatus::StaticError && r.output.empty(), "whole-file checking before display");
        expect(code(r,"E1204"), "type mismatch code retained");
    }
    {
        auto r = runtime::execute("display(i*j*j);");
        expect(code(r,"E1301") && r.output.empty(), "unparenthesised triple rejected before evaluation");
    }
    {
        auto r = runtime::execute("real z=0;display(\"before\");real x=1/z;display(\"after\");");
        expect(r.status == runtime::ExecutionStatus::RuntimeError && code(r,"R1001"), "dynamic division zero stops execution");
        expect(r.output == std::vector<std::string>{"before"}, "partial display output retained on runtime failure");
        expect(!r.runtimeDiagnostics.empty() && r.runtimeDiagnostics.front().span.offset > 0, "runtime error retains expression source span");
    }
    for (const auto& source : {"integer x=9223372036854775807; x=x+1;", "integer x=-9223372036854775808; x=-x;",
                              "integer x=-9223372036854775808; x=x*(-1);", "integer x=-9223372036854775808; x=x-1;"}) {
        auto r = runtime::execute(source);
        expect(code(r,"R1002"), "checked integer overflow");
    }
    {
        auto r = runtime::execute("real x=-1;display(sqrt(x));");
        expect(code(r,"R1001"), "sqrt domain failure");
        r = runtime::execute("integer n=5;display(coeff(i,n));");
        expect(code(r,"R1004"), "runtime coefficient bounds");
        r = runtime::execute("integer z=0;integer range r=1:z:3;");
        expect(code(r,"R1008"), "runtime range zero step");
        r = runtime::execute("display(inv(pent([0,0,0,0,0])));");
        expect(!r.ok() && !r.runtimeDiagnostics.empty(), "zero reciprocal failure");
    }
    {
        runtime::ExecutionOptions bounded;
        bounded.maxOperations = 1;
        auto r = runtime::execute("integer x=1;", {}, bounded);
        expect(code(r,"R1005"), "operation budget");
        bounded.maxOperations = 100;
        bounded.maxOutputBytes = 2;
        r = runtime::execute("display(\"long\");", {}, bounded);
        expect(code(r,"R1005") && r.output.empty(), "output budget");
        bounded.cancellation = std::make_shared<std::atomic_bool>(true);
        r = runtime::execute("integer x=1;", {}, bounded);
        expect(r.status == runtime::ExecutionStatus::Cancelled, "pre-cancelled source never runs");
    }
    {
        language::AnalysisOptions old;
        old.languageVersion = "0.1";
        auto r = runtime::execute("real x=1;", old);
        expect(code(r,"R1007"), "old editor profile cannot silently execute");
    }
    {
        runtime::InterpreterSession session;
        auto r = session.submit("real x=2");
        expect(r.ok() && r.output == std::vector<std::string>{"x = 2"}, "console declaration echoes without semicolon");
        r = session.submit("x+3");
        expect(r.ok() && r.output == std::vector<std::string>{"5"}, "console remembers variables");
        r = session.submit("x=7;");
        expect(r.ok() && r.output.empty(), "console semicolon suppresses echo");
        r = session.submit("x+1 % trailing comment");
        expect(r.ok() && r.output == std::vector<std::string>{"8"}, "automatic semicolon precedes trailing comment");
        r = session.submit("display(\"x\",x)");
        expect(r.ok() && r.output == std::vector<std::string>{"x 7"}, "display always prints once");
        r = session.submit("real z=0; x=x/z;");
        expect(code(r,"R1001"), "console runtime error");
        r = session.submit("x");
        expect(r.ok() && r.output == std::vector<std::string>{"7"}, "failed assignment preserves previous variable");
        auto cancelled = runtime::ExecutionOptions{};
        cancelled.cancellation = std::make_shared<std::atomic_bool>(true);
        r = session.submit("x=999;", {}, cancelled);
        expect(r.status == runtime::ExecutionStatus::Cancelled, "cancelled console request");
        r = session.submit("x");
        expect(r.ok() && r.output == std::vector<std::string>{"7"}, "cancel preserves prior workspace");
        r = session.submit("use_algebra(\"A_gamma\",gamma=0);");
        expect(code(r,"R1009"), "context changes require clearing a nonempty workspace");
        r = session.runFile("real y=9;display(y);");
        expect(r.ok() && r.output == std::vector<std::string>{"9"}, "Run File produces fresh scope");
        expect(!session.submit("x").ok(), "previous file workspace is not imported");
        expect(session.submit("y").output == std::vector<std::string>{"9"}, "console sees most recent file values");
        expect(!session.runFile("real broken=i;").ok(), "bad file has no execution");
        expect(session.submit("y").output == std::vector<std::string>{"9"}, "static error preserves command workspace");
        session.reset();
        expect(session.submit("use_algebra(\"A_gamma\",gamma=0);").ok(), "empty console may select context");
        expect(session.submit("i*j").output == std::vector<std::string>{"k"}, "console context persists");
        session.reset();
        expect(session.submit("i*j").output != std::vector<std::string>{"k"}, "Clear Workspace restores caller defaults");
    }
    {
        runtime::InterpreterSession session;
        runtime::ExecutionOptions bounded;
        bounded.maxWorkspaceVariables = 1;
        expect(session.submit("integer x=1;", {}, bounded).ok(), "first workspace slot");
        expect(code(session.submit("integer y=2;", {}, bounded), "R1005"), "workspace variable budget");
        expect(session.submit("x").output == std::vector<std::string>{"1"}, "workspace preserved after limit");
    }
    {
        runtime::InterpreterSession session;
        expect(session.submit("real p=2; real q=3;").ok(), "clear test workspace");
        auto r = session.submit("clc");
        expect(r.ok() && r.clearConsole && r.output.empty(), "clc requests transcript clear without output");
        expect(session.submit("p+q").output == std::vector<std::string>{"5"}, "clc preserves variables");
        expect(session.submit("clear p").ok(), "clear selected variable");
        expect(!session.submit("p").ok(), "cleared variable is undeclared");
        expect(session.submit("q").output == std::vector<std::string>{"3"}, "clear keeps other variables");
        expect(session.submit("clear missing").ok(), "clearing missing variable is a harmless no-op");
        expect(!session.submit("clear i").ok(), "immutable basis cannot be cleared");
        expect(session.submit("clear all").ok(), "clear all variables");
        expect(!session.submit("q").ok(), "clear all empties user bindings");
        expect(session.submit("use_algebra(\"A_gamma\",gamma=0);").ok(), "select console context after clear");
        expect(session.submit("real n=1; clear;").ok(), "bare clear command");
        expect(session.submit("i*j").output == std::vector<std::string>{"k"}, "clear preserves active algebra context");
        r = session.runFile("display(\"old\"); clc; display(\"new\");");
        expect(r.ok() && r.clearConsole && r.output == std::vector<std::string>{"new"}, "script clc respects statement order");
        r = session.runFile("integer p=1; clear p; real p=2; display(p);");
        expect(r.ok() && r.output == std::vector<std::string>{"2"}, "script can redeclare a cleared name with a new type");
    }
    std::cout << "Interpreter checks: " << checks << ", failures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
