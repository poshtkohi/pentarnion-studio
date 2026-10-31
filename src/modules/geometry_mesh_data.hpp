#pragma once
#include "geometry_extra_common.hpp"
#include <Eigen/Dense>
#include <array>
#include <map>
#include <vector>
namespace penta::geometry_extra {
struct Mesh {
 unsigned dimension=3,coordinateKind=0;double gamma=0;std::vector<double> points;std::vector<uint64_t> faces,boundaries;
 uint64_t vertices()const{return dimension?points.size()/dimension:0;}
 uint64_t triangles()const{return faces.size()/3;}
 penta_surface_mesh_v1 descriptor()const{return {dimension,coordinateKind,vertices(),triangles(),boundaries.size()/3,points.data(),faces.data(),boundaries.data(),gamma};}
};
struct Face {
 std::array<uint64_t,3> nodes{};Eigen::Matrix<double,5,2> normalized;Eigen::Matrix2d inverse;
 long double scale=1;double area=0,reducedArea=0,minAngle=0,aspect=0;
};
Mesh loadMesh(const penta_surface_mesh_v1&,Work&);
Face faceGeometry(const Mesh&,uint64_t,double,Work&);
struct Inspection {
 double area=0,length=0,volume=0,minAngle=0,maxAspect=0;bool closed=false,consistent=true,volumeAvailable=false;
 std::vector<double> normals,projectors,areas;std::vector<uint64_t> boundaries;
};
Inspection inspectMesh(const Mesh&,double,Work&,bool normals=false);
}
