#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int main(){using namespace penta;unsigned checks=0,failures=0;runtime::ExecutionOptions options;options.dataDirectory=std::filesystem::temp_directory_path()/("penta-mesh-intersection-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));options.maxOperations=100000000;std::filesystem::create_directories(options.dataDirectory);auto run=[&](const std::string& source,bool expected=true){++checks;const auto result=runtime::execute(source,{},options);if(result.ok()!=expected){++failures;for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<" @"<<d.span.offset<<": "<<d.message<<'\n';}};
run("record a=trimesh([0,0,0;2,0,0;0,2,0],[1,2,3]);record b=trimesh([.5,-1,-1;.5,2,-1;.5,.5,1],[1,2,3]);record serial=meshintersect(a,b,options(threads=1,first_values=[0;2;2]));record parallel=meshintersect(a,b,options(threads=4,first_values=[0;2;2]));assert(serial.primitive_count==1&&serial.counts(1)==2);assert(norm(serial.points-parallel.points,\"fro\")==0);assert(norm(serial.first_values-serial.points(:,1)-serial.points(:,2),\"fro\")<1e-9);assert(norm(sum(serial.first_barycentric,2)-1,\"fro\")<1e-12);matrix repeated_faces=zeros(64,3);matrix repeated_points=zeros(192,3);for(row=1:64){integer base=3*(row-1);repeated_points(base+1:base+3,:)=a.vertices;repeated_faces(row,:)=[base+1,base+2,base+3];}record many=trimesh(repeated_points,repeated_faces);record batch=meshintersect(many,b,options(threads=4));assert(batch.primitive_count==64&&batch.parallel_calls>0);");
run("record a=trimesh([0,0,0;1,0,0;0,1,0],[1,2,3]);record b=trimesh([3,3,0;4,3,0;3,4,0],[1,2,3]);record result=meshintersect(a,b);assert(result.primitive_count==0&&result.point_count==0);assert(size(result.points,2)==3);");
run("record a=trimesh([0,0;1,0;0,1],[1,2,3]);record result=meshintersect(a,a);",false);
#ifdef PENTA_SOURCE_DIR
std::ifstream input(std::filesystem::path(PENTA_SOURCE_DIR)/"examples/geometry-queries/triangle-pair-intersections.p");run(std::string((std::istreambuf_iterator<char>(input)),{}));
#endif
std::error_code ignored;std::filesystem::remove_all(options.dataDirectory,ignored);std::cout<<checks<<" mesh intersection language checks, "<<failures<<" failures\n";return failures?1:0;}
