#pragma once
#include "noise_models.hpp"
#include <span>
#include <string>
#include <vector>
namespace penta::language {struct FunctionDefinition;}
namespace penta::inference {
struct Bands {
    std::vector<double> mean,standardDeviation,lower,upper,simultaneousLower,simultaneousUpper;
    double simultaneousCritical=0;
};
Bands predictionBands(std::span<const double> samples,std::size_t rows,std::size_t columns,double confidence);
std::vector<double> observationSamples(std::span<const double> predictions,std::size_t rows,std::size_t columns,
    std::size_t draws,noise::Model,double variance,double degrees,std::uint64_t seed);
double normalCritical(double confidence);
struct WaldInterval {std::vector<double> lower,upper;std::size_t rank=0;bool valid=false;};
WaldInterval waldInterval(std::span<const double> point,std::span<const double> standardizedJacobian,std::size_t observations,double confidence);
struct ProfileRegion {double lower=0,upper=0;bool lowerOpen=false,upperOpen=false,invalidBoundary=false;};
std::vector<ProfileRegion> profileRegions(std::span<const double> grid,std::span<const double> deviance,std::span<const double> valid,double threshold);
struct StructuralIdentifiability {
    bool supported=false,identifiable=false;std::size_t dimension=0,rank=0,degree=0,operations=0;
    std::string status,reason,parameterNames,independent,coefficientExact,nullspaceExact;
    std::vector<double> coefficients,nullspace;
};
StructuralIdentifiability structuralIdentifiability(const language::FunctionDefinition&);
}
