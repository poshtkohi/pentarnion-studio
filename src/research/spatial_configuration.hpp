#pragma once
#include <array>
#include <string>
namespace penta::research {
struct PdeConfiguration {
    int dimension=1,nx=21,ny=21,component=0;
    std::string valueType="real",method="fd",integrator="imex_euler",boundary="dirichlet";
    std::string initial="amplitude*sin(wave*x/domain_x)",reaction="0.0*u",boundaryValue="0.0";
    double lengthX=1,lengthY=1,diffusion=.1,timeStep=.002,robinAlpha=1;
};
struct GeometryConfiguration {
    std::string output="pentarnion";
    std::array<std::string,5> coordinates{{"u","v","amplitude*sin(u)*cos(v)","amplitude*cos(u)*sin(v)","0.25*u*v"}};
    double u0=-2,u1=2,v0=-2,v1=2;
    int nu=21,nv=21;
    std::array<int,3> projection{{1,2,3}};
};
struct MechanicsConfiguration {
    std::string form="newton",method="verlet",expression="-frequency*q";
    std::string initialQ="1.0",initialSecond="0.0",mass="1.0";
    double timeStep=.01;
    int dimensions=1,constraints=1;
    std::string constraintMode="position",constraintExpression="q*q-1.0";
};
struct SpatialConfiguration {PdeConfiguration pde;GeometryConfiguration geometry;MechanicsConfiguration mechanics;};
}
