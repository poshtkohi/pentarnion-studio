#include "geometry_api.h"
#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
using Matrix2=Eigen::Matrix<double,2,2,Eigen::RowMajor>;
void calculate(const penta_chart_sample_v1& sample,double tolerance,penta_chart_point_v1& out){
    const auto d=sample.dimension;
    if(sample.struct_size<sizeof(sample)||out.struct_size<sizeof(out)||d<2||d>5||sample.custom_metric>1||!std::isfinite(tolerance)||tolerance<=0||tolerance>=.1)throw std::runtime_error("Invalid chart geometry layout or metric tolerance.");
    const auto size=out.struct_size;out={};out.struct_size=size;out.dimension=d;out.extrinsic_available=!sample.custom_metric;
    for(unsigned mu=0;mu<d;++mu){if(!std::isfinite(sample.x[mu]))throw std::runtime_error("Non-finite chart position.");out.x[mu]=sample.x[mu];for(unsigned a=0;a<2;++a){if(!std::isfinite(sample.first[mu*2+a]))throw std::runtime_error("Non-finite chart tangent.");out.tangent[mu*2+a]=sample.first[mu*2+a];}for(unsigned ab=0;ab<4;++ab)if(!std::isfinite(sample.second[mu*4+ab]))throw std::runtime_error("Non-finite chart second derivative.");}
    Matrix2 g=Matrix2::Zero(),inverse;double dg[2][2][2]{},ddg[2][2][2][2]{};
    Eigen::Matrix<double,5,2> jac=Eigen::Matrix<double,5,2>::Zero();
    for(unsigned mu=0;mu<d;++mu)for(unsigned a=0;a<2;++a)jac(mu,a)=sample.first[mu*2+a];
    const auto induced=(jac.transpose()*jac).eval();
    if(sample.custom_metric){
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){const unsigned i=a+b;g(a,b)=sample.metric[i];for(unsigned c=0;c<2;++c){dg[a][b][c]=sample.metric_first[i*2+c];for(unsigned e=0;e<2;++e)ddg[a][b][c][e]=sample.metric_second[i*4+c*2+e];}}
    }else{
        g=induced;
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b)for(unsigned c=0;c<2;++c)for(unsigned mu=0;mu<d;++mu)dg[a][b][c]+=sample.second[mu*4+a*2+c]*sample.first[mu*2+b]+sample.first[mu*2+a]*sample.second[mu*4+b*2+c];
    }
    for(const auto value:g.reshaped())if(!std::isfinite(value))throw std::runtime_error("Non-finite chart metric.");
    for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b)for(unsigned c=0;c<2;++c){if(!std::isfinite(dg[a][b][c]))throw std::runtime_error("Non-finite metric derivative.");for(unsigned e=0;e<2;++e)if(!std::isfinite(ddg[a][b][c][e]))throw std::runtime_error("Non-finite metric second derivative.");}
    const double determinant=g.determinant(),largest=.5*g(0,0)+.5*g(1,1)+std::hypot(.5*g(0,0)-.5*g(1,1),g(0,1));
    if(g(0,0)<=0||g(1,1)<=0||!std::isfinite(determinant)||determinant<=0||!std::isfinite(largest)||determinant/largest<=tolerance*largest)throw std::runtime_error("Metric is not positive definite or the chart is numerically singular.");
    inverse=g.inverse();if(!inverse.allFinite())throw std::runtime_error("Chart inverse metric overflowed.");out.area_density=std::sqrt(determinant);
    for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){out.metric[a*2+b]=g(a,b);out.inverse_metric[a*2+b]=inverse(a,b);for(unsigned c=0;c<2;++c){double gamma=0;for(unsigned e=0;e<2;++e)gamma+=.5*inverse(a,e)*(dg[e][c][b]+dg[e][b][c]-dg[b][c][e]);out.christoffel[a*4+b*2+c]=gamma;}}
    double gaussian=0;
    if(sample.custom_metric){
        double r=-.5*(ddg[0][0][1][1]+ddg[1][1][0][0]-2*ddg[0][1][0][1]);
        for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b)r+=g(a,b)*(out.christoffel[a*4+1]*out.christoffel[b*4+1]-out.christoffel[a*4]*out.christoffel[b*4+3]);
        gaussian=r/determinant;
    }else{
        for(unsigned mu=0;mu<d;++mu)for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){double normal=sample.second[mu*4+a*2+b];for(unsigned e=0;e<2;++e)normal-=jac(mu,e)*out.christoffel[e*4+a*2+b];out.second_fundamental[mu*4+a*2+b]=normal;out.mean_curvature_vector[mu]+=.5*inverse(a,b)*normal;}
        for(unsigned mu=0;mu<d;++mu)gaussian+=out.second_fundamental[mu*4]*out.second_fundamental[mu*4+3]-out.second_fundamental[mu*4+1]*out.second_fundamental[mu*4+2];
        gaussian/=determinant;
    }
    // Projectors use the embedding's Euclidean tangent space even when an
    // independent intrinsic metric is supplied; they do not define that metric.
    const double embeddingDeterminant=induced.determinant(),embeddingLargest=.5*induced(0,0)+.5*induced(1,1)+std::hypot(.5*induced(0,0)-.5*induced(1,1),induced(0,1));
    if(!induced.allFinite()||embeddingDeterminant<=0||!std::isfinite(embeddingDeterminant)||embeddingDeterminant/embeddingLargest<=tolerance*embeddingLargest)throw std::runtime_error("Embedding is not an immersed chart.");
    const auto projector=(jac*induced.inverse()*jac.transpose()).eval();
    for(unsigned mu=0;mu<d;++mu)for(unsigned nu=0;nu<d;++nu){out.tangent_projector[mu*d+nu]=projector(mu,nu);out.normal_projector[mu*d+nu]=(mu==nu?1.0:0)-projector(mu,nu);}
    if(!std::isfinite(gaussian)||!std::isfinite(out.area_density))throw std::runtime_error("Chart curvature/area overflowed.");
    out.gaussian_curvature=gaussian;out.scalar_curvature=2*gaussian;
    // Convention: R^a_bcd=K(delta^a_c*g_bd-delta^a_d*g_bc), Ric_bd=R^a_bad.
    for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b){out.ricci[a*2+b]=gaussian*g(a,b);for(unsigned c=0;c<2;++c)for(unsigned e=0;e<2;++e)out.riemann[((a*2+b)*2+c)*2+e]=gaussian*((a==c?g(b,e):0)-(a==e?g(b,c):0));}
}
int point(const penta_chart_sample_v1* sample,double tolerance,penta_chart_point_v1* out,char* error,uint64_t capacity)noexcept{
    try{if(!sample||!out)throw std::runtime_error("Missing chart geometry buffers.");calculate(*sample,tolerance,*out);return 0;}catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown chart geometry failure.");return 4;}
}
int geodesic(const penta_geometry_host_v1* host,const penta_geodesic_request_v1* request,penta_geodesic_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{
        if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_GEOMETRY_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw std::runtime_error("Incompatible geodesic ABI.");
        const auto& r=*request;auto& out=*result;
        if(r.dimension<2||r.dimension>5||r.transport>1||!r.evaluate||!out.times||!out.coordinates||!out.velocity||!out.positions||!out.transport||!out.speed_squared||!out.transport_norm_squared||!r.max_steps||r.max_steps>10000000)throw std::runtime_error("Invalid geodesic buffers, dimension or step budget.");
        for(const auto value:{r.t0,r.t1,r.time_step,r.sample_step,r.metric_tolerance,r.max_wall_seconds,r.initial[0],r.initial[1],r.velocity[0],r.velocity[1],r.transport_initial[0],r.transport_initial[1]})if(!std::isfinite(value))throw std::runtime_error("Geodesic settings must be finite.");
        if(r.t1<r.t0||r.time_step<=0||r.sample_step<=0||r.max_wall_seconds<=0)throw std::runtime_error("Invalid geodesic time settings.");
        for(unsigned a=0;a<2;++a)if(std::isnan(r.lower[a])||std::isnan(r.upper[a])||r.lower[a]>=r.upper[a])throw std::runtime_error("Invalid chart coordinate bounds.");
        const auto started=std::chrono::steady_clock::now();out.samples=out.steps=out.evaluations=0;out.max_relative_speed_drift=out.max_relative_transport_drift=0;
        auto check=[&]{if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Geodesic cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>r.max_wall_seconds)throw std::runtime_error("Geodesic wall-time budget exceeded.");};
        using State=std::array<double,6>;State state{r.initial[0],r.initial[1],r.velocity[0],r.velocity[1],r.transport_initial[0],r.transport_initial[1]};
        auto geometry=[&](const State& y){
            check();for(unsigned a=0;a<2;++a)if(y[a]<r.lower[a]||y[a]>r.upper[a])throw std::runtime_error("Geodesic left the declared coordinate chart.");
            char detail[2048]{};if(host->charge&&host->charge(host->context,1,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Geodesic work budget exceeded.");
            penta_chart_sample_v1 sample{};sample.struct_size=sizeof(sample);if(r.evaluate(r.callback_context,y[0],y[1],&sample,detail,sizeof(detail)))throw std::runtime_error(detail[0]?detail:"Compiled chart callback failed.");++out.evaluations;
            if(sample.dimension!=r.dimension)throw std::runtime_error("Chart callback embedding dimension changed.");
            penta_chart_point_v1 geometry{};geometry.struct_size=sizeof(geometry);calculate(sample,r.metric_tolerance,geometry);return geometry;
        };
        auto rhs=[&](const State& y){const auto g=geometry(y);State d{};d[0]=y[2];d[1]=y[3];for(unsigned a=0;a<2;++a)for(unsigned b=0;b<2;++b)for(unsigned c=0;c<2;++c){d[2+a]-=g.christoffel[a*4+b*2+c]*y[2+b]*y[2+c];if(r.transport)d[4+a]-=g.christoffel[a*4+b*2+c]*y[2+b]*y[4+c];}return d;};
        double initialSpeed=0,initialTransport=0;
        auto save=[&](double time){
            if(out.samples>=out.capacity)throw std::runtime_error("Geodesic retained-sample budget exceeded.");
            const auto g=geometry(state);const auto row=out.samples;out.times[row]=time;
            double speed=0,transport=0;for(unsigned a=0;a<2;++a){out.coordinates[row*2+a]=state[a];out.velocity[row*2+a]=state[2+a];out.transport[row*2+a]=r.transport?state[4+a]:0;for(unsigned b=0;b<2;++b){speed+=g.metric[a*2+b]*state[2+a]*state[2+b];transport+=g.metric[a*2+b]*state[4+a]*state[4+b];}}for(unsigned mu=0;mu<r.dimension;++mu)out.positions[row*r.dimension+mu]=g.x[mu];
            out.speed_squared[row]=speed;out.transport_norm_squared[row]=r.transport?transport:0;
            if(!row){initialSpeed=speed;initialTransport=transport;}out.max_relative_speed_drift=std::max(out.max_relative_speed_drift,std::abs(speed-initialSpeed)/std::max(1.0,std::abs(initialSpeed)));if(r.transport)out.max_relative_transport_drift=std::max(out.max_relative_transport_drift,std::abs(transport-initialTransport)/std::max(1.0,std::abs(initialTransport)));++out.samples;
        };
        double t=r.t0;save(t);uint64_t sampleIndex=1;
        while(t<r.t1){check();if(out.steps>=r.max_steps)throw std::runtime_error("Geodesic step budget exceeded.");const double target=std::min(r.t1,r.t0+sampleIndex*r.sample_step);if(target<=t){++sampleIndex;continue;}const double dt=std::min(r.time_step,target-t);if(t+dt<=t)throw std::runtime_error("Geodesic step cannot advance time.");State trial;const auto a=rhs(state);for(unsigned i=0;i<6;++i)trial[i]=state[i]+dt*a[i]/2;const auto b=rhs(trial);for(unsigned i=0;i<6;++i)trial[i]=state[i]+dt*b[i]/2;const auto c=rhs(trial);for(unsigned i=0;i<6;++i)trial[i]=state[i]+dt*c[i];const auto d=rhs(trial);for(unsigned i=0;i<6;++i){state[i]+=dt*(a[i]+2*b[i]+2*c[i]+d[i])/6;if(!std::isfinite(state[i]))throw std::runtime_error("Non-finite geodesic state.");}t+=dt;++out.steps;if(t>=target-16*std::numeric_limits<double>::epsilon()*std::max(1.0,std::abs(target))){t=target;save(t);++sampleIndex;}}
        out.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();return 0;
    }catch(const std::exception& failure){message(error,capacity,failure.what());return 1;}catch(...){message(error,capacity,"Unknown geodesic failure.");return 4;}
}
const penta_geometry_api_v1 api{sizeof(api),PENTA_GEOMETRY_ABI_V1,point,geodesic};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_geometry_api_v1* penta_geometry_query_v1(uint32_t version){return version==PENTA_GEOMETRY_ABI_V1?&api:nullptr;}
