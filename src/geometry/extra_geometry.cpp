#include "extra_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <mutex>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace penta::geometry {
namespace {
struct Loaded{void* handle;const penta_geometry_extra_api_v1* api;};std::mutex moduleMutex;std::map<std::filesystem::path,Loaded> modules;
std::filesystem::path executable(){
#ifdef _WIN32
 std::vector<wchar_t> value(32768);const auto n=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));if(!n||n>=value.size())throw Error(4,"Cannot locate extra geometry module directory.");return std::filesystem::path(std::wstring(value.data(),n)).parent_path();
#else
 std::vector<char> value(32768);const auto n=readlink("/proc/self/exe",value.data(),value.size());if(n<=0||static_cast<std::size_t>(n)>=value.size())throw Error(4,"Cannot locate extra geometry module directory.");return std::filesystem::path(std::string(value.data(),n)).parent_path();
#endif
}
void message(char* out,uint64_t capacity,const char* text){if(out&&capacity){const auto n=std::min<uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,n);out[n]=0;}}
struct Host {
 const std::shared_ptr<std::atomic_bool>& cancellation;const std::function<void(std::size_t)>& charge;runtime::ParallelSettings settings;runtime::ParallelStatistics statistics;std::exception_ptr failure;
 static int cancelled(void* raw)noexcept{const auto& self=*static_cast<Host*>(raw);return self.cancellation&&self.cancellation->load(std::memory_order_relaxed);}
 static int account(void* raw,uint64_t amount,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{if(self.charge)self.charge(static_cast<std::size_t>(amount));return 0;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return 1;}catch(...){self.failure=std::current_exception();return 1;}}
 static int parallel(void* raw,uint64_t count,uint64_t cost,penta_numeric_range_task_v1 task,void* taskContext,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Host*>(raw);try{std::string failure;runtime::ParallelStatistics statistics;const auto status=runtime::ThreadBudget::instance().run(count,cost,task,taskContext,self.settings,self.cancellation,statistics,failure);statistics.parallelCalls+=self.statistics.parallelCalls;statistics.serialCalls+=self.statistics.serialCalls;statistics.threadsUsed=std::max(statistics.threadsUsed,self.statistics.threadsUsed);statistics.peakActiveThreads=std::max(statistics.peakActiveThreads,self.statistics.peakActiveThreads);self.statistics=statistics;if(status)message(error,capacity,failure.c_str());return status;}catch(const std::exception& e){self.failure=std::current_exception();message(error,capacity,e.what());return 4;}catch(...){self.failure=std::current_exception();return 4;}}
 penta_geometry_extra_host_v1 descriptor(){return {sizeof(penta_geometry_extra_host_v1),PENTA_GEOMETRY_EXTRA_ABI_V1,this,cancelled,account,parallel};}
};
void check(int status,const char* error,const Host& host){if(host.failure)std::rethrow_exception(host.failure);if(status)throw Error(status,error[0]?error:"Extended native geometry operation failed.");}
uint64_t combinations(unsigned n,unsigned k){if(k>n)return 0;uint64_t result=1;for(unsigned j=1;j<=k;++j)result=result*(n-j+1)/j;return result;}
}
ExtraModule::ExtraModule(std::filesystem::path directory){if(directory.empty())directory=executable()/"modules";if(!directory.is_absolute())throw Error(1,"Geometry module directory must be absolute.");
#ifdef _WIN32
 path_=directory/"penta_geometry.dll";
#else
 path_=directory/"libpenta_geometry.so";
#endif
}
const penta_geometry_extra_api_v1& ExtraModule::api()const{
 std::lock_guard lock(moduleMutex);if(const auto found=modules.find(path_);found!=modules.end())return *found->second.api;
#ifdef _WIN32
 const auto handle=LoadLibraryExW(path_.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw Error(4,"Cannot load optional penta_geometry module or dependencies.");const auto query=reinterpret_cast<penta_geometry_extra_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_geometry_extra_query_v1")));auto release=[&]{FreeLibrary(handle);};
#else
 const auto handle=dlopen(path_.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw Error(4,"Cannot load optional penta_geometry module or dependencies.");const auto query=reinterpret_cast<penta_geometry_extra_query_function_v1>(dlsym(handle,"penta_geometry_extra_query_v1"));auto release=[&]{dlclose(handle);};
#endif
 const auto result=query?query(PENTA_GEOMETRY_EXTRA_ABI_V1):nullptr;if(!result||result->struct_size<sizeof(*result)||result->abi_version!=PENTA_GEOMETRY_EXTRA_ABI_V1||!result->mesh||!result->forms||!result->laplace||!result->heat){release();throw Error(5,"penta_geometry lacks the negotiated mesh/forms/surface capability.");}return *modules.emplace(path_,Loaded{handle,result}).first->second.api;
}
MeshResult ExtraModule::mesh(penta_mesh_request_v1 request,const runtime::ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
 for(const auto size:request.sizes)if(size>1000000)throw Error(1,"Mesh size option exceeds its bound.");
 if(request.nu>100000||request.nv>100000)throw Error(1,"Parametric mesh axes exceed their bound.");
 uint64_t vertices=request.mesh.vertices,faces=request.mesh.triangles,boundaries=std::min<uint64_t>(300000,3*faces+request.mesh.boundaries);unsigned dimension=request.mesh.dimension;
 switch(request.operation){case PENTA_MESH_PLANE:vertices=request.sizes[0]*request.sizes[1];faces=request.sizes[0]&&request.sizes[1]?2*(request.sizes[0]-1)*(request.sizes[1]-1):0;boundaries=2*(request.sizes[0]+request.sizes[1]);dimension=3;break;
 case PENTA_MESH_SPHERE:{if(request.sizes[0]>7)throw Error(1,"Sphere refinements exceed7.");uint64_t power=1;for(uint64_t n=0;n<request.sizes[0];++n)power*=4;vertices=4*power+2;faces=8*power;boundaries=0;dimension=3;break;}
 case PENTA_MESH_CIRCLE:vertices=request.sizes[0];faces=0;boundaries=vertices;dimension=3;break;
 case PENTA_MESH_CYLINDER:vertices=request.sizes[0]*(request.sizes[1]+1)+((request.flags&1)?2:0);faces=2*request.sizes[0]*(request.sizes[1]+((request.flags&1)?1:0));boundaries=(request.flags&1)?0:2*request.sizes[0];dimension=3;break;
 case PENTA_MESH_PARAMETRIC:vertices=request.nu*request.nv;faces=2*((request.flags&1)?request.nu:request.nu?request.nu-1:0)*((request.flags&2)?request.nv:request.nv?request.nv-1:0);boundaries=2*(request.nu+request.nv);dimension=request.dimension;break;
 case PENTA_MESH_TRANSFORM:dimension=request.dimension;break;
 case PENTA_MESH_REFINE:if(request.sizes[0]>6)throw Error(1,"Refinement levels exceed6.");for(uint64_t n=0;n<request.sizes[0];++n){vertices+=3*faces;faces*=4;boundaries=std::min<uint64_t>(300000,2*boundaries);}vertices=std::min<uint64_t>(100000,vertices);break;
 case PENTA_MESH_LINE_PLANE:case PENTA_MESH_RAY_SPHERE:vertices=2;faces=boundaries=0;dimension=request.dimension;break;
 case PENTA_MESH_READ:vertices=100000;faces=200000;boundaries=300000;dimension=5;break;default:break;}
 if(vertices>100000||faces>200000||boundaries>300000||dimension<2||dimension>5)throw Error(7,"Mesh output estimate exceeds bounded storage.");
 MeshResult result;result.mesh.points.resize(vertices*dimension);result.mesh.faces.resize(3*faces);result.mesh.boundaries.resize(3*boundaries);result.areas.resize(faces);const bool inspect=request.operation==PENTA_MESH_INFO;if(inspect){result.projectors.resize(faces*dimension*dimension);if(dimension==3)result.normals.resize(3*faces);}auto& out=result.report;out.struct_size=sizeof(out);out.points=result.mesh.points.data();out.point_capacity=result.mesh.points.size();out.faces=result.mesh.faces.data();out.face_capacity=result.mesh.faces.size();out.boundary_tags=result.mesh.boundaries.data();out.boundary_capacity=result.mesh.boundaries.size();out.normals=result.normals.data();out.normal_capacity=result.normals.size();out.normal_projectors=result.projectors.data();out.projector_capacity=result.projectors.size();out.face_areas=result.areas.data();out.area_capacity=result.areas.size();request.struct_size=sizeof(request);Host context{cancellation,charge,settings,{},{}};auto host=context.descriptor();char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().mesh(&host,&request,&out,error,sizeof(error));});check(status,error,context);if(out.vertices>vertices||out.triangles>faces||out.boundaries>boundaries||out.dimension<2||out.dimension>5||out.vertices*out.dimension>result.mesh.points.size())throw Error(5,"Invalid native mesh output dimensions.");result.mesh.dimension=out.dimension;result.mesh.coordinateKind=out.coordinate_kind;result.mesh.gamma=out.gamma;result.mesh.points.resize(out.vertices*out.dimension);result.mesh.faces.resize(3*out.triangles);result.mesh.boundaries.resize(3*out.boundaries);result.areas.resize(out.triangles);if(inspect)result.projectors.resize(out.triangles*out.dimension*out.dimension);result.parallel=context.statistics;out.points=out.normals=out.normal_projectors=out.face_areas=nullptr;out.faces=out.boundary_tags=nullptr;return result;
}
FormResult ExtraModule::forms(uint32_t operation,const Form& a,const Form* b,uint64_t maxWork,double tolerance,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
 if(a.dimension<1||a.dimension>8||a.degree>32)throw Error(1,"Invalid differential-form dimensions.");
 const auto validate=[](const Form& value){if(value.dimension<1||value.dimension>8||value.degree>32||value.order>2||value.metric.size()!=value.dimension*value.dimension||value.coefficients.size()!=combinations(value.dimension,value.degree)||(!value.constant&&value.order>=1&&value.first.size()!=value.coefficients.size()*value.dimension)||(!value.constant&&value.order>=2&&value.second.size()!=value.coefficients.size()*value.dimension*value.dimension))throw Error(1,"Inconsistent form coefficient/derivative layout.");};validate(a);if(b)validate(*b);if(operation==PENTA_FORM_WEDGE&&!b)throw Error(1,"Wedge requires two forms.");
 const auto degree=operation==PENTA_FORM_CODIFFERENTIAL?(a.degree?a.degree-1:33):operation==PENTA_FORM_HODGE?(a.degree<=a.dimension?a.dimension-a.degree:33):operation==PENTA_FORM_D?a.degree+1:operation==PENTA_FORM_WEDGE&&b?a.degree+b->degree:a.degree;if(degree>32)throw Error(1,"Invalid output form degree.");const auto count=combinations(a.dimension,degree);FormResult result;result.form=a;result.form.degree=degree;result.form.coefficients.resize(count);result.form.first.resize(count*a.dimension);result.form.second.resize(count*a.dimension*a.dimension);penta_form_request_v1 request{};request.struct_size=sizeof(request);request.operation=operation;request.a=a.descriptor();if(b)request.b=b->descriptor();request.max_work=maxWork;request.tolerance=tolerance;penta_form_result_v1 out{};out.struct_size=sizeof(out);out.capacity=count;out.coefficients=result.form.coefficients.data();out.first=result.form.first.data();out.second=result.form.second.data();Host context{cancellation,charge,{},{},{}};auto host=context.descriptor();char error[2048]{};int status=0;const bool manifold=!a.metricFirst.empty()||!a.metricSecond.empty()||operation==PENTA_FORM_CODIFFERENTIAL||operation==PENTA_FORM_LAPLACIAN;
 if(manifold){if(a.dimension>6||(!a.metricFirst.empty()&&a.metricFirst.size()!=a.dimension*a.dimension*a.dimension)||(!a.metricSecond.empty()&&a.metricSecond.size()!=a.dimension*a.dimension*a.dimension*a.dimension))throw Error(1,"Manifold form metric jets have incompatible dimensions.");(void)api();const penta_manifold_forms_api_v1* capability=nullptr;{std::lock_guard lock(moduleMutex);const auto handle=modules.at(path_).handle;
#ifdef _WIN32
 const auto query=reinterpret_cast<penta_manifold_forms_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle),"penta_manifold_forms_query_v1")));
