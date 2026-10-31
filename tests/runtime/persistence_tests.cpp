#include "runtime/interpreter.hpp"
#include "runtime/data_io.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
using namespace penta;
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
void errors(const runtime::ExecutionResult& result){if(result.ok())return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
std::map<std::string,std::pair<std::string,std::string>> values(const runtime::ExecutionResult& r){std::map<std::string,std::pair<std::string,std::string>> v;for(const auto& item:r.variables)v[item.name]={item.type,item.value};return v;}
}
int main(){
    const std::string name="penta-data-λ-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(name.data()),name.size()));
    std::filesystem::create_directories(directory);runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"cache";
    runtime::InterpreterSession session;
    auto original=session.runFile("integer biggest=9223372036854775807;real r=-0.125;pentarnion p=1+i-2*j+ell;string label=\"Unicode λ and <>&\";matrix A=[1,2;3,4];matrix P=[i,j;k,ell];save(\"workspace.pws\");",{},options);errors(original);expect(original.ok(),"save all supported workspace values");
    expect(std::filesystem::exists(directory/"workspace.pws"),"relative IO uses dataDirectory");
    const auto expected=values(original);expect(session.submit("clear;",{},options).ok(),"clear before restore");
    auto restored=session.submit("load(\"workspace.pws\");",{},options);errors(restored);expect(restored.ok(),"load standalone restores names");expect(values(restored)==expected,"lossless Integer, real, Pentarnion, string and both matrix types");
    auto use=session.submit("display(biggest,p,A(2,1),label);",{},options);errors(use);expect(use.ok(),"restored names immediately usable next submission");
    auto selected=session.submit("save(\"selected.pws\",\"p\",\"A\");clear;load(\"selected.pws\");",{},options);errors(selected);expect(selected.ok(),"selected variables persist");expect(values(selected).size()==2&&values(selected).contains("p")&&values(selected).contains("A"),"selected save excludes other values");
    const auto before=values(selected);runtime::dataio::writeBytesAtomic(directory/"bad.pws","{\"format\":\"PentarnionWorkspace\",\"version\":1,\"algebra\":\"A_gamma_v1\",\"gamma\":1,\"variables\":{\"a\":{\"type\":\"real\",\"value\":9},\"z\":{\"type\":\"realmatrix\",\"rows\":2,\"columns\":2,\"value\":[1]}}}");
    auto bad=session.submit("load(\"bad.pws\");",{},options);expect(!bad.ok(),"reject corrupt dimensions");expect(values(bad)==before,"failed load atomically preserves complete workspace");
    for(const auto& invalid:{
        R"({"type":"integer range","value":[1,1.5,3]})",
        R"({"type":"integer range","value":[18446744073709551615,1,3]})",
        R"({"type":"integer range","value":[1,1,9223372036854775808]})",
        R"({"type":"realmatrix","rows":0.5,"columns":1,"value":[]})",
        R"({"type":"realmatrix","rows":1,"columns":1.5,"value":[1]})",
        R"({"type":"realmatrix","rows":-1,"columns":1,"value":[]})",
        R"({"type":"solution","state_names":["x"],"state_widths":[1.5],"times":[0],"value":[1]})",
        R"({"type":"solution","state_names":["x"],"state_widths":[-1],"times":[0],"value":[1]})"}){
        const std::string document=std::string(R"({"format":"PentarnionWorkspace","version":1,"algebra":"A_gamma_v1","gamma":1,"variables":{"broken":)")+invalid+"}}";
        runtime::dataio::writeBytesAtomic(directory/"bad-fields.pws",document);
        auto rejected=session.submit("load(\"bad-fields.pws\");",{},options);
        expect(!rejected.ok(),"reject fractional/overflow/negative saved integer schema fields");expect(values(rejected)==before,"invalid saved schema preserves prior workspace atomically");
    }
    runtime::dataio::writeBytesAtomic(directory/"budget.pws",R"({"format":"PentarnionWorkspace","version":1,"algebra":"A_gamma_v1","gamma":1,"variables":{"B":{"type":"realmatrix","rows":1,"columns":4,"value":[1,2,3,4]}}})");
    auto matrixBudget=options;matrixBudget.maxMatrixCoordinates=6;
    auto calleeMatrix=runtime::execute("matrix caller=zeros(1,4);function void readBudget(){load(\"budget.pws\");}readBudget();",{},matrixBudget);
    expect(!calleeMatrix.ok(),"load inside function counts caller-retained matrix coordinates");
    expect(values(calleeMatrix).contains("caller")&&!values(calleeMatrix).contains("B"),"failed callee load retains caller and discards local changes");
    expect(!session.submit("load(\"missing.pws\");",{},options).ok(),"missing data file diagnostic");
    auto declared=runtime::execute("real r=0;matrix A=[];load(\"workspace.pws\",\"r\",\"A\");assert(r==-0.125);assert(A(2,2)==4);",{},options);errors(declared);expect(declared.ok(),"typed targets support load and use in one checked script");
    auto fixedSaved=runtime::execute("matrix<1,2> A=[1,2];save(\"fixed-array.pws\",\"A\");",{},options);errors(fixedSaved);expect(fixedSaved.ok(),"save fixed array schema");
    runtime::InterpreterSession fixedTarget;const auto fixedBefore=fixedTarget.submit("matrix<2,1> A=[3;4];integer marker=17;",{},options);errors(fixedBefore);
    const auto fixedRejected=fixedTarget.submit("load(\"fixed-array.pws\",\"A\");",{},options);expect(!fixedRejected.ok(),"load rejects same-type arrays with different declared fixed shapes");expect(values(fixedRejected)==values(fixedBefore),"fixed shape mismatch preserves all workspace bindings atomically");
    auto dynamicResize=runtime::execute("matrix A=[];load(\"fixed-array.pws\",\"A\");assert(size(A,1)==1&&size(A,2)==2);A=[3;4;5];save(\"dynamic-array.pws\",\"A\");",{},options);errors(dynamicResize);expect(dynamicResize.ok(),"dynamic matrix targets retain resizing when loading a saved fixed array");
    auto dynamicSaved=runtime::execute("matrix A=[9,8];save(\"dynamic-pair.pws\",\"A\");",{},options);errors(dynamicSaved);expect(dynamicSaved.ok(),"save dynamic array with compatible dimensions");
    runtime::InterpreterSession retainedFixed;const auto fixedLoaded=retainedFixed.submit("matrix<1,2> A=[0,0];load(\"dynamic-pair.pws\",\"A\");assert(A==[9,8]);",{},options);errors(fixedLoaded);expect(fixedLoaded.ok(),"compatible dynamic snapshot loads into a fixed array target");
    expect(!retainedFixed.submit("A=[1;2];",{},options).ok(),"loading dynamic snapshot preserves the target fixed schema for later submissions");
    auto genericSaved=runtime::execute("algebra H=builtin_algebra(\"quaternion\");matrix<H,1,2> A=[H.i,H.j];save(\"generic-array.pws\",\"A\");",{},options);errors(genericSaved);expect(genericSaved.ok(),"save generic array algebra identity");
    runtime::InterpreterSession genericTarget;const auto genericBefore=genericTarget.submit("algebra O=builtin_algebra(\"octonion\");matrix<O,1,2> A=[O.e1,O.e2];",{},options);errors(genericBefore);
    const auto genericRejected=genericTarget.submit("load(\"generic-array.pws\",\"A\");",{},options);expect(!genericRejected.ok(),"load rejects same-type generic arrays from another algebra");expect(values(genericRejected)==values(genericBefore),"generic array algebra mismatch preserves workspace transactionally");
    runtime::dataio::writeBytesAtomic(directory/"column-state.pws",R"({"format":"PentarnionWorkspace","version":1,"algebra":"A_gamma_v1","gamma":1,"variables":{"s":{"type":"solution","state_names":["x"],"state_widths":[2],"state_metadata":[{"type":"realmatrix","shape":[2,1]}],"times":[0],"value":[3,4]}}})");
    runtime::dataio::writeBytesAtomic(directory/"row-state.pws",R"({"format":"PentarnionWorkspace","version":1,"algebra":"A_gamma_v1","gamma":1,"variables":{"s":{"type":"solution","state_names":["x"],"state_widths":[2],"state_metadata":[{"type":"realmatrix","shape":[1,2]}],"times":[0],"value":[1,2]}}})");
    runtime::InterpreterSession arrayState;const auto stateBefore=arrayState.submit("load(\"column-state.pws\");",{},options);errors(stateBefore);expect(stateBefore.ok(),"restore explicit fixed-array solution schema");
    const auto stateRejected=arrayState.submit("load(\"row-state.pws\",\"s\");",{},options);expect(!stateRejected.ok(),"loaded solution rejects equal-width array states with different shapes");expect(values(stateRejected)==values(stateBefore),"solution shape mismatch preserves the original workspace snapshot");
    const auto stateKept=arrayState.submit("matrix<2,1> retained=s.x(1);assert(retained==[3;4]);",{},options);errors(stateKept);expect(stateKept.ok(),"rejected array-state load retains original sampled values and shape");
    auto tabular=runtime::execute("matrix A=[1.25,-2;3,4.5];writematrix(A,\"table.csv\");matrix C=readmatrix(\"table.csv\");assert(C==A);writematrix(A,\"table.xlsx\",\"Data\");matrix X=readmatrix(\"table.xlsx\",\"Data\");assert(X==A);csvwrite(\"alias.csv\",A);matrix D=csvread(\"alias.csv\");assert(D==A);xlswrite(\"alias.xlsx\",A,\"Demo\");matrix E=xlsread(\"alias.xlsx\",\"Demo\");assert(E==A);",{},options);errors(tabular);expect(tabular.ok(),"native CSV/XLSX and compatibility alias builtin roundtrips");
    auto algebraMismatch=runtime::execute("use_algebra(\"A_gamma\",gamma=2);load(\"workspace.pws\");",{},options);expect(!algebraMismatch.ok(),"reject workspace from different algebra context");
    auto definitions=runtime::execute("function real square(real x){return x*x;}real x=square(3);save(\"values.pws\");",{},options);errors(definitions);expect(definitions.ok(),"default save excludes executable definitions");
    auto explicitPure=runtime::execute("function real square(real x){return x*x;}save(\"pure-function.pws\",\"square\");",{},options);errors(explicitPure);expect(explicitPure.ok(),"explicit isolated pure function snapshot is accepted as checked source data");
    auto failExplicit=runtime::execute("real captured=3;callback captured_square=@(real x)=>captured*x*x;save(\"values.pws\",\"captured_square\");",{},options);expect(!failExplicit.ok(),"captured callback persistence is rejected clearly");
    const auto intact=runtime::dataio::readBytes(directory/"values.pws");expect(intact.find("\"x\"")!=std::string::npos,"failed save preserves existing destination");
    auto solution=session.runFile("ode decay(t,pentarnion p;real a){p'=-a*p;}solution s=odesolve(decay,p=1+i,a=0.25,t0=0,t1=0.2,sample_step=0.1);save(\"solution.pws\",\"s\");",{},options);errors(solution);expect(solution.ok(),"persist sampled ODE solution");
    expect(session.submit("clear;",{},options).ok(),"clear sampled solution before restore");auto loadSolution=session.submit("load(\"solution.pws\");",{},options);errors(loadSolution);expect(loadSolution.ok(),"restore sampled ODE schema and values");
    auto useSolution=session.submit("display(s.t(end),coeff(s.p,1)(end));",{},options);errors(useSolution);expect(useSolution.ok(),"restored solution supports members/coefficients/indexing");
    auto solutionBudget=options;solutionBudget.maxSolutionCoordinates=20;
    auto calleeSolution=session.submit("function void readState(){load(\"solution.pws\");}readState();",{},solutionBudget);
    expect(!calleeSolution.ok(),"load inside function counts caller-retained sampled solution coordinates");
    expect(values(calleeSolution).contains("s"),"failed callee solution load retains original caller solution");
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
