#include "runtime/interpreter.hpp"
#include <chrono>
#include <iostream>
namespace {int checks=0;void run(const std::string& source,const penta::runtime::ExecutionOptions& options,bool success=true){++checks;const auto result=penta::runtime::execute(source,{},options);if(result.ok()==success)return;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,110)<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<" near "<<source.substr(d.span.offset,110)<<'\n';throw std::runtime_error("Higher-index DAE language fixture failed.");}}
int main(int argc,char**argv){penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-linear-dae-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/"kernels";options.maxOperations=100000000;if(argc>1)options.numericModuleDirectory=std::filesystem::absolute(argv[1]);try{
    run(R"(
native function [real first,real second,real third] forcing_term(real t,real omega){return 0.0,0.0,-sin(omega*t);}
matrix descriptor=[1,0,0;0,0,1;0,0,0];matrix linear=[-2,0,0;0,1,0;0,0,1];
record solved=lineardaesolve(descriptor,linear,forcing_term,[1,0,0],options(parameters=options(omega=1.3),t0=0.2,t1=1.2,sample_step=0.01,rtol=1e-9,atol=1e-11));
assert(solved.index==2);assert(solved.differential_dimension==1);assert(norm(solved.values(:,1)-exp(-2*(solved.t-0.2)))<1e-6);assert(norm(solved.values(:,2)-1.3*cos(1.3*solved.t))<1e-8);assert(norm(solved.values(:,3)-sin(1.3*solved.t))<1e-8);assert(solved.jacobian=="analytic_reduced_matrix");
record changed=lineardaesolve(descriptor,linear,forcing_term,[1,0,0],options(parameters=options(omega=1.4),t0=0.2,t1=1.2,sample_step=0.02));assert(changed.cache_key==solved.cache_key);assert(changed.cache_hit);
)",options);
    run(R"(
native function [real first,real second,real third,real fourth] forcing_term(real t,real omega){return 0.0,0.0,0.0,-sin(omega*t);}
matrix descriptor=[1,0,0,0;0,0,1,0;0,0,0,1;0,0,0,0];matrix linear=[-2,0,0,0;0,1,0,0;0,0,1,0;0,0,0,1];
real omega=1.3;real start=0.2;record solved=lineardaesolve(descriptor,linear,forcing_term,[1,0,0,0],options(parameters=options(omega=omega),t0=start,t1=1.2,sample_step=0.02,sensitivity_parameters=[1],parameter_lower=0.5,parameter_upper=2.0));
assert(solved.index==3);assert(norm(solved.values(:,2)+omega^2*sin(omega*solved.t))<1e-8);assert(norm(solved.derivatives(:,2)+omega^3*cos(omega*solved.t))<1e-8);
assert(abs(solved.initial_parameter_jacobian(4,1)-start*cos(omega*start))<1e-7);
assert(abs(solved.initial_derivative_parameter_jacobian(2,1)-(-3*omega^2*cos(omega*start)+omega^3*start*sin(omega*start)))<1e-6);
assert(norm(solved.sensitivities(:,4)-solved.t.*cos(omega*solved.t))<1e-5);assert(solved.sensitivity_solves==2);
)",options);
    run(R"(native function real forcing_term(real t){return -sin(t);}record solved=lineardaesolve([0],[1],forcing_term,[0],options(t1=1,sample_step=0.02));assert(solved.index==1);assert(solved.differential_dimension==0);assert(norm(solved.values-sin(solved.t))<1e-10);assert(norm(solved.derivatives-cos(solved.t))<1e-10);)",options);
    run(R"(native function [real first,real second] residual(real t,real x,real z,real xp,real zp){return xp+x,z-x*x;}record info=daeanalyze(residual,0.0,[1,1],[-1,-2]);assert(info.supported);assert(info.local_index==1);assert(info.rank_mass==1);assert(info.residual_norm==0);native function [real first,real second] higher(real t,real x,real z,real xp,real zp){return zp-x,z+sin(t);}record unsupported=daeanalyze(higher,0.0,[1,0],[0,1]);assert(unsupported.supported==0);assert(unsupported.local_index==-1);)",options);
    run(R"(native function [real first,real second] forcing_term(real t){return 0.0,-sin(t);}record bad=lineardaesolve([0,1;0,0],[1,0;0,1],forcing_term,[0,0],options(initialization="check"));)",options,false);
    run(R"(native function [real first,real second] forcing_term(real t){return 0.0,0.0;}record bad=lineardaesolve([1,0;0,1e-13],[-1,0;0,-1],forcing_term,[1,0],options());)",options,false);
    std::cout<<"PASS "<<checks<<" higher-index DAE language checks\n";
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);}
