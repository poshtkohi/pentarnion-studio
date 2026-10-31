#include "science_extended_common.hpp"
#include <boost/math/distributions/normal.hpp>
#include <boost/math/distributions/uniform.hpp>
#include <boost/math/distributions/exponential.hpp>
#include <boost/math/distributions/gamma.hpp>
#include <boost/math/distributions/lognormal.hpp>
#include <boost/math/distributions/weibull.hpp>
#include <boost/math/distributions/beta.hpp>
#include <boost/math/distributions/students_t.hpp>
#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/fisher_f.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/math/distributions/binomial.hpp>
#include <boost/math/distributions/bernoulli.hpp>
#include <boost/math/distributions/negative_binomial.hpp>
#include <boost/math/distributions/laplace.hpp>
#include <boost/math/distributions/cauchy.hpp>
#include <boost/math/distributions/logistic.hpp>
#include <random>
namespace penta::science::extended {
namespace {
using Policy=boost::math::policies::policy<boost::math::policies::max_root_iterations<128>,boost::math::policies::max_series_iterations<2048>>;
template<class Distribution>double value(const Distribution& d,const std::string& mode,double x,bool discrete,double maximum,Work& work){
    if(mode=="pdf"||mode=="cdf"){const auto bounds=boost::math::support(d);if(x<bounds.first)return 0;if(x>bounds.second)return mode=="pdf"?0:1;}
    if(mode=="pdf"){if(discrete&&(x<0||x!=std::trunc(x)||x>maximum))return 0;return boost::math::pdf(d,x);}
    if(mode=="cdf"){if(discrete){if(x<0)return 0;if(x>=maximum)return 1;x=std::floor(x);}return boost::math::cdf(d,x);}
    require(x>0&&x<1,"Finite distribution inverse probabilities must lie strictly inside(0,1).");if(!discrete)return boost::math::quantile(d,x);
    // Smallest integer k with F(k)>=p, independent of Boost's configurable
    // real-valued/discrete quantile rounding policy.
    std::uint64_t low=0,high=1;if(std::isfinite(maximum))high=static_cast<std::uint64_t>(maximum);else{while(boost::math::cdf(d,double(high))<x){work.spend(32);require(high<1000000000,"Discrete inverse exceeded its one-billion-count bracket.");high=std::min<std::uint64_t>(1000000000,high*2);}}while(low<high){work.spend(32);const auto middle=low+(high-low)/2;if(boost::math::cdf(d,double(middle))>=x)high=middle;else low=middle+1;}return double(low);
}
}
void distribution(const penta_science_request_v2& q,const Json& o,Work& work,Json& meta,std::vector<double>& out){
    const auto family=text(o,"distribution","normal"),mode=text(o,"mode","pdf");require(mode=="pdf"||mode=="cdf"||mode=="inverse"||mode=="random","Distribution mode must be pdf,cdf,inverse or random.");const auto count=mode=="random"?integer(o,"count",1,1,1000000):static_cast<std::size_t>(q.a_count);require(count>=1&&count<=q.output_capacity,"Distribution output exceeds its checked buffer.");const auto seed=integer(o,"seed",0,0,INT64_MAX);std::mt19937_64 engine(seed);out.resize(count);auto fill=[&](const auto& d,bool discrete=false,double maximum=INFINITY){for(std::size_t j=0;j<count;++j){work.spend(64);double x=mode=="random"?std::clamp((double(engine()>>11)+.5)*0x1.0p-53,std::nextafter(0.,1.),std::nextafter(1.,0.)):q.a[j];out[j]=finite(value(d,mode=="random"?"inverse":mode,x,discrete,maximum,work));}};
    const auto location=number(o,"location",0),scale=number(o,"scale",1),shape=number(o,"shape",1),probability=number(o,"probability",.5);using namespace boost::math;
    if(family=="normal")fill(normal_distribution<double,Policy>(location,scale));
    else if(family=="uniform")fill(uniform_distribution<double,Policy>(number(o,"lower",0),number(o,"upper",1)));
    else if(family=="exponential")fill(exponential_distribution<double,Policy>(number(o,"rate",1)));
    else if(family=="gamma")fill(gamma_distribution<double,Policy>(shape,scale));
    else if(family=="lognormal")fill(lognormal_distribution<double,Policy>(location,scale));
    else if(family=="weibull")fill(weibull_distribution<double,Policy>(shape,scale));
    else if(family=="beta")fill(beta_distribution<double,Policy>(number(o,"alpha",1),number(o,"beta",1)));
    else if(family=="student_t")fill(students_t_distribution<double,Policy>(number(o,"df",1)));
    else if(family=="chi_squared")fill(chi_squared_distribution<double,Policy>(number(o,"df",1)));
    else if(family=="fisher_f")fill(fisher_f_distribution<double,Policy>(number(o,"df1",1),number(o,"df2",1)));
    else if(family=="poisson")fill(poisson_distribution<double,Policy>(number(o,"lambda",1)),true);
    else if(family=="binomial"){const auto trials=integer(o,"trials",1,0,1000000000);fill(binomial_distribution<double,Policy>(double(trials),probability),true,double(trials));}
    else if(family=="bernoulli")fill(bernoulli_distribution<double,Policy>(probability),true,1);
    else if(family=="negative_binomial")fill(negative_binomial_distribution<double,Policy>(number(o,"successes",1),probability),true);
    else if(family=="laplace")fill(laplace_distribution<double,Policy>(location,scale));
    else if(family=="cauchy")fill(cauchy_distribution<double,Policy>(location,scale));
    else if(family=="logistic")fill(logistic_distribution<double,Policy>(location,scale));
    else throw Error(PENTA_NUMERIC_ARGUMENT,"Unknown distribution family.");
    meta={{"distribution",family},{"mode",mode},{"count",count},{"seed",seed},{"inverse_semantics","continuous quantile; discrete smallest k with CDF(k)>=p"},{"backend","Boost.Math"}};
}
}
