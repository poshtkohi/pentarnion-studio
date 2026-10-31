#pragma once
#include "modules/geometry_api.h"
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>
namespace penta::geometry {
struct Error:std::runtime_error{int status;Error(int code,const std::string& message):std::runtime_error(message),status(code){}};
struct Path {std::vector<double> times,coordinates,velocities,positions,transport,speedSquared,transportNormSquared;penta_geodesic_result_v1 report{};};
class Module {
public:
    explicit Module(std::filesystem::path directory={});
    penta_chart_point_v1 point(const penta_chart_sample_v1&,double tolerance)const;
    Path geodesic(const penta_geodesic_request_v1&,const std::shared_ptr<std::atomic_bool>&,const std::function<void(std::size_t)>&)const;
private:
    std::filesystem::path path_;const penta_geometry_api_v1& api()const;
};
}
