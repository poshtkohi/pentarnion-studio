#include "modules/petsc_api.h"
#include "runtime/thread_budget.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
namespace {
int checks=0,failures=0;void verify(bool okay,const char* text){++checks;if(!okay){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
struct Host {
    std::shared_ptr<std::atomic_bool> cancellation=std::make_shared<std::atomic_bool>(false);penta::runtime::ParallelSettings settings{4,1};penta::runtime::ParallelStatistics stats;bool stop=false;unsigned snapshots=0;
    static int cancelled(void* raw){return static_cast<Host*>(raw)->cancellation->load();}
    static int charge(void*,uint64_t,char*,uint64_t){return 0;}
    static int parallel(void* raw,uint64_t n,uint64_t cost,penta_numeric_range_task_v1 fn,void* ctx,char* error,uint64_t cap){auto& self=*static_cast<Host*>(raw);std::string text;const int status=penta::runtime::ThreadBudget::instance().run(n,cost,fn,ctx,self.settings,self.cancellation,self.stats,text);if(error&&cap){std::strncpy(error,text.c_str(),cap-1);error[cap-1]=0;}return status;}
    static int observer(void* raw,const penta_iteration_snapshot_v1* state,char*,uint64_t){auto& self=*static_cast<Host*>(raw);++self.snapshots;return self.stop&&state->phase==PENTA_ITERATION_ITER?1:0;}
    penta_petsc_host_v1 api(){return {sizeof(penta_petsc_host_v1),PENTA_PETSC_ABI_V1,this,cancelled,charge,parallel,observer,4};}
};
struct Problem {
    uint64_t n=2048;std::vector<uint64_t> offsets,columns;std::vector<double> matrix,rhs,initial,solution,residual;penta_petsc_request_v1 request{};penta_petsc_result_v1 result{};
    Problem(){offsets.push_back(0);rhs.resize(n);initial.resize(n);solution.resize(n);residual.resize(n);for(uint64_t row=0;row<n;++row){if(row){columns.push_back(row-1);matrix.push_back(-1);}columns.push_back(row);matrix.push_back(4);if(row+1<n){columns.push_back(row+1);matrix.push_back(-1);}offsets.push_back(columns.size());rhs[row]=4*std::sin(.17*(row+1))-(row?std::sin(.17*row):0)-(row+1<n?std::sin(.17*(row+2)):0);}request.struct_size=sizeof(request);request.operation=PENTA_PETSC_LINEAR;request.method=PENTA_PETSC_GMRES;request.preconditioner=PENTA_PETSC_JACOBI;request.dimension=n;request.nonzeros=columns.size();request.max_iterations=1000;request.max_evaluations=1000;request.max_work=100000000;request.max_bytes=128*1024*1024;request.row_offsets=offsets.data();request.columns=columns.data();request.values=matrix.data();request.rhs=rhs.data();request.initial=initial.data();request.rtol=1e-11;request.atol=1e-12;request.step_tolerance=1e-12;request.max_seconds=20;result.struct_size=sizeof(result);result.capacity=n;result.solution=solution.data();result.residual=residual.data();}
    int run(Host& owner,char* error){auto host=owner.api();int status=-1;penta::runtime::ThreadBudget::instance().withReservation([&]{status=penta_petsc_query_v1(PENTA_PETSC_ABI_V1)->solve(&host,&request,&result,error,1024);});return status;}
};
int native(void* raw,const double* x,uint64_t n,double* f,double* jac,char*,uint64_t){if(n!=2)return 1;const double target=raw?*static_cast<double*>(raw):3;f[0]=x[0]*x[0]+x[1]-target;f[1]=x[0]+x[1]*x[1]-target;if(jac){jac[0]=2*x[0];jac[1]=1;jac[2]=1;jac[3]=2*x[1];}return 0;}
}
int main(){verify(!penta_petsc_query_v1(2),"ABI version rejected");Host host;char error[1024]{};Problem p;verify(p.run(host,error)==0&&p.result.converged,"parallel PETSc shell GMRES converges");double largest=0;for(uint64_t n=0;n<p.n;++n)largest=std::max(largest,std::abs(p.solution[n]-std::sin(.17*(n+1))));verify(largest<1e-10,"independent manufactured sparse solution");verify(p.result.residual_norm<1e-8&&p.result.relative_residual<1e-10,"true residual verified");verify(host.stats.parallelCalls>0,"shared CPU pool executes sparse products");verify(host.snapshots>2,"iteration observer receives linear iterates");
    Problem cg;cg.request.method=PENTA_PETSC_CG;verify(cg.run(host,error)==0&&cg.result.converged,"CG SPD solve");Problem ilu;ilu.request.preconditioner=PENTA_PETSC_ILU;verify(ilu.run(host,error)==0&&ilu.result.converged,"PETSc AIJ ILU solve");
    Problem bad;bad.columns[0]=bad.n;verify(bad.run(host,error)==PENTA_NUMERIC_ARGUMENT,"bad CSR rejected");Problem memory;memory.request.max_bytes=1024;verify(memory.run(host,error)==PENTA_NUMERIC_ARGUMENT,"memory precheck");Problem quota;quota.request.max_work=2;verify(quota.run(host,error)==7,"native work bound");host.cancellation->store(true);Problem cancelled;verify(cancelled.run(host,error)==PENTA_NUMERIC_CANCELLED,"cancellation");host.cancellation->store(false);host.stop=true;Problem stopped;verify(stopped.run(host,error)==0&&stopped.result.stopped&&!stopped.result.converged,"observer stop is not convergence");host.stop=false;
    Problem nonlinear;nonlinear.request.operation=PENTA_PETSC_NONLINEAR;nonlinear.request.dimension=2;nonlinear.request.evaluate=native;nonlinear.initial[0]=nonlinear.initial[1]=1;nonlinear.request.preconditioner=PENTA_PETSC_ILU;verify(nonlinear.run(host,error)==0&&nonlinear.result.converged,"SNES native automatic-Jacobian bridge");const auto golden=(std::sqrt(13.0)-1)/2;verify(std::abs(nonlinear.solution[0]-golden)<1e-11&&std::abs(nonlinear.solution[1]-golden)<1e-11&&nonlinear.result.jacobian_evaluations>0,"independent nonlinear golden root");
    double target=4;nonlinear.request.callback_context=&target;nonlinear.request.atol=1e-12;verify(nonlinear.run(host,error)==0&&nonlinear.result.converged&&nonlinear.result.residual_norm<=1e-12,"SNES continues until the requested absolute residual is satisfied");host.stop=true;nonlinear.initial[0]=nonlinear.initial[1]=10;verify(nonlinear.run(host,error)==0&&nonlinear.result.stopped&&!nonlinear.result.converged,"SNES iteration observer stops cleanly");
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
