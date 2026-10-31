#include "runtime/interpreter.hpp"
#include "graph/network_solver.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
using namespace penta;
int main(){
    int failures=0,checks=0;auto expect=[&](bool okay,const char* message){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
    const auto folder=std::filesystem::temp_directory_path()/("penta-edge-operators-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    try{
        const auto checked=language::analyse("algebra H=builtin_algebra(\"quaternion\");ode node(t,element<H> q){q'=0*q;}");expect(checked.ok(),"Quaternion node model checks.");
        std::shared_ptr<const language::OdeDefinition> model;for(const auto& statement:checked.program.statements)if(statement.ode)model=statement.ode;if(!model)throw std::runtime_error("Missing model");
        networks::Graph graph{2,false,{1,2},{1}};ode::NativeOptions native;native.cacheDirectory=folder/"odes";
        const auto H=algebra::universal::builtin("quaternion");const auto i=algebra::universal::Element::basis(H,1);
        const std::vector<double> initial{0,0,0,0,0,0,1,0};networks::Options options;options.generalCoupling=true;options.edgeOperators=algebra::universal::leftMatrix(i);options.t1=.3;options.step=.001;options.sampleStep=.03;options.threads=1;
        const auto left=networks::Module().solve(*model,graph,1,initial,{},options,native);double error=0;for(std::size_t row=0;row<left.times.size();++row){const auto t=left.times[row];error=std::max({error,std::abs(left.values[row*8+2]-.5*(1-std::cos(2*t))),std::abs(left.values[row*8+3]-.5*std::sin(2*t)),std::abs(left.values[row*8+6]-.5*(1+std::cos(2*t))),std::abs(left.values[row*8+7]+.5*std::sin(2*t))});}expect(error<1e-12,"Left quaternion coupling matches independent exact rotating difference.");expect(left.stateWidths==std::vector<std::size_t>{4},"Network retains all four quaternion coordinates.");
        options.threads=4;const auto parallel=networks::Module().solve(*model,graph,1,initial,{},options,native);expect(left.values==parallel.values,"Parallel ordered-edge accumulation is deterministic.");
        options.edgeOperators=algebra::universal::rightMatrix(i);const auto right=networks::Module().solve(*model,graph,1,initial,{},options,native);expect(std::abs(right.values.back()-(-left.values.back()))<1e-13&&right.values.back()>0,"Right action preserves the opposite quaternion rotation.");
        options.method=2;options.step=0;options.rtol=1e-10;options.atol=1e-12;const auto bdf=networks::Module().solve(*model,graph,1,initial,{},options,native);error=0;for(std::size_t k=0;k<right.values.size();++k)error=std::max(error,std::abs(right.values[k]-bdf.values[k]));expect(error<2e-8,"Matrix-free BDF Jv/preconditioner include full ordered edge blocks.");expect(bdf.report.jacobian_vector_products>0&&bdf.report.block_factorizations>0,"BDF actually exercises native Jacobian products and block factors.");
        options.edgeOperators.pop_back();bool rejected=false;try{(void)networks::Module().solve(*model,graph,1,initial,{},options,native);}catch(const networks::Error&){rejected=true;}expect(rejected,"Malformed operator storage rejects before integration.");
        runtime::ExecutionOptions run;run.dataDirectory=folder;run.odeCacheDirectory=folder/"language";run.maxOperations=10000000;
        auto execute=[&](const std::string& source){const auto result=runtime::execute(source,{},run);if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}return result;};
        expect(execute("algebra H=builtin_algebra(\"quaternion\");ode node(t,element<H> q){q'=0*q;}record G=graph([1,2]);matrix<H,2,1> Q=[0;H.j];record s=networksolve(node,G,Q,options(coupling=\"general\",edge_left=H.i,t1=0.1,step=0.001,sample_step=0.1));matrix node_k=networkcoeff(s,1,4);assert(abs(node_k(end)-0.5*sin(0.2))<1e-10);assert(s.coordinates==4);assert(s.edge_grouping==\"left*delta\");").ok(),"Language accepts immutable algebra arrays and records ordered coupling.");
        expect(execute("ode node(t,pentarnion q){q'=0*q;}record G=graph([1,2]);record s=networksolve(node,G,[0;j],options(coupling=\"general\",edge_left=i,t1=0.01,step=0.001,sample_step=0.01));assert(s.coordinates==5);").ok(),"Legacy Pentarnion network coupling remains supported.");
        expect(execute("algebra O=builtin_algebra(\"octonion\");ode node(t,element<O> q){q'=0*q;}record G=graph([1,2]);matrix<O,2,1> Q=[0;O.e2];record a=networksolve(node,G,Q,options(coupling=\"general\",edge_left=O.e1,edge_right=O.e4,edge_grouping=\"(left*delta)*right\",t1=0.01,step=0.001,sample_step=0.01));record b=networksolve(node,G,Q,options(coupling=\"general\",edge_left=O.e1,edge_right=O.e4,edge_grouping=\"left*(delta*right)\",t1=0.01,step=0.001,sample_step=0.01));assert(norm(a.values-b.values,\"fro\")>0.001);").ok(),"Octonion two-sided edge grouping changes the actual trajectory.");
        const auto bad=execute("ode node(t,pentarnion q){q'=0*q;}record G=graph([1,2]);record s=networksolve(node,G,[0;j],options(edge_left=i,t1=0.01));");expect(!bad.ok(),"Algebra coupling requires explicit general semantics.");
    }catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);std::cout<<checks<<" ordered network checks, "<<failures<<" failures\n";return failures?1:0;
}
