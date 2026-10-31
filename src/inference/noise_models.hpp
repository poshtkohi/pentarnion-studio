#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>
namespace penta::inference::noise {
enum class Model : std::uint32_t {Gaussian=0,Lognormal=1,StudentT=2,Poisson=3};
struct Residual {double value=0,derivative=1;};
inline void validate(Model model,double variance,double degrees){
    if(static_cast<std::uint32_t>(model)>3||!std::isfinite(variance)||!std::isfinite(degrees)||degrees<=0||(model!=Model::Poisson&&variance<=0))throw std::runtime_error("Noise models require positive finite scale variance and Student-t degrees of freedom.");
}
inline Residual residual(Model model,double prediction,double observation,double variance,double degrees){
    if(!std::isfinite(prediction)||!std::isfinite(observation))throw std::runtime_error("Nonfinite likelihood input.");
    if(model==Model::Gaussian)return {prediction-observation,1};
    validate(model,variance,degrees);
    if(model==Model::Lognormal){if(prediction<=0||observation<=0)throw std::runtime_error("Lognormal likelihood requires positive model medians and observations.");const double scale=std::sqrt(variance);return {(std::log(prediction)-std::log(observation))/scale,1/(prediction*scale)};}
    if(model==Model::StudentT){const long double e=static_cast<long double>(prediction)-observation,z=e/std::sqrt(static_cast<long double>(variance)),nu=degrees,term=std::log1p(z*z/nu);if(e==0)return {0,static_cast<double>(std::sqrt((nu+1)/(nu*variance)))};const long double value=std::copysign(std::sqrt((nu+1)*term),e);return {static_cast<double>(value),static_cast<double>((nu+1)*e/((nu*variance+e*e)*value))};}
    if(prediction<=0||observation<0||observation!=std::trunc(observation))throw std::runtime_error("Poisson likelihood requires positive model means and nonnegative integer counts.");
    if(observation==0){const double value=static_cast<double>(std::sqrt(2*static_cast<long double>(prediction)));return {value,1/value};}
    const double relative=(prediction-observation)/observation;
    if(std::abs(relative)<1e-7){const double inv=1/std::sqrt(observation);return {(prediction-observation)*inv*(1-relative/3+7*relative*relative/36),inv*(1-2*relative/3+7*relative*relative/12)};}
    const long double mu=prediction,y=observation,divergence=mu-y+y*(std::log(y)-std::log(mu));const double value=static_cast<double>(std::copysign(std::sqrt(2*std::max(0.L,divergence)),mu-y));return {value,static_cast<double>((1-y/mu)/value)};
}
inline std::uint64_t mix(std::uint64_t value){value+=UINT64_C(0x9e3779b97f4a7c15);value=(value^(value>>30))*UINT64_C(0xbf58476d1ce4e5b9);value=(value^(value>>27))*UINT64_C(0x94d049bb133111eb);return value^(value>>31);}
inline double uniform(std::mt19937_64& random){return (static_cast<double>(random()>>11)+.5)*0x1.0p-53;}
inline double normal(std::mt19937_64& random){return std::sqrt(-2*std::log(uniform(random)))*std::cos(2*std::acos(-1.0)*uniform(random));}
inline double gamma(std::mt19937_64& random,double shape){
    if(shape<1)return gamma(random,shape+1)*std::pow(uniform(random),1/shape);
    const double d=shape-1.0/3,c=1/std::sqrt(9*d);
    for(unsigned iteration=0;iteration<10000;++iteration){const double x=normal(random),base=1+c*x;if(base<=0)continue;const double v=base*base*base,u=uniform(random);if(u<1-.0331*x*x*x*x||std::log(u)<.5*x*x+d*(1-v+std::log(v)))return d*v;}
    throw std::runtime_error("Gamma noise sampler exceeded its bounded rejection budget.");
}
inline double sample(Model model,double prediction,double variance,double degrees,std::mt19937_64& random){
    validate(model,variance,degrees);if(!std::isfinite(prediction))throw std::runtime_error("Nonfinite prediction for observation noise.");double value=0;
    if(model==Model::Gaussian)value=prediction+std::sqrt(variance)*normal(random);
    else if(model==Model::Lognormal){if(prediction<=0)throw std::runtime_error("Lognormal predictions must be positive medians.");value=std::exp(std::log(prediction)+std::sqrt(variance)*normal(random));}
    else if(model==Model::StudentT)value=prediction+std::sqrt(variance)*normal(random)/std::sqrt(2*gamma(random,degrees/2)/degrees);
    else{if(prediction<=0||prediction>1e8)throw std::runtime_error("Poisson simulation requires means in(0,1e8].");std::poisson_distribution<std::uint64_t> distribution(prediction);value=static_cast<double>(distribution(random));}
    if(!std::isfinite(value))throw std::runtime_error("Observation noise overflowed.");
    return value;
}
}
