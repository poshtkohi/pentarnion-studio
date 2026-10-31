#include "export/code_export.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
int checks=0,failures=0;
void expect(bool value,const std::string& message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
void bundle(const penta::exporting::Result& result,const std::filesystem::path& directory){if(!result.ok())throw std::runtime_error(result.diagnostics.front().message);std::filesystem::create_directories(directory);for(const auto& file:result.files){expect(std::filesystem::path(file.name).filename()==file.name,"Safe flat export file name");std::ofstream(directory/file.name,std::ios::binary)<<file.contents;}}
}
int main(int argc,char** argv){try{
    using namespace penta::exporting;
    const auto output=argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/"penta-export-tests";
    const std::string basic=R"P(use_algebra("A_gamma",gamma=1);
pentarnion p=1+i+j+ell;
pentarnion q=1-i+j-ell;
pentarnion r=p*q;
assert(coeff(p+q,0)==2);
assert(coeff(p+q,2)==2);
assert(abs(coeff(r,1)-sqrt(2))<1e-12);
assert(abs(coeff(r,3)-2*sqrt(2))<1e-12);
pentarnion bracket_left=(i*j)*ell;
pentarnion bracket_right=i*(j*ell);
assert(norm2(bracket_left-bracket_right)>0.01);
assert(norm2(exp(i)-cos(1)-i*sin(1))<1e-24);
matrix A=[1,2,3;4,5,6];
matrix B=reshape(A,3,2);
assert(B==[1,5;4,3;2,6]);
assert(A(2)==4);assert(A(end)==6);
matrix selected=A(A>3);assert(selected==[4;5;6]);
matrix saved=A;A(A>3)=0;assert(saved(2)==4);assert(A(2)==0);
matrix row=[1,2,3];matrix picked=row([3,1]);assert(picked==[3,1]);
matrix col=[1;2;3];matrix column_pick=col([3,1]);assert(column_pick==[3;1]);
matrix product=[1,2;3,4]*[5;6];assert(product==[17;39]);
matrix entries=[1,2;3,4].*[5,6;7,8];assert(entries==[5,12;21,32]);
pentarnionmatrix P=[i,j];pentarnionmatrix Q=[k;ell];
pentarnionmatrix ordered=P*Q;assert(norm2(ordered(1)-(i*k+j*ell))<1e-24);
integer a_b=3;integer ax5fb=4;assert(a_b+ax5fb==7);
function real square(real x){return x*x;}
real squared=square(3.0);assert(squared==9);
integer total=0;for(idx=1:5){total=total+idx;}assert(total==15);
integer count=0;while(count<3){count=count+1;}assert(count==3);
if(total==15){display("PORTABLE_ALGEBRA_ARRAYS_PASS");}else{assert(0);}
)P";
    const auto checked=penta::language::analyse(basic);expect(checked.ok(),"Export fixture checks in Studio");for(const auto& d:checked.diagnostics)std::cerr<<d.code<<" "<<d.span.offset<<": "<<d.message<<'\n';
    for(const auto target:{Target::Python,Target::Matlab}){auto result=exportProgram(checked,target,Options{"arithmetic-and-shapes.p"});expect(result.ok(),"Both arithmetic targets export");expect(result.files.size()==4&&!result.sourceMap.empty(),"Runtime, compatibility report and source map emitted");if(result.ok())bundle(result,output/(target==Target::Python?"python-basic":"matlab-basic"));}
    const auto unsupported=penta::language::analyse("rng(1);");expect(unsupported.ok(),"Unsupported construct is valid Studio code");const auto refused=exportProgram(unsupported,Target::Python);expect(!refused.ok()&&refused.files.empty()&&refused.diagnostics.front().span.length>0,"Unsupported AST produces a span and no executable bundle");
    const auto invalid=exportProgram(penta::language::analyse("real a=;"),Target::Python);expect(!invalid.ok()&&invalid.files.empty(),"Invalid source cannot be exported");
    const auto figure=penta::language::analyse("pentarnion p=i+j;figure(1,\"Name\",\"Portable vectors\",\"NumberTitle\",\"off\");subplot(1,2,1);plot2(\"i\",\"j\",p);title(\"Arrow\");matrix t=linspace(0,1,11);subplot(1,2,2);plot(t,t.*t);xlabel(\"t\");ylabel(\"t squared\");");expect(figure.ok(),"Basic figure fixture checks");for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(figure,target);expect(result.ok(),"Basic figures export for both targets");if(result.ok())bundle(result,output/(target==Target::Python?"python-figures":"matlab-figures"));}
    const auto ode=penta::language::analyse(R"P(use_algebra("A_gamma",gamma=1);
ode decay(t,real x,pentarnion p;real rate){x'=-rate*x;p'=-rate*p;}
solution s=odesolve(decay,x=1,p=1+i+j+k+ell,rate=0.7,t0=0,t1=2,sample_step=0.05,method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
assert(abs(s.x(end)-exp(-1.4))<1e-8);
assert(norm2(s.p(end)-(1+i+j+k+ell)*exp(-1.4))<1e-16);
display("PORTABLE_ANALYTIC_JACOBIAN_ODE_PASS");)P");expect(ode.ok(),"Mixed real/Pentarnion ODE fixture checks");for(const auto& d:ode.diagnostics)std::cerr<<d.code<<" "<<d.span.offset<<": "<<d.message<<'\n';
    expect(!exportProgram(ode,Target::Python).ok(),"Solver substitution requires explicit adapter opt-in");
    const auto adapted=exportProgram(ode,Target::Python,Options{"real-and-pentarnion-decay.p",true});expect(adapted.ok(),"Explicit SciPy BDF/analytic Jacobian export succeeds");if(adapted.ok())bundle(adapted,output/"python-ode");
    expect(!exportProgram(ode,Target::Matlab,Options{"real-and-pentarnion-decay.p",true}).ok(),"MATLAB solver substitution has its own explicit opt-in");
    const auto matlabAdapted=exportProgram(ode,Target::Matlab,Options{"real-and-pentarnion-decay.p",false,true});expect(matlabAdapted.ok(),"Explicit MATLAB ode15s/analytic Jacobian export succeeds");if(matlabAdapted.ok()){bundle(matlabAdapted,output/"matlab-ode");expect(matlabAdapted.files.size()==5,"MATLAB ODE bundles the checked analytic dual calculus");}
    const auto structures=penta::language::analyse(R"P(
function [real low,real high] bounds(real center,real radius=1.0){return center-radius,center+radius;}
function real scale(real x,options cfg=options(gain=2.0,offset=1.0)){return x*cfg.gain+cfg.offset;}
[real low,real high]=bounds(radius=2.0,center=10.0);assert(low==8&&high==12);
[real default_low,real default_high]=bounds(10.0);assert(default_low==9&&default_high==11);
assert(scale(3.0)==7);record cfg=options(gain=3.0,offset=4.0);assert(scale(cfg=cfg,x=2.0)==10);
real gain=2.0;callback captured=@(real x)=>gain*x;gain=99.0;assert(captured(5.0)==10);
complex z=complex(.2,.3);assert(abs(exp(z)-exp(.2)*(cos(.3)+complex(0,1)*sin(.3)))<1e-13);
assert(realpart(z)==.2&&imagpart(z)==.3);assert(abs(conj(z)-complex(.2,-.3))<1e-13);
record nested=options(model=options(rate=.7),value=z);assert(nested.model.rate==.7&&nested.value==z);
matrix A=[3,1;1,2];matrix exact=[2;-1];assert(norm(linsolve(A,A*exact)-exact)<1e-12);
assert(norm(A*inv(A)-eye(2),"fro")<1e-12);assert(abs(det(A)-5)<1e-12);
pentarnion a=2+.2*i+.3*j;pentarnion p=1-i+.5*ell;
pentarnion left_result=a\(a*p);pentarnion right_result=(p*a)/a;
assert(norm2(left_result-p)<1e-20&&norm2(right_result-p)<1e-20);
pentarnion ap=a*p;matrix ap_coefficients=[coeff(ap,0);coeff(ap,1);coeff(ap,2);coeff(ap,3);coeff(ap,4)];
assert(norm(leftop([a])*[1;-1;0;0;.5]-ap_coefficients)<1e-12);
display("PORTABLE_STRUCTURES_AND_EQUATIONS_PASS");
)P");expect(structures.ok(),"Records, tuples, named/default calls, captured lambdas, complex and ordered equations check");for(const auto&d:structures.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
    for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(structures,target,Options{"structures-and-equations.p"});expect(result.ok(),"Both target adapters export checked structures/equations");for(const auto&d:result.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-structures":"matlab-structures"));}
    const auto fitting=penta::language::analyse(R"P(
native function [real first,real second] residual(real a,real b){return a-2.0,b+1.0;}
record fit=lsqfit(residual,[0,0],options(covariance=1,noise_variance=.04));
assert(fit.converged&&fit.covariance_valid);assert(norm(fit.x-[2,-1])<1e-8);
assert(norm(fit.covariance-.04*eye(2),"fro")<1e-10);
native function real affine_model(real slope,real offset,real x){return slope*x+offset;}
matrix observed_x=linspace(0,1,11);matrix observed_y=2*observed_x+1;
record fitted_curve=curvefit(affine_model,[0,0],observed_x,observed_y,options(covariance=1));
assert(norm(fitted_curve.x-[2,1])<1e-8);assert(norm(fitted_curve.fitted-observed_y.')<1e-8);
display("PORTABLE_SCIENTIFIC_FIT_PASS");
)P");expect(fitting.ok(),"Checked fitting/curve adapter fixture checks");for(const auto&d:fitting.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';expect(!exportProgram(fitting,Target::Python).ok(),"Scientific algorithm substitution requires explicit adapter opt-in");for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(fitting,target,Options{"native-fitting.p",false,false,true});expect(result.ok(),"Both explicit fitting target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-fitting":"matlab-fitting"));}
    const auto optimization=penta::language::analyse(R"P(
native function real bowl(real x,real y,real cx,real cy){return (x-cx)*(x-cx)+(y-cy)*(y-cy);}
record minimum=minimize(bowl,[0,0],options(parameters=options(cx=2.0,cy=1.0),algorithm="lbfgs",max_evaluations=500));
assert(minimum.converged&&norm(minimum.x-[2,1])<1e-6);assert(minimum.native_evaluations==0);
native function real constrained_bowl(real x,real y){return (x-2)*(x-2)+(y-1)*(y-1);}
native function real halfplane(real x,real y){return x+y-1;}
record constrained=minimize(constrained_bowl,[0,0],options(algorithm="slsqp",inequality=halfplane,max_evaluations=1000));
assert(constrained.feasible&&norm(constrained.x-[1,0])<1e-5);
record interrupted=minimize(constrained_bowl,[0,0],options(algorithm="lbfgs",max_evaluations=1));
assert(!interrupted.converged&&norm(interrupted.x)<1e-12);
display("PORTABLE_OPTIMIZATION_PASS");
)P");expect(optimization.ok(),"Checked target objective/constraint fixture checks");for(const auto&d:optimization.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(optimization,target,Options{"native-objective-and-constraints.p",false,false,true});expect(result.ok(),"Both explicit target optimization adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-optimization":"matlab-optimization"));}
    const auto graphs=penta::language::analyse(R"P(
record network=graph([1,2;2,3;3,1;3,3],options(n=4,weights=[1,2,4,7]));
record graph_info=graphinfo(network);assert(graph_info.degree==[2;2;4;0]);
assert(graphadjacency(network)==[0,1,4,0;1,0,2,0;4,2,7,0;0,0,0,0]);
assert(graphlaplacian(network)==[5,-1,-4,0;-1,3,-2,0;-4,-2,6,0;0,0,0,0]);
record parts=graphcomponents(network);assert(parts.count==2);
record path=shortestpath(network,1,3);assert(path.distance==3&&path.path==[1,2,3]);
record forest=spanningtree(network);assert(forest.total_weight==3&&!forest.spanning);
record cycle=graphcycle(network);assert(cycle.found==1);
record directed_network=digraph([1,2;2,1;2,3;4,4],options(n=4));
record strong_parts=graphcomponents(directed_network,options(kind="strong"));assert(strong_parts.count==3);
record order=toposort(directed_network);assert(!order.acyclic);
record negative_network=digraph([1,2;2,3;3,2],options(weights=[1,-2,1]));
record failed_path=shortestpath(negative_network,1,3);assert(failed_path.negative_cycle&&!failed_path.distance_available);
record chain=graph([1,2;2,3]);record between=centrality(chain,options(kind="betweenness"));assert(between.scores==[0;1;0]);
record closeness_result=centrality(chain,options(kind="closeness"));assert(norm(closeness_result.scores-[2/3;1;2/3])<1e-12);
matrix positions=graphlayout(chain,options(dimensions=3));assert(size(positions,2)==3);
display("PORTABLE_GRAPH_ANALYSIS_PASS");
)P");expect(graphs.ok(),"Graph target fixture checks in Studio");for(const auto&d:graphs.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(graphs,target,Options{"weighted-graph-analysis.p",false,false,true});expect(result.ok(),"Both explicit graph target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-graphs":"matlab-graphs"));}
    const auto pdes=penta::language::analyse(R"P(
native function real zero_reaction(real u,real t,real x){return 0.0;}
matrix x=linspace(0,1,21);matrix initial=sin(3.141592653589793*x);
record fd=pdesolve(zero_reaction,x,initial,options(method="fd",boundary="dirichlet",diffusion=.1,t1=.01,time_step=.0001,sample_step=.005));
record fem=pdesolve(zero_reaction,x,initial,options(method="fem",boundary="dirichlet",diffusion=.1,t1=.01,time_step=.0001,sample_step=.005));
matrix exact=exp(-.1*3.141592653589793*3.141592653589793*.01)*initial;
assert(norm(fd.values(end,:)-exact)<.0001&&norm(fem.values(end,:)-exact)<.0001);
record imex=pdesolve(zero_reaction,x,initial,options(method="fem",integrator="imex_euler",boundary="dirichlet",diffusion=.1,t1=.01,time_step=.0001,sample_step=.005));
assert(norm(imex.values(end,:)-exact)<.0001&&imex.linear_solves>0);
native function pentarnion pent_decay(pentarnion u,real t,real x){return -u;}
pentarnionmatrix field=ones(1,21)*(1+2*i-j+.5*k+.25*ell);
record pent_field=pdesolve(pent_decay,x,field,options(method="fem",boundary="neumann",diffusion=.1,t1=.01,time_step=.0001));
matrix coefficient=pdecoeff(pent_field,1);assert(norm(coefficient(end,:)-2*exp(-.01)*ones(1,21))<1e-10);
assert(norm2(pent_field.mean(end)-(1+2*i-j+.5*k+.25*ell)*exp(-.01))<1e-20);
display("PORTABLE_FD_FEM_PDE_PASS");
)P");expect(pdes.ok(),"PDE target fixture checks in Studio");for(const auto&d:pdes.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(pdes,target,Options{"diffusion-and-whole-field-decay.p",false,false,true});expect(result.ok(),"Both explicit1D PDE target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-pdes":"matlab-pdes"));}
    const auto mechanicsGeometry=penta::language::analyse(R"P(
native function real spring_force(real t,real q){return -q;}
record rk_motion=mechanics(spring_force,1.0,0.0,options(method="rk4",t1=1.0,time_step=.001,sample_step=.02));
record verlet_motion=mechanics(spring_force,1.0,0.0,options(method="verlet",t1=1.0,time_step=.001,sample_step=.02));
assert(abs(rk_motion.q(end)-cos(1.0))<1e-7&&abs(verlet_motion.v(end)+sin(1.0))<1e-7);
assert(norm(rk_motion.energy_balance)<.001);
matrix theta=linspace(0,3.141592653589793,101);matrix circle=[cos(theta).',sin(theta).'];
record curve=curvegeometry(circle);assert(abs(curve.length-3.141592653589793)<.001&&abs(curve.curvature(51)-1)<.001);
figure(4);surf([-1,1],[-1,1],[1,0;0,1]);
figure(5);imagesc([-1,1],[-1,1],[1,0;0,1]);
figure(6);contour([-1,1],[-1,1],[1,0;0,1],[.25,.5,.75]);
figure(7);isosurface([-1,1],[-1,1],[-1,1],[-1,1,-1,1,-1,1,-1,1],0.0);
display("PORTABLE_NEWTON_GEOMETRY_PASS");
)P");expect(mechanicsGeometry.ok(),"Newton mechanics and geometry target fixture checks");for(const auto&d:mechanicsGeometry.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(mechanicsGeometry,target,Options{"oscillator-and-sampled-geometry.p",false,false,true});expect(result.ok(),"Both Newton mechanics/geometry target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-mechanics-geometry":"matlab-mechanics-geometry"));}
    const auto advanced=penta::language::analyse(R"P(
ode nonlinear(t,real x;real rate){x'=-rate*x*x;}
native function real threshold(real t,real x,real rate){return x-.5;}
solution event_solution=odesolve(nonlinear,x=1.,rate=2.,t0=0.,t1=1.,sample_step=.1,rtol=1e-10,atol=1e-12,sensitivities="all",events=threshold,event_direction=-1,event_terminal=1);
assert(abs(event_solution.t(end)-.5)<1e-8&&abs(event_solution.x(end)-.5)<1e-9);
matrix parameter_sensitivity=odesens(event_solution,1);matrix initial_sensitivity=odesens(event_solution,2);
assert(abs(parameter_sensitivity(end)+.125)<1e-8&&abs(initial_sensitivity(end)-.25)<1e-8);
record localized=odeevents(event_solution);assert(localized.terminal&&localized.index(1)==1);
matrix dense=deval(event_solution,[.1,.2],"x");assert(norm(dense-[1/1.2,1/1.4])<1e-8);
native function real mass_rhs(real t,real x,real rate){return -(1+t+x)*rate*x;}
native function matrix<1,1> state_mass(real t,real x){return [1+t+x];}
record regular_mass=masssolve(mass_rhs,state_mass,1.,-2.,options(parameters=options(rate=2.),t1=1.,rtol=1e-10,atol=1e-12));
assert(abs(regular_mass.values(end)-exp(-2))<1e-8&&regular_mass.maximum_sample_residual<1e-11);
native function real lagrangian(real t,real q,real v){return .5*v*v-.5*q*q;}
native function real hamiltonian(real t,real q,real p){return .5*p*p+.5*q*q;}
record lagrange_motion=mechanics(lagrangian,1.,0.,options(form="lagrangian",t1=1.,time_step=.005,sample_step=.02));
record hamilton_motion=mechanics(hamiltonian,1.,0.,options(form="hamiltonian",method="midpoint",t1=1.,time_step=.005,sample_step=.02));
assert(abs(lagrange_motion.q(end)-cos(1))<2e-6&&abs(hamilton_motion.v(end)+sin(1))<2e-6);
assert(norm(hamilton_motion.energy_balance)<1e-8);
display("PORTABLE_ADVANCED_ODE_ANALYTIC_MECHANICS_PASS");
)P");expect(advanced.ok(),"Events, sensitivities, state/time mass and analytical mechanics target fixture checks");for(const auto&d:advanced.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(advanced,target,Options{"events-mass-sensitivities-analytical-mechanics.p",true,true,true});expect(result.ok(),"Both advanced analytic target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-advanced-ode":"matlab-advanced-ode"));}
    const auto hybrids=penta::language::analyse(R"P(
ode flight(t,real h,real v;real gravity,real restitution){h'=v;v'=-gravity;}
native function real ground(real t,real h,real v,real gravity,real restitution){return h;}
native function [real height,real velocity] bounce(real t,real h,real v,real gravity,real restitution){return 0.0,-restitution*v;}
record motion=hybridsolve(flight,ground,bounce,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.0,sample_step=.05,rtol=1e-10,atol=1e-12,event_direction=-1));
real impact_time=sqrt(2/9.81);assert(motion.events==1&&abs(motion.event_times(1)-impact_time)<1e-8);
assert(abs(motion.event_time_derivatives(1,1)+impact_time/(2*9.81))<1e-8);
matrix pre=hybridval(motion,motion.event_times(1),"pre");matrix post=hybridval(motion,motion.event_times(1),"post");assert(pre(2)<0&&post(2)>0);
record stopped=hybridsolve(flight,ground,bounce,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.0,rtol=1e-10,atol=1e-12,event_direction=-1,event_terminal=1));
assert(abs(stopped.terminal_value_derivatives(1,2)-.4*sqrt(2/9.81))<1e-8);
native function [real first,real second] sections(real t,real h,real v,real gravity,real restitution){return h,2*h;}
native function [real height,real velocity] ordered_reset(real event_index,real t,real h,real v,real gravity,real restitution){if(event_index==1){return 0.0,-restitution*v;}return h,v+1.0;}
record simultaneous=hybridsolve(flight,sections,ordered_reset,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.0,rtol=1e-10,atol=1e-12,event_direction=-1,event_terminal=1,event_priority=[2,1],simultaneous="all"));
assert(simultaneous.event_info.group_indices==[2,1]);assert(abs(simultaneous.event_after(1,2)+.8*(-sqrt(2*9.81)+1))<1e-8);
display("PORTABLE_HYBRID_RESETS_SALTATION_PASS");
)P");expect(hybrids.ok(),"Hybrid reset/priority/saltation target fixture checks");for(const auto&d:hybrids.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(hybrids,target,Options{"hybrid-reset-priority-saltation.p",true,true,true});expect(result.ok(),"Both hybrid epoch target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-hybrid":"matlab-hybrid"));}
    const auto reverseCosts=penta::language::analyse(R"P(
ode decay_cost(t,real x;real rate){x'=-rate*x;}
native function real terminal_cost(real t,real x,real rate){return .5*x*x;}
native function real running_cost(real t,real x,real rate){return .3*x*x;}
record cost=odeadjoint(decay_cost,1.,options(parameters=options(rate=.7),objective=terminal_cost,running_cost=running_cost,t0=0.,t1=2.,sample_step=.2,rtol=1e-10,atol=1e-12));
real expected=.5*exp(-2.8)+.3*(1-exp(-2.8))/1.4;
real expected_rate=-2*exp(-2.8)+.3*(-(1-exp(-2.8))/(2*.7*.7)+2*exp(-2.8)/.7);
assert(abs(cost.objective-expected)<1e-8&&abs(cost.initial_gradient(1)-2*expected)<1e-8&&abs(cost.parameter_gradient(1)-expected_rate)<1e-8);
ode flight_cost(t,real h,real v;real gravity,real restitution){h'=v;v'=-gravity;}
native function real ground_cost(real t,real h,real v,real gravity,real restitution){return h;}
native function [real height,real velocity] reset_cost(real t,real h,real v,real gravity,real restitution){return 0.0,-restitution*v;}
native function real hybrid_terminal(real t,real h,real v,real gravity,real restitution){return .5*v*v+.3*h+.2*t;}
native function real hybrid_running(real t,real h,real v,real gravity,real restitution){return .2*(h*h+v*v);}
native function real hybrid_event_cost(real t,real h,real v,real gravity,real restitution){return .1*v*v+.05*t;}
function real closed_cost(real gravity,real restitution,real terminal_flag){
    real impact=sqrt(2/gravity);real time_final=1.;if(terminal_flag){time_final=impact;}
    real duration=time_final-impact;real upward=restitution*gravity*impact;
    real pre_integral=.2*(impact+(gravity*gravity-gravity)*impact^3/3+gravity*gravity*impact^5/20);
    real post_integral=.2*(upward*upward*duration^3/3-upward*gravity*duration^4/4+gravity*gravity*duration^5/20+upward*upward*duration-upward*gravity*duration^2+gravity*gravity*duration^3/3);
    real height=upward*duration-.5*gravity*duration^2;real velocity=upward-gravity*duration;
    return .5*velocity*velocity+.3*height+.2*time_final+pre_integral+post_integral+.1*gravity*gravity*impact*impact+.05*impact;
}
for(terminal_flag=0:1){
    record reverse=hybridadjoint(flight_cost,ground_cost,reset_cost,hybrid_terminal,[1,0],options(parameters=options(gravity=9.81,restitution=.8),t1=1.,sample_step=.2,rtol=1e-10,atol=1e-12,event_direction=-1,event_terminal=terminal_flag,running_cost=hybrid_running,event_cost=hybrid_event_cost));
    assert(abs(reverse.adjoint.objective-closed_cost(9.81,.8,terminal_flag))<1e-7);
    real gravity_derivative=(closed_cost(9.81001,.8,terminal_flag)-closed_cost(9.80999,.8,terminal_flag))/.00002;
    real restitution_derivative=(closed_cost(9.81,.80001,terminal_flag)-closed_cost(9.81,.79999,terminal_flag))/.00002;
    assert(abs(reverse.adjoint.parameter_gradient(1)-gravity_derivative)<2e-5&&abs(reverse.adjoint.parameter_gradient(2)-restitution_derivative)<2e-5);
}
display("PORTABLE_CONTINUOUS_HYBRID_REVERSE_COSTS_PASS");
)P");expect(reverseCosts.ok(),"Continuous/hybrid reverse-cost target fixture checks");for(const auto&d:reverseCosts.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto target:{Target::Python,Target::Matlab}){const auto result=exportProgram(reverseCosts,target,Options{"continuous-and-hybrid-reverse-costs.p",true,true,true});expect(result.ok(),"Both reverse-cost target adapters export");for(const auto&d:result.diagnostics)std::cerr<<d.message<<'\n';if(result.ok())bundle(result,output/(target==Target::Python?"python-reverse-costs":"matlab-reverse-costs"));}
    const auto complexDerivative=penta::language::analyse("native function real complex_phase(real x){complex z=complex(x,0.);return realpart(exp(z));}record f=lsqfit(complex_phase,1.);");
    const auto refusedDerivative=exportProgram(complexDerivative,Target::Python,Options{"unmapped-complex-derivative.p",false,false,true});expect(!refusedDerivative.ok()&&refusedDerivative.files.empty(),"Unsupported analytic complex callback calculus never becomes a runnable target bundle");
    penta::language::AnalysisOptions imports;imports.moduleResolver=[](const std::string&,const std::filesystem::path&)->std::optional<penta::language::ModuleSource>{return penta::language::ModuleSource{"helpers.p","function real cube(real x){return x*x*x;}"};};
    const auto module=penta::language::analyse("import \"helpers.p\" as helper;real value=helper.cube(3.0);assert(value==27);",imports);expect(module.ok(),"Resolved import fixture checks");const auto imported=exportProgram(module,Target::Python,Options{"resolved-import.p"});expect(imported.ok(),"Resolved imported function exports");for(const auto& d:imported.diagnostics)std::cerr<<d.message<<'\n';if(imported.ok()){bundle(imported,output/"python-import");bool mapped=false;for(const auto& item:imported.sourceMap)if(item.source=="helpers.p")mapped=true;expect(mapped,"Imported definition retains original source identity");}
}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::cout<<checks<<" export checks, "<<failures<<" failures\n";return failures?1:0;}
