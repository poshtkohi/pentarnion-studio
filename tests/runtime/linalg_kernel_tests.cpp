#include "modules/blas_coordinator_api.h"
#include "runtime/linalg_module_loader.hpp"
#include "algebra/pentarnion.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif
namespace {
using namespace penta::runtime;
int checks=0,failures=0;
std::filesystem::path testModuleDirectory(){if(const auto* path=std::getenv("PENTA_LINALG_TEST_MODULE_DIR"))return std::filesystem::path(path);return {};}
void check(bool okay,const std::string& message){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
bool close(double x,double y,double tolerance=1e-10){return std::abs(x-y)<=tolerance*(1+std::abs(y));}
bool close(const std::vector<double>& a,const std::vector<double>& b,double tolerance=1e-10){if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(!close(a[i],b[i],tolerance))return false;return true;}
std::vector<double> product(const std::vector<double>& a,size_t m,size_t k,const std::vector<double>& b,size_t n){std::vector<double> result(m*n);for(size_t i=0;i<m;++i)for(size_t j=0;j<n;++j)for(size_t c=0;c<k;++c)result[i*n+j]+=a[i*k+c]*b[c*n+j];return result;}
std::vector<double> transpose(const std::vector<double>& a,size_t rows,size_t cols){std::vector<double> out(a.size());for(size_t r=0;r<rows;++r)for(size_t c=0;c<cols;++c)out[c*rows+r]=a[r*cols+c];return out;}
struct Input {size_t rows,columns;uint32_t width=1;std::vector<double> data;penta_linalg_matrix_v1 view()const{return {rows,columns,rows*columns,width,0,data.data()};}};
struct Call {
    penta_linalg_request_v1 request{};penta_linalg_result_v1 result{};
    std::array<std::vector<double>,4> output;
    ParallelSettings settings;ParallelStatistics statistics;
    explicit Call(uint32_t op){request.struct_size=sizeof(request);request.operation=op;request.batches=1;request.gamma=1;request.rcond_min=1e-12;request.residual_tolerance=1e-10;request.iterative_tolerance=1e-10;request.max_iterations=1000;request.drop_tolerance=1e-4;request.fill_factor=10;result.struct_size=sizeof(result);settings.threads=4;}
    void buffer(size_t slot,size_t rows,size_t cols,size_t width=1,size_t batches=1){output[slot].resize(rows*cols*width*batches);result.output[slot]={output[slot].data(),output[slot].size(),rows,cols,static_cast<uint32_t>(width),0};}
    void run(const std::shared_ptr<std::atomic_bool>& cancel={}){LinalgModule(testModuleDirectory()).compute(request,result,settings,cancel,statistics);}
};
template<class F>void rejects(F action,int status,const std::string& name){try{action();check(false,name);}catch(const NumericModuleError& error){check(error.status==status,name+": "+error.what());}}
void dense(){
    Input a{2,2,1,{4,1,1,3}},b{2,1,1,{1,2}};
    Call det(PENTA_LA_DETERMINANT);det.request.a=a.view();det.run();check(close(det.result.scalar,11),"real determinant");check(det.result.backend_threads==4,"LAPACKE receives explicit four-thread backend grant");
    Call inverse(PENTA_LA_INVERSE);inverse.request.a=a.view();inverse.buffer(0,2,2);inverse.run();check(close(inverse.output[0],{3./11,-1./11,-1./11,4./11}),"guarded inverse values");check(inverse.result.relative_residual<1e-14,"inverse backward residual");
    Call solve(PENTA_LA_SOLVE);solve.request.a=a.view();solve.request.b=b.view();solve.buffer(0,2,1);solve.run();check(close(solve.output[0],{1./11,7./11})&&solve.result.converged,"dense LU solve");
    solve.request.method=PENTA_LA_METHOD_CHOLESKY;solve.run();check(close(solve.output[0],{1./11,7./11}),"dense Cholesky solve");
    Call lu(PENTA_LA_LU);lu.request.a=a.view();lu.buffer(0,2,2);lu.buffer(1,2,2);lu.buffer(2,2,2);lu.run();check(close(product(lu.output[0],2,2,lu.output[1],2),product(lu.output[2],2,2,a.data,2)),"P*A=L*U reconstruction");
    Call chol(PENTA_LA_CHOLESKY);chol.request.a=a.view();chol.buffer(0,2,2);chol.run();check(close(product(chol.output[0],2,2,transpose(chol.output[0],2,2),2),a.data),"Cholesky L*L-transpose reconstruction");
    Call automatic(PENTA_LA_CHOLESKY);automatic.request.a=a.view();automatic.buffer(0,2,2);automatic.settings.threads=0;automatic.run();check(automatic.result.backend_threads==1,"automatic modest dense factorization uses measured serial policy");check(automatic.statistics.requestedThreads==0&&automatic.statistics.effectiveThreads==1&&automatic.settings.threads==0,"automatic backend records requested0/effective1 without changing caller settings");
    Input rect{3,2,1,{1,2,3,4,5,7}};Call qr(PENTA_LA_QR);qr.request.a=rect.view();qr.buffer(0,3,2);qr.buffer(1,2,2);qr.buffer(2,2,2);qr.run();check(close(product(qr.output[0],3,2,qr.output[1],2),product(rect.data,3,2,qr.output[2],2)),"pivoted QR A*P=Q*R");
    Call svd(PENTA_LA_SVD);svd.request.a=rect.view();svd.buffer(0,3,2);svd.buffer(1,2,2);svd.buffer(2,2,2);svd.buffer(3,2,1);svd.run();check(close(product(product(svd.output[0],3,2,svd.output[1],2),3,2,transpose(svd.output[2],2,2),2),rect.data),"thin SVD reconstruction");check(svd.result.rank==2&&svd.result.rcond>0&&svd.result.rcond<1,"SVD rank and reciprocal2-condition");
    Input singular{2,2,1,{1,2,2,4}};Call rank(PENTA_LA_RANK);rank.request.a=singular.view();rank.run();check(rank.result.rank==1,"rank-deficient matrix rank");inverse.request.a=singular.view();rejects([&]{inverse.run();},PENTA_NUMERIC_DOMAIN,"rank-deficient inverse rejected");
    Input notPositive{2,2,1,{1,0,0,-1}};chol.request.a=notPositive.view();rejects([&]{chol.run();},PENTA_NUMERIC_DOMAIN,"non-positive-definite Cholesky rejected");
    Input inconsistent{2,1,1,{1,0}};solve.request.a=singular.view();solve.request.b=inconsistent.view();solve.request.method=PENTA_LA_METHOD_SVD;solve.run();check(solve.result.rank==1&&!solve.result.converged&&solve.result.residual_norm>.1,"least-squares candidate is not an equation root");
    Input zero{0,0,1,{}};det.request.a=zero.view();det.run();check(det.result.scalar==1,"empty determinant convention");
}
void spectral(){
    Input a{2,2,1,{0,-1,1,0}};Call eig(PENTA_LA_EIGEN);eig.request.a=a.view();eig.buffer(0,2,2);eig.buffer(1,2,2);eig.buffer(2,2,1);eig.buffer(3,2,1);eig.run();check(close(eig.output[2],{0,0})&&close(std::abs(eig.output[3][0]),1)&&close(eig.output[3][0],-eig.output[3][1]),"explicit split complex conjugate eigenvalues");
    auto avr=product(a.data,2,2,eig.output[0],2),avi=product(a.data,2,2,eig.output[1],2);bool relation=true;for(size_t r=0;r<2;++r)for(size_t c=0;c<2;++c){const auto index=r*2+c;relation=relation&&close(avr[index],eig.output[0][index]*eig.output[2][c]-eig.output[1][index]*eig.output[3][c])&&close(avi[index],eig.output[0][index]*eig.output[3][c]+eig.output[1][index]*eig.output[2][c]);}check(relation,"complex eigenvector A*v=lambda*v without Pentarnion i ambiguity");
    Call schur(PENTA_LA_SCHUR);schur.request.a=a.view();schur.buffer(0,2,2);schur.buffer(1,2,2);schur.run();check(close(product(product(schur.output[0],2,2,schur.output[1],2),2,2,transpose(schur.output[0],2,2),2),a.data),"real Schur Q*T*Q-transpose reconstruction");
}
void products(){
    Input a{256,256,1,std::vector<double>(256*256)},b=a;for(size_t i=0;i<a.data.size();++i){a.data[i]=static_cast<double>(i%13)*.01;b.data[i]=static_cast<double>(i%7)*.02;}
    Call multiply(PENTA_LA_MULTIPLY);multiply.request.a=a.view();multiply.request.b=b.view();multiply.buffer(0,256,256);multiply.settings.threads=1;multiply.run();const auto serial=multiply.output[0];multiply.settings.threads=4;multiply.run();multiply.run();check(multiply.output[0]==serial,"same GEMM tile arithmetic at one/four host threads");check(multiply.statistics.threadsUsed>1&&multiply.statistics.peakActiveThreads>1&&multiply.statistics.threadsUsed<=4,"large GEMM actually uses shared concurrent workers");check(close(serial,product(a.data,256,256,b.data,256),1e-12),"CBLAS product matches independent scalar reference");
    Call transposeCall(PENTA_LA_TRANSPOSE);transposeCall.request.a=a.view();transposeCall.buffer(0,256,256);transposeCall.run();check(transposeCall.output[0]==transpose(a.data,256,256),"native transpose");
    Call reduction(PENTA_LA_REDUCE);reduction.request.a=a.view();reduction.request.axis=1;reduction.request.reduce_operation=PENTA_LA_SUM;reduction.buffer(0,1,256);reduction.run();bool sums=true;for(size_t c=0;c<256;++c){long double total=0;for(size_t r=0;r<256;++r)total+=a.data[r*256+c];sums=sums&&reduction.output[0][c]==static_cast<double>(total);}check(sums,"fixed-order independent column reductions");
    const penta::algebra::Context context(1);const penta::algebra::Pentarnion p(context,{1,.2,-.3,.4,.5}),q(context,{-.1,.4,.2,-.5,.1}),r(context,{.2,-.2,.6,.3,-.4});
    auto nativeMultiply=[&](const penta::algebra::Pentarnion& left,const penta::algebra::Pentarnion& right){Input x{1,1,5,std::vector<double>(left.coefficients().begin(),left.coefficients().end())},y{1,1,5,std::vector<double>(right.coefficients().begin(),right.coefficients().end())};Call c(PENTA_LA_MULTIPLY);c.request.a=x.view();c.request.b=y.view();c.buffer(0,1,1,5);c.run();penta::algebra::Coefficients coefficients{};std::copy(c.output[0].begin(),c.output[0].end(),coefficients.begin());return penta::algebra::Pentarnion(context,coefficients);};
    const auto pq=nativeMultiply(p,q);check(pq.coefficients()==penta::algebra::multiply(p,q).coefficients(),"Pentarnion product retains binary core convention");const auto left=nativeMultiply(pq,r),right=nativeMultiply(p,nativeMultiply(q,r));check(left.coefficients()!=right.coefficients(),"explicit nonassociative matrix grouping remains observable");
    Input pinput{1,1,5,std::vector<double>(p.coefficients().begin(),p.coefficients().end())};Call op(PENTA_LA_LEFT_OPERATOR);op.request.a=pinput.view();op.buffer(0,5,5);op.run();const auto expected=penta::algebra::leftMatrix(p);bool matches=true;for(size_t i=0;i<5;++i)for(size_t j=0;j<5;++j)matches=matches&&op.output[0][i*5+j]==expected[i][j];check(matches,"left coefficient operator matches published scalar operator");const auto leftOperator=op.output[0];op.request.operation=PENTA_LA_RIGHT_OPERATOR;op.run();const auto rightOperator=penta::algebra::rightMatrix(p);matches=true;for(size_t i=0;i<5;++i)for(size_t j=0;j<5;++j)matches=matches&&op.output[0][i*5+j]==rightOperator[i][j];check(matches&&op.output[0]!=leftOperator,"right operator matches its distinct scalar multiplication action");
    auto cancel=std::make_shared<std::atomic_bool>(true);std::fill(multiply.output[0].begin(),multiply.output[0].end(),7);rejects([&]{multiply.run(cancel);},PENTA_NUMERIC_CANCELLED,"pre-cancelled matrix operation rejected");check(multiply.output[0].front()==7&&multiply.output[0].back()==7,"pre-cancel output remains unchanged");cancel->store(false);multiply.run(cancel);check(multiply.output[0]==serial,"module/pool usable after cancellation");
    Input empty{0,2,1,{}},rightEmpty{2,3,1,std::vector<double>(6,1)};Call emptyProduct(PENTA_LA_MULTIPLY);emptyProduct.request.a=empty.view();emptyProduct.request.b=rightEmpty.view();emptyProduct.buffer(0,0,3);emptyProduct.statistics.threadsUsed=8;emptyProduct.run();check(emptyProduct.statistics.threadsUsed==0&&emptyProduct.statistics.lastElements==0,"empty GEMM resets actual worker statistics");
    Input longPent{256,256,5,std::vector<double>(256*256*5)};for(size_t i=0;i<256*256;++i){longPent.data[5*i]=1;longPent.data[5*i+1]=.01;}Call active(PENTA_LA_MULTIPLY);active.request.a=longPent.view();active.request.b=longPent.view();active.buffer(0,256,256,5);cancel->store(false);std::thread cancellation([&]{std::this_thread::sleep_for(std::chrono::milliseconds(3));cancel->store(true);});bool wasCancelled=false;try{active.run(cancel);}catch(const NumericModuleError& error){wasCancelled=error.status==PENTA_NUMERIC_CANCELLED;}cancellation.join();check(wasCancelled,"active Pentarnion matrix operation joins workers on cancellation");cancel->store(false);multiply.run(cancel);check(multiply.output[0]==serial,"pool and BLAS configuration recover after active cancellation");
}
void sparse(){
    std::vector<uint64_t> indices{0,1,0,1},pointers{0,2,4};std::vector<double> values{4,1,1,3};penta_linalg_sparse_v1 a{2,2,4,1,0,indices.data(),pointers.data(),values.data(),4,3,4};Input b{2,1,1,{1,2}};
    Call multiply(PENTA_LA_SPARSE_MULTIPLY);multiply.request.sparse_a=a;multiply.request.b=b.view();multiply.buffer(0,2,1);multiply.run();check(close(multiply.output[0],{6,7}),"CSC sparse-dense product");
    Call solve(PENTA_LA_SPARSE_SOLVE);solve.request.sparse_a=a;solve.request.b=b.view();solve.request.method=PENTA_LA_METHOD_SPARSELU;solve.buffer(0,2,1);solve.run();check(solve.result.converged&&close(solve.output[0],{1./11,7./11}),"SparseLU solve and residual");
    solve.request.method=PENTA_LA_METHOD_BICGSTAB;for(const auto preconditioner:{PENTA_LA_ILUT,PENTA_LA_DIAGONAL,PENTA_LA_IDENTITY}){solve.request.preconditioner=preconditioner;solve.run();check(solve.result.converged&&close(solve.output[0],{1./11,7./11}),"BiCGSTAB explicit preconditioner");}
    std::vector<uint64_t> outIndices(16),outPointers(3);std::vector<double> outValues(16);Call mm(PENTA_LA_SPARSE_MULTIPLY);mm.request.sparse_a=a;mm.request.sparse_b=a;mm.result.sparse={outIndices.data(),outPointers.data(),outValues.data(),16,3,16,0,0,0,0,0};mm.run();check(mm.result.sparse.nonzeros==4&&close(std::vector<double>(outValues.begin(),outValues.begin()+4),{17,7,7,10}),"canonical sparse-sparse product");
    Call tr(PENTA_LA_SPARSE_TRANSPOSE);tr.request.sparse_a=a;tr.result.sparse={outIndices.data(),outPointers.data(),outValues.data(),16,3,16,0,0,0,0,0};tr.run();check(tr.result.sparse.nonzeros==4&&std::equal(indices.begin(),indices.end(),outIndices.begin())&&close(std::vector<double>(outValues.begin(),outValues.begin()+4),values),"sparse transpose");
    auto invalid=a;std::vector<uint64_t> badRows{0,0,0,1};invalid.row_indices=badRows.data();multiply.request.sparse_a=invalid;rejects([&]{multiply.run();},PENTA_NUMERIC_ARGUMENT,"noncanonical duplicate CSC rows rejected");
    const size_t dimension=2000;std::vector<uint64_t> diagonalRows(dimension),diagonalPointers(dimension+1);std::vector<double> diagonalValues(dimension,2);for(size_t n=0;n<dimension;++n){diagonalRows[n]=n;diagonalPointers[n]=n;}diagonalPointers.back()=dimension;Input rhs{dimension,4,1,std::vector<double>(dimension*4,1)};Call limited(PENTA_LA_SPARSE_SOLVE);limited.request.sparse_a={dimension,dimension,dimension,1,0,diagonalRows.data(),diagonalPointers.data(),diagonalValues.data(),dimension,dimension+1,dimension};limited.request.b=rhs.view();limited.request.method=PENTA_LA_METHOD_SPARSELU;limited.buffer(0,dimension,4);limited.settings.threads=64;limited.run();const auto factorEstimate=64*dimension*dimension+128*dimension;const auto memoryLanes=(512ull*1024*1024)/factorEstimate;check(limited.statistics.lastElements<=memoryLanes&&limited.statistics.threadsUsed<=memoryLanes,"sparse RHS concurrency respects conservative aggregate factor workspace estimate");check(limited.result.converged&&std::all_of(limited.output[0].begin(),limited.output[0].end(),[](double x){return x==.5;}),"memory-limited lanes still solve all right-hand sides");
    std::vector<uint64_t> singleRow{0},singlePointer{0,1};std::vector<double> malformed{1,std::numeric_limits<double>::quiet_NaN(),0,0,0};Call badFinite(PENTA_LA_SPARSE_TRANSPOSE);badFinite.request.sparse_a={1,1,1,5,0,singleRow.data(),singlePointer.data(),malformed.data(),1,2,5};badFinite.result.sparse={outIndices.data(),outPointers.data(),outValues.data(),16,3,16,0,0,0,0,0};rejects([&]{badFinite.run();},PENTA_NUMERIC_DOMAIN,"every Pentarnion sparse coefficient is checked for finiteness");
}
void budget(){
    auto job=[](bool factorization){for(size_t iteration=0;iteration<6;++iteration){Input a{64,64,1,std::vector<double>(4096)};for(size_t r=0;r<64;++r)for(size_t c=0;c<64;++c)a.data[r*64+c]=r==c?4:.001;Call call(factorization?PENTA_LA_CHOLESKY:PENTA_LA_MULTIPLY);call.request.a=a.view();call.request.b=a.view();call.buffer(0,64,64);call.run();}return true;};
    auto a=std::async(std::launch::async,job,false),b=std::async(std::launch::async,job,true);const bool completed=a.wait_for(std::chrono::seconds(15))==std::future_status::ready&&b.wait_for(std::chrono::seconds(15))==std::future_status::ready;check(completed,"concurrent GEMM/factorization share reservation without ABBA deadlock");if(!completed)std::exit(2);check(a.get()&&b.get(),"concurrent backend results complete");
    struct Nested {std::atomic_bool okay{true};} nested;
    const auto nestedTask=[](void* raw,uint64_t first,uint64_t last,char* error,uint64_t capacity)->int{try{auto& task=*static_cast<Nested*>(raw);for(auto n=first;n<last;++n){static_cast<void>(n);Input x{2,2,1,{4,1,1,3}};Call call(PENTA_LA_CHOLESKY);call.request.a=x.view();call.buffer(0,2,2);call.settings.threads=4;call.run();if(call.result.backend_threads!=1)task.okay=false;}return 0;}catch(const std::exception& failure){if(capacity){const auto text=std::string(failure.what());const auto size=std::min<size_t>(text.size(),capacity-1);std::copy_n(text.data(),size,error);error[size]=0;}return PENTA_NUMERIC_INTERNAL;}};
    ParallelSettings settings;settings.threads=4;settings.serialThreshold=1;ParallelStatistics stats;std::string error;const auto status=ThreadBudget::instance().run(4,2048,nestedTask,&nested,settings,{},stats,error);check(status==0&&nested.okay,"native calls inside outer parallel workers receive one backend CPU slot");check(stats.peakActiveThreads<=4,"nested backends respect outer CPU budget");
    auto folder=std::filesystem::temp_directory_path()/"penta-linalg-missing-module-check";std::filesystem::create_directories(folder);LinalgModule missing(folder);Input x{1,1,1,{1}};Call c(PENTA_LA_DETERMINANT);c.request.a=x.view();rejects([&]{missing.compute(c.request,c.result,c.settings,{},c.statistics);},PENTA_NUMERIC_INTERNAL,"missing optional linear algebra module gives controlled failure");std::error_code ignored;std::filesystem::remove(folder,ignored);
}
void sharedBlasEpoch(){
    // Exercise the real module ABI with four independently granted serial calls.
    // GEMM calls the host range service after acquiring its BLAS scope. A
    // barrier at that public callback proves actual concurrent admission; it
    // does not depend on an internal cancellation-checkpoint count.
    std::filesystem::path modulePath;
#ifdef _WIN32
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);modulePath=(testModuleDirectory().empty()?std::filesystem::path(std::wstring(executable,length)).parent_path()/"modules":testModuleDirectory())/"penta_linalg.dll";auto handle=LoadLibraryExW(modulePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!handle)throw std::runtime_error("Cannot load module for epoch concurrency test.");const auto query=reinterpret_cast<penta_linalg_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(handle,"penta_linalg_query_v1")));auto openblas=GetModuleHandleW(L"libopenblas.dll");const auto getParallel=openblas?reinterpret_cast<int(*)()>(reinterpret_cast<void*>(GetProcAddress(openblas,"openblas_get_parallel"))):nullptr;const auto getThreads=openblas?reinterpret_cast<int(*)()>(reinterpret_cast<void*>(GetProcAddress(openblas,"openblas_get_num_threads"))):nullptr;
#else
    char executable[32768]{};const auto length=readlink("/proc/self/exe",executable,sizeof(executable));if(length<=0)throw std::runtime_error("Cannot determine test module path.");modulePath=(testModuleDirectory().empty()?std::filesystem::path(std::string(executable,static_cast<std::size_t>(length))).parent_path()/"modules":testModuleDirectory())/"libpenta_linalg.so";auto handle=dlopen(modulePath.c_str(),RTLD_NOW|RTLD_LOCAL);if(!handle)throw std::runtime_error("Cannot load module for epoch concurrency test.");const auto query=reinterpret_cast<penta_linalg_query_function_v1>(dlsym(handle,"penta_linalg_query_v1"));const auto getParallel=reinterpret_cast<int(*)()>(dlsym(handle,"openblas_get_parallel"));const auto getThreads=reinterpret_cast<int(*)()>(dlsym(handle,"openblas_get_num_threads"));
