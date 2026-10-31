#include "fem/fem_solver.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <numbers>
#ifndef PENTA_FEM_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#endif
namespace {
int checks=0,failures=0;
void expect(bool good,const char* message){++checks;if(!good){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class F>void reject(F&& fn,const char* message){bool caught=false;try{fn();}catch(const penta::fem::Error&){caught=true;}expect(caught,message);}
struct Mesh {
    std::vector<double> points;std::vector<uint64_t> faces,boundaries;
    explicit Mesh(unsigned n=1){
        for(unsigned y=0;y<=n;++y)for(unsigned x=0;x<=n;++x){points.push_back(static_cast<double>(x)/n);points.push_back(static_cast<double>(y)/n);}
        auto id=[&](unsigned x,unsigned y){return uint64_t(y*(n+1)+x+1);};
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const auto a=id(x,y),b=id(x+1,y),c=id(x,y+1),d=id(x+1,y+1);faces.insert(faces.end(),{a,b,d,a,d,c});}
        for(unsigned k=0;k<n;++k){boundaries.insert(boundaries.end(),{id(0,k),id(0,k+1),1,id(n,k),id(n,k+1),2,id(k,0),id(k+1,0),3,id(k,n),id(k+1,n),4});}
    }
    penta_fem_request_v1 request()const{penta_fem_request_v1 q{};q.order=1;q.visualization_refinement=3;q.vertices=points.size()/2;q.triangles=faces.size()/3;q.boundaries=boundaries.size()/3;q.points=points.data();q.faces=faces.data();q.boundary_tags=boundaries.data();q.dirichlet_count=UINT64_MAX;q.diffusion_constant=1;q.relative_tolerance=1e-11;q.absolute_tolerance=1e-14;q.max_iterations=10000;q.max_work=100000000;q.max_bytes=256*1024*1024;q.max_seconds=30;return q;}
};
int polynomial(void*,double x,double y,double* value,char*,uint64_t){*value=x*(1-x)*y*(1-y);return 0;}
int forcing(void*,double x,double y,double* value,char*,uint64_t){*value=2*(x*(1-x)+y*(1-y));return 0;}
int sine(void*,double x,double y,double* value,char*,uint64_t){*value=std::sin(std::numbers::pi*x)*std::sin(std::numbers::pi*y);return 0;}
int sineForcing(void* c,double x,double y,double* value,char* error,uint64_t n){sine(c,x,y,value,error,n);*value*=2*std::numbers::pi*std::numbers::pi;return 0;}
int linear(void*,double x,double y,double* value,char*,uint64_t){*value=x+y;return 0;}
int variableDiffusion(void*,double x,double,double* value,char*,uint64_t){*value=1+x;return 0;}
int variableForcing(void*,double x,double y,double* value,char*,uint64_t){*value=-1+2*(x+y);return 0;}
int failing(void*,double,double,double*,char* error,uint64_t capacity){if(capacity>5){std::copy_n("fail",5,error);}return 1;}
struct Reentry {
    const penta::fem::Module& module;penta_fem_request_v1 request;
    bool attempted=false,rejected=false;
    static int forcing(void* raw,double x,double y,double* value,char*,uint64_t){
        auto& self=*static_cast<Reentry*>(raw);
        if(!self.attempted){self.attempted=true;try{self.module.solve(self.request);}catch(const penta::fem::Error& failure){self.rejected=failure.code==PENTA_NUMERIC_ARGUMENT&&std::string(failure.what()).find("Reentrant FEM solve")!=std::string::npos;}}
        *value=2*(x*(1-x)+y*(1-y));return 0;
    }
};
void native(){
    penta::fem::Module module;Mesh coarse;auto q=coarse.request();q.forcing=forcing;q.exact=polynomial;q.order=4;
    const auto fourth=module.solve(q);expect(fourth.report.converged&&fourth.report.relative_residual<2e-11,"MFEM P4 converges with a verified assembled-system residual.");expect(fourth.report.dofs==25&&fourth.coordinates.size()==50&&fourth.values.size()==25,"P4 retains all25 nodal DOFs, including edge/interior nodes.");expect(fourth.report.has_exact&&fourth.report.l2_error<2e-12,"Degree4 manufactured polynomial is reproduced by H1 P4.");
    double maxError=0;for(std::size_t n=0;n<fourth.values.size();++n){const auto x=fourth.coordinates[2*n],y=fourth.coordinates[2*n+1];maxError=std::max(maxError,std::abs(fourth.values[n]-x*(1-x)*y*(1-y)));}expect(maxError<2e-11,"Stored DOF coordinates and values obey the manufactured solution.");expect(fourth.report.visualization_vertices==20&&fourth.report.visualization_triangles==18,"Per-element visualization sampling is explicitly distinct from H1 DOF connectivity.");bool ids=true;for(const auto id:fourth.visualizationFaces)ids=ids&&id>=1&&id<=fourth.report.visualization_vertices;expect(ids,"Sampled triangle indices are valid one-based IDs.");
    for(unsigned order=1;order<=4;++order){q.order=order;const auto r=module.solve(q);expect(r.report.converged&&r.report.dofs==(order+1)*(order+1),"All supported orders1..4 expose correct conforming H1 dimensions.");}
    Mesh m4(4),m8(8);auto a=m4.request(),b=m8.request();a.forcing=b.forcing=sineForcing;a.exact=b.exact=sine;auto s4=module.solve(a),s8=module.solve(b);expect(s4.report.l2_error/s8.report.l2_error>3.6&&s4.report.l2_error/s8.report.l2_error<4.4,"P1 h-refinement demonstrates second-order L2 convergence.");a.order=b.order=2;auto p4=module.solve(a),p8=module.solve(b);expect(p4.report.l2_error/p8.report.l2_error>6.5,"P2 h-refinement demonstrates higher-order L2 convergence.");
    b.order=1;b.forcing=variableForcing;b.diffusion=variableDiffusion;b.reaction_constant=2;b.dirichlet=linear;b.exact=linear;auto variable=module.solve(b);expect(variable.report.converged&&variable.report.l2_error<2e-10,"Variable diffusion, positive reaction and nonzero native Dirichlet data preserve a manufactured linear solution.");expect(variable.report.min_diffusion>1&&variable.report.min_reaction==2,"Report records coefficient values checked at quadrature points.");
    a=m4.request();a.forcing=forcing;a.max_iterations=1;auto shortRun=module.solve(a);expect(!shortRun.report.converged&&shortRun.report.iterations==1,"Iteration-limited solve returns honest nonconvergence rather than success.");
    a=m4.request();a.forcing=forcing;a.diffusion_constant=-1;reject([&]{module.solve(a);},"Negative diffusion is rejected at evaluated quadrature points.");a.diffusion_constant=1;a.reaction_constant=-1;reject([&]{module.solve(a);},"Negative reaction is rejected.");a.reaction_constant=0;a.dirichlet_count=0;reject([&]{module.solve(a);},"Pure Neumann is rejected explicitly without inventing an anchor/mean constraint.");a.dirichlet_count=1;uint64_t badTag=99;a.dirichlet_tags=&badTag;reject([&]{module.solve(a);},"Unknown Dirichlet tag rejects.");a.dirichlet_count=UINT64_MAX;a.max_work=1;reject([&]{module.solve(a);},"Native FEM work budget is enforced.");a.max_work=100000000;a.max_seconds=1e-20;reject([&]{module.solve(a);},"Native FEM wall budget is enforced.");a.max_seconds=30;a.max_bytes=65536;reject([&]{module.solve(a);},"Memory estimate rejects before unbounded assembly allocation.");a.max_bytes=256*1024*1024;auto cancelled=std::make_shared<std::atomic_bool>(true);reject([&]{module.solve(a,cancelled);},"Native FEM cancellation is enforced.");a.forcing=failing;reject([&]{module.solve(a);},"Native callback failures escape safely through MFEM exceptions.");
    Mesh invalid;invalid.faces[2]=invalid.faces[1];auto bad=invalid.request();bad.forcing=forcing;reject([&]{module.solve(bad);},"Repeated triangle vertex rejects before MFEM.");invalid=Mesh{};invalid.boundaries.resize(9);bad=invalid.request();bad.forcing=forcing;reject([&]{module.solve(bad);},"Missing external boundary tag rejects.");invalid=Mesh{};invalid.points[0]=std::numeric_limits<double>::infinity();bad=invalid.request();bad.forcing=forcing;reject([&]{module.solve(bad);},"Nonfinite geometry rejects before MFEM.");
    a=m4.request();a.forcing=forcing;Reentry reentry{module,a};a.forcing=Reentry::forcing;a.forcing_context=&reentry;const auto outer=module.solve(a);expect(reentry.attempted&&reentry.rejected&&outer.report.converged,"Same-thread native callback reentry is rejected explicitly while the outer solve remains valid.");a.forcing=forcing;a.forcing_context=nullptr;expect(module.solve(a).report.converged,"The FEM reentry guard is cleared after a completed outer solve.");
    auto one=std::async(std::launch::async,[&]{return module.solve(a);});auto two=std::async(std::launch::async,[&]{return module.solve(a);});const auto r1=one.get(),r2=two.get();expect(r1.report.converged&&r2.report.converged&&r1.values==r2.values,"Concurrent callers are safely serialized through the shared budget and MFEM lock.");
}
#ifndef PENTA_FEM_NATIVE_ONLY
void language(){
    penta::runtime::ExecutionOptions execution;
    execution.dataDirectory=std::filesystem::temp_directory_path()/("penta-fem-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    execution.odeCacheDirectory=execution.dataDirectory/"odes";
    std::filesystem::create_directories(execution.dataDirectory);
    const std::string source=R"(
native function real rhs(real x,real y,real scale){return scale*2*(x*(1-x)+y*(1-y));}
native function real exact(real x,real y,real scale){return scale*x*(1-x)*y*(1-y);}
record M=trimesh([0,0;1,0;1,1;0,1],[1,2,3;1,3,4],options(boundary_tags=[1,2,1;2,3,2;3,4,3;4,1,4]));
record s=femsolve(M,@rhs,options(order=4,exact=@exact,parameters=options(scale=1)));
assert(s.converged==1);assert(s.l2_error<1e-10);femplot(s);
record t=femsolve(M,@rhs,options(order=4,exact=@exact,parameters=options(scale=2)));
assert(t.cache_hit==1);assert(norm(t.dof_values-2*s.dof_values)<1e-9);
)";const auto result=penta::runtime::execute(source,{},execution);if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.message<<'\n';}expect(result.ok(),"Language FEM callbacks compile once and runtime parameters change only values.");expect(!result.figureActions.empty()&&result.figureActions.back().mesh.has_value(),"femplot emits a native immutable mesh snapshot.");
    penta::runtime::InterpreterSession session;
    expect(session.submit("native function real forcing(real x,real y){return 1;}record mesh=trimesh([0,0;1,0;1,1;0,1],[1,2,3;1,3,4],options(boundary_tags=[1,2,1;2,3,2;3,4,3;4,1,4]));",{},execution).ok(),"FEM host-quota fixture retains a real R2 mesh and native callback.");
    auto capped=execution;capped.maxNativeOperations=1;
    const auto rejected=session.submit("record limited=femsolve(mesh,@forcing,options(max_work=1000000));",{},capped);
    expect(!rejected.ok()&&!rejected.runtimeDiagnostics.empty()&&rejected.runtimeDiagnostics.front().code=="R1005","Explicit FEM max_work cannot exceed the host native allowance.");
    capped.maxNativeOperations=0;
    const auto disabled=session.submit("record disabled=femsolve(mesh,@forcing);",{},capped);
    expect(!disabled.ok()&&!disabled.runtimeDiagnostics.empty()&&disabled.runtimeDiagnostics.front().code=="R1005","Zero host native allowance rejects FEM before numerical assembly.");
    expect(session.submit("record recovered=femsolve(mesh,@forcing);assert(recovered.converged);",{},execution).ok(),"Normal FEM execution recovers after host budget rejection.");
    std::error_code ignored;std::filesystem::remove_all(execution.dataDirectory,ignored);
}
#endif
}
int main(){try{native();
#ifndef PENTA_FEM_NATIVE_ONLY
language();
#endif
}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';++failures;}std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
