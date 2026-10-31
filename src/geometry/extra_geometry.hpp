#pragma once
#include "geometry_module.hpp"
#include "modules/geometry_extra_api.h"
#include "modules/mesh_query_api.h"
#include "modules/intrinsic_geometry_api.h"
#include "modules/manifold_forms_api.h"
#include "modules/form_pullback_api.h"
#include "modules/mesh_intersection_api.h"
#include "runtime/thread_budget.hpp"
#include <array>
#include <functional>
#include <string>
namespace penta::geometry {
struct SurfaceMesh {
    unsigned dimension=3,coordinateKind=0;double gamma=0;
    std::vector<double> points;std::vector<uint64_t> faces,boundaries;
    penta_surface_mesh_v1 descriptor()const{return {dimension,coordinateKind,dimension?points.size()/dimension:0,faces.size()/3,boundaries.size()/3,points.data(),faces.data(),boundaries.data(),gamma};}
};
struct MeshResult {SurfaceMesh mesh;penta_mesh_result_v1 report{};std::vector<double> normals,projectors,areas;runtime::ParallelStatistics parallel;};
struct Form {
    unsigned dimension=1,degree=0,order=2;bool constant=true;int orientation=1;
    std::vector<double> coefficients,first,second,metric,point;std::string source="constant",cacheKey;
    std::vector<double> metricFirst,metricSecond;std::string metricCacheKey;
    penta_form_value_v1 descriptor()const{return {dimension,degree,order,constant?1u:0u,orientation,0,coefficients.data(),first.data(),second.data(),metric.data()};}
};
struct FormResult {Form form;uint64_t work=0;};
struct SurfaceResult {std::vector<double> times,values,means,energy;penta_surface_heat_result_v1 report{};runtime::ParallelStatistics parallel;};
struct QueryResult {std::vector<double> points,values,distances,parameters,barycentric;std::vector<uint64_t> faces,edges;std::vector<int32_t> classification;penta_mesh_query_result_v1 report{};runtime::ParallelStatistics parallel;};
struct IntrinsicResult {std::vector<double> inverse,connection,connectionFirst,riemann,ricci,torsion,metricCovariant,tensorCovariant,frameConnection,frameBracket,frameMetric;penta_intrinsic_result_v1 report{};};
struct TransitionResult {std::vector<double> inverse,metric,connection,vector,covector;penta_chart_transition_result_v1 report{};};
struct IntersectionResult {std::vector<double> points,firstBarycentric,secondBarycentric,firstValues,secondValues;std::vector<uint64_t> offsets,counts,firstFaces,secondFaces;std::vector<int32_t> dimensions,coplanar;penta_mesh_intersection_result_v1 report{};runtime::ParallelStatistics parallel;};
class ExtraModule {
public:
    explicit ExtraModule(std::filesystem::path directory={});
    MeshResult mesh(penta_mesh_request_v1,const runtime::ParallelSettings& = {},
        const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    FormResult forms(uint32_t,const Form&,const Form* other=nullptr,uint64_t maxWork=1000000000,double tolerance=1e-12,
        const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    double laplace(const penta_chart_point_v1&,const std::array<double,2>&,const std::array<double,4>&)const;
    SurfaceResult heat(penta_surface_heat_request_v1,const runtime::ParallelSettings& = {},
        const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    QueryResult query(penta_mesh_query_request_v1,const runtime::ParallelSettings& = {},
        const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    IntrinsicResult intrinsic(penta_intrinsic_request_v1,const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    TransitionResult transition(penta_chart_transition_request_v1)const;
    FormResult pullback(penta_form_pullback_request_v1,const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
    IntersectionResult intersection(penta_mesh_intersection_request_v1,const runtime::ParallelSettings& = {},const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
private:
    std::filesystem::path path_;const penta_geometry_extra_api_v1& api()const;
};
}