#else
 const auto query=reinterpret_cast<penta_manifold_forms_query_function_v1>(dlsym(handle,"penta_manifold_forms_query_v1"));
#endif
 capability=query?query(PENTA_MANIFOLD_FORMS_ABI_V1):nullptr;}if(!capability||capability->struct_size<sizeof(*capability)||capability->abi_version!=PENTA_MANIFOLD_FORMS_ABI_V1||!capability->compute)throw Error(5,"Loaded geometry module lacks variable-metric manifold forms.");penta_manifold_form_request_v1 extended{};extended.base=request;extended.base.struct_size=sizeof(extended);extended.metric_first=a.metricFirst.empty()?nullptr:a.metricFirst.data();extended.metric_second=a.metricSecond.empty()?nullptr:a.metricSecond.data();runtime::ThreadBudget::instance().withReservation([&]{status=capability->compute(&host,&extended,&out,error,sizeof(error));});}
 else {runtime::ThreadBudget::instance().withReservation([&]{status=api().forms(&host,&request,&out,error,sizeof(error));});}
 check(status,error,context);if(out.count!=count)throw Error(5,"Invalid native form coefficient count.");result.form.order=out.derivative_order;result.form.constant=out.constant_coefficients;if(result.form.order<1)result.form.first.clear();if(result.form.order<2)result.form.second.clear();result.work=out.work_units;return result;
}
double ExtraModule::laplace(const penta_chart_point_v1& chart,const std::array<double,2>& first,const std::array<double,4>& second)const{double value=0;char error[1024]{};const auto status=api().laplace(&chart,first.data(),second.data(),&value,error,sizeof(error));if(status)throw Error(status,error[0]?error:"Laplace-Beltrami failed.");return value;}
SurfaceResult ExtraModule::heat(penta_surface_heat_request_v1 request,const runtime::ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,const std::function<void(std::size_t)>& charge)const{
 if(request.mesh.vertices<1||request.mesh.vertices>100000||(request.width!=1&&request.width!=5)||!std::isfinite(request.sample_step)||request.sample_step<=0||!std::isfinite(request.t0)||!std::isfinite(request.t1)||request.t1<request.t0)throw Error(1,"Invalid surface heat dimensions/time interval.");
 const double capacity=std::ceil((request.t1-request.t0)/request.sample_step)+2;if(!std::isfinite(capacity)||capacity>100000||capacity*request.mesh.vertices*request.width>8000000)throw Error(7,"Surface heat output exceeds bounded coordinate storage.");const auto samples=static_cast<uint64_t>(capacity);SurfaceResult result;result.times.resize(samples);result.values.resize(samples*request.mesh.vertices*request.width);result.means.resize(samples*request.width);result.energy.resize(samples);auto& out=result.report;out.struct_size=sizeof(out);out.capacity=samples;out.times=result.times.data();out.values=result.values.data();out.means=result.means.data();out.energy=result.energy.data();request.struct_size=sizeof(request);Host context{cancellation,charge,settings,{},{}};auto host=context.descriptor();char error[2048]{};int status=0;runtime::ThreadBudget::instance().withReservation([&]{status=api().heat(&host,&request,&out,error,sizeof(error));});check(status,error,context);if(out.samples>samples)throw Error(5,"Invalid surface heat output sample count.");result.times.resize(out.samples);result.values.resize(out.samples*request.mesh.vertices*request.width);result.means.resize(out.samples*request.width);result.energy.resize(out.samples);out.times=out.values=out.means=out.energy=nullptr;result.parallel=context.statistics;return result;
}
#include "mesh_query_wrapper.inc"
#include "intrinsic_geometry_wrapper.inc"
#include "form_pullback_wrapper.inc"
#include "mesh_intersection_wrapper.inc"
}
