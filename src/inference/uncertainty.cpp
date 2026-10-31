#include "uncertainty.hpp"
#include "language/frontend.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <Eigen/Dense>
#include <boost/math/distributions/normal.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
namespace penta::inference {
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
double quantile(std::vector<double> values,double probability){std::sort(values.begin(),values.end());const double x=probability*(values.size()-1);const auto i=static_cast<std::size_t>(x);return values[i]+(x-i)*(values[std::min(i+1,values.size()-1)]-values[i]);}
}
double normalCritical(double confidence){require(std::isfinite(confidence)&&confidence>0&&confidence<1,"Confidence must be in(0,1).");return boost::math::quantile(boost::math::normal_distribution<double>(),(1+confidence)/2);}
Bands predictionBands(std::span<const double> samples,std::size_t rows,std::size_t columns,double confidence){
    require(rows>=2&&rows<=1000000&&columns>=1&&columns<=4096&&rows<=8000000/columns&&samples.size()==rows*columns,"Prediction ensembles require2..1e6 rows and a bounded complete sample matrix.");(void)normalCritical(confidence);for(auto value:samples)require(std::isfinite(value),"Prediction ensembles cannot silently omit nonfinite samples.");
    Bands answer;for(auto* values:{&answer.mean,&answer.standardDeviation,&answer.lower,&answer.upper,&answer.simultaneousLower,&answer.simultaneousUpper})values->resize(columns);std::vector<double> values(rows),maxima(rows,0);const double tail=(1-confidence)/2;
    for(std::size_t col=0;col<columns;++col){long double mean=0;for(std::size_t row=0;row<rows;++row)mean+=static_cast<long double>(samples[row*columns+col])/rows;answer.mean[col]=static_cast<double>(mean);long double variance=0;for(std::size_t row=0;row<rows;++row){values[row]=samples[row*columns+col];const long double delta=static_cast<long double>(values[row])-mean;variance+=delta*delta/(rows-1);}const double sd=std::sqrt(static_cast<double>(variance));require(std::isfinite(sd),"Prediction variance overflowed.");answer.standardDeviation[col]=sd;answer.lower[col]=quantile(values,tail);answer.upper[col]=quantile(values,1-tail);for(std::size_t row=0;row<rows;++row)if(sd>0)maxima[row]=std::max(maxima[row],std::abs((values[row]-answer.mean[col])/sd));}
    answer.simultaneousCritical=quantile(maxima,confidence);for(std::size_t col=0;col<columns;++col){const double width=answer.simultaneousCritical*answer.standardDeviation[col];answer.simultaneousLower[col]=answer.mean[col]-width;answer.simultaneousUpper[col]=answer.mean[col]+width;}return answer;
}
std::vector<double> observationSamples(std::span<const double> predictions,std::size_t rows,std::size_t columns,std::size_t draws,noise::Model model,double variance,double degrees,std::uint64_t seed){
    require(rows>=2&&columns>=1&&draws>=2&&draws<=1000000&&draws<=8000000/columns&&predictions.size()==rows*columns,"Future-observation draws exceed their bounded ensemble layout.");noise::validate(model,variance,degrees);std::vector<double> output(draws*columns);
    for(std::size_t row=0;row<draws;++row){std::mt19937_64 random(noise::mix(seed^noise::mix(row)));const auto source=std::min(rows-1,static_cast<std::size_t>(noise::uniform(random)*rows));for(std::size_t col=0;col<columns;++col)output[row*columns+col]=noise::sample(model,predictions[source*columns+col],variance,degrees,random);}
    return output;
}
WaldInterval waldInterval(std::span<const double> point,std::span<const double> standardizedJacobian,std::size_t observations,double confidence){
    const auto n=point.size();require(n>=1&&n<=16&&observations>=n&&observations<=4096&&standardizedJacobian.size()==observations*n,"Invalid native coverage Jacobian dimensions.");const double critical=normalCritical(confidence);WaldInterval answer;answer.lower.assign(n,0);answer.upper.assign(n,0);Eigen::MatrixXd matrix(observations,n);for(std::size_t row=0;row<observations;++row)for(std::size_t col=0;col<n;++col){const double value=standardizedJacobian[row*n+col];require(std::isfinite(value),"Coverage Jacobian is nonfinite.");matrix(row,col)=value;}
    Eigen::JacobiSVD<Eigen::MatrixXd> decomposition(matrix,Eigen::ComputeThinV);const auto singular=decomposition.singularValues();const double threshold=std::numeric_limits<double>::epsilon()*std::max(n,observations)*singular[0];for(Eigen::Index i=0;i<singular.size();++i)answer.rank+=singular[i]>threshold;if(answer.rank!=n)return answer;const auto covariance=(decomposition.matrixV()*singular.array().square().inverse().matrix().asDiagonal()*decomposition.matrixV().transpose()).eval();if(!covariance.allFinite())return answer;
    for(std::size_t col=0;col<n;++col){const double sd=std::sqrt(std::max(0.0,covariance(col,col)));answer.lower[col]=point[col]-critical*sd;answer.upper[col]=point[col]+critical*sd;if(!std::isfinite(answer.lower[col])||!std::isfinite(answer.upper[col])||!(answer.lower[col]<point[col])||!(answer.upper[col]>point[col]))return answer;}answer.valid=true;return answer;
}
std::vector<ProfileRegion> profileRegions(std::span<const double> grid,std::span<const double> deviance,std::span<const double> valid,double threshold){
    require(grid.size()>=2&&grid.size()<=4096&&grid.size()==deviance.size()&&grid.size()==valid.size()&&std::isfinite(threshold)&&threshold>0,"Invalid sampled profile interval layout/threshold.");std::vector<std::size_t> order(grid.size());std::iota(order.begin(),order.end(),0);std::sort(order.begin(),order.end(),[&](auto a,auto b){return grid[a]<grid[b];});for(std::size_t i=0;i<order.size();++i){require(std::isfinite(grid[order[i]])&&std::isfinite(deviance[order[i]]),"Nonfinite sampled profile values.");if(i)require(grid[order[i]]>grid[order[i-1]],"Profile intervals require distinct grid values.");}
    for(auto flag:valid)require(flag==0||flag==1,"Profile validity flags must be0 or1.");
    std::vector<ProfileRegion> answer;auto crossing=[&](std::size_t a,std::size_t b){return grid[a]+(threshold-deviance[a])/(deviance[b]-deviance[a])*(grid[b]-grid[a]);};
    for(std::size_t position=0;position<order.size();){const auto index=order[position];if(valid[index]<=0||deviance[index]>threshold){++position;continue;}const auto first=position;while(position+1<order.size()&&valid[order[position+1]]>0&&deviance[order[position+1]]<=threshold)++position;const auto last=position;ProfileRegion region;region.lower=grid[order[first]];region.upper=grid[order[last]];if(first==0)region.lowerOpen=true;else if(valid[order[first-1]]<=0){region.lowerOpen=true;region.invalidBoundary=true;}else region.lower=crossing(order[first-1],order[first]);if(last+1==order.size())region.upperOpen=true;else if(valid[order[last+1]]<=0){region.upperOpen=true;region.invalidBoundary=true;}else region.upper=crossing(order[last],order[last+1]);answer.push_back(region);++position;}
    return answer;
}
#include "structural_identifiability.inc"
}
