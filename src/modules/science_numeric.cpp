#define PENTA_NUMERIC_MODULE_BUILD
#include "science_api.h"
#include <unsupported/Eigen/FFT>
#include <boost/math/distributions/normal.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>
namespace {
void message(char* p,uint64_t n,const char* s){if(p&&n){const auto k=std::min<std::size_t>(n-1,std::strlen(s));std::memcpy(p,s,k);p[k]=0;}}
double average(const double* p,std::size_t n){long double a=0;for(std::size_t j=0;j<n;++j)a+=p[j];return static_cast<double>(a/n);}
double finiteReal(long double value){if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw std::domain_error("Science result is not finite.");return static_cast<double>(value);}
long double varianceWide(const double* p,std::size_t n,bool population){if(n<(population?1u:2u))throw std::invalid_argument("Variance requires two samples, or one sample for population normalization.");long double m=0;for(std::size_t j=0;j<n;++j)m+=static_cast<long double>(p[j]);m/=n;long double sum=0;for(std::size_t j=0;j<n;++j){const long double d=p[j]-m;sum+=d*d;}return sum/(population?n:n-1);}
double standardDeviation(const double* p,std::size_t n,bool population){return finiteReal(std::sqrt(varianceWide(p,n,population)));}
double quantile(const std::vector<double>& sorted,double q){if(q<0||q>1)throw std::invalid_argument("Quantile probability must lie in [0,1].");const double p=q*(sorted.size()-1);const auto j=static_cast<std::size_t>(p);return std::lerp(sorted[j],sorted[std::min(j+1,sorted.size()-1)],p-j);}
void check(const penta_numeric_host_v1* h){if(h&&h->cancelled&&h->cancelled(h->context))throw std::runtime_error("Science operation cancelled.");}
}
extern "C" int penta_science_numeric(const penta_numeric_host_v1* host,const penta_science_request_v1* request,penta_science_report_v1* report,char* error,uint64_t cap){
 try{
  if(!host||host->abi_version!=PENTA_NUMERIC_ABI_V1||!request||request->struct_size<sizeof(*request)||!report||report->struct_size<sizeof(*report)||!request->a||!request->output||request->count==0||request->count>1000000)throw std::invalid_argument("Invalid or empty science module request.");
  const auto& r=*request;const std::size_t n=r.count;check(host);
  for(const auto p:r.parameters)if(!std::isfinite(p))throw std::invalid_argument("Science parameters must be finite.");
  for(std::size_t j=0;j<n;++j)if(!std::isfinite(r.a[j]))throw std::invalid_argument("Science samples must be finite real values.");
  auto need=[&](std::size_t size){if(size>r.output_capacity)throw std::invalid_argument("Science output capacity is insufficient.");};
  std::vector<double> out;
  switch(r.operation){
  case PENTA_SCI_FFT:case PENTA_SCI_IFFT:{
   if(n>262144)throw std::invalid_argument("FFT size exceeds262144 samples.");
   // Existing Eigen's default KissFFT backend is bounded to smooth sizes here.
   auto factor=n;for(const auto p:{2u,3u,5u})while(factor%p==0)factor/=p;
   if(factor!=1)throw std::invalid_argument("FFT size must have only factors2,3,5; explicitly pad other sample counts.");
   if(r.b&&r.secondary_count!=n)throw std::invalid_argument("FFT real/imaginary lengths must match.");
   need(2*n);std::vector<std::complex<double>> input(n),output;for(std::size_t j=0;j<n;++j){const double imaginary=r.b?r.b[j]:0;if(!std::isfinite(imaginary))throw std::invalid_argument("FFT imaginary samples must be finite.");input[j]={r.a[j],imaginary};}
   Eigen::FFT<double> fft;if(r.operation==PENTA_SCI_FFT)fft.fwd(output,input);else fft.inv(output,input);
   out.resize(2*n);for(std::size_t j=0;j<n;++j){out[2*j]=output[j].real();out[2*j+1]=output[j].imag();}report->count=n;break;
  }
  case PENTA_SCI_WELCH:{
   if(r.parameters[1]<2||r.parameters[1]>static_cast<double>(n)||std::floor(r.parameters[1])!=r.parameters[1]||r.parameters[2]<0||r.parameters[2]>=r.parameters[1]||std::floor(r.parameters[2])!=r.parameters[2])throw std::invalid_argument("Invalid integer Welch segment/overlap.");
   const auto length=static_cast<std::size_t>(r.parameters[1]);const auto overlap=static_cast<std::size_t>(r.parameters[2]);const double fs=r.parameters[0];
   if(!(fs>0)||length<2||length>n||overlap>=length)throw std::invalid_argument("Welch requires fs>0,2<=segment<=samples and0<=overlap<segment.");
   auto factor=length;for(const auto p:{2u,3u,5u})while(factor%p==0)factor/=p;if(factor!=1)throw std::invalid_argument("Welch segment size must have only factors2,3,5.");
   const auto bins=length/2+1;need(2*bins);out.assign(2*bins,0);std::vector<double> window(length);long double energy=0;for(std::size_t j=0;j<length;++j){window[j]=0.5-0.5*std::cos(2*std::acos(-1.0)*j/(length-1));energy+=window[j]*window[j];}
   if(energy==0)throw std::invalid_argument("Hann window needs at least three samples.");
   std::size_t segments=0;Eigen::FFT<double> fft;
   for(std::size_t start=0;start+length<=n;start+=length-overlap){check(host);std::vector<std::complex<double>> input(length),spectrum;const double m=r.parameters[3]!=0?average(r.a+start,length):0;for(std::size_t j=0;j<length;++j)input[j]={(r.a[start+j]-m)*window[j],0};fft.fwd(spectrum,input);for(std::size_t k=0;k<bins;++k){const double scale=(k!=0&&!(length%2==0&&k==length/2))?2:1;out[2*k+1]+=scale*std::norm(spectrum[k])/(fs*static_cast<double>(energy));}++segments;}
   for(std::size_t k=0;k<bins;++k){out[2*k]=fs*k/length;out[2*k+1]/=segments;}report->count=bins;break;
  }
  case PENTA_SCI_AUTOCORR:{
   if(r.parameters[0]<0||r.parameters[0]>=static_cast<double>(n)||std::floor(r.parameters[0])!=r.parameters[0])throw std::invalid_argument("Autocorrelation max_lag must be an integer smaller than sample count.");
   const auto lag=static_cast<std::size_t>(r.parameters[0]);if(n>100000000/(lag+1))throw std::invalid_argument("Autocorrelation exceeds its work budget.");need(lag+1);const double m=r.parameters[1]!=0?average(r.a,n):0;long double zero=0;for(std::size_t j=0;j<n;++j){const long double d=r.a[j]-m;zero+=d*d;}if(!(zero>0))throw std::invalid_argument("Normalized autocorrelation requires nonzero centered energy.");out.resize(lag+1);for(std::size_t k=0;k<=lag;++k){check(host);long double v=0;for(std::size_t j=0;j+k<n;++j)v+=(static_cast<long double>(r.a[j])-m)*(r.a[j+k]-m);out[k]=static_cast<double>(v/zero);}report->count=out.size();break;
  }
  case PENTA_SCI_PEAKS:{
   if(r.parameters[1]<1||r.parameters[1]>static_cast<double>(n)||std::floor(r.parameters[1])!=r.parameters[1])throw std::invalid_argument("Peak distance must be a positive integer no larger than sample count.");
   const auto distance=static_cast<std::size_t>(r.parameters[1]);std::vector<std::size_t> candidates;for(std::size_t j=1;j+1<n;++j){if(r.a[j]<r.parameters[0]||r.a[j]<=r.a[j-1])continue;std::size_t end=j;while(end+1<n&&r.a[end+1]==r.a[j])++end;if(end+1<n&&r.a[end]>r.a[end+1])candidates.push_back(j);j=end;}
   std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){return r.a[a]>r.a[b];});std::set<std::size_t> peaks;for(const auto j:candidates){check(host);auto it=peaks.lower_bound(j);bool accept=it==peaks.end()||*it-j>=distance;if(accept&&it!=peaks.begin()){--it;accept=j-*it>=distance;}if(accept)peaks.insert(j);}need(2*peaks.size());for(const auto j:peaks){out.push_back(r.a[j]);out.push_back(static_cast<double>(j+1));}report->count=peaks.size();break;
  }
  case PENTA_SCI_VARIANCE:out={finiteReal(varianceWide(r.a,n,r.parameters[0]!=0))};report->count=1;break;
  case PENTA_SCI_STDDEV:out={standardDeviation(r.a,n,r.parameters[0]!=0)};report->count=1;break;
  case PENTA_SCI_COVARIANCE:{
   const auto rows=static_cast<std::size_t>(r.rows),cols=static_cast<std::size_t>(r.columns);const bool population=r.parameters[0]!=0;if(!rows||!cols||rows>n||cols!=n/rows||n%rows||rows<(population?1u:2u))throw std::invalid_argument("Covariance requires observations in rows and at least two rows for sample normalization.");if(cols>1000000/cols||n>100000000/cols)throw std::invalid_argument("Covariance exceeds its storage/work budget.");need(cols*cols);std::vector<long double> means(cols,0);for(std::size_t row=0;row<rows;++row)for(std::size_t col=0;col<cols;++col)means[col]+=static_cast<long double>(r.a[row*cols+col])/rows;out.resize(cols*cols);for(std::size_t a=0;a<cols;++a)for(std::size_t b=0;b<=a;++b){check(host);long double v=0;for(std::size_t row=0;row<rows;++row)v+=(r.a[row*cols+a]-means[a])*(r.a[row*cols+b]-means[b]);out[a*cols+b]=out[b*cols+a]=static_cast<double>(v/(population?rows:rows-1));}report->count=cols;break;
  }
  case PENTA_SCI_QUANTILE:{if(!r.b||r.secondary_count==0)throw std::invalid_argument("Quantile requires probability samples.");std::vector<double> sorted(r.a,r.a+n);std::sort(sorted.begin(),sorted.end());for(std::size_t j=0;j<r.secondary_count;++j){if(!std::isfinite(r.b[j]))throw std::invalid_argument("Quantile probabilities must be finite.");out.push_back(quantile(sorted,r.b[j]));}report->count=out.size();break;}
  case PENTA_SCI_HISTOGRAM:{if(!r.b||r.secondary_count<2)throw std::invalid_argument("Histogram requires at least two finite increasing edges.");for(std::size_t j=0;j<r.secondary_count;++j)if(!std::isfinite(r.b[j])||(j&&r.b[j]<=r.b[j-1]))throw std::invalid_argument("Histogram edges must be finite and strictly increasing.");const auto bins=r.secondary_count-1;out.assign(bins,0);for(std::size_t j=0;j<n;++j){if(r.a[j]<r.b[0]||r.a[j]>r.b[bins])continue;const auto it=std::upper_bound(r.b,r.b+r.secondary_count,r.a[j]);const auto k=std::min<std::size_t>(bins-1,static_cast<std::size_t>(it-r.b-1));++out[k];}report->count=bins;break;}
  case PENTA_SCI_BOOTSTRAP:{const auto reps=static_cast<std::size_t>(r.parameters[0]),stat=static_cast<std::size_t>(r.parameters[1]);if(reps<2||reps>100000||reps>10000000/n||stat>2)throw std::invalid_argument("Bootstrap requires2..100000 replicates and at most ten-million sampled values.");std::mt19937_64 random(r.seed);std::uniform_int_distribution<std::size_t> pick(0,n-1);std::vector<double> sample(n);out.resize(reps);for(std::size_t j=0;j<reps;++j){check(host);for(auto& x:sample)x=r.a[pick(random)];if(stat==0)out[j]=average(sample.data(),n);else if(stat==1){std::sort(sample.begin(),sample.end());out[j]=quantile(sample,0.5);}else out[j]=standardDeviation(sample.data(),n,false);}report->count=reps;break;}
  case PENTA_SCI_NORMPDF:case PENTA_SCI_NORMCDF:case PENTA_SCI_NORMINV:{if(!(r.parameters[1]>0))throw std::invalid_argument("Normal distribution sigma must be positive.");boost::math::normal_distribution<double> normal(r.parameters[0],r.parameters[1]);out.resize(n);for(std::size_t j=0;j<n;++j){if(r.operation==PENTA_SCI_NORMPDF)out[j]=boost::math::pdf(normal,r.a[j]);else if(r.operation==PENTA_SCI_NORMCDF)out[j]=boost::math::cdf(normal,r.a[j]);else {if(!(r.a[j]>0&&r.a[j]<1))throw std::invalid_argument("Finite normal inverse probabilities must lie strictly inside(0,1).");out[j]=boost::math::quantile(normal,r.a[j]);}}report->count=n;break;}
  default:throw std::invalid_argument("Unknown science operation.");
  }
  check(host);need(out.size());for(const auto x:out)if(!std::isfinite(x))throw std::domain_error("Science result is not finite.");std::copy(out.begin(),out.end(),r.output);return PENTA_NUMERIC_OK;
 }catch(const std::invalid_argument& e){message(error,cap,e.what());return PENTA_NUMERIC_ARGUMENT;}catch(const std::exception& e){message(error,cap,e.what());return host&&host->cancelled&&host->cancelled(host->context)?PENTA_NUMERIC_CANCELLED:PENTA_NUMERIC_DOMAIN;}catch(...){message(error,cap,"Unknown science module failure.");return PENTA_NUMERIC_INTERNAL;}
}
