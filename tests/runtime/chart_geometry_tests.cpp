#include "geometry/geometry_module.hpp"
#ifndef PENTA_GEOMETRY_KERNEL_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
namespace {
int checks=0,failures=0;
void require(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
template<class F>void rejects(F&& f,const std::string& label){bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,label);}
constexpr double pi=std::numbers::pi;
struct Chart {
    unsigned kind=0,dimension=3;double radius=2;bool custom=false;std::shared_ptr<std::atomic_bool> cancellation;
    static int evaluate(void* raw,double u,double v,penta_chart_sample_v1* out,char*,uint64_t){
        auto& self=*static_cast<Chart*>(raw);const auto size=out->struct_size;*out={};out->struct_size=size;out->dimension=self.dimension;
        if(self.cancellation)self.cancellation->store(true);
        if(self.kind==0){out->x[0]=u;out->x[1]=v;out->first[0]=out->first[3]=1;}
        else if(self.kind==1){out->x[0]=self.radius*std::cos(u);out->x[1]=self.radius*std::sin(u);out->x[2]=v;out->first[0]=-self.radius*std::sin(u);out->first[2]=self.radius*std::cos(u);out->first[5]=1;out->second[0]=-self.radius*std::cos(u);out->second[4]=-self.radius*std::sin(u);}
        else{
            std::array<double,3> x{self.radius*std::sin(u)*std::cos(v),self.radius*std::sin(u)*std::sin(v),self.radius*std::cos(u)};
            std::array<double,6> j{self.radius*std::cos(u)*std::cos(v),-self.radius*std::sin(u)*std::sin(v),self.radius*std::cos(u)*std::sin(v),self.radius*std::sin(u)*std::cos(v),-self.radius*std::sin(u),0};
            std::array<double,12> h{-x[0],-self.radius*std::cos(u)*std::sin(v),-self.radius*std::cos(u)*std::sin(v),-x[0],-x[1],self.radius*std::cos(u)*std::cos(v),self.radius*std::cos(u)*std::cos(v),-x[1],-x[2],0,0,0};
            // A constant isometric R3 -> R5 embedding mixes the original sphere
            // across all five coefficient coordinates without changing K.
            const double rotation[5][3]={{.6,0,0},{0,.6,0},{0,0,1},{.8,0,0},{0,.8,0}};
            for(unsigned mu=0;mu<self.dimension;++mu)for(unsigned nu=0;nu<3;++nu){const double a=self.dimension==5?rotation[mu][nu]:(mu==nu?1.0:0);out->x[mu]+=a*x[nu];for(unsigned b=0;b<2;++b)out->first[mu*2+b]+=a*j[nu*2+b];for(unsigned bc=0;bc<4;++bc)out->second[mu*4+bc]+=a*h[nu*4+bc];}
        }
        if(self.custom){out->custom_metric=1;out->metric[0]=self.radius*self.radius;out->metric[2]=self.radius*self.radius*std::sin(u)*std::sin(u);out->metric_first[4]=self.radius*self.radius*std::sin(2*u);out->metric_second[8]=2*self.radius*self.radius*std::cos(2*u);}
        return 0;
    }
    penta_chart_sample_v1 sample(double u,double v){penta_chart_sample_v1 out{};out.struct_size=sizeof(out);evaluate(this,u,v,&out,nullptr,0);return out;}
};
penta_geodesic_request_v1 request(Chart& chart){penta_geodesic_request_v1 r{};r.struct_size=sizeof(r);r.dimension=chart.dimension;r.initial[0]=.9;r.initial[1]=.2;r.velocity[0]=.3;r.velocity[1]=.4;r.t1=.7;r.time_step=.01;r.sample_step=.1;r.max_wall_seconds=30;r.metric_tolerance=1e-12;r.max_steps=100000;r.lower[0]=r.lower[1]=-std::numeric_limits<double>::infinity();r.upper[0]=r.upper[1]=std::numeric_limits<double>::infinity();r.callback_context=&chart;r.evaluate=Chart::evaluate;return r;}
void nativeTests(){
    penta::geometry::Module module;
    for(const unsigned kind:{0u,1u,2u})for(const unsigned dimension:{3u,5u}){
        Chart chart;chart.kind=kind;chart.dimension=dimension;const auto sample=chart.sample(.8,.4);const auto point=module.point(sample,1e-12);const auto label="chart kind="+std::to_string(kind)+" dim="+std::to_string(dimension);
        require(std::abs(point.gaussian_curvature-(kind==2?.25:0))<1e-12,label+" Gaussian curvature");require(std::abs(point.scalar_curvature-2*point.gaussian_curvature)<1e-14,label+" scalar curvature");
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){double product=0;for(unsigned c=0;c<2;++c)product+=point.metric[a*2+c]*point.inverse_metric[c*2+b];require(std::abs(product-(a==b?1:0))<1e-12,label+" inverse metric");require(std::abs(point.ricci[a*2+b]-point.gaussian_curvature*point.metric[a*2+b])<1e-12,label+" Ricci convention");}
        double trace=0;
        for(unsigned mu=0;mu<dimension;++mu){trace+=point.normal_projector[mu*dimension+mu];for(unsigned nu=0;nu<dimension;++nu){double square=0;for(unsigned rho=0;rho<dimension;++rho)square+=point.normal_projector[mu*dimension+rho]*point.normal_projector[rho*dimension+nu];require(std::abs(square-point.normal_projector[mu*dimension+nu])<1e-12,label+" normal projector");}}
        require(std::abs(trace-(dimension-2))<1e-12,label+" full normal-space codimension");
        if(kind==2){for(unsigned mu=0;mu<dimension;++mu)require(std::abs(point.mean_curvature_vector[mu]+point.x[mu]/4)<1e-12,label+" mean curvature vector");require(std::abs(point.christoffel[3]+std::sin(.8)*std::cos(.8))<1e-12,label+" sphere connection");}
    }
    {Chart chart;chart.custom=true;const auto p=module.point(chart.sample(.8,.3),1e-12);require(std::abs(p.gaussian_curvature-.25)<1e-12,"independent supplied sphere metric on plane chart");require(!p.extrinsic_available,"custom metric does not claim extrinsic fundamental form");}
    {Chart chart;chart.kind=2;rejects([&]{module.point(chart.sample(0,.2),1e-12);},"singular sphere chart");auto s=chart.sample(.8,.2);s.custom_metric=1;s.metric[0]=1;s.metric[2]=-1;rejects([&]{module.point(s,1e-12);},"indefinite metric rejected");s=chart.sample(.8,.2);s.first[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{module.point(s,1e-12);},"nonfinite derivative rejected");}
    {Chart chart;auto r=request(chart);r.transport=1;r.transport_initial[0]=.7;r.transport_initial[1]=-.3;const auto p=module.geodesic(r,{},[](std::size_t){});const auto n=p.report.samples-1;require(std::abs(p.coordinates[n*2]-(.9+.3*.7))<1e-13&&std::abs(p.coordinates[n*2+1]-(.2+.4*.7))<1e-13,"plane geodesic straight line");require(std::abs(p.transport[n*2]-.7)<1e-14&&std::abs(p.transport[n*2+1]+.3)<1e-14,"plane parallel transport");}
    for(const unsigned dimension:{3u,5u}){
        Chart chart;chart.kind=2;chart.dimension=dimension;auto r=request(chart);r.initial[0]=pi/2;r.initial[1]=.1;r.velocity[0]=0;r.velocity[1]=1;r.t1=1;r.transport=1;r.transport_initial[0]=1;const auto p=module.geodesic(r,{},[](std::size_t){});const auto n=p.report.samples-1;require(std::abs(p.coordinates[n*2]-pi/2)<1e-12&&std::abs(p.coordinates[n*2+1]-1.1)<1e-12,"great-circle equator chart path");const auto expected=chart.sample(pi/2,1.1);for(unsigned mu=0;mu<dimension;++mu)require(std::abs(p.positions[n*dimension+mu]-expected.x[mu])<1e-12,"great-circle embedded position");require(p.report.max_relative_speed_drift<1e-12&&p.report.max_relative_transport_drift<1e-12,"sphere equator speed/transport metric norm");
    }
    {Chart chart;chart.kind=2;auto r=request(chart);r.initial[0]=.7;r.velocity[0]=.4;r.velocity[1]=0;r.transport=1;r.transport_initial[1]=1/std::sin(.7);const auto p=module.geodesic(r,{},[](std::size_t){});const auto n=p.report.samples-1;require(std::abs(p.transport[n*2+1]-1/std::sin(p.coordinates[n*2]))<1e-9,"meridian parallel transport coordinate change");require(p.report.max_relative_transport_drift<1e-9,"meridian transported norm preserved numerically");}
    double previous=0;
    for(const double step:{.1,.05,.025}){
        Chart chart;chart.kind=2;auto r=request(chart);r.time_step=step;r.sample_step=r.t1;const auto p=module.geodesic(r,{},[](std::size_t){});const auto start=chart.sample(r.initial[0],r.initial[1]);const double omega=std::sqrt(r.velocity[0]*r.velocity[0]+std::pow(std::sin(r.initial[0])*r.velocity[1],2));double error=0;for(unsigned mu=0;mu<3;++mu){const double tangent=start.first[mu*2]*r.velocity[0]+start.first[mu*2+1]*r.velocity[1];const double exact=start.x[mu]*std::cos(omega*r.t1)+tangent/omega*std::sin(omega*r.t1);error=std::max(error,std::abs(p.positions[(p.report.samples-1)*3+mu]-exact));}if(previous)require(previous/error>13&&previous/error<19,"geodesic fourth-order convergence against ambient great circle");previous=error;
    }
    {Chart chart;auto r=request(chart);r.upper[0]=.91;rejects([&]{module.geodesic(r,{},[](std::size_t){});},"chart bounds enforced");r.upper[0]=2;r.max_steps=1;rejects([&]{module.geodesic(r,{},[](std::size_t){});},"geodesic step cap");r.max_steps=100000;rejects([&]{module.geodesic(r,{},[](std::size_t){throw std::runtime_error("quota");});},"geodesic owner work cap");auto flag=std::make_shared<std::atomic_bool>(false);chart.cancellation=flag;rejects([&]{module.geodesic(r,flag,[](std::size_t){});},"geodesic cooperative cancellation");}
}
#ifndef PENTA_GEOMETRY_KERNEL_ONLY
void languageTests(){
    penta::runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-chart-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(options.dataDirectory);options.odeCacheDirectory=options.dataDirectory/".penta-cache/odes";
    auto run=[&](const std::string& code,bool good=true){const auto result=penta::runtime::execute(code,{},options);require(result.ok()==good,"compiled chart language case");if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}};
    const std::string sphere="native function [real x,real y,real z] sphere(real u,real v,real radius){return radius*sin(u)*cos(v),radius*sin(u)*sin(v),radius*cos(u);}";
    run(sphere+"record g=chartgeometry(sphere,[.8,.4],options(metric=\"euclidean\",parameters=options(radius=2.0)));assert(abs(g.gaussian_curvature-.25)<1e-12);assert(abs(g.metric(1,1)-4)<1e-12);assert(abs(g.christoffel(1,4)+sin(.8)*cos(.8))<1e-12);record h=chartgeometry(sphere,[.8,.4],options(parameters=options(radius=3.0)));assert(h.cache_hit&&g.cache_key==h.cache_key);assert(abs(h.gaussian_curvature-1/9)<1e-12);");
    run("native function pentarnion chart(real u,real v){return .6*sin(u)*cos(v)+.6*sin(u)*sin(v)*i+cos(u)*j+.8*sin(u)*cos(v)*k+.8*sin(u)*sin(v)*ell;}record g=chartgeometry(chart,[.8,.4]);assert(g.embedding_dimension==5&&size(g.normal_projector,1)==5);assert(abs(g.gaussian_curvature-1)<1e-12);");
    run("native function [real x,real y] plane(real u,real v){return u,v;}native function [real E,real F,real G] metric(real u,real v,real radius){return radius*radius,0.0,radius*radius*sin(u)*sin(u);}record g=chartgeometry(plane,[.8,.4],options(metric=metric,metric_parameters=options(radius=2.0)));assert(g.extrinsic_available==0&&g.metric_kind==\"user_defined\");assert(abs(g.gaussian_curvature-.25)<1e-12);");
    run(sphere+"record p=geodesic(sphere,[1.5707963267948966,.1],[0,1],options(parameters=options(radius=2.0),t1=.2,time_step=.01,sample_step=.1,transport=[1,0],lower=[.01,-10],upper=[3.13,10]));assert(p.status==\"completed\"&&p.connection==\"levi_civita\");assert(abs(p.coordinates(end,2)-.3)<1e-12);assert(p.max_relative_transport_drift<1e-12);");
    run(sphere+"record g=chartgeometry(sphere,[0,.2],options(parameters=options(radius=2.0)));",false);
    run(sphere+"record g=chartgeometry(sphere,[.8,.2],options(parameters=options(radius=2.0),metric=\"algebra\"));",false);
    run(sphere+"record g=chartgeometry(sphere,[.8,.2],options(parameters=options(radius=2.0),max_native_operations=1));",false);
    std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);
}
#endif
}
int main(){try{nativeTests();
#ifndef PENTA_GEOMETRY_KERNEL_ONLY
languageTests();
#endif
}catch(const std::exception& failure){++failures;std::cerr<<"UNEXPECTED: "<<failure.what()<<'\n';}std::cout<<checks<<" chart geometry checks, "<<failures<<" failures\n";return failures?1:0;}
