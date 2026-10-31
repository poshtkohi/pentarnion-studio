#include "pde_stiff_api.h"
#include "pde_kernel_support.hpp"
#include <Eigen/Core>
#include <Eigen/Eigenvalues>
#include <Eigen/LU>
#include <Eigen/SparseCore>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
using Sparse=Eigen::SparseMatrix<double,Eigen::RowMajor>;
using Entry=Eigen::Triplet<double>;
using Vector=std::vector<double>;
using Point=std::array<double,5>;
struct Failure:std::runtime_error{int status;Failure(int value,const std::string& message):std::runtime_error(message),status(value){}};
struct Retry:std::runtime_error{using std::runtime_error::runtime_error;};
double finite(long double value){if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Failure(PENTA_NUMERIC_DOMAIN,"Implicit PDE arithmetic is not finite/representable.");return static_cast<double>(value);}
struct Work {
    const penta_pde_host_v1& host;const penta_pde_stiff_request_v1& q;penta_pde_stiff_result_v1& out;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();std::atomic_uint64_t used{0};
    void check()const{if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_NUMERIC_CANCELLED,"Implicit PDE solve cancelled.");if(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()>q.max_seconds)throw Failure(7,"Implicit PDE wall-time allowance exceeded.");}
    void spend(uint64_t count=1){check();auto before=used.load(std::memory_order_relaxed);do{if(count>q.max_work-before)throw Failure(7,"Implicit PDE native work allowance exceeded.");}while(!used.compare_exchange_weak(before,before+count,std::memory_order_relaxed));}
    void commit(){check();const auto count=used.load(std::memory_order_relaxed);char error[1024]{};if(host.charge&&host.charge(host.context,count,error,sizeof(error)))throw Failure(7,error[0]?error:"Implicit PDE host work allowance exceeded.");out.work_units=count;}
    template<class Function>void parallel(uint64_t count,uint64_t cost,Function&& function){spend(count*cost);std::atomic_int status{0};auto task=[&](uint64_t first,uint64_t last){try{function(first,last);}catch(const Failure& failure){int expected=0;status.compare_exchange_strong(expected,failure.status);throw;}catch(const Retry&){int expected=0;status.compare_exchange_strong(expected,-1);throw;}};try{penta::pde::kernel::range(&host,count,cost,task,32);}catch(const std::exception& failure){if(status.load()==-1)throw Retry(failure.what());if(status.load())throw Failure(status.load(),failure.what());throw;}}
};
struct Piece {std::array<uint64_t,3> nodes{};unsigned count=2;double measure=0,spacing=0;Point point{},tangent{};std::array<Point,3> gradient{};};
struct Geometry {unsigned dimension=1;uint64_t nodes=0;std::vector<Point> points;Vector mass;std::vector<Piece> pieces;std::vector<Point> boundaryNormals;std::array<bool,2> periodic{};std::array<double,2> spacing{};};
uint64_t baseBytes(const penta_pde_stiff_request_v1& q,const penta_pde_stiff_result_v1& out){return out.value_capacity*sizeof(double)+q.nodes*q.components*(q.gmres_restart+20)*sizeof(double)+q.nodes*q.components*q.components*2*sizeof(double)+(q.nodes+q.triangles)*512+q.boundary_count*128;}
long double dot(const Point& a,const Point& b,unsigned dimension){long double sum=0;for(unsigned k=0;k<dimension;++k)sum+=static_cast<long double>(a[k])*b[k];return sum;}
Point difference(const Point& a,const Point& b,unsigned dimension){Point result{};for(unsigned k=0;k<dimension;++k)result[k]=finite(static_cast<long double>(a[k])-b[k]);return result;}
double length(const Point& a,unsigned dimension){return finite(std::sqrt(dot(a,a,dimension)));}
void validate(const penta_pde_stiff_request_v1& q,const penta_pde_stiff_result_v1& out){
    if(q.abi_version!=PENTA_PDE_STIFF_ABI_V1||q.components<1||q.components>64||q.nodes<2||q.nodes>16384||q.nodes>262144/q.components||q.dimension<1||q.dimension>5||!q.initial||!q.reaction||!out.times||!out.values||!out.means||!out.spatial_rms)throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid implicit PDE descriptors or flattened real-coordinate layout.");
    for(const auto value:{q.diffusion,q.t0,q.t1,q.time_step,q.sample_step,q.minimum_step,q.maximum_step,q.rtol,q.atol,q.newton_tolerance,q.linear_tolerance,q.max_seconds})if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit PDE settings must be finite.");
    if(q.method<1||q.method>3||q.adaptive>1||q.diffusion<0||q.t1<q.t0||q.time_step<=0||q.sample_step<=0||q.minimum_step<=0||q.maximum_step<q.minimum_step||q.rtol<=0||q.rtol>=.1||q.atol<=0||q.newton_tolerance<=0||q.newton_tolerance>=.1||q.linear_tolerance<=0||q.linear_tolerance>=.1||q.max_seconds<=0||q.max_seconds>3600||!q.max_steps||q.max_steps>1000000||!q.max_newton||q.max_newton>100||!q.max_linear||q.max_linear>10000||q.gmres_restart<2||q.gmres_restart>64||!q.max_work||q.max_work>1000000000000ULL||q.max_bytes<1048576||q.max_bytes>1073741824||q.boundary_count>65536||(q.boundary_count&&!q.boundaries))throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid implicit PDE tolerance, time or resource allowance.");
    const auto total=q.nodes*q.components;const long double samples=std::ceil((q.t1-q.t0)/q.sample_step)+2;
    if(samples>100000||out.sample_capacity>100000||samples>out.sample_capacity||out.value_capacity>8000000||samples*total>out.value_capacity||samples*total>8000000||baseBytes(q,out)>q.max_bytes||q.max_bytes-baseBytes(q,out)<total*96)throw Failure(7,"Implicit PDE retained field/Krylov/block memory allowance exceeded.");
    for(uint64_t k=0;k<total;++k)if(!std::isfinite(q.initial[k]))throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit PDE initial coefficients must be finite.");
}
Geometry geometry(Work& work){
    const auto& q=work.q;Geometry result;result.dimension=q.dimension;result.nodes=q.nodes;result.points.resize(q.nodes);result.mass.assign(q.nodes,0);
    if(q.method==PENTA_STIFF_FD1||q.method==PENTA_STIFF_FD2){
        const auto nx=q.nx,ny=q.method==PENTA_STIFF_FD1?1:q.ny;if(q.dimension!=(ny==1?1u:2u)||nx<3||nx>4096||ny<1||ny>256||(q.method==PENTA_STIFF_FD2&&ny<3)||nx*ny!=q.nodes||!q.x||(ny>1&&!q.y))throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit FD requires matching uniform real axes.");
        auto spacing=[](const double* axis,uint64_t n){const double h=axis[1]-axis[0];if(!std::isfinite(h)||h<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit FD axes must be finite and strictly increasing.");for(uint64_t i=0;i<n;++i)if(!std::isfinite(axis[i])||std::abs((axis[i]-axis[0])-i*h)>1e-10*std::max(std::abs(axis[i]-axis[0]),h))throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit FD currently requires uniform axes.");return h;};
        const double hx=spacing(q.x,nx),hy=ny>1?spacing(q.y,ny):1;
        std::array<uint64_t,4> periodic{};for(uint64_t index=0;index<q.boundary_count;++index){const auto& edge=q.boundaries[index];if(edge.kind!=PENTA_STIFF_PERIODIC)continue;if(!edge.first||edge.first>q.nodes||!edge.second||edge.second>q.nodes)throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid periodic FD boundary nodes.");const auto a=edge.first-1,b=edge.second-1;if(ny==1){if(a!=b||(a!=0&&a+1!=nx))throw Failure(PENTA_NUMERIC_ARGUMENT,"FD1 periodic boundaries must pair both endpoints.");++periodic[a==0?0:1];}else if(a%nx==0&&b%nx==0)++periodic[0];else if(a%nx+1==nx&&b%nx+1==nx)++periodic[1];else if(a/nx==0&&b/nx==0)++periodic[2];else if(a/nx+1==ny&&b/nx+1==ny)++periodic[3];else throw Failure(PENTA_NUMERIC_ARGUMENT,"Periodic FD2 boundaries must cover complete outer sides.");}
        const bool px=periodic[0]||periodic[1],py=periodic[2]||periodic[3];if((px&&(periodic[0]!=(ny==1?1:ny-1)||periodic[1]!=(ny==1?1:ny-1)))||(py&&(periodic[2]!=nx-1||periodic[3]!=nx-1)))throw Failure(PENTA_NUMERIC_ARGUMENT,"Periodic FD boundaries require both complete opposing sides.");
        result.periodic={px,py};result.spacing={hx,hy};
        for(uint64_t y=0;y<ny;++y)for(uint64_t x=0;x<nx;++x){const auto node=y*nx+x;result.points[node]={q.x[x],ny>1?q.y[y]:0,0,0,0};const double wx=!px&&(x==0||x+1==nx)?.5:1,wy=!py&&ny>1&&(y==0||y+1==ny)?.5:1;result.mass[node]=hx*hy*wx*wy;
            if(x+1<nx||px){Piece p;p.nodes={node,x+1<nx?node+1:y*nx,0};p.measure=hy*wy/hx;p.spacing=hx;p.tangent[0]=1;p.point=result.points[node];p.point[0]+=hx*.5;result.pieces.push_back(p);}
            if(ny>1&&(y+1<ny||py)){Piece p;p.nodes={node,y+1<ny?node+nx:x,0};p.measure=hx*wx/hy;p.spacing=hy;p.tangent[1]=1;p.point=result.points[node];p.point[1]+=hy*.5;result.pieces.push_back(p);}
        }
    }else {
        if(q.dimension<2||q.dimension>5||!q.points||!q.faces||!q.triangles||q.triangles>32768)throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit P1 requires real triangle coordinates in dimensions2..5.");
        for(uint64_t node=0;node<q.nodes;++node)for(unsigned k=0;k<q.dimension;++k){const auto value=q.points[node*q.dimension+k];if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_ARGUMENT,"Triangle coordinates must be finite.");result.points[node][k]=value;}
        std::set<std::array<uint64_t,3>> seen;std::map<std::pair<uint64_t,uint64_t>,unsigned> edges;
        for(uint64_t triangle=0;triangle<q.triangles;++triangle){work.spend(64*q.dimension);Piece p;p.count=3;for(unsigned k=0;k<3;++k){const auto id=q.faces[triangle*3+k];if(!id||id>q.nodes)throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit P1 triangles require valid one-based vertex IDs.");p.nodes[k]=id-1;}auto sorted=p.nodes;std::sort(sorted.begin(),sorted.end());if(sorted[0]==sorted[1]||sorted[1]==sorted[2]||!seen.insert(sorted).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Repeated/duplicate implicit P1 triangle.");for(unsigned k=0;k<3;++k)if(++edges[std::minmax(p.nodes[k],p.nodes[(k+1)%3])]>2)throw Failure(PENTA_NUMERIC_ARGUMENT,"Nonmanifold implicit P1 edge.");
            const auto a=difference(result.points[p.nodes[1]],result.points[p.nodes[0]],q.dimension),b=difference(result.points[p.nodes[2]],result.points[p.nodes[0]],q.dimension);const long double aa=dot(a,a,q.dimension),bb=dot(b,b,q.dimension),ab=dot(a,b,q.dimension),det=aa*bb-ab*ab;if(det<=128*std::numeric_limits<double>::epsilon()*aa*bb||!std::isfinite(det))throw Failure(PENTA_NUMERIC_ARGUMENT,"Degenerate or ill-conditioned implicit P1 triangle.");p.measure=finite(std::sqrt(det)*.5);
            for(unsigned k=0;k<q.dimension;++k){p.gradient[1][k]=finite((bb*a[k]-ab*b[k])/det);p.gradient[2][k]=finite((aa*b[k]-ab*a[k])/det);p.gradient[0][k]=-p.gradient[1][k]-p.gradient[2][k];for(unsigned vertex=0;vertex<3;++vertex)p.point[k]+=result.points[p.nodes[vertex]][k]/3;}
            for(auto node:p.nodes)result.mass[node]+=p.measure/3;
            result.pieces.push_back(p);
        }
        for(const auto mass:result.mass)if(!std::isfinite(mass)||mass<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit P1 mesh contains unused vertices or invalid mass.");
    }
    result.boundaryNormals.resize(q.boundary_count);
    for(uint64_t k=0;k<q.boundary_count;++k){const auto& edge=q.boundaries[k];if(!edge.first||edge.first>q.nodes||!edge.second||edge.second>q.nodes||!edge.tag||edge.kind<1||edge.kind>4||(q.method==PENTA_STIFF_TRIANGLE_P1&&edge.kind==PENTA_STIFF_PERIODIC)||!std::isfinite(edge.robin_alpha)||edge.robin_alpha<0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid implicit PDE boundary edge/type/Robin coefficient.");const auto a=edge.first-1,b=edge.second-1;auto& normal=result.boundaryNormals[k];
        if(q.method==PENTA_STIFF_FD1){if(a!=b||(a!=0&&a+1!=q.nodes))throw Failure(PENTA_NUMERIC_ARGUMENT,"Implicit FD1 boundaries must identify the two endpoint nodes.");normal[0]=a==0?-1:1;}
        else {const auto tangent=difference(result.points[b],result.points[a],q.dimension);const auto norm=dot(tangent,tangent,q.dimension);if(norm<=0)throw Failure(PENTA_NUMERIC_ARGUMENT,"A boundary edge has zero length.");if(q.method==PENTA_STIFF_FD2){const auto x=a%q.nx,y=a/q.nx,distance=a>b?a-b:b-a;const bool vertical=distance==q.nx||(result.periodic[1]&&distance==q.nx*(q.ny-1)),horizontal=distance==1||(result.periodic[0]&&distance==q.nx-1);if(x==0&&b%q.nx==0&&vertical)normal[0]=-1;else if(x+1==q.nx&&b%q.nx+1==q.nx&&vertical)normal[0]=1;else if(y==0&&b/q.nx==0&&horizontal)normal[1]=-1;else if(y+1==q.ny&&b/q.nx+1==q.ny&&horizontal)normal[1]=1;else throw Failure(PENTA_NUMERIC_ARGUMENT,"FD2 boundary edges must join adjacent nodes on one outer side.");}
            else {const Piece* adjacent=nullptr;for(const auto& p:result.pieces)if(std::find(p.nodes.begin(),p.nodes.end(),a)!=p.nodes.end()&&std::find(p.nodes.begin(),p.nodes.end(),b)!=p.nodes.end()){if(adjacent)throw Failure(PENTA_NUMERIC_ARGUMENT,"Tagged P1 edge is not an exterior boundary.");adjacent=&p;}if(!adjacent)throw Failure(PENTA_NUMERIC_ARGUMENT,"Tagged P1 boundary edge is absent from the mesh.");Point outward{};for(unsigned d=0;d<q.dimension;++d)outward[d]=(result.points[a][d]+result.points[b][d])*.5-adjacent->point[d];const auto projection=dot(outward,tangent,q.dimension)/norm;for(unsigned d=0;d<q.dimension;++d)normal[d]=finite(outward[d]-projection*tangent[d]);const auto n=length(normal,q.dimension);if(n==0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Cannot determine the surface boundary conormal.");for(unsigned d=0;d<q.dimension;++d)normal[d]/=n;}
        }
    }
    return result;
}
struct Operator {Sparse matrix;Vector load,fixedValues;std::vector<unsigned char> fixed;};
class Engine {
    Work& work_;const penta_pde_stiff_request_v1& q_;penta_pde_stiff_result_v1& out_;Geometry geometry_;const uint64_t c_,total_;
    std::atomic_uint64_t reactions_{0},jacobians_{0},coefficients_{0};
    bool constantCoefficientChecked_=false;
    void coefficient(double time,const Point& point,Vector& diffusion,Point& velocity){
        diffusion.assign(c_*c_,0);for(uint64_t k=0;k<c_;++k)diffusion[k*c_+k]=q_.diffusion;
        if(q_.diffusion_block)std::copy_n(q_.diffusion_block,c_*c_,diffusion.begin());
        velocity={};if(q_.velocity)std::copy_n(q_.velocity,q_.dimension,velocity.begin());
        if(!q_.coefficient&&constantCoefficientChecked_)return;
        if(q_.coefficient){char error[1024]{};if(q_.coefficient(q_.operator_context,time,point.data(),q_.dimension,diffusion.data(),velocity.data(),error,sizeof(error)))throw Failure(PENTA_NUMERIC_DOMAIN,error[0]?error:"Native space/time PDE coefficient failed.");coefficients_.fetch_add(1,std::memory_order_relaxed);}
        double scale=0;Eigen::MatrixXd symmetric(c_,c_);for(uint64_t i=0;i<c_;++i)for(uint64_t j=0;j<c_;++j){const auto value=diffusion[i*c_+j];if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_DOMAIN,"Diffusion block must be finite.");scale=std::max(scale,std::abs(value));symmetric(i,j)=value*.5+diffusion[j*c_+i]*.5;}
        for(unsigned d=0;d<q_.dimension;++d)if(!std::isfinite(velocity[d]))throw Failure(PENTA_NUMERIC_DOMAIN,"Advection velocity must be finite.");
        work_.spend(std::max<uint64_t>(1,c_*c_*c_));if(scale){symmetric/=scale;Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> spectrum(symmetric,Eigen::EigenvaluesOnly);if(spectrum.info()!=Eigen::Success||spectrum.eigenvalues().minCoeff()<-1e-12)throw Failure(PENTA_NUMERIC_DOMAIN,"Ordered diffusion requires a positive-semidefinite symmetric part in the declared real coordinates.");}
    }
    Operator assemble(double time){
        work_.check();Operator result;result.load.assign(total_,0);result.fixedValues.assign(total_,0);result.fixed.assign(q_.nodes,0);
        const auto capacity=std::min<uint64_t>(8000000,(q_.max_bytes-baseBytes(q_,out_))/96);std::atomic_uint64_t entries{0};std::vector<std::vector<Entry>> pieces(geometry_.pieces.size());
        work_.parallel(geometry_.pieces.size(),std::max<uint64_t>(1,c_*c_*16),[&](uint64_t first,uint64_t last){for(uint64_t id=first;id<last;++id){const auto& p=geometry_.pieces[id];Vector diffusion;Point velocity{};coefficient(time,p.point,diffusion,velocity);auto& values=pieces[id];values.reserve(p.count*p.count*c_*2);
            auto add=[&](uint64_t row,uint64_t col,long double value){if(value==0)return;if(entries.fetch_add(1,std::memory_order_relaxed)>=capacity)throw Failure(7,"Implicit PDE sparse assembly exceeds its explicit storage bound.");values.emplace_back(static_cast<int>(row),static_cast<int>(col),finite(value));};
            if(p.count==2){const auto a=p.nodes[0],b=p.nodes[1];for(uint64_t i=0;i<c_;++i)for(uint64_t j=0;j<c_;++j){const long double value=static_cast<long double>(p.measure)*diffusion[i*c_+j];add(a*c_+i,a*c_+j,value);add(a*c_+i,b*c_+j,-value);add(b*c_+i,a*c_+j,-value);add(b*c_+i,b*c_+j,value);}
                const double flow=finite(dot(p.tangent,velocity,q_.dimension));const auto area=p.measure*p.spacing;const auto upwind=flow>=0?a:b;for(uint64_t i=0;i<c_;++i){add(a*c_+i,upwind*c_+i,static_cast<long double>(flow)*area);add(b*c_+i,upwind*c_+i,-static_cast<long double>(flow)*area);}
            }else {Point projectedVelocity{};std::array<double,3> directional{};for(unsigned a=0;a<3;++a)directional[a]=finite(dot(velocity,p.gradient[a],q_.dimension));double speed=0,h=0;for(unsigned a=0;a<3;++a){h=std::max(h,length(difference(geometry_.points[p.nodes[a]],geometry_.points[p.nodes[(a+1)%3]],q_.dimension),q_.dimension));for(unsigned d=0;d<q_.dimension;++d)projectedVelocity[d]+=directional[a]*geometry_.points[p.nodes[a]][d];}speed=length(projectedVelocity,q_.dimension);const auto stabilization=speed?h/(2*speed):0;
                for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b){const auto stiffness=p.measure*dot(p.gradient[a],p.gradient[b],q_.dimension);for(uint64_t i=0;i<c_;++i)for(uint64_t j=0;j<c_;++j)add(p.nodes[a]*c_+i,p.nodes[b]*c_+j,stiffness*diffusion[i*c_+j]);for(uint64_t i=0;i<c_;++i)add(p.nodes[a]*c_+i,p.nodes[b]*c_+i,static_cast<long double>(p.measure)*(-directional[a]/3+stabilization*directional[a]*directional[b]));}
            }
        }});
        std::vector<Entry> all;all.reserve(static_cast<std::size_t>(entries.load())+q_.boundary_count*c_*2);for(auto& piece:pieces)all.insert(all.end(),piece.begin(),piece.end());
        std::set<std::pair<uint64_t,uint64_t>> tagged;
        for(uint64_t index=0;index<q_.boundary_count;++index){work_.spend(c_*8);const auto& edge=q_.boundaries[index];const auto a=edge.first-1,b=edge.second-1;if(!tagged.insert(std::minmax(a,b)).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"A PDE boundary edge is specified more than once.");if(edge.kind==PENTA_STIFF_PERIODIC)continue;double measure=a==b?1:length(difference(geometry_.points[a],geometry_.points[b],q_.dimension),q_.dimension);Point point{};for(unsigned d=0;d<q_.dimension;++d)point[d]=(geometry_.points[a][d]+geometry_.points[b][d])*.5;if(q_.method==PENTA_STIFF_FD2){const unsigned tangent=geometry_.boundaryNormals[index][0]!=0?1:0;measure=geometry_.spacing[tangent];if(geometry_.periodic[tangent]&&std::abs(geometry_.points[a][tangent]-geometry_.points[b][tangent])>1.5*measure)point[tangent]=std::max(geometry_.points[a][tangent],geometry_.points[b][tangent])+measure*.5;}
            auto evaluate=[&](const Point& at){Vector value(c_,0);if(edge.value)std::copy_n(edge.value,c_,value.begin());double alpha=edge.robin_alpha;if(q_.boundary){char error[1024]{};if(q_.boundary(q_.boundary_context,time,edge.tag,at.data(),geometry_.boundaryNormals[index].data(),q_.dimension,value.data(),&alpha,error,sizeof(error)))throw Failure(PENTA_NUMERIC_DOMAIN,error[0]?error:"Native time-dependent PDE boundary failed.");}for(const auto v:value)if(!std::isfinite(v))throw Failure(PENTA_NUMERIC_DOMAIN,"Boundary coefficients must be finite.");if(!std::isfinite(alpha)||alpha<0)throw Failure(PENTA_NUMERIC_DOMAIN,"Robin alpha must be finite and nonnegative.");return std::pair{std::move(value),alpha};};
            if(edge.kind==PENTA_STIFF_DIRICHLET){for(const auto node:{a,b}){const auto [value,alpha]=evaluate(geometry_.points[node]);(void)alpha;if(result.fixed[node])for(uint64_t k=0;k<c_;++k)if(std::abs(result.fixedValues[node*c_+k]-value[k])>q_.atol+q_.rtol*std::abs(value[k]))throw Failure(PENTA_NUMERIC_ARGUMENT,"Conflicting Dirichlet values meet at a boundary vertex.");result.fixed[node]=1;std::copy(value.begin(),value.end(),result.fixedValues.begin()+node*c_);}}
            else {const auto [value,alpha]=evaluate(point);const auto count=a==b?1:2;for(unsigned endpoint=0;endpoint<static_cast<unsigned>(count);++endpoint){const auto node=endpoint?b:a;for(uint64_t k=0;k<c_;++k){result.load[node*c_+k]=finite(static_cast<long double>(result.load[node*c_+k])+measure*value[k]/count);if(edge.kind==PENTA_STIFF_ROBIN&&alpha!=0)all.emplace_back(static_cast<int>(node*c_+k),static_cast<int>(node*c_+k),finite(static_cast<long double>(measure)*alpha/count));}}}
        }
        result.matrix.resize(total_,total_);result.matrix.setFromTriplets(all.begin(),all.end());result.matrix.makeCompressed();out_.maximum_nonzeros=std::max(out_.maximum_nonzeros,static_cast<uint64_t>(result.matrix.nonZeros()));return result;
    }
    Vector product(const Sparse& matrix,const Vector& value){Vector out(total_);work_.parallel(q_.nodes,std::max<uint64_t>(1,matrix.nonZeros()/q_.nodes),[&](uint64_t first,uint64_t last){for(uint64_t node=first;node<last;++node)for(uint64_t k=0;k<c_;++k){const auto row=node*c_+k;long double sum=0;for(Sparse::InnerIterator item(matrix,static_cast<int>(row));item;++item)sum+=static_cast<long double>(item.value())*value[static_cast<uint64_t>(item.col())];out[row]=finite(sum);}});return out;}
    long double inner(const Vector& a,const Vector& b){const auto groups=(total_+1023)/1024;std::vector<long double> sums(groups);work_.parallel(groups,1024,[&](uint64_t first,uint64_t last){for(uint64_t group=first;group<last;++group){const auto end=std::min(total_,(group+1)*1024);long double sum=0;for(uint64_t i=group*1024;i<end;++i)sum+=static_cast<long double>(a[i])*b[i];sums[group]=sum;}});return std::accumulate(sums.begin(),sums.end(),0.L);}
    long double norm(const Vector& value){return std::sqrt(std::max(0.L,inner(value,value)));}
    struct Reaction {Vector value,jacobian;};
    Reaction reaction(double time,const Vector& state,bool jacobian){Reaction out;out.value.resize(total_);if(jacobian)out.jacobian.resize(q_.nodes*c_*c_);work_.parallel(q_.nodes,std::max<uint64_t>(1,c_*(jacobian?c_+1:1)),[&](uint64_t first,uint64_t last){for(uint64_t node=first;node<last;++node){char error[1024]{};if(q_.reaction(q_.reaction_context,time,geometry_.points[node].data(),q_.dimension,state.data()+node*c_,static_cast<uint32_t>(c_),out.value.data()+node*c_,jacobian?out.jacobian.data()+node*c_*c_:nullptr,error,sizeof(error)))throw Failure(PENTA_NUMERIC_DOMAIN,error[0]?error:"Compiled implicit PDE reaction/Jacobian failed.");reactions_.fetch_add(1,std::memory_order_relaxed);if(jacobian)jacobians_.fetch_add(1,std::memory_order_relaxed);}});for(const auto value:out.value)if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_DOMAIN,"Implicit PDE reaction is nonfinite.");for(const auto value:out.jacobian)if(!std::isfinite(value))throw Failure(PENTA_NUMERIC_DOMAIN,"Compiler-generated local reaction Jacobian is nonfinite.");return out;}
    Vector residual(const Operator& spatial,double time,double dt,const Vector& old,const Vector& state,Reaction* local=nullptr){auto force=reaction(time,state,local!=nullptr);auto out=product(spatial.matrix,state);for(uint64_t node=0;node<q_.nodes;++node)for(uint64_t k=0;k<c_;++k){const auto i=node*c_+k;out[i]=spatial.fixed[node]?state[i]-spatial.fixedValues[i]:finite(static_cast<long double>(out[i])+static_cast<long double>(geometry_.mass[node])*(state[i]-old[i])/dt-geometry_.mass[node]*force.value[i]-spatial.load[i]);}if(local)*local=std::move(force);return out;}
    double errorNorm(const Vector& residual,const Vector& old,const Vector& state,double dt,const Operator* spatial=nullptr){long double sum=0;for(uint64_t node=0;node<q_.nodes;++node)for(uint64_t k=0;k<c_;++k){const auto i=node*c_+k;long double scale=q_.atol+q_.rtol*std::max(std::abs(old[i]),std::abs(state[i]));if(spatial&&!spatial->fixed[node])scale*=geometry_.mass[node]/dt;const auto ratio=residual[i]/scale;sum+=ratio*ratio;}return finite(std::sqrt(sum/total_));}
    Sparse jacobian(const Operator& spatial,const Reaction& local,double dt){
        const auto maximum=std::min<uint64_t>(8000000,(q_.max_bytes-baseBytes(q_,out_))/96);if(static_cast<uint64_t>(spatial.matrix.nonZeros())+q_.nodes*c_*c_>maximum)throw Failure(7,"Compiler-generated block Jacobian exceeds the sparse storage allowance.");
        std::vector<Entry> entries;entries.reserve(static_cast<std::size_t>(spatial.matrix.nonZeros())+q_.nodes*c_*c_);
        for(int row=0;row<spatial.matrix.outerSize();++row)if(!spatial.fixed[static_cast<uint64_t>(row)/c_])for(Sparse::InnerIterator item(spatial.matrix,row);item;++item)entries.emplace_back(item.row(),item.col(),item.value());
        for(uint64_t node=0;node<q_.nodes;++node)for(uint64_t i=0;i<c_;++i){if(spatial.fixed[node])entries.emplace_back(static_cast<int>(node*c_+i),static_cast<int>(node*c_+i),1);else for(uint64_t j=0;j<c_;++j){const auto value=finite((i==j?static_cast<long double>(geometry_.mass[node])/dt:0)-static_cast<long double>(geometry_.mass[node])*local.jacobian[(node*c_+i)*c_+j]);if(value!=0)entries.emplace_back(static_cast<int>(node*c_+i),static_cast<int>(node*c_+j),value);}}
        Sparse result(total_,total_);result.setFromTriplets(entries.begin(),entries.end());result.makeCompressed();out_.maximum_nonzeros=std::max(out_.maximum_nonzeros,static_cast<uint64_t>(result.nonZeros()));return result;
    }
    Vector linear(const Sparse& matrix,const Vector& rhs){
        const auto rhsNorm=norm(rhs);if(rhsNorm==0)return Vector(total_,0);
        if(q_.linear){Vector answer(total_,0);std::vector<uint64_t> offsets(total_+1),columns(static_cast<std::size_t>(matrix.nonZeros()));for(uint64_t row=0;row<=total_;++row)offsets[row]=static_cast<uint64_t>(matrix.outerIndexPtr()[row]);for(std::size_t at=0;at<columns.size();++at)columns[at]=static_cast<uint64_t>(matrix.innerIndexPtr()[at]);work_.spend(total_+columns.size());uint64_t iterations=0,spent=0;char error[1024]{};const int status=q_.linear(q_.linear_context,total_,columns.size(),offsets.data(),columns.data(),matrix.valuePtr(),rhs.data(),q_.max_work-work_.used.load(),q_.linear_tolerance,answer.data(),&iterations,&spent,error,sizeof(error));work_.spend(spent);out_.linear_iterations+=iterations;if(status==10)throw Retry(error[0]?error:"Host sparse solver did not converge.");if(status)throw Failure(status,error[0]?error:"Host sparse solver failed.");const auto computed=product(matrix,answer);Vector residual(total_);for(uint64_t k=0;k<total_;++k)residual[k]=finite(static_cast<long double>(rhs[k])-computed[k]);if(norm(residual)>1.1*q_.linear_tolerance*rhsNorm)throw Retry("Host sparse solver candidate failed the independent Newton residual check.");return answer;}
        Vector blocks(q_.nodes*c_*c_);
        work_.parallel(q_.nodes,std::max<uint64_t>(1,c_*c_*c_*3),[&](uint64_t first,uint64_t last){for(uint64_t node=first;node<last;++node){Eigen::MatrixXd block(c_,c_);for(uint64_t i=0;i<c_;++i)for(uint64_t j=0;j<c_;++j)block(i,j)=matrix.coeff(static_cast<int>(node*c_+i),static_cast<int>(node*c_+j));const auto scale=block.cwiseAbs().maxCoeff();if(!std::isfinite(scale)||scale==0)throw Retry("Zero local block in the Newton preconditioner.");block/=scale;Eigen::FullPivLU<Eigen::MatrixXd> factor(block);factor.setThreshold(1e-14);if(factor.rank()<static_cast<Eigen::Index>(c_)){block.diagonal().array()+=1e-8;factor.compute(block);}Eigen::MatrixXd inverse=factor.inverse()/scale;if(!inverse.allFinite())throw Retry("Local Newton block preconditioner is not finite.");for(uint64_t i=0;i<c_;++i)for(uint64_t j=0;j<c_;++j)blocks[(node*c_+i)*c_+j]=inverse(i,j);}});
        auto precondition=[&](const Vector& value){Vector result(total_);work_.parallel(q_.nodes,std::max<uint64_t>(1,c_*c_),[&](uint64_t first,uint64_t last){for(uint64_t node=first;node<last;++node)for(uint64_t i=0;i<c_;++i){long double sum=0;for(uint64_t j=0;j<c_;++j)sum+=static_cast<long double>(blocks[(node*c_+i)*c_+j])*value[node*c_+j];result[node*c_+i]=finite(sum);}});return result;};
        auto subtract=[](const Vector& a,const Vector& b){Vector result(a.size());for(std::size_t k=0;k<a.size();++k)result[k]=finite(static_cast<long double>(a[k])-b[k]);return result;};
        Vector answer(total_,0);uint64_t iterations=0;const auto restart=static_cast<unsigned>(std::min<uint64_t>(q_.gmres_restart,total_));
        while(iterations<q_.max_linear){work_.check();const auto trueResidual=subtract(rhs,product(matrix,answer));if(norm(trueResidual)<=q_.linear_tolerance*rhsNorm)return answer;auto residual=precondition(trueResidual);const auto beta=norm(residual);if(beta==0)throw Retry("Newton preconditioning lost a nonzero residual.");
            std::vector<Vector> basis;basis.reserve(restart+1);for(auto& value:residual)value=finite(value/beta);basis.push_back(std::move(residual));std::vector<long double> h((restart+1)*restart,0),cosine(restart),sine(restart),g(restart+1,0);g[0]=beta;unsigned columns=0;
            for(unsigned j=0;j<restart&&iterations<q_.max_linear;++j){++iterations;++out_.linear_iterations;auto next=precondition(product(matrix,basis[j]));
                // Two-pass modified Gram-Schmidt limits loss of orthogonality
                // for stiff/nonnormal ordered real-coordinate operators.
                for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<=j;++i){const auto projection=inner(next,basis[i]);h[i*restart+j]+=projection;for(uint64_t k=0;k<total_;++k)next[k]=finite(static_cast<long double>(next[k])-projection*basis[i][k]);}
                const auto nextNorm=norm(next);h[(j+1)*restart+j]=nextNorm;if(nextNorm>0){for(auto& value:next)value=finite(value/nextNorm);basis.push_back(std::move(next));}
                for(unsigned i=0;i<j;++i){const auto a=h[i*restart+j],b=h[(i+1)*restart+j];h[i*restart+j]=cosine[i]*a+sine[i]*b;h[(i+1)*restart+j]=-sine[i]*a+cosine[i]*b;}
                const auto a=h[j*restart+j],b=h[(j+1)*restart+j],magnitude=std::hypot(a,b);if(magnitude==0)throw Retry("Newton GMRES encountered a singular projected operator.");cosine[j]=a/magnitude;sine[j]=b/magnitude;h[j*restart+j]=magnitude;h[(j+1)*restart+j]=0;g[j+1]=-sine[j]*g[j];g[j]*=cosine[j];columns=j+1;if(std::abs(g[j+1])<=q_.linear_tolerance*beta||nextNorm==0)break;
            }
            std::vector<long double> y(columns);for(unsigned i=columns;i-->0;){long double value=g[i];for(unsigned j=i+1;j<columns;++j)value-=h[i*restart+j]*y[j];if(h[i*restart+i]==0)throw Retry("Singular Newton GMRES triangular system.");y[i]=value/h[i*restart+i];}
            for(uint64_t k=0;k<total_;++k){long double value=answer[k];for(unsigned j=0;j<columns;++j)value+=y[j]*basis[j][k];answer[k]=finite(value);}
        }
        if(norm(subtract(rhs,product(matrix,answer)))>q_.linear_tolerance*rhsNorm)throw Retry("Parallel sparse Newton GMRES did not meet its true residual tolerance.");
        return answer;
    }
    Vector step(double time,double dt,const Vector& old){
        auto spatial=assemble(time+dt);Vector state=old;for(uint64_t node=0;node<q_.nodes;++node)if(spatial.fixed[node])std::copy_n(spatial.fixedValues.begin()+node*c_,c_,state.begin()+node*c_);
        for(uint64_t iteration=0;iteration<q_.max_newton;++iteration){work_.check();Reaction local;auto force=residual(spatial,time+dt,dt,old,state,&local);const auto before=errorNorm(force,old,state,dt,&spatial);if(before<=q_.newton_tolerance)return state;for(auto& value:force)value=-value;auto update=linear(jacobian(spatial,local,dt),force);++out_.newton_iterations;double alpha=1;bool accepted=false;
            for(unsigned attempt=0;attempt<12;++attempt){Vector trial(total_);for(uint64_t k=0;k<total_;++k)trial[k]=finite(static_cast<long double>(state[k])+alpha*update[k]);try{const auto after=errorNorm(residual(spatial,time+dt,dt,old,trial),old,trial,dt,&spatial);if(after<before*(1-1e-4*alpha)||after<=q_.newton_tolerance){state=std::move(trial);accepted=true;break;}}catch(const Failure& failure){if(failure.status!=PENTA_NUMERIC_DOMAIN)throw;}alpha*=.5;}
            if(!accepted)throw Retry("Implicit PDE Newton line search did not reduce its scaled residual.");
        }
        throw Retry("Implicit PDE Newton iteration allowance exhausted.");
    }
public:
    Engine(Work& work):work_(work),q_(work.q),out_(work.out),geometry_(geometry(work)),c_(q_.components),total_(q_.nodes*c_){if(!q_.coefficient){Vector diffusion;Point velocity{};coefficient(q_.t0,Point{},diffusion,velocity);constantCoefficientChecked_=true;}}
    void solve();
    void counters(){out_.reaction_evaluations=reactions_.load();out_.jacobian_evaluations=jacobians_.load();out_.operator_evaluations=coefficients_.load();out_.work_units=work_.used.load();}
};
void Engine::solve(){
    Vector state(q_.initial,q_.initial+total_);const auto initialBoundary=assemble(q_.t0);for(uint64_t node=0;node<q_.nodes;++node)if(initialBoundary.fixed[node])std::copy_n(initialBoundary.fixedValues.begin()+node*c_,c_,state.begin()+node*c_);
    const long double measure=std::accumulate(geometry_.mass.begin(),geometry_.mass.end(),0.L);if(!std::isfinite(measure)||measure<=0)throw Failure(PENTA_NUMERIC_DOMAIN,"Implicit PDE mass measure is not finite and positive.");
    bool stopped=false;auto save=[&](double time){const auto row=out_.samples;if(row>=out_.sample_capacity||(row+1)>out_.value_capacity/total_)throw Failure(7,"Implicit PDE output coordinate allowance exhausted.");out_.times[row]=time;std::copy(state.begin(),state.end(),out_.values+row*total_);long double rms=0;for(uint64_t k=0;k<c_;++k){long double mean=0;for(uint64_t node=0;node<q_.nodes;++node)mean+=static_cast<long double>(geometry_.mass[node])*state[node*c_+k]/measure;out_.means[row*c_+k]=finite(mean);for(uint64_t node=0;node<q_.nodes;++node){const auto delta=state[node*c_+k]-mean;rms+=geometry_.mass[node]*delta*delta/measure;}}out_.spatial_rms[row]=finite(std::sqrt(std::max(0.L,rms)));++out_.samples;if(q_.sample){char error[1024]{};const int result=q_.sample(q_.sample_context,time,state.data(),q_.nodes,static_cast<uint32_t>(c_),error,sizeof(error));if(result<0)throw Failure(PENTA_NUMERIC_DOMAIN,error[0]?error:"Implicit PDE sample observer failed.");stopped=result>0;}};
    out_.samples=out_.accepted_steps=out_.rejected_steps=0;out_.minimum_used_step=std::numeric_limits<double>::infinity();double time=q_.t0,dt=std::clamp(q_.time_step,q_.minimum_step,q_.maximum_step);uint64_t nextSample=1;save(time);
    while(time<q_.t1&&!stopped){work_.check();if(out_.accepted_steps+out_.rejected_steps>=q_.max_steps)throw Failure(7,"Implicit PDE accepted/rejected time-step allowance exhausted.");const auto target=std::min(q_.t1,q_.t0+nextSample*q_.sample_step);if(target<=time){++nextSample;continue;}const auto actual=std::min({dt,q_.maximum_step,target-time});if(time+actual<=time)throw Failure(PENTA_NUMERIC_DOMAIN,"Implicit PDE step cannot advance binary64 time.");
        double error=0;Vector next;try{if(q_.adaptive){const auto coarse=step(time,actual,state),half=step(time,actual*.5,state);next=step(time+actual*.5,actual*.5,half);Vector difference(total_);for(uint64_t k=0;k<total_;++k)difference[k]=next[k]-coarse[k];error=errorNorm(difference,state,next,actual);out_.maximum_error_estimate=std::max(out_.maximum_error_estimate,error);}else next=step(time,actual,state);}catch(const Retry& failure){if(!q_.adaptive)throw Failure(PENTA_NUMERIC_DOMAIN,failure.what());error=std::numeric_limits<double>::infinity();}
        if(q_.adaptive&&error>1){++out_.rejected_steps;const auto factor=std::isfinite(error)?std::clamp(.8/std::sqrt(error),.1,.5):.5;dt=actual*factor;if(dt<q_.minimum_step)throw Failure(PENTA_NUMERIC_DOMAIN,"Implicit PDE error/Newton control reached minimum_step without convergence.");continue;}
        state=std::move(next);time+=actual;++out_.accepted_steps;out_.minimum_used_step=std::min(out_.minimum_used_step,actual);out_.maximum_used_step=std::max(out_.maximum_used_step,actual);if(q_.adaptive)dt=std::clamp(actual*(error>1e-16?std::clamp(.9/std::sqrt(error),.2,2.):2.),q_.minimum_step,q_.maximum_step);
        if(time>=target-16*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(target))){time=target;save(time);++nextSample;}
    }
    if(!out_.accepted_steps)out_.minimum_used_step=0;
    out_.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-work_.started).count();penta::pde::kernel::message(out_.status,sizeof(out_.status),stopped?"user_stopped":"completed");penta::pde::kernel::message(out_.message,sizeof(out_.message),"Fully implicit backward Euler; adaptive two-half-step control; analytic local block Jacobian; parallel GMRES/block-Jacobi; P1 uses lumped mass.");counters();work_.commit();
}
int execute(const penta_pde_host_v1* host,const penta_pde_stiff_request_v1* request,penta_pde_stiff_result_v1* result,char* error,uint64_t capacity)noexcept{
    try{if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_PDE_ABI_V1||!request||request->struct_size<sizeof(*request)||!result||result->struct_size<sizeof(*result))throw Failure(PENTA_NUMERIC_VERSION,"Incompatible implicit PDE capability ABI.");validate(*request,*result);result->newton_iterations=result->linear_iterations=result->maximum_nonzeros=0;result->maximum_error_estimate=0;Work work{*host,*request,*result};Engine engine(work);engine.solve();return 0;}
    catch(const Failure& failure){penta::pde::kernel::message(error,capacity,failure.what());return host&&host->cancelled&&host->cancelled(host->context)?PENTA_NUMERIC_CANCELLED:failure.status;}
    catch(const std::bad_alloc&){penta::pde::kernel::message(error,capacity,"Implicit PDE memory allocation exceeded available storage.");return 7;}
    catch(const std::exception& failure){penta::pde::kernel::message(error,capacity,failure.what());return host&&host->cancelled&&host->cancelled(host->context)?PENTA_NUMERIC_CANCELLED:PENTA_NUMERIC_DOMAIN;}
    catch(...){penta::pde::kernel::message(error,capacity,"Unknown implicit PDE solver failure.");return PENTA_NUMERIC_INTERNAL;}
}
}
#ifdef _WIN32
#define PENTA_STIFF_EXPORT __declspec(dllexport)
#else
#define PENTA_STIFF_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PENTA_STIFF_EXPORT const penta_pde_stiff_api_v1* penta_pde_stiff_query_v1(uint32_t version){static const penta_pde_stiff_api_v1 api{sizeof(api),PENTA_PDE_STIFF_ABI_V1,execute};return version==PENTA_PDE_STIFF_ABI_V1?&api:nullptr;}
