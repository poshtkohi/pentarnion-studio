// Optional CLI benchmark: report measurements; never assert a speedup threshold.
#include "modules/blas_coordinator_scope.hpp"
#include "runtime/thread_budget.hpp"
#include <cblas.h>
#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
namespace {
struct Problem{unsigned n;std::vector<double> a,b,c;explicit Problem(unsigned size):n(size),a(n*n),b(n*n),c(n*n){for(unsigned row=0;row<n;++row)for(unsigned col=0;col<n;++col){a[row*n+col]=(int(row%17)-int(col%13))*.03125;b[row*n+col]=(int(row%7)+int(col%11))*.015625;}}void solve(){cblas_dgemm(CblasRowMajor,CblasNoTrans,CblasNoTrans,n,n,n,1,a.data(),n,b.data(),n,0,c.data(),n);}bool verify()const{for(unsigned sample=0;sample<128;++sample){const auto row=(sample*37)%n,col=(sample*73+11)%n;long double expected=0;for(unsigned k=0;k<n;++k)expected+=static_cast<long double>(a[row*n+k])*b[k*n+col];if(c[row*n+col]!=static_cast<double>(expected))return false;}return true;}};
struct Batch{std::array<Problem*,2> problems;std::barrier<> ready;explicit Batch(Problem& a,Problem& b,unsigned participants):problems{&a,&b},ready(participants){}static int execute(void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity)noexcept{auto& self=*static_cast<Batch*>(raw);try{for(auto at=first;at<last;++at){penta::blas::Scope lease(1,1,false);self.ready.arrive_and_wait();self.problems[at]->solve();}return 0;}catch(const std::exception& failure){if(error&&capacity){std::strncpy(error,failure.what(),capacity-1);error[capacity-1]=0;}return 4;}catch(...){return 4;}}};
penta_blas_status_v1 status(){penta_blas_status_v1 value{};value.struct_size=sizeof(value);char error[128]{};const auto* api=penta_blas_coordinator_query_v1(PENTA_BLAS_COORDINATOR_ABI_V1);if(api->status(&value,error,sizeof(error)))throw std::runtime_error(error);return value;}
double seconds(std::chrono::steady_clock::time_point start){return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
}
int main(int argc,char** argv){try{const unsigned n=argc>1?static_cast<unsigned>(std::stoul(argv[1])):384,repeats=argc>2?static_cast<unsigned>(std::stoul(argv[2])):5;if(argc>3||n<64||n>1024||repeats<1||repeats>20)throw std::runtime_error("Use penta-blas-benchmark [dimension64..1024] [repeats1..20]");if(openblas_get_parallel()==OPENBLAS_SEQUENTIAL)throw std::runtime_error("Concurrent BLAS benchmark requires a threaded OpenBLAS backend.");const unsigned slots=std::min(2u,std::max(1u,std::thread::hardware_concurrency()));Problem first(n),second(n);penta::blas::Scope epoch(1,1,false);first.solve();second.solve();const auto before=status();double serial=0,parallel=0;auto cancelled=std::make_shared<std::atomic_bool>(false);penta::runtime::ParallelStatistics statistics;for(unsigned repeat=0;repeat<repeats;++repeat){auto start=std::chrono::steady_clock::now();first.solve();second.solve();serial+=seconds(start);Batch batch(first,second,slots);std::string error;start=std::chrono::steady_clock::now();const auto code=penta::runtime::ThreadBudget::instance().run(2,static_cast<uint64_t>(n)*n*n,Batch::execute,&batch,{slots,1},cancelled,statistics,error);parallel+=seconds(start);if(code)throw std::runtime_error(error);if(!first.verify()||!second.verify())throw std::runtime_error("Independent dyadic matrix checksum failed");}const auto after=status();std::cout<<"dimension,repeats,cpu_slots,serial_pair_seconds,parallel_pair_seconds,speedup,compatible_overlaps,maximum_active_owners,checksums_pass\n"<<n<<','<<repeats<<','<<slots<<','<<serial/repeats<<','<<parallel/repeats<<','<<serial/parallel<<','<<after.compatible_overlaps-before.compatible_overlaps<<','<<after.maximum_active_owners<<",1\n";return 0;}catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}}
