#pragma once
#include "pde_time_api.h"
#include <Eigen/Sparse>
#include <functional>
#include <vector>
namespace penta::pde::kernel {
struct SpatialSystem {
    uint64_t nodes=0,components=1;
    bool identityMass=false;
    Eigen::SparseMatrix<double> mass,stiffness;
    std::vector<double> initial,weights,fixedValues;
    std::vector<unsigned char> fixed;
};
struct TimeSettings {
    uint32_t integrator=PENTA_PDE_RK4_V1;
    double t0=0,t1=1,step=.001,sample=.05,maximumWall=120;
    uint64_t maximumSteps=1000000,maximumReactions=20000000;
};
using NodeReaction=std::function<int(double,uint64_t,const double*,double*,char*,uint64_t)>;
void integrate(const penta_pde_host_v1*,const SpatialSystem&,const TimeSettings&,const NodeReaction&,penta_pde_time_result_v1*);
}
