#include "runtime/interpreter.hpp"
#include "pde/pde_module.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
namespace {
int checks=0,failed=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failed;std::cerr<<"FAIL: "<<label<<'\n';}}
using Point=std::array<double,3>;
Point sub(Point a,const Point& b){for(int d=0;d<3;++d)a[d]-=b[d];return a;}
Point cross(const Point& a,const Point& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double dot(const Point& a,const Point& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double norm(const Point& a){return std::hypot(a[0],a[1],a[2]);}
double triangleArea(const Point& a,const Point& b,const Point& c){return .5*norm(cross(sub(b,a),sub(c,a)));}
double cubePlaneArea(const Point& normal,double level){
    // Independent oracle: intersect only the twelve cube boundary edges, order
    // the resulting convex polygon using the known plane normal, and integrate
    // its triangle fan. No tetrahedral case table is used in this oracle.
    const Point corners[8]{{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    const int edges[12][2]{{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    std::vector<Point> points;
    auto insert=[&](const Point& p){for(const auto& q:points)if(norm(sub(p,q))<1e-13)return;points.push_back(p);};
    for(const auto& edge:edges){const auto a=corners[edge[0]],b=corners[edge[1]];const double va=dot(normal,a)-level,vb=dot(normal,b)-level;if(va==0)insert(a);if(vb==0)insert(b);if((va<0&&vb>0)||(va>0&&vb<0)){const double t=va/(va-vb);Point p{};for(int d=0;d<3;++d)p[d]=a[d]+t*(b[d]-a[d]);insert(p);}}
    if(points.size()<3)return 0;
    Point center{};for(const auto& p:points)for(int d=0;d<3;++d)center[d]+=p[d]/points.size();
    const auto u=sub(points[0],center),v=cross(normal,u);
    std::sort(points.begin(),points.end(),[&](const auto& a,const auto& b){const auto x=sub(a,center),y=sub(b,center);return std::atan2(dot(x,v),dot(x,u))<std::atan2(dot(y,v),dot(y,u));});
    double area=0;for(std::size_t i=1;i+1<points.size();++i)area+=triangleArea(points[0],points[i],points[i+1]);return area;
}
double meshArea(const penta::pde::Mesh& mesh){double area=0;for(std::size_t t=0;t<mesh.faces.size();t+=3){Point points[3];for(int vertex=0;vertex<3;++vertex)for(int d=0;d<3;++d)points[vertex][d]=mesh.xyz[mesh.faces[t+vertex]*3+d];area+=triangleArea(points[0],points[1],points[2]);}return area;}
void independentPlanes(){
    using namespace penta::pde;Module module;const double axis[]{0,1};std::size_t planeChecks=0;
    for(int a=-2;a<=2;++a)for(int b=-2;b<=2;++b)for(int c=-2;c<=2;++c){if(!a&&!b&&!c)continue;const Point normal{double(a),double(b),double(c)};
        for(int step=-4;step<=4;++step){const double level=.5*step,expected=cubePlaneArea(normal,level);if(expected==0)continue;++planeChecks;double values[8];for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x)values[(z*2+y)*2+x]=a*x+b*y+c*z;
            penta_geometry_request_v1 request{};request.struct_size=sizeof(request);request.operation=3;request.nx=request.ny=request.nz=2;request.x=request.y=request.z=axis;request.values=values;request.levels=&level;request.level_count=1;
            try {const auto mesh=module.geometry(request,1000,{},{});const auto context="normal("+std::to_string(a)+","+std::to_string(b)+","+std::to_string(c)+"), level="+std::to_string(level);
                expect(std::abs(meshArea(mesh)-expected)<1e-12,"independent cube-plane area: "+context);
                bool onPlane=true,positive=true;for(std::size_t p=0;p<mesh.xyz.size();p+=3)onPlane=onPlane&&std::abs(a*mesh.xyz[p]+b*mesh.xyz[p+1]+c*mesh.xyz[p+2]-level)<2e-13;
                for(std::size_t t=0;t<mesh.faces.size();t+=3){Point p[3];for(int vertex=0;vertex<3;++vertex)for(int d=0;d<3;++d)p[vertex][d]=mesh.xyz[mesh.faces[t+vertex]*3+d];positive=positive&&triangleArea(p[0],p[1],p[2])>0;}
                expect(onPlane,"all generated vertices satisfy analytic plane: "+context);expect(positive,"no zero-area triangles: "+context);
            }catch(const Error& error){expect(false,"cube-plane construction threw: "+std::string(error.what()));}
        }
    }
    expect(planeChecks==646,"all646 independent nonzero plane sections exercised");
}
penta::runtime::ExecutionResult run(const std::string& source,bool good=true){auto result=penta::runtime::execute(source);expect(result.ok()==good,source);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}return result;}
void language(){
    auto surface=run("surf([0,1],[0,1],[0,1;1,2]);");if(surface.ok()){const auto& m=*surface.figureActions.back().mesh;expect(m.points.size()==4&&m.triangles.size()==2,"structured surface triangulation");expect(m.points.back()[2]==2&&m.scalars.back()==2,"physical heights/scalars retained");}
    auto image=run("imagesc([0,1],[0,1],[0,1;1,2]);");if(image.ok())expect(image.figureActions.back().mesh->points.back()[2]==0&&image.figureActions.back().mesh->scalars.back()==2,"flat image retains scalar field independently of height");
    auto contour=run("contour([0,1],[0,1],[0,1;1,2],.5);");if(contour.ok()){const auto& m=*contour.figureActions.back().mesh;expect(m.segments.size()==2,"piecewise-linear contour through two triangles");for(const auto& p:m.points)expect(std::abs(p[0]+p[1]-.5)<1e-14,"contour lies on analytic level line");}
    auto empty=run("contour([0,1],[0,1],[0,1;1,2],5.0);");if(empty.ok())expect(empty.figureActions.back().mesh->points.empty(),"empty level set stays empty");
    auto shared=run("contour([0,1],[0,1],[0,1;-1,0],0.0);");if(shared.ok())expect(shared.figureActions.back().mesh->segments.size()==1,"exact diagonal level edge emitted once across neighboring triangles");
    auto across=run("contour([0,1,2],[0,1],[-1,0,1;-1,0,1],0.0);");if(across.ok())expect(across.figureActions.back().mesh->segments.size()==1,"exact level edge emitted once across neighboring cells");
    auto boundary=run("contour([0,1],[0,1],[0,1;0,1],[0,0]);");if(boundary.ok()){const auto& m=*boundary.figureActions.back().mesh;expect(m.segments.size()==1,"boundary level edge included and duplicate levels deduplicated");for(const auto& p:m.points)expect(p[0]==0,"boundary contour keeps exact grid coordinates");}
    auto constant=run("contour([0,1],[0,1],[1,1;1,1],1.0);");if(constant.ok())expect(constant.figureActions.back().mesh->points.empty(),"constant-level area has no unique one-dimensional contour");
    auto scalarExtreme=run("contour([0,1],[0,1],[-1e308,1e308;-1e308,1e308],0.0);");if(scalarExtreme.ok())for(const auto& p:scalarExtreme.figureActions.back().mesh->points)expect(p[0]==0.5,"finite opposite-extreme scalar interpolation gives midpoint");
    auto coordinateExtreme=run("contour([-1e308,1e308],[0,1],[-1,1;-1,1],0.0);");if(coordinateExtreme.ok())for(const auto& p:coordinateExtreme.figureActions.back().mesh->points)expect(p[0]==0&&std::isfinite(p[1]),"finite opposite-extreme coordinate interpolation gives origin");
    auto hole=run("isosurface([0,1],[0,1],[0,1],[0,1;-2,-1;-2,-1;-4,-3],0.0);");if(hole.ok()){const auto& m=*hole.figureActions.back().mesh;double area=0;for(const auto& t:m.triangles)area+=triangleArea(m.points[t[0]],m.points[t[1]],m.points[t[2]]);expect(std::abs(area-.375)<1e-14,"exact-level vertex plane has full triangular area0.375");}
    auto wholeLevel=run("isosurface([0,1],[0,1],[0,1],[0,0;0,0;0,0;0,0],0.0);");if(wholeLevel.ok())expect(wholeLevel.figureActions.back().mesh->points.empty(),"constant-level volume has no unique two-dimensional surface");
    auto touchPoint=run("isosurface([0,1],[0,1],[0,1],[0,1;1,2;1,2;2,3],0.0);");if(touchPoint.ok())expect(touchPoint.figureActions.back().mesh->triangles.empty(),"point tangency does not create degenerate triangles");
    auto touchEdge=run("isosurface([0,1],[0,1],[0,1],[0,1;1,2;0,1;1,2],0.0);");if(touchEdge.ok())expect(touchEdge.figureActions.back().mesh->triangles.empty(),"line tangency does not create degenerate triangles");
    auto thin=run("isosurface([-1e308,1e308],[0,1e-308],[0,1],[-1,1;-1,1;-1,1;-1,1],0.0);");if(thin.ok()){const auto& m=*thin.figureActions.back().mesh;expect(!m.triangles.empty(),"valid extremely thin surface remains present");for(const auto& p:m.points)expect(p[0]==0&&std::isfinite(p[1])&&std::isfinite(p[2]),"thin surface coordinates remain finite");}
    run("matrix t=linspace(0,6.283185307179586,101);matrix points=[cos(t)',sin(t)'];record g=curvegeometry(points);assert(abs(g.length-6.283185307179586)<.002);assert(abs(g.curvature(50)-1)<.002);");
    run("surf([0,1],[0,1],[1,2,3]);",false);run("surf([0,0],[0,1],[1,2;3,4]);",false);run("contour([0,1],[0,1],[i,j;k,ell]);",false);run("curvegeometry([0,0;0,0;1,1]);",false);
}
}
int main(){independentPlanes();language();std::cout<<checks<<" geometry checks, "<<failed<<" failures\n";return failed?1:0;}
