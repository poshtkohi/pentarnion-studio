#include "mechanics/mechanics_module.hpp"
#ifndef PENTA_MECHANICS_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>
namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F&& f,const std::string& label){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,label);}
struct Model {
    unsigned form=0,kind=0,n=1;double mass=1,stiffness=1,damping=.2;std::atomic_uint64_t calls{0};std::shared_ptr<std::atomic_bool> cancellation;
    static int evaluate(void* raw,double,const double* q,const double* second,uint32_t order,double* out,double* gradient,double* hessian,char*,uint64_t){auto& self=*static_cast<Model*>(raw);if(self.calls.fetch_add(1)>3&&self.cancellation)self.cancellation->store(true);const unsigned n=self.n,m=1+2*n;if(order)std::fill_n(gradient,m,0);if(order==2)std::fill_n(hessian,m*m,0);
        if(self.form==0){double radius=0;if(self.kind==2)for(unsigned j=0;j<n;++j)radius+=q[j]*q[j];if(self.kind==2&&radius==0)return 1;for(unsigned j=0;j<n;++j)out[j]=self.kind==2?-q[j]/std::pow(radius,1.5):-self.stiffness*q[j];}
        else if(self.form==1){const double v=second[0];if(self.kind==3){out[0]=.5*(1+q[0]*q[0])*v*v-.5*q[0]*q[0];if(order){gradient[1]=q[0]*v*v-q[0];gradient[2]=(1+q[0]*q[0])*v;}if(order==2){hessian[4]=v*v-1;hessian[5]=hessian[7]=2*q[0]*v;hessian[8]=1+q[0]*q[0];}}
            else{out[0]=.5*self.mass*v*v+(self.kind==1?self.stiffness*std::cos(q[0]):-.5*self.stiffness*q[0]*q[0]);if(order){gradient[1]=self.kind==1?-self.stiffness*std::sin(q[0]):-self.stiffness*q[0];gradient[2]=self.mass*v;}if(order==2){hessian[4]=self.kind==1?-self.stiffness*std::cos(q[0]):-self.stiffness;hessian[8]=self.mass;}}}
        else{out[0]=.5*(self.stiffness*q[0]*q[0]+second[0]*second[0]/self.mass)+(self.kind==4?.2*q[0]*second[0]:0);if(order){gradient[1]=self.stiffness*q[0]+(self.kind==4?.2*second[0]:0);gradient[2]=second[0]/self.mass+(self.kind==4?.2*q[0]:0);}if(order==2){hessian[4]=self.stiffness;hessian[8]=1/self.mass;if(self.kind==4)hessian[5]=hessian[7]=.2;}}return 0;
    }
    static int force(void* raw,double,const double*,const double* velocity,double* out,char*,uint64_t){const auto& self=*static_cast<Model*>(raw);out[0]=-self.damping*velocity[0];return 0;}
};
struct Problem {
    std::vector<double> q,second,masses;penta_mechanics_request_v1 request{};
    Problem(Model& model,unsigned n=1):q(n,1),second(n,0),masses(n,1){model.n=n;auto& r=request;r.struct_size=sizeof(r);r.dimension=n;r.form=model.form;r.method=model.form==2?2:0;r.position_only=1;r.max_newton_iterations=20;r.initial_q=q.data();r.initial_second=second.data();r.masses=masses.data();r.t1=1;r.time_step=.01;r.sample_step=.1;r.max_wall_seconds=30;r.newton_tolerance=1e-12;r.regularity_tolerance=1e-12;r.max_steps=1000000;r.max_evaluations=1000000;r.model_context=&model;r.model=Model::evaluate;}
};
void nativeTests(){
    penta::mechanics::Module module;
    for(const unsigned form:{0u,1u,2u}){Model model;model.form=form;Problem p(model);const auto s=module.solve(p.request,{},[](std::size_t){});const double tolerance=form==2?2e-5:1e-8;require(std::abs(s.q.back()-std::cos(1))<tolerance,"Newton/Lagrange/Hamilton oscillator position");require(std::abs(s.velocity.back()+std::sin(1))<tolerance,"mechanical velocity");require(std::abs(s.momentum.back()-s.velocity.back())<1e-13,"momentum from mass/Lv/canonical state");require(s.report.samples==11&&s.times.back()==1,"mechanics retained times");if(form)require(s.report.max_relative_energy_drift<1e-9,"autonomous oscillator defined energy");else require(!s.report.energy_defined&&s.kinetic.back()>0,"Newton diagnostic is kinetic only");}
    double previous=0;
    for(const double step:{.1,.05,.025}){Model model;Problem p(model);p.request.time_step=step;p.request.sample_step=1;const auto s=module.solve(p.request,{},[](std::size_t){});const double e=std::abs(s.q.back()-std::cos(1));if(previous)require(previous/e>14&&previous/e<18,"Newton RK4 fourth-order convergence");previous=e;}
    previous=0;
    for(const double step:{.1,.05,.025}){Model model;model.form=2;model.kind=4;Problem p(model);p.second[0]=.3;p.request.time_step=step;p.request.sample_step=1;const auto s=module.solve(p.request,{},[](std::size_t){});const double omega=std::sqrt(.96),q=std::cos(omega)+.5/omega*std::sin(omega),e=std::abs(s.q.back()-q);if(previous)require(previous/e>3.8&&previous/e<4.2,"nonseparable Hamiltonian midpoint second-order convergence");previous=e;require(s.report.max_relative_energy_drift<1e-10,"quadratic nonseparable Hamiltonian energy");}
    {Model model;model.form=1;model.kind=1;Problem p(model);p.q[0]=.5;p.request.t1=4;p.request.time_step=.002;const auto s=module.solve(p.request,{},[](std::size_t){});require(s.report.max_relative_energy_drift<1e-9,"nonlinear pendulum Lagrangian energy");require(s.q.back()<.5&&s.q.back()>-.51,"bounded pendulum trajectory");}
    {Model model;model.form=1;model.kind=3;Problem p(model);p.second[0]=.3;const auto s=module.solve(p.request,{},[](std::size_t){});require(s.report.max_relative_energy_drift<1e-8,"coordinate-dependent regular Lagrangian mass");}
    {Model model;model.form=1;Problem p(model);p.request.force_context=&model;p.request.force=Model::force;const auto s=module.solve(p.request,{},[](std::size_t){});const double omega=std::sqrt(1-.01),q=std::exp(-.1)*(std::cos(omega)+.1/omega*std::sin(omega));require(std::abs(s.q.back()-q)<1e-8,"native generalized force in Euler-Lagrange equations");require(s.energy.back()<s.energy.front(),"damped physical energy decreases");}
    {Model model;model.kind=2;Problem p(model,2);p.q[1]=0;p.second[1]=1;p.request.method=1;p.request.t1=2*std::numbers::pi;p.request.time_step=.005;p.request.sample_step=.1;const auto s=module.solve(p.request,{},[](std::size_t){});double maxEnergyError=0,maxAngularError=0;for(std::size_t row=0;row<s.report.samples;++row){const double x=s.q[row*2],y=s.q[row*2+1],px=s.momentum[row*2],py=s.momentum[row*2+1];maxEnergyError=std::max(maxEnergyError,std::abs(s.kinetic[row]-1/std::hypot(x,y)+.5));maxAngularError=std::max(maxAngularError,std::abs(x*py-y*px-1));}require(std::abs(s.q[s.q.size()-2]-1)<1e-4&&std::abs(s.q.back())<2e-4,"Kepler Verlet one-orbit reference");require(maxEnergyError<1e-7&&maxAngularError<1e-12,"Kepler numerical energy/angular momentum diagnostics");}
    {Model model;Problem p(model,5);const auto s=module.solve(p.request,{},[](std::size_t){});for(unsigned j=0;j<5;++j)require(std::abs(s.q[(s.report.samples-1)*5+j]-std::cos(1))<1e-8,"five coordinate Newton output");}
    {Model model;Problem p(model);p.request.method=1;p.request.position_only=0;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"Verlet velocity-dependent force layout refused");p.request.method=2;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"non-Hamiltonian midpoint refused");p.request.method=0;p.masses[0]=0;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"nonpositive Newton mass refused");p.masses[0]=1;p.request.max_steps=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"mechanics step cap");p.request.max_steps=100000;p.request.max_evaluations=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"mechanics model evaluation cap");p.request.max_evaluations=100000;auto flag=std::make_shared<std::atomic_bool>(false);model.calls=0;model.cancellation=flag;rejects([&]{module.solve(p.request,flag,[](std::size_t){});},"mechanics cooperative cancellation");}
    {Model model;model.form=1;model.mass=0;Problem p(model);rejects([&]{module.solve(p.request,{},[](std::size_t){});},"singular Lagrangian refused");}
    {Model model;model.form=2;Problem p(model);p.request.max_newton_iterations=1;rejects([&]{module.solve(p.request,{},[](std::size_t){});},"implicit midpoint iteration limit");}
}
#ifndef PENTA_MECHANICS_KERNEL_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-mechanics-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& source,bool good=true){const auto result=penta::runtime::execute(source,{},options);require(result.ok()==good,"compiled mechanics language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string force="native function real force(real t,real q,real stiffness){return -stiffness*q;}";
    run(force+"record a=mechanics(force,1.0,0.0,options(parameters=options(stiffness=1.0),mass=1.0,method=\"verlet\",t1=1.0,time_step=.001));assert(abs(a.q(end,1)-cos(1))<1e-6&&a.energy_defined==0);record b=mechanics(force,1.0,0.0,options(parameters=options(stiffness=2.0),mass=2.0,method=\"verlet\",t1=1.0,time_step=.001));assert(a.cache_key==b.cache_key&&b.cache_hit);assert(norm(b.p-2*b.v,\"fro\")<1e-12);");
    run("native function real H(real t,real q,real p,real mass,real stiffness){return .5*(p*p/mass+stiffness*q*q);}record a=mechanics(H,1.0,0.0,options(form=\"hamiltonian\",parameters=options(mass=2.0,stiffness=8.0),t1=.5,time_step=.01));assert(a.second_input==\"momentum\"&&a.native_jacobian==\"analytic_hamiltonian_hessian\");assert(abs(a.q(end,1)-cos(1))<1e-4&&a.max_relative_energy_drift<1e-9);assert(norm(a.p-2*a.v,\"fro\")<1e-10);");
    run("native function real L(real t,real q,real v){return .5*v*v+cos(q);}record a=mechanics(L,.5,0.0,options(form=\"lagrangian\",t1=1.0,time_step=.005));assert(a.energy_defined&&a.max_relative_energy_drift<1e-8);assert(a.second_input==\"velocity\");");
    run("native function real L(real t,real q,real v){return .5*v*v-.5*q*q;}native function real Q(real t,real q,real v,real damping){return -damping*v;}record a=mechanics(L,1.0,0.0,options(form=\"lagrangian\",force=Q,force_parameters=options(damping=.2),t1=1.0));assert(a.force_evaluations>0&&a.energy(end)<a.energy(1));");
    run("native function pentarnion force(real t,pentarnion q){return -q;}record a=mechanics(force,1+i+j+k+ell,0*i,options(method=\"verlet\",t1=.1,time_step=.001));assert(a.coordinate_dimension==5&&norm(a.q(end,:)-cos(.1)*ones(1,5),2)<1e-6);");
    run(force+"record a=mechanics(force,1.0,0.0,options(parameters=options(stiffness=1.0),mass=0));",false);
    run("native function real force(real t,real q,real v){return -q-.2*v;}record a=mechanics(force,1.0,0.0,options(force_layout=\"state\",method=\"verlet\"));",false);
    run("native function real L(real t,real q,real v){return -q*q;}record a=mechanics(L,1.0,0.0,options(form=\"lagrangian\"));",false);
    run(force+"record a=mechanics(force,1.0,0.0,options(parameters=options(stiffness=1.0),max_native_operations=1));",false);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_MECHANICS_KERNEL_ONLY
languageTests();
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" mechanics checks, "<<failures<<" failures\n";return failures?1:0;}
