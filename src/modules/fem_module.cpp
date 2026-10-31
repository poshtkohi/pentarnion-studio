#include "fem_api.h"
#include "fem_mean_api.h"
#include "fem_parallel_api.h"
#include "fem_shared_state.hpp"
#include <mfem.hpp>
#ifndef MFEM_USE_EXCEPTIONS
#error "The Studio MFEM backend requires MFEM_USE_EXCEPTIONS=ON; aborting error handlers are unsafe inside Studio."
#endif
static_assert(MFEM_VERSION==41000,"Revalidate the bounded MFEM wrapper before changing its pinned library version.");
#if !defined(MFEM_USE_DOUBLE) || defined(MFEM_USE_SINGLE)
#error "The Studio MFEM ABI requires a double-precision MFEM SDK."
#endif
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr int PENTA_NUMERIC_RESOURCE=7;
// MFEM_THREAD_SAFE is enabled in the SDK, but library-wide lazy state and device
// configuration are deliberately protected until broader concurrent use is verified.
using penta::fem::detail::mfemMutex;
using penta::fem::detail::activeMfemSolve;
struct Failure:std::runtime_error {int code;Failure(int c,const std::string& s):std::runtime_error(s),code(c){}};
struct SolveScope {
    SolveScope(){if(activeMfemSolve)throw Failure(PENTA_NUMERIC_ARGUMENT,"Reentrant FEM solve from a native callback is not supported.");activeMfemSolve=true;}
    ~SolveScope(){activeMfemSolve=false;}
};
void text(char* out,uint64_t capacity,const char* message){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(message));std::memcpy(out,message,n);out[n]=0;}}
struct Work {
    const penta_fem_host_v1& host;const penta_fem_request_v1& request;penta_fem_result_v1& result;
    const penta_fem_mean_request_v1* mean=nullptr;penta_fem_mean_result_v1* meanResult=nullptr;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    void check(uint64_t amount=1){
        if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_NUMERIC_CANCELLED,"FEM operation cancelled.");
        if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>request.max_seconds)throw Failure(PENTA_NUMERIC_RESOURCE,"FEM wall-time budget exceeded.");
        if(amount>request.max_work-result.work_units)throw Failure(PENTA_NUMERIC_RESOURCE,"FEM work budget exceeded.");
        result.work_units+=amount;
        char error[512]{};if(host.charge&&host.charge(host.context,amount,error,sizeof(error)))throw Failure(PENTA_NUMERIC_RESOURCE,error[0]?error:"FEM shared operation budget exceeded.");
    }
};
class Field final:public mfem::Coefficient {
    Work& work; penta_fem_field_v1 callback;void* context;double constant;unsigned kind;
public:
    Field(Work& w,penta_fem_field_v1 cb,void* ctx,double c,unsigned k):work(w),callback(cb),context(ctx),constant(c),kind(k){}
    mfem::real_t Eval(mfem::ElementTransformation& transform,const mfem::IntegrationPoint& point)override{
        work.check();mfem::Vector position(2);transform.Transform(point,position);double value=constant;
        if(callback){char error[1024]{};++work.result.evaluations;if(callback(context,position[0],position[1],&value,error,sizeof(error)))throw Failure(PENTA_NUMERIC_ARGUMENT,error[0]?error:"Native FEM field callback failed.");}
        if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM field callback returned a nonfinite real value.");
        if(kind==1){if(value<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Diffusion must be positive at every evaluated quadrature point.");work.result.min_diffusion=std::min(work.result.min_diffusion,value);}
        if(kind==2){if(value<0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Reaction must be nonnegative at every evaluated quadrature point.");work.result.min_reaction=std::min(work.result.min_reaction,value);}
        return value;
    }
};
class Monitor final:public mfem::IterativeSolverController {
    Work& work;uint64_t cost;
public:
    Monitor(Work& w,uint64_t n):work(w),cost(n){}
    void MonitorResidual(int,mfem::real_t,const mfem::Vector&,bool)override{work.check(cost);}
};
using Edge=std::pair<uint64_t,uint64_t>;
Edge edge(uint64_t a,uint64_t b){return std::minmax(a,b);}
#include "fem_parallel_kernel.inc"
void solve(Work& w){
    const auto& q=w.request;auto& r=w.result;
    if(q.order<1||q.order>4||q.visualization_refinement<1||q.visualization_refinement>16||q.vertices<3||q.vertices>100000||!q.triangles||q.triangles>100000||q.boundaries>300000||!q.points||!q.faces||!q.boundary_tags||!q.forcing)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM requires a bounded tagged real R2 triangle mesh, forcing and order1..4.");
    if(!std::isfinite(q.relative_tolerance)||q.relative_tolerance<=0||q.relative_tolerance>=1||!std::isfinite(q.absolute_tolerance)||q.absolute_tolerance<0||!std::isfinite(q.max_seconds)||q.max_seconds<=0||q.max_seconds>3600||!q.max_iterations||q.max_iterations>100000||!q.max_work||q.max_work>1000000000||q.max_bytes<65536||q.max_bytes>1073741824||(q.dirichlet_count!=UINT64_MAX&&q.dirichlet_count>q.boundaries))throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid FEM solver/resource bounds.");
    const uint64_t p=q.order,local=(p+1)*(p+2)/2;
    const uint64_t dofBound=q.vertices+3*q.triangles*(p-1)+(p>=3?q.triangles*(p-1)*(p-2)/2:0);
    const uint64_t subdivisions=q.visualization_refinement,perVertex=(subdivisions+1)*(subdivisions+2)/2;
    const uint64_t vertexBound=q.triangles*perVertex,triangleBound=q.triangles*subdivisions*subdivisions;
    r.estimated_bytes=65536+q.vertices*160+q.triangles*(512+local*local*40)+dofBound*128+vertexBound*24+triangleBound*24;
    if(w.mean)r.estimated_bytes+=q.triangles*local*local*16+dofBound*48;
    if(r.estimated_bytes>q.max_bytes||dofBound>r.dof_capacity||vertexBound>r.vertex_capacity||triangleBound>r.triangle_capacity)throw Failure(PENTA_NUMERIC_RESOURCE,"FEM estimated assembly/output memory exceeds its explicit budget.");
    if(!r.dof_coordinates||!r.dof_values||!r.visualization_coordinates||!r.visualization_values||!r.visualization_faces)throw Failure(PENTA_NUMERIC_ARGUMENT,"Missing FEM output buffers.");
    std::vector<uint64_t> parent(q.vertices),used(q.vertices,0);for(uint64_t v=0;v<q.vertices;++v)parent[v]=v;
    auto root=[&](uint64_t v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;};
    std::map<Edge,unsigned> edges;std::set<std::array<uint64_t,3>> uniqueFaces;
    for(uint64_t v=0;v<2*q.vertices;++v)if(!std::isfinite(q.points[v]))throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh coordinates must be finite.");
    for(uint64_t n=0;n<q.triangles;++n){
        w.check(16);std::array<uint64_t,3> face{};for(unsigned j=0;j<3;++j){const auto id=q.faces[3*n+j];if(!id||id>q.vertices)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM triangles require valid one-based vertices.");face[j]=id-1;++used[id-1];}
        auto sorted=face;std::sort(sorted.begin(),sorted.end());if(sorted[0]==sorted[1]||sorted[1]==sorted[2]||!uniqueFaces.insert(sorted).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh has a repeated/duplicate triangle.");
        const double ax=q.points[2*face[0]],ay=q.points[2*face[0]+1],bx=q.points[2*face[1]],by=q.points[2*face[1]+1],cx=q.points[2*face[2]],cy=q.points[2*face[2]+1];
        const double scale=std::max({std::abs(bx-ax),std::abs(by-ay),std::abs(cx-ax),std::abs(cy-ay)});
        const double area=(bx-ax)*(cy-ay)-(by-ay)*(cx-ax);
        if(!std::isfinite(area)||!std::isfinite(scale)||scale==0||std::abs(area)<=64*std::numeric_limits<double>::epsilon()*scale*scale)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh contains a degenerate or numerically singular triangle.");
        for(unsigned j=0;j<3;++j){if(++edges[edge(face[j],face[(j+1)%3])]>2)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh has a nonmanifold edge.");parent[root(face[j])]=root(face[0]);}
    }
    for(const auto count:used)if(!count)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh contains an unused vertex.");
    std::map<uint64_t,int> tagToAttribute;std::set<Edge> tagged;
    for(uint64_t b=0;b<q.boundaries;++b){const auto a=q.boundary_tags[3*b],c=q.boundary_tags[3*b+1],tag=q.boundary_tags[3*b+2];if(!a||a>q.vertices||!c||c>q.vertices||a==c||!tag||tag>1000000)throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid FEM boundary endpoint/tag.");const auto e=edge(a-1,c-1);const auto found=edges.find(e);if(found==edges.end()||found->second!=1||!tagged.insert(e).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM boundary tags must describe each external edge exactly once.");tagToAttribute.emplace(tag,0);}
    for(const auto& [e,count]:edges)if(count==1&&!tagged.contains(e))throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mesh is missing an external boundary tag.");
    int next=0;for(auto& [tag,attribute]:tagToAttribute){(void)tag;attribute=++next;}
    std::set<uint64_t> selected;
    if(q.dirichlet_count==UINT64_MAX){for(const auto& [tag,attr]:tagToAttribute){(void)attr;selected.insert(tag);}}
    else{if(q.dirichlet_count&&!q.dirichlet_tags)throw Failure(PENTA_NUMERIC_ARGUMENT,"Missing selected FEM boundary tags.");for(uint64_t n=0;n<q.dirichlet_count;++n){const auto tag=q.dirichlet_tags[n];if(!tagToAttribute.contains(tag)||!selected.insert(tag).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Selected Dirichlet tags must be distinct existing boundary tags.");}}
    std::set<uint64_t> anchored;
    for(uint64_t b=0;b<q.boundaries;++b)if(selected.contains(q.boundary_tags[3*b+2]))anchored.insert(root(q.boundary_tags[3*b]-1));
    std::set<uint64_t> unanchored;for(uint64_t v=0;v<q.vertices;++v)if(!anchored.contains(root(v)))unanchored.insert(root(v));
    if(w.mean){if(!std::isfinite(w.mean->target_mean)||!std::isfinite(w.mean->compatibility_tolerance)||w.mean->compatibility_tolerance<=0||w.mean->compatibility_tolerance>=.01||q.reaction||q.reaction_constant!=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM mean constraints require finite means/tolerances and a pure diffusion operator (reaction zero).");if(unanchored.empty())throw Failure(PENTA_NUMERIC_ARGUMENT,"A mean constraint requires at least one component without a Dirichlet anchor.");w.meanResult->nullspace_components=unanchored.size();w.meanResult->compatibility_residual=w.meanResult->maximum_mean_error=0;}
    else if(!unanchored.empty()&&(q.reaction||q.reaction_constant<=0))throw Failure(PENTA_NUMERIC_ARGUMENT,"An unanchored pure-Neumann FEM component requires an explicit mean constraint.");
    mfem::Mesh mesh(2,static_cast<int>(q.vertices),static_cast<int>(q.triangles),static_cast<int>(q.boundaries),2);
    for(uint64_t v=0;v<q.vertices;++v)mesh.AddVertex(q.points+2*v);
    for(uint64_t n=0;n<q.triangles;++n)mesh.AddTriangle(static_cast<int>(q.faces[3*n]-1),static_cast<int>(q.faces[3*n+1]-1),static_cast<int>(q.faces[3*n+2]-1));
    for(uint64_t b=0;b<q.boundaries;++b)mesh.AddBdrSegment(static_cast<int>(q.boundary_tags[3*b]-1),static_cast<int>(q.boundary_tags[3*b+1]-1),tagToAttribute.at(q.boundary_tags[3*b+2]));
    mesh.FinalizeTriMesh(1,0,true);w.check(q.triangles);
    mfem::H1_FECollection collection(static_cast<int>(p),2,mfem::BasisType::GaussLobatto);mfem::FiniteElementSpace space(&mesh,&collection);
    r.dofs=static_cast<uint64_t>(space.GetVSize());if(r.dofs>r.dof_capacity)throw Failure(PENTA_NUMERIC_INTERNAL,"MFEM returned more DOFs than its checked bound.");
    mfem::Array<int> marker(next);marker=0;for(const auto tag:selected)marker[tagToAttribute.at(tag)-1]=1;
    mfem::Array<int> essential;space.GetEssentialTrueDofs(marker,essential);
    std::vector<uint64_t> dofComponent(r.dofs,UINT64_MAX);mfem::Array<int> localDofs,elementVertices;
    if(w.mean){for(int e=0;e<mesh.GetNE();++e){mesh.GetElementVertices(e,elementVertices);const auto component=root(static_cast<uint64_t>(elementVertices[0]));space.GetElementDofs(e,localDofs);for(const auto encoded:localDofs){const auto d=encoded>=0?encoded:-1-encoded;if(dofComponent[static_cast<std::size_t>(d)]!=UINT64_MAX&&dofComponent[static_cast<std::size_t>(d)]!=component)throw Failure(PENTA_NUMERIC_INTERNAL,"A FEM degree of freedom belongs to conflicting connected components.");dofComponent[static_cast<std::size_t>(d)]=component;}}for(const auto component:unanchored){space.GetVertexDofs(static_cast<int>(component),localDofs);if(localDofs.Size()!=1)throw Failure(PENTA_NUMERIC_INTERNAL,"Expected one H1 scalar vertex DOF for a mean gauge.");essential.Append(localDofs[0]>=0?localDofs[0]:-1-localDofs[0]);}essential.Sort();essential.Unique();}
    r.min_diffusion=r.min_reaction=std::numeric_limits<double>::infinity();
    Field forcing(w,q.forcing,q.forcing_context,0,0),diffusion(w,q.diffusion,q.diffusion_context,q.diffusion_constant,1),reaction(w,q.reaction,q.reaction_context,q.reaction_constant,2),dirichlet(w,q.dirichlet,q.dirichlet_context,q.dirichlet_constant,0),exact(w,q.exact,q.exact_context,0,0);
    // Overintegration reduces aliasing for variable native coefficients. It is a
    // quadrature choice, not a proof of global coefficient positivity/accuracy.
    const auto& rule=mfem::IntRules.Get(mfem::Geometry::TRIANGLE,static_cast<int>(2*p+6));
    mfem::LinearForm rhs(&space);auto* load=new mfem::DomainLFIntegrator(forcing);load->SetIntRule(&rule);rhs.AddDomainIntegrator(load);rhs.Assemble();
    if(w.mean){std::map<uint64_t,std::pair<long double,long double>> compatibility;for(uint64_t d=0;d<r.dofs;++d)if(unanchored.contains(dofComponent[d])){auto& value=compatibility[dofComponent[d]];value.first+=rhs[static_cast<int>(d)];value.second+=std::abs(rhs[static_cast<int>(d)]);}for(const auto& [component,value]:compatibility){(void)component;const auto relative=value.second?std::abs(value.first)/value.second:0;w.meanResult->compatibility_residual=std::max(w.meanResult->compatibility_residual,static_cast<double>(relative));if(relative>w.mean->compatibility_tolerance)throw Failure(PENTA_NUMERIC_ARGUMENT,"Pure-Neumann forcing violates the per-component zero-integral compatibility condition.");}}
    mfem::BilinearForm form(&space);auto* stiffness=new mfem::DiffusionIntegrator(diffusion);stiffness->SetIntRule(&rule);form.AddDomainIntegrator(stiffness);auto* mass=new mfem::MassIntegrator(reaction);mass->SetIntRule(&rule);form.AddDomainIntegrator(mass);form.Assemble();
    std::unique_ptr<mfem::SparseMatrix> physical;std::unique_ptr<mfem::Vector> physicalRhs;if(w.mean){form.Finalize();physical=std::make_unique<mfem::SparseMatrix>(form.SpMat());physicalRhs=std::make_unique<mfem::Vector>(rhs);}
    mfem::GridFunction solution(&space);solution=0.0;solution.ProjectBdrCoefficient(dirichlet,marker);
    mfem::OperatorPtr system;mfem::Vector X,B;form.FormLinearSystem(essential,solution,rhs,system,X,B);
    auto* matrix=system.As<mfem::SparseMatrix>();if(!matrix)throw Failure(PENTA_NUMERIC_INTERNAL,"MFEM did not assemble the expected serial sparse matrix.");
    const double tightening=w.mean?std::sqrt(static_cast<double>(r.dofs)):1;const auto* parallel=w.host.struct_size>=sizeof(penta_fem_parallel_host_v1)?reinterpret_cast<const penta_fem_parallel_host_v1*>(&w.host):nullptr;bool solverConverged=false;
    if(parallel&&parallel->parallel_for&&parallel->threads_granted>1){const auto result=parallelCg(w,*matrix,B,X,q.relative_tolerance/tightening,q.absolute_tolerance/tightening);r.iterations=result.iterations;solverConverged=result.converged;}
    else {mfem::GSSmoother preconditioner(*matrix);mfem::CGSolver solver;solver.SetOperator(*matrix);solver.SetPreconditioner(preconditioner);solver.SetRelTol(q.relative_tolerance/tightening);solver.SetAbsTol(q.absolute_tolerance/tightening);solver.SetMaxIter(static_cast<int>(q.max_iterations));solver.SetPrintLevel(-1);Monitor monitor(w,static_cast<uint64_t>(std::max(1,matrix->NumNonZeroElems())));solver.SetController(monitor);w.check();solver.Mult(B,X);w.check();r.iterations=static_cast<uint64_t>(solver.GetNumIterations());solverConverged=solver.GetConverged();}
    mfem::Vector residual(B.Size());matrix->Mult(X,residual);residual-=B;long double residualSquared=0,loadSquared=0;for(int k=0;k<B.Size();++k){residualSquared+=static_cast<long double>(residual[k])*residual[k];loadSquared+=static_cast<long double>(B[k])*B[k];}r.residual_norm=static_cast<double>(std::sqrt(residualSquared));const double norm=static_cast<double>(std::sqrt(loadSquared));r.relative_residual=norm>0?r.residual_norm/norm:r.residual_norm;
    if(!std::isfinite(r.residual_norm)||!std::isfinite(r.relative_residual))throw Failure(PENTA_NUMERIC_INTERNAL,"FEM solve produced a nonfinite algebraic residual.");
    r.converged=solverConverged&&r.residual_norm<=std::max(q.absolute_tolerance,q.relative_tolerance*norm)*1.05;
    form.RecoverFEMSolution(X,rhs,solution);
    if(w.mean){mfem::ConstantCoefficient one(1);mfem::LinearForm weights(&space);auto* integral=new mfem::DomainLFIntegrator(one);integral->SetIntRule(&rule);weights.AddDomainIntegrator(integral);weights.Assemble();std::map<uint64_t,std::pair<long double,long double>> means;for(uint64_t d=0;d<r.dofs;++d)if(unanchored.contains(dofComponent[d])){auto& value=means[dofComponent[d]];value.first+=static_cast<long double>(weights[static_cast<int>(d)])*solution[static_cast<int>(d)];value.second+=weights[static_cast<int>(d)];}for(const auto& [component,value]:means){if(value.second<=0||!std::isfinite(value.second))throw Failure(PENTA_NUMERIC_INTERNAL,"Invalid FEM component measure for a mean constraint.");const auto shift=w.mean->target_mean-value.first/value.second;for(uint64_t d=0;d<r.dofs;++d)if(dofComponent[d]==component){solution[static_cast<int>(d)]+=static_cast<double>(shift);if(!std::isfinite(solution[static_cast<int>(d)]))throw Failure(PENTA_NUMERIC_DOMAIN,"Mean-constrained FEM solution is not finite.");}}
        means.clear();for(uint64_t d=0;d<r.dofs;++d)if(unanchored.contains(dofComponent[d])){auto& value=means[dofComponent[d]];value.first+=static_cast<long double>(weights[static_cast<int>(d)])*solution[static_cast<int>(d)];value.second+=weights[static_cast<int>(d)];}for(const auto& [component,value]:means){(void)component;w.meanResult->maximum_mean_error=std::max(w.meanResult->maximum_mean_error,static_cast<double>(std::abs(value.first/value.second-w.mean->target_mean)));}
        mfem::Vector residual(physicalRhs->Size());physical->Mult(solution,residual);residual-=*physicalRhs;long double residualSquared=0,loadSquared=0;for(uint64_t d=0;d<r.dofs;++d)if(unanchored.contains(dofComponent[d])){residualSquared+=static_cast<long double>(residual[static_cast<int>(d)])*residual[static_cast<int>(d)];loadSquared+=static_cast<long double>((*physicalRhs)[static_cast<int>(d)])*(*physicalRhs)[static_cast<int>(d)];}const double physicalResidual=static_cast<double>(std::sqrt(residualSquared)),physicalLoad=static_cast<double>(std::sqrt(loadSquared));r.residual_norm=std::max(r.residual_norm,physicalResidual);r.relative_residual=std::max(r.relative_residual,physicalLoad?physicalResidual/physicalLoad:physicalResidual);r.converged=r.converged&&physicalResidual<=1.05*std::max(q.absolute_tolerance,q.relative_tolerance*physicalLoad);}
    std::vector<unsigned char> assigned(r.dofs,0);mfem::Array<int> dofs;mfem::Vector position(2);
    for(int e=0;e<mesh.GetNE();++e){w.check(local);space.GetElementDofs(e,dofs);const auto& nodes=space.GetFE(e)->GetNodes();auto* transform=mesh.GetElementTransformation(e);for(int n=0;n<nodes.GetNPoints();++n){const auto d=dofs[n]>=0?dofs[n]:-1-dofs[n];transform->Transform(nodes.IntPoint(n),position);r.dof_coordinates[2*d]=position[0];r.dof_coordinates[2*d+1]=position[1];assigned[d]=1;}}
    for(uint64_t d=0;d<r.dofs;++d){if(!assigned[d]||!std::isfinite(solution[static_cast<int>(d)]))throw Failure(PENTA_NUMERIC_INTERNAL,"MFEM returned an invalid nodal DOF.");r.dof_values[d]=solution[static_cast<int>(d)];}
    r.has_exact=q.exact!=nullptr;r.l2_error=r.has_exact?solution.ComputeL2Error(exact):0;
    if(!std::isfinite(r.l2_error))throw Failure(PENTA_NUMERIC_ARGUMENT,"FEM exact-solution L2 diagnostic overflowed.");
    // Independent per-element triangular sampling deliberately retains no fake
    // H1 connectivity. Full high-order nodes/values above remain authoritative.
    uint64_t vertex=0,face=0;
    for(int e=0;e<mesh.GetNE();++e){w.check(perVertex);const auto base=vertex;auto* transform=mesh.GetElementTransformation(e);std::vector<uint64_t> offsets(subdivisions+1);uint64_t at=0;for(uint64_t row=0;row<=subdivisions;++row){offsets[row]=at;for(uint64_t col=0;col<=subdivisions-row;++col){mfem::IntegrationPoint point;point.Set2(static_cast<double>(col)/subdivisions,static_cast<double>(row)/subdivisions);transform->Transform(point,position);r.visualization_coordinates[2*vertex]=position[0];r.visualization_coordinates[2*vertex+1]=position[1];r.visualization_values[vertex]=solution.GetValue(e,point);if(!std::isfinite(r.visualization_values[vertex]))throw Failure(PENTA_NUMERIC_INTERNAL,"FEM visualization sample is nonfinite.");++vertex;++at;}}
        auto triangle=[&](uint64_t a,uint64_t b,uint64_t c){r.visualization_faces[3*face]=base+a+1;r.visualization_faces[3*face+1]=base+b+1;r.visualization_faces[3*face+2]=base+c+1;++face;};
        for(uint64_t row=0;row<subdivisions;++row)for(uint64_t col=0;col<subdivisions-row;++col){const auto a=offsets[row]+col,b=a+1,c=offsets[row+1]+col;triangle(a,b,c);if(col+1<subdivisions-row)triangle(b,c+1,c);}
    }
    r.visualization_vertices=vertex;r.visualization_triangles=face;w.check();
}
int executeCommon(const penta_fem_host_v1* host,const penta_fem_request_v1* request,penta_fem_result_v1* result,char* error,uint64_t capacity,const penta_fem_mean_request_v1* mean,penta_fem_mean_result_v1* meanResult)noexcept{
    try{if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_FEM_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw Failure(PENTA_NUMERIC_ARGUMENT,"Incompatible FEM ABI descriptors.");SolveScope scope;result->work_units=result->evaluations=result->iterations=0;Work work{*host,*request,*result,mean,meanResult};work.check(0);std::unique_lock<std::mutex> lock(mfemMutex,std::defer_lock);while(!lock.try_lock()){work.check(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}solve(work);return 0;}
    catch(const Failure& failure){text(error,capacity,failure.what());return failure.code;}
    catch(const std::bad_alloc&){text(error,capacity,"FEM memory allocation failed within its estimated resource bound.");return PENTA_NUMERIC_RESOURCE;}
    catch(const std::exception& failure){text(error,capacity,failure.what());return PENTA_NUMERIC_INTERNAL;}
    catch(...){text(error,capacity,"MFEM raised an unknown exception.");return PENTA_NUMERIC_INTERNAL;}
}
int execute(const penta_fem_host_v1* host,const penta_fem_request_v1* request,penta_fem_result_v1* result,char* error,uint64_t capacity)noexcept{return executeCommon(host,request,result,error,capacity,nullptr,nullptr);}
int executeMean(const penta_fem_host_v1* host,const penta_fem_mean_request_v1* request,penta_fem_mean_result_v1* result,char* error,uint64_t capacity)noexcept{if(!request||request->base.struct_size<sizeof(*request)||request->abi_version!=PENTA_FEM_MEAN_ABI_V1||!result||result->base.struct_size<sizeof(*result)){text(error,capacity,"Incompatible mean-constrained FEM capability ABI.");return PENTA_NUMERIC_VERSION;}return executeCommon(host,&request->base,&result->base,error,capacity,request,result);}
const penta_fem_parallel_api_v1 api{{sizeof(penta_fem_parallel_api_v1),PENTA_FEM_ABI_V1,execute},PENTA_FEM_CAP_PARALLEL_CG_V1};
const penta_fem_mean_api_v1 meanApi{sizeof(penta_fem_mean_api_v1),PENTA_FEM_MEAN_ABI_V1,executeMean};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_fem_api_v1* penta_fem_query_v1(uint32_t version){return version==PENTA_FEM_ABI_V1?&api.base:nullptr;}
extern "C" PENTA_NUMERIC_EXPORT const penta_fem_mean_api_v1* penta_fem_mean_query_v1(uint32_t version){return version==PENTA_FEM_MEAN_ABI_V1?&meanApi:nullptr;}
