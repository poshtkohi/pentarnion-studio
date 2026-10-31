#pragma once
#include "mechanics_module.hpp"
#include "modules/mechanics_constraint_api.h"
namespace penta::mechanics {
struct ConstrainedResult {
 std::vector<double> times,q,v,p,energy,kinetic,potential,g,gv,lambda,positionMultipliers,velocityMultipliers,positionError,velocityError,positionIterations,correction;
 penta_holonomic_result_v1 report{};
};
class ConstrainedModule {
public:
 explicit ConstrainedModule(std::filesystem::path directory={});
 ConstrainedResult solve(penta_holonomic_request_v1,const std::shared_ptr<std::atomic_bool>& = {},const std::function<void(std::size_t)>& = {})const;
private:
 std::filesystem::path path_;const penta_mechanics_constraint_api_v1& api()const;
};
}
