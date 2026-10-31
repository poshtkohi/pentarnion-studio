#include "pde_api.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>
namespace {
using Point=std::array<double,3>;
using WidePoint=std::array<long double,3>;
WidePoint difference(const Point& a,const Point& b){return {static_cast<long double>(a[0])-b[0],static_cast<long double>(a[1])-b[1],static_cast<long double>(a[2])-b[2]};}
WidePoint crossProduct(const WidePoint& a,const WidePoint& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
long double length(const WidePoint& p){return std::hypot(p[0],p[1],p[2]);}
long double dot(const WidePoint& a,const WidePoint& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool hasArea(const Point& a,const Point& b,const Point& c){
    auto u=difference(b,a),v=difference(c,a);long double scale=0;for(int d=0;d<3;++d)scale=std::max({scale,std::abs(u[d]),std::abs(v[d])});
    if(scale==0)return false;
    for(int d=0;d<3;++d){u[d]/=scale;v[d]/=scale;}return length(crossProduct(u,v))>0;
}
void insertDistinct(std::vector<Point>& hits,const Point& point){if(std::find(hits.begin(),hits.end(),point)==hits.end())hits.push_back(point);}
std::vector<std::size_t> polygonOrder(const std::vector<Point>& hits){
    if(hits.size()<3)return {};
    WidePoint center{};for(const auto& point:hits)for(int d=0;d<3;++d)center[d]+=static_cast<long double>(point[d])/hits.size();
    std::vector<WidePoint> offsets(hits.size());long double scale=0;
    for(std::size_t i=0;i<hits.size();++i)for(int d=0;d<3;++d){offsets[i][d]=static_cast<long double>(hits[i][d])-center[d];scale=std::max(scale,std::abs(offsets[i][d]));}
    if(scale==0)return {};
    for(auto& point:offsets)for(auto& coordinate:point)coordinate/=scale;
    WidePoint normal{};long double bestArea=0;
    for(std::size_t i=0;i<hits.size();++i)for(std::size_t j=i+1;j<hits.size();++j){const auto candidate=crossProduct(offsets[i],offsets[j]);const auto area=length(candidate);if(area>bestArea){normal=candidate;bestArea=area;}}
    if(bestArea==0)return {};
    for(auto& coordinate:normal)coordinate/=bestArea;
    auto u=*std::max_element(offsets.begin(),offsets.end(),[](const auto& a,const auto& b){return length(a)<length(b);});
    const auto uLength=length(u);for(auto& coordinate:u)coordinate/=uLength;
    auto v=crossProduct(normal,u);const auto vLength=length(v);if(vLength==0)return {};for(auto& coordinate:v)coordinate/=vLength;
    std::vector<std::size_t> order(hits.size());std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](auto a,auto b){return std::atan2(dot(offsets[a],v),dot(offsets[a],u))<std::atan2(dot(offsets[b],v),dot(offsets[b],u));});return order;
}
Point interpolate(const Point& a,const Point& b,double va,double vb,double level){
    // Normalize the scalar ratio before subtraction: finite opposite extremes
    // must not turn the denominator into infinity, or a midpoint into an endpoint.
    const long double scalarScale=std::max({std::abs(static_cast<long double>(va)),std::abs(static_cast<long double>(vb)),std::abs(static_cast<long double>(level))});
    if(scalarScale==0)throw std::runtime_error("Cannot interpolate an entirely constant level edge.");
    const long double left=static_cast<long double>(va)/scalarScale,right=static_cast<long double>(vb)/scalarScale,target=static_cast<long double>(level)/scalarScale;
    const long double t=std::clamp((target-left)/(right-left),0.0L,1.0L);Point result{};
    for(int d=0;d<3;++d){result[d]=static_cast<double>((1-t)*static_cast<long double>(a[d])+t*static_cast<long double>(b[d]));if(!std::isfinite(result[d]))throw std::runtime_error("Level interpolation exceeds the finite coordinate domain.");}return result;
}
int build(const penta_pde_host_v1* host,const penta_geometry_request_v1* request,penta_geometry_result_v1* output,char* error,uint64_t capacity)noexcept{
    try {
        if(!host||!request||!output||host->abi_version!=PENTA_PDE_ABI_V1||host->struct_size<sizeof(*host)||request->struct_size<sizeof(*request)||output->struct_size<sizeof(*output)||request->operation>3||request->nx<2||request->ny<2||!request->x||!request->y||!request->values||!output->xyz||!output->scalars)throw std::runtime_error("Invalid geometry ABI or grid.");
        const auto* r=request;auto* o=output;const bool volume=r->operation==3;
        if(volume&&(r->nz<2||!r->z||!r->levels||r->level_count!=1))throw std::runtime_error("Isosurface needs a three-dimensional grid and one finite level.");
        if(r->nx>100000||r->ny>100000||r->nz>100000||r->nx*r->ny>1000000||(volume&&r->nx*r->ny>1000000/r->nz))throw std::runtime_error("Geometry grid budget exceeded.");
        auto axis=[&](const double* a,uint64_t n){for(uint64_t i=0;i<n;++i)if(!std::isfinite(a[i])||(i&&a[i]<=a[i-1]))throw std::runtime_error("Geometry axes must be finite and strictly increasing.");};axis(r->x,r->nx);axis(r->y,r->ny);if(volume)axis(r->z,r->nz);
        const auto size=r->nx*r->ny*(volume?r->nz:1);for(uint64_t i=0;i<size;++i)if(!std::isfinite(r->values[i]))throw std::runtime_error("Geometry samples must be finite.");
        if(r->operation>=2){if(!r->levels||!r->level_count||r->level_count>32)throw std::runtime_error("Contours need 1..32 finite levels.");for(uint64_t i=0;i<r->level_count;++i)if(!std::isfinite(r->levels[i]))throw std::runtime_error("Contour levels must be finite.");}
        auto check=[&](uint64_t work=1){if(host->cancelled&&host->cancelled(host->context))throw std::runtime_error("Geometry build cancelled.");if(host->charge&&host->charge(host->context,work,error,capacity))throw std::runtime_error(error[0]?error:"Geometry operation budget exceeded.");};
        auto point=[&](const Point& p,double scalar){if(o->points>=o->point_capacity)throw std::runtime_error("Geometry vertex budget exceeded; reduce grid resolution.");const auto id=static_cast<uint32_t>(o->points++);std::copy(p.begin(),p.end(),o->xyz+3*id);o->scalars[id]=scalar;return id;};
        auto triangle=[&](uint32_t a,uint32_t b,uint32_t c){if(!o->faces||o->triangles>=o->triangle_capacity)throw std::runtime_error("Geometry triangle budget exceeded.");o->faces[o->triangles*3]=a;o->faces[o->triangles*3+1]=b;o->faces[o->triangles*3+2]=c;++o->triangles;};
        auto segment=[&](const Point& a,const Point& b,double value){if(a==b)return;if(!o->edges||o->segments>=o->segment_capacity)throw std::runtime_error("Geometry segment budget exceeded.");const auto x=point(a,value),y=point(b,value);o->edges[o->segments*2]=x;o->edges[o->segments*2+1]=y;++o->segments;};
        if(r->operation<=1){
            for(uint64_t j=0;j<r->ny;++j){check(r->nx);for(uint64_t i=0;i<r->nx;++i){const auto v=r->values[j*r->nx+i];point({r->x[i],r->y[j],r->operation==1?0:v},v);}}
            for(uint64_t j=0;j+1<r->ny;++j){check(2*(r->nx-1));for(uint64_t i=0;i+1<r->nx;++i){const auto a=static_cast<uint32_t>(j*r->nx+i),b=a+1,c=a+static_cast<uint32_t>(r->nx),d=c+1;triangle(a,b,d);triangle(a,d,c);}}return 0;
        }
        if(r->operation==2){
            constexpr int faces[2][3]{{0,1,2},{0,2,3}};
            std::set<std::tuple<double,uint64_t,uint64_t>> ownedEdges;
            for(uint64_t j=0;j+1<r->ny;++j){check((r->nx-1)*r->level_count*2);for(uint64_t i=0;i+1<r->nx;++i){
                Point p[4]{{r->x[i],r->y[j],0},{r->x[i+1],r->y[j],0},{r->x[i+1],r->y[j+1],0},{r->x[i],r->y[j+1],0}};
                const uint64_t ids[4]{j*r->nx+i,j*r->nx+i+1,(j+1)*r->nx+i+1,(j+1)*r->nx+i};double values[4];for(int a=0;a<4;++a)values[a]=r->values[ids[a]];
                for(uint64_t l=0;l<r->level_count;++l){const double level=r->levels[l];if(std::find(r->levels,r->levels+l,level)!=r->levels+l)continue;
                    for(const auto& face:faces){std::vector<Point> hits;int zeroCount=0;std::array<uint64_t,2> zeroIds{};
                        for(int a:face)if(values[a]==level){if(zeroCount<2)zeroIds[static_cast<std::size_t>(zeroCount)]=ids[a];++zeroCount;insertDistinct(hits,p[a]);}
                        if(zeroCount==3)continue; // The level fills the triangle: no unique one-dimensional contour.
                        for(int e=0;e<3;++e){const int a=face[e],b=face[(e+1)%3];if((values[a]<level&&values[b]>level)||(values[a]>level&&values[b]<level))insertDistinct(hits,interpolate(p[a],p[b],values[a],values[b],level));}
                        if(hits.size()!=2)continue;
                        if(zeroCount==2){std::sort(zeroIds.begin(),zeroIds.end());if(!ownedEdges.emplace(level,zeroIds[0],zeroIds[1]).second)continue;}
                        segment(hits[0],hits[1],level);
                    }
                }
            }}return 0;
        }
        constexpr int tets[6][4]{{0,1,2,6},{0,2,3,6},{0,3,7,6},{0,7,4,6},{0,4,5,6},{0,5,1,6}};
        constexpr int edges[6][2]{{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
        constexpr int offsets[8][3]{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        std::set<std::array<uint64_t,3>> ownedFaces;const double level=r->levels[0];
        for(uint64_t k=0;k+1<r->nz;++k)for(uint64_t j=0;j+1<r->ny;++j){check((r->nx-1)*6);for(uint64_t i=0;i+1<r->nx;++i){
            Point p[8];double values[8];uint64_t ids[8];
            for(int a=0;a<8;++a){const auto x=i+offsets[a][0],y=j+offsets[a][1],z=k+offsets[a][2];p[a]={r->x[x],r->y[y],r->z[z]};ids[a]=(z*r->ny+y)*r->nx+x;values[a]=r->values[ids[a]];}
            for(const auto& tet:tets){std::vector<Point> hits;std::array<uint64_t,3> zeroIds{};int zeroCount=0;
                for(int a:tet)if(values[a]==level){if(zeroCount<3)zeroIds[static_cast<std::size_t>(zeroCount)]=ids[a];++zeroCount;insertDistinct(hits,p[a]);}
                if(zeroCount==4)continue; // An entirely constant-level volume has no unique two-dimensional isosurface.
                for(const auto& edge:edges){const int a=tet[edge[0]],b=tet[edge[1]];if((values[a]<level&&values[b]>level)||(values[a]>level&&values[b]<level))insertDistinct(hits,interpolate(p[a],p[b],values[a],values[b],level));}
                if(hits.size()<3)continue;
                const auto order=polygonOrder(hits);if(order.empty())continue;
                if(zeroCount==3){std::sort(zeroIds.begin(),zeroIds.end());if(!ownedFaces.insert(zeroIds).second)continue;}
                // A polygon containing equality vertices may have only three
                // distinct intersections. Triangulate the actual convex polygon,
                // never the number of crossing edges with repeated endpoints.
                std::vector<uint32_t> vertices;for(auto index:order)vertices.push_back(point(hits[index],level));
                for(std::size_t a=1;a+1<order.size();++a)if(hasArea(hits[order[0]],hits[order[a]],hits[order[a+1]]))triangle(vertices[0],vertices[a],vertices[a+1]);
            }
        }}return 0;
    }catch(const std::exception& failure){if(error&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(failure.what()));std::memcpy(error,failure.what(),static_cast<std::size_t>(n));error[n]=0;}return 1;}catch(...){return 1;}
}
}
#ifdef _WIN32
#define PENTA_GEOMETRY_EXPORT __declspec(dllexport)
#else
#define PENTA_GEOMETRY_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_GEOMETRY_EXPORT penta_geometry_build_v1 penta_geometry_query_v1(uint32_t version){return version==PENTA_PDE_ABI_V1?build:nullptr;}