#endif
    if(!query||!getParallel||!getThreads)throw std::runtime_error("Missing public BLAS/module queries in concurrency test.");
#ifdef _WIN32
    const auto support=GetModuleHandleW(L"penta_blas.dll");const auto sharedQuery=support?reinterpret_cast<penta_blas_coordinator_query_function_v1>(reinterpret_cast<void*>(GetProcAddress(support,"penta_blas_coordinator_query_v1"))):nullptr;
#else
    const auto sharedQuery=reinterpret_cast<penta_blas_coordinator_query_function_v1>(dlsym(handle,"penta_blas_coordinator_query_v1"));
#endif
    if(!sharedQuery)throw std::runtime_error("Missing shared BLAS coordinator query.");
    const auto* coordination=sharedQuery(PENTA_BLAS_COORDINATOR_ABI_V1);
    const auto* api=query(PENTA_LINALG_ABI_V1);check(api&&(api->capabilities&PENTA_LINALG_CAP_VALUE_ONLY),"value-only capability is additive");const int previous=getThreads();
    if(getParallel()!=0){
        std::barrier rendezvous(5);std::atomic_uint ready{0};
        struct Host {const penta_blas_coordinator_api_v1* coordination;std::barrier<>* rendezvous;std::atomic_uint* ready;int(*threads)();bool one=true;};
        const auto cancellation=[](void*)->int{return 0;};
        const auto parallel=[](void* raw,uint64_t count,uint64_t,penta_numeric_range_task_v1 task,void* opaque,char* error,uint64_t capacity)->int{auto& host=*static_cast<Host*>(raw);penta_blas_host_v1 caller{sizeof(caller),PENTA_BLAS_COORDINATOR_ABI_V1,1,0,nullptr,nullptr};penta_blas_request_v1 request{sizeof(request),1,0,0};penta_blas_lease_v1 lease{sizeof(lease),0,0,0,0};const auto admitted=host.coordination->begin(&caller,&request,&lease,error,capacity);if(admitted)return admitted;host.one=host.threads()==1;host.ready->fetch_add(1);host.rendezvous->arrive_and_wait();host.one=host.one&&host.threads()==1;const auto computed=count?task(opaque,0,count,error,capacity):0;const auto ended=host.coordination->end(&lease,error,capacity);return computed?computed:ended;};
        auto work=[&]{Host data{coordination,&rendezvous,&ready,getThreads,true};penta_numeric_host_v1 host{sizeof(host),PENTA_NUMERIC_ABI_V1,&data,cancellation,parallel};Input input{32,32,1,std::vector<double>(1024)};for(std::size_t r=0;r<32;++r)for(std::size_t c=0;c<32;++c)input.data[r*32+c]=r==c?4:.001;Call call(PENTA_LA_MULTIPLY);call.request.threads=1;call.request.a=call.request.b=input.view();call.buffer(0,32,32);char error[1024]{};const auto status=api->compute(&host,&call.request,&call.result,error,sizeof(error));if(status)throw std::runtime_error(error);return data.one&&close(call.output[0],product(input.data,32,32,input.data,32));};
        std::array<std::future<bool>,4> workers;for(auto& worker:workers)worker=std::async(std::launch::async,work);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(ready.load()!=4&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();check(ready.load()==4,"four serial backend calls enter concurrently");if(ready.load()!=4)std::quick_exit(2);
        struct Writer {std::atomic_bool finished{false};} writerData;
        auto writer=std::async(std::launch::async,[&]{const auto serial=[](void*,uint64_t count,uint64_t,penta_numeric_range_task_v1 task,void* opaque,char* error,uint64_t capacity)->int{return count?task(opaque,0,count,error,capacity):0;};penta_numeric_host_v1 host{sizeof(host),PENTA_NUMERIC_ABI_V1,nullptr,cancellation,serial};Input input{2,2,1,{4,1,1,3}};Call call(PENTA_LA_CHOLESKY);call.request.threads=4;call.request.a=input.view();call.buffer(0,2,2);char error[1024]{};const auto status=api->compute(&host,&call.request,&call.result,error,sizeof(error));writerData.finished=true;return status==0&&call.result.backend_threads==4&&close(product(call.output[0],2,2,transpose(call.output[0],2,2),2),input.data);});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));check(!writerData.finished&&getThreads()==1,"different BLAS thread settings wait for the active serial epoch");rendezvous.arrive_and_wait();bool completed=true;for(auto& worker:workers)completed=completed&&worker.wait_for(std::chrono::seconds(10))==std::future_status::ready;check(completed,"four serial backend calls join the same BLAS epoch concurrently");if(!completed)std::quick_exit(2);for(auto& worker:workers)check(worker.get(),"concurrent independent products retain one BLAS slot and match the scalar reference");const bool writerReady=writer.wait_for(std::chrono::seconds(10))==std::future_status::ready;check(writerReady,"different-count writer completes after serial readers");if(!writerReady)std::quick_exit(2);check(writer.get()&&writerData.finished,"explicit four-thread backend grant remains supported");
    }else check(true,"sequential OpenBLAS keeps exclusive safe fallback");
    check(getThreads()==previous,"BLAS global configuration restored after shared epoch");
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
}
void valueOnly(){
    Input input{2,2,1,{4,1,1,3}};Call determinant(PENTA_LA_DETERMINANT_VALUE);determinant.request.a=input.view();determinant.run();check(close(determinant.result.scalar,11)&&determinant.result.rank==0&&determinant.result.rcond==0,"value-only determinant omits diagnostic metadata explicitly");Call cholesky(PENTA_LA_CHOLESKY_FACTOR);cholesky.request.a=input.view();cholesky.buffer(0,2,2);cholesky.run();check(close(product(cholesky.output[0],2,2,transpose(cholesky.output[0],2,2),2),input.data)&&cholesky.result.rank==0&&cholesky.result.rcond==0,"factor-only Cholesky retains SPD factor without invented diagnostics");Input indefinite{2,2,1,{1,0,0,-1}};cholesky.request.a=indefinite.view();rejects([&]{cholesky.run();},PENTA_NUMERIC_DOMAIN,"factor-only Cholesky still rejects non-SPD input");
    cholesky.request.a=input.view();cholesky.settings.threads=0;cholesky.run();check(cholesky.result.backend_threads==1&&cholesky.statistics.requestedThreads==0&&cholesky.statistics.effectiveThreads==1,"new factor-only operation preserves small automatic one-thread policy");
    Call original(PENTA_LA_CHOLESKY);original.request.a=input.view();original.buffer(0,2,2);original.run();check(original.result.rank==2&&original.result.rcond>0,"original ABI Cholesky retains exact2-norm metadata");
}
void nestedBenchmark(){
    std::cout<<"operation,dimension,outer_requested,actual_host_workers,inner_backend_threads,calls,seconds\n";
    for(std::size_t dimension:{std::size_t(64),std::size_t(128)})for(std::size_t outer:{std::size_t(1),std::size_t(4)}){
        struct Work {std::size_t dimension;std::atomic_bool correct{true};} work{dimension};
        const auto task=[](void* opaque,uint64_t first,uint64_t last,char* error,uint64_t capacity)->int{try{auto& work=*static_cast<Work*>(opaque);const auto n=work.dimension;Input a{n,n,1,std::vector<double>(n*n)};for(std::size_t r=0;r<n;++r)for(std::size_t c=0;c<n;++c)a.data[r*n+c]=r==c?4:.001;for(auto job=first;job<last;++job){(void)job;Call call(PENTA_LA_CHOLESKY);call.request.a=a.view();call.buffer(0,n,n);call.run();if(call.result.backend_threads!=1||call.result.rank!=n||!close(call.output[0][0],2))work.correct=false;}return 0;}catch(const std::exception& failure){if(capacity){const auto size=std::min<std::size_t>(capacity-1,std::strlen(failure.what()));std::copy_n(failure.what(),size,error);error[size]=0;}return PENTA_NUMERIC_INTERNAL;}};
        ParallelSettings settings;settings.threads=outer;settings.serialThreshold=1;ParallelStatistics stats;std::string error;const auto begin=std::chrono::steady_clock::now();const auto status=ThreadBudget::instance().run(64,2048,task,&work,settings,{},stats,error);const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();if(status||!work.correct)throw std::runtime_error("Nested benchmark numerical/thread guard failed: "+error);std::cout<<"nested_cholesky,"<<dimension<<','<<outer<<','<<stats.threadsUsed<<",1,64,"<<elapsed<<'\n';
    }
}
void benchmark(bool valueOnlyMode=false){
    std::cout<<"operation,dimension,requested_threads,configured_backend_threads,actual_host_workers,seconds_per_call\n";
    for(const size_t dimension:{size_t(32),size_t(128),size_t(256)}){
        Input a{dimension,dimension,1,std::vector<double>(dimension*dimension)},b=a;
        for(size_t row=0;row<dimension;++row)for(size_t column=0;column<dimension;++column){a.data[row*dimension+column]=row==column?4:.001;b.data[row*dimension+column]=static_cast<double>((row+3*column)%17)*.01;}
        for(const auto operation:{PENTA_LA_MULTIPLY,PENTA_LA_CHOLESKY,PENTA_LA_SVD})for(const size_t threads:{size_t(1),size_t(4)}){
            Call call(valueOnlyMode&&operation==PENTA_LA_CHOLESKY?PENTA_LA_CHOLESKY_FACTOR:operation);call.settings.threads=threads;call.request.a=a.view();call.request.b=b.view();call.buffer(0,dimension,dimension);
            if(operation==PENTA_LA_SVD){call.buffer(1,dimension,dimension);call.buffer(2,dimension,dimension);call.buffer(3,dimension,1);}
            call.run();const size_t repeats=dimension==256?4:8;const auto begin=std::chrono::steady_clock::now();for(size_t n=0;n<repeats;++n)call.run();const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()/repeats;
            std::cout<<(operation==PENTA_LA_MULTIPLY?"gemm":operation==PENTA_LA_CHOLESKY?"cholesky":"svd")<<','<<dimension<<','<<threads<<','<<call.result.backend_threads<<','<<call.statistics.threadsUsed<<','<<seconds<<'\n';
        }
    }
}
}
int main(int argc,char** argv){try{if(argc==2&&std::string(argv[1])=="--benchmark-nested"){nestedBenchmark();return 0;}if(argc==2&&(std::string(argv[1])=="--benchmark"||std::string(argv[1])=="--benchmark-language")){benchmark(std::string(argv[1])=="--benchmark-language");return 0;}dense();spectral();products();sparse();budget();valueOnly();sharedBlasEpoch();}catch(const std::exception& error){check(false,std::string("uncaught native test failure: ")+error.what());}std::cout<<checks<<" native linear algebra checks, "<<failures<<" failures\n";return failures?1:0;}

