#include "modules/calculus_api.h"
#include <array>
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
namespace {
int checks=0;
void check(bool condition){++checks;if(!condition)throw std::runtime_error("calculus kernel check "+std::to_string(checks));}
bool close(double a,double b){return std::abs(a-b)<1e-10*(1+std::abs(b));}
int run(penta_numeric_calculus_request_v1 request,const penta_numeric_host_v1* host=nullptr){request.struct_size=sizeof(request);char error[512]{};return penta_numeric_calculus_v1(host,&request,error,sizeof(error));}
}
int main(){try{
    const std::vector<double> x{-1,0,2,3},y{1,0,4,9};std::vector<double> output(4);
    penta_numeric_calculus_request_v1 r{};r.operation=PENTA_CALC_DERIVATIVE1;r.rows=4;r.columns=1;r.components=1;r.axis=0;r.value_stride=1;r.data=y.data();r.data_capacity=y.size();r.coordinates=x.data();r.coordinate_count=x.size();r.output=output.data();r.output_capacity=output.size();
    check(run(r)==0);for(std::size_t n=0;n<x.size();++n)check(close(output[n],2*x[n]));
    r.operation=PENTA_CALC_DERIVATIVE2;check(run(r)==0);for(auto v:output)check(close(v,2));
    // Two samples determine a secant, not curvature. Reject before touching
    // output (all axes/real and five-coordinate inputs), keeping first derivatives.
    {
        const std::array<double,2> pairX{0,2},pairY{1,5};std::array<double,2> sentinel{123,456};
        penta_numeric_calculus_request_v1 shortRequest{};shortRequest.struct_size=sizeof(shortRequest);
        shortRequest.operation=PENTA_CALC_DERIVATIVE2;shortRequest.rows=2;shortRequest.columns=1;shortRequest.components=1;shortRequest.axis=0;shortRequest.value_stride=1;
        shortRequest.data=pairY.data();shortRequest.data_capacity=2;shortRequest.coordinates=pairX.data();shortRequest.coordinate_count=2;shortRequest.output=sentinel.data();shortRequest.output_capacity=2;
        char diagnostic[512]{};check(penta_numeric_calculus_v1(nullptr,&shortRequest,diagnostic,sizeof(diagnostic))==PENTA_NUMERIC_ARGUMENT);
        check(std::string(diagnostic).find("Insufficient data")!=std::string::npos);check(sentinel[0]==123&&sentinel[1]==456);
        shortRequest.operation=PENTA_CALC_DERIVATIVE1;check(run(shortRequest)==0);check(close(sentinel[0],2)&&close(sentinel[1],2));
        shortRequest.operation=PENTA_CALC_DERIVATIVE2;shortRequest.rows=1;shortRequest.columns=2;shortRequest.axis=1;check(run(shortRequest)==PENTA_NUMERIC_ARGUMENT);
        shortRequest.rows=1;shortRequest.columns=1;shortRequest.axis=0;shortRequest.coordinate_count=1;shortRequest.data_capacity=1;check(run(shortRequest)==PENTA_NUMERIC_ARGUMENT);
        std::array<double,10> pentInput{},pentOutput{};pentOutput.fill(99);shortRequest.rows=2;shortRequest.components=5;shortRequest.value_stride=5;shortRequest.data=pentInput.data();shortRequest.data_capacity=10;shortRequest.output=pentOutput.data();shortRequest.output_capacity=10;shortRequest.coordinate_count=2;
        check(run(shortRequest)==PENTA_NUMERIC_ARGUMENT);for(double entry:pentOutput)check(entry==99);
        shortRequest.rows=0;shortRequest.coordinate_count=0;check(run(shortRequest)==0); // Empty arrays remain empty.
    }
    // glibc also exports ::finite(double). The C entry point must call our
    // throwing validator, including when a one-sample derivative is zero.
    for(const auto coordinate:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        double sample=7,sentinel=99;penta_numeric_calculus_request_v1 single{};single.operation=PENTA_CALC_DERIVATIVE1;single.rows=single.columns=single.components=single.value_stride=single.data_capacity=single.coordinate_count=single.output_capacity=1;single.data=&sample;single.coordinates=&coordinate;single.output=&sentinel;check(run(single)==PENTA_NUMERIC_DOMAIN);check(sentinel==99);
    }
    const std::vector<double> reversedX{3,2,0,-1},reversedY{9,4,0,1};r.coordinates=reversedX.data();r.data=reversedY.data();r.operation=PENTA_CALC_DERIVATIVE1;check(run(r)==0);for(std::size_t n=0;n<x.size();++n)check(close(output[n],2*reversedX[n]));
    const std::vector<double> nodes{0,.25,.5,.75,1},line{1,1.5,2,2.5,3};r.rows=5;r.data=line.data();r.data_capacity=line.size();r.coordinates=nodes.data();r.coordinate_count=nodes.size();r.operation=PENTA_CALC_TRAPZ;check(run(r)==0);check(close(output[0],2));
    output.resize(5);r.output=output.data();r.output_capacity=output.size();r.operation=PENTA_CALC_CUMTRAPZ;check(run(r)==0);check(close(output.front(),0));check(close(output.back(),2));
    const std::vector<double> cube{0,1},cubeDerivative{0,3},endpoints{0,1},query{.25,.5,.75};output.resize(3);r.operation=PENTA_CALC_INTERP_HERMITE;r.rows=2;r.data=cube.data();r.data_capacity=cube.size();r.derivative_data=cubeDerivative.data();r.derivative_capacity=cubeDerivative.size();r.coordinates=endpoints.data();r.coordinate_count=2;r.query=query.data();r.query_count=query.size();r.output=output.data();r.output_capacity=output.size();check(run(r)==0);for(std::size_t n=0;n<query.size();++n)check(close(output[n],query[n]*query[n]*query[n]));
    const std::vector<double> outside{2};r.query=outside.data();r.query_count=1;check(run(r)==PENTA_NUMERIC_DOMAIN);r.operation=PENTA_CALC_INTERP_LINEAR;r.extrapolate=1;check(run(r)==0);check(close(output[0],2));
    const std::vector<double> badGrid{0,0};r.coordinates=badGrid.data();check(run(r)==PENTA_NUMERIC_ARGUMENT);
    r.coordinates=endpoints.data();r.data_capacity=1;check(run(r)==PENTA_NUMERIC_ARGUMENT);r.data_capacity=2;
    std::atomic_bool cancelled{true};penta_numeric_host_v1 host{};host.struct_size=sizeof(host);host.abi_version=PENTA_NUMERIC_ABI_V1;host.context=&cancelled;host.cancelled=[](void* value){return static_cast<std::atomic_bool*>(value)->load()?1:0;};check(run(r,&host)==PENTA_NUMERIC_CANCELLED);
    std::array<double,10> pent{};pent[0]=1;pent[1]=2;pent[5]=3;pent[6]=4;std::array<double,5> pResult{};r={};r.operation=PENTA_CALC_TRAPZ;r.rows=2;r.columns=1;r.components=5;r.axis=0;r.value_stride=5;r.data=pent.data();r.data_capacity=pent.size();r.coordinates=endpoints.data();r.coordinate_count=2;r.output=pResult.data();r.output_capacity=pResult.size();check(run(r)==0);check(close(pResult[0],2)&&close(pResult[1],3));
    // Matrix row-major storage, integrate each row across columns.
    const std::vector<double> matrix{1,2,3,2,4,6},columnCoordinates{0,1,2};output.resize(2);r={};r.operation=PENTA_CALC_TRAPZ;r.rows=2;r.columns=3;r.components=1;r.axis=1;r.value_stride=1;r.data=matrix.data();r.data_capacity=matrix.size();r.coordinates=columnCoordinates.data();r.coordinate_count=3;r.output=output.data();r.output_capacity=2;check(run(r)==0);check(close(output[0],4)&&close(output[1],8));
    r.operation=PENTA_CALC_DIFF;r.difference_order=2;check(run(r)==0);check(close(output[0],0)&&close(output[1],0));
    double firstSample=0,integral=0;r={};r.operation=PENTA_CALC_QUADRATURE;r.components=1;r.data=&firstSample;r.data_capacity=1;r.coordinates=endpoints.data();r.coordinate_count=2;r.output=&integral;r.output_capacity=1;r.absolute_tolerance=1e-12;r.relative_tolerance=1e-12;r.maximum_intervals=1000;
    r.integrand=[](void*,double x,double* value,uint32_t,char*,uint64_t)->int{value[0]=x*x*x;return PENTA_NUMERIC_OK;};check(run(r)==0);check(close(integral,.25));
    r.integrand=[](void*,double,double*,uint32_t,char*,uint64_t)->int{return PENTA_NUMERIC_CANCELLED;};check(run(r)==PENTA_NUMERIC_CANCELLED);
    firstSample=1;r.integrand=[](void*,double x,double* value,uint32_t,char*,uint64_t)->int{value[0]=std::exp(x);return PENTA_NUMERIC_OK;};r.absolute_tolerance=1e-16;r.relative_tolerance=0;r.maximum_intervals=1;check(run(r)==PENTA_NUMERIC_DOMAIN);
    std::array<double,5> initialPent{1,0,0,0,0},integralPent{};r.components=5;r.data=initialPent.data();r.data_capacity=5;r.output=integralPent.data();r.output_capacity=5;r.maximum_intervals=1000;r.absolute_tolerance=1e-12;
    r.integrand=[](void*,double x,double* value,uint32_t,char*,uint64_t)->int{value[0]=1;value[1]=x;value[2]=x*x;value[3]=value[4]=0;return PENTA_NUMERIC_OK;};check(run(r)==0);check(close(integralPent[0],1)&&close(integralPent[1],.5)&&close(integralPent[2],1./3));
    std::cout<<checks<<" calculus kernel checks, 0 failures\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
