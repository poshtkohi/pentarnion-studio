#include "native_functions/native_callback.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <new>
#include <thread>
namespace allocation_probe {
std::atomic_uint64_t allocations{0};
thread_local bool counting=false;
}
// This test deliberately replaces the matching global allocation/deallocation
// pair with malloc/free to count host allocations. Keep the pair out of line:
// GCC otherwise inlines delete's free into callers of operator new and diagnoses
// a false mismatched-allocation warning despite this matching replacement pair.
#if defined(__GNUC__) || defined(__clang__)
#define PENTA_VALUE_ALLOCATOR_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define PENTA_VALUE_ALLOCATOR_NOINLINE __declspec(noinline)
#else
#define PENTA_VALUE_ALLOCATOR_NOINLINE
#endif
PENTA_VALUE_ALLOCATOR_NOINLINE void* operator new(std::size_t bytes){if(allocation_probe::counting)++allocation_probe::allocations;if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();}
PENTA_VALUE_ALLOCATOR_NOINLINE void* operator new[](std::size_t bytes){return ::operator new(bytes);}
PENTA_VALUE_ALLOCATOR_NOINLINE void operator delete(void* p)noexcept{std::free(p);}
PENTA_VALUE_ALLOCATOR_NOINLINE void operator delete[](void* p)noexcept{std::free(p);}
PENTA_VALUE_ALLOCATOR_NOINLINE void operator delete(void* p,std::size_t)noexcept{std::free(p);}
PENTA_VALUE_ALLOCATOR_NOINLINE void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#undef PENTA_VALUE_ALLOCATOR_NOINLINE
namespace {
using namespace penta;
int checks=0,failures=0;
void expect(bool value,const std::string& label){++checks;if(!value){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
std::shared_ptr<const language::FunctionDefinition> function(const std::string& source){const auto analysis=language::analyse(source);if(!analysis.ok()){for(const auto& d:analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';throw std::runtime_error("Native values fixture did not parse.");}for(const auto& statement:analysis.program.statements)if(statement.function)return statement.function;throw std::runtime_error("Missing callback fixture.");}
template<class Task>std::string error(Task&& task){try{task();return {};}catch(const ode::OdeError& e){return e.code;}catch(...){return "unexpected";}}
void run(){
 native_functions::Options settings;settings.build.cacheDirectory=std::filesystem::temp_directory_path()/("penta-native-values-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));settings.build.cancellation=std::make_shared<std::atomic_bool>(false);std::atomic_size_t charges{0};settings.chargeOperations=[&](std::size_t count){charges.fetch_add(count,std::memory_order_relaxed);};
 const auto realDefinition=function("native function real real_value(real x,real y){real z=x*y;return sin(z)+exp(x/7.0)-cos(y);}");
 native_functions::Callback real(*realDefinition,settings);
 const auto pentDefinition=function("native function pentarnion pent_value(pentarnion a,pentarnion b,pentarnion p){return (a*b)*p-a*(b*p)+sin(p);}");native_functions::Callback pent(*pentDefinition,settings);
 const auto tupleDefinition=function("native function [real first,pentarnion second,real third] tuple_value(real x,pentarnion p){return x*x,p*p+exp(p),coeff(p,3)+x;}");native_functions::Callback tuple(*tupleDefinition,settings);
 native_functions::DerivativeRequest order0;order0.order=0;
 auto compare=[&](const native_functions::Callback& callback,double gamma,const std::vector<double>& input){charges=0;const auto reference=callback.evaluate(gamma,input,order0);const auto charged=charges.load();std::array<double,128> storage{};charges=0;callback.evaluateValues(gamma,input,std::span<double>(storage.data(),callback.outputCount()));bool same=reference.values.size()==callback.outputCount();for(std::size_t j=0;j<callback.outputCount();++j)same=same&&bits(reference.values[j],storage[j]);expect(same,"value-only result matches order0 exact binary64 bits");expect(charges.load()==charged&&charged>0,"value-only native host charges match order0 exactly");expect(reference.nativeEvaluations==1,"reference order0 invokes the same entry once");};
 const std::vector<double> realInput{.3,-.7};const std::vector<double> pentInput{1,.2,-.3,.4,.5, -.2,.3,.1,-.5,.2, .1,.2,.3,-.1,.15};const std::vector<double> tupleInput{-.3,.2,.3,-.1,.4,.15};
 for(const auto gamma:{-1.0,0.0,1.0,2.5}){compare(real,gamma,realInput);compare(pent,gamma,pentInput);compare(tuple,gamma,tupleInput);}
 std::array<double,7> output{};real.evaluateValues(1,realInput,std::span<double>(output.data(),1));allocation_probe::allocations=0;allocation_probe::counting=true;for(int n=0;n<1000;++n)real.evaluateValues(1,realInput,std::span<double>(output.data(),1));allocation_probe::counting=false;expect(allocation_probe::allocations==0,"successful value-only host path allocates no heap buffers");allocation_probe::allocations=0;allocation_probe::counting=true;const auto control=real.evaluate(1,realInput,order0);allocation_probe::counting=false;expect(allocation_probe::allocations>0&&!control.values.empty(),"allocation probe detects existing order0 allocations");
 std::vector<double> alias=realInput;const auto expected=real.evaluate(1,alias,order0).values[0];real.evaluateValues(1,alias,std::span<double>(alias.data(),1));expect(bits(alias[0],expected)&&bits(alias[1],realInput[1]),"output may alias input without changing the computed value");
 output.fill(12345);expect(error([&]{real.evaluateValues(1,{.1},std::span<double>(output.data(),1));})=="N2001","wrong input layout rejects");expect(error([&]{real.evaluateValues(1,realInput,{});})=="N2001","empty output layout rejects");expect(error([&]{real.evaluateValues(1,realInput,std::span<double>(output.data(),2));})=="N2001","oversized output layout rejects");expect(error([&]{native_functions::Callback{}.evaluateValues(1,{},std::span<double>(output.data(),1));})=="N2001","uninitialized callback rejects");
 for(const double invalid:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){expect(error([&]{real.evaluateValues(invalid,realInput,std::span<double>(output.data(),1));})=="N2001","nonfinite gamma rejects");auto input=realInput;input[0]=invalid;expect(error([&]{real.evaluateValues(1,input,std::span<double>(output.data(),1));})=="N2001","nonfinite input rejects");}expect(output[0]==12345,"argument errors leave borrowed output intact");expect(error([&]{real.evaluateValues(1000001,realInput,std::span<double>(output.data(),1));})=="N2001","gamma range matches existing evaluator");
 const auto lateDefinition=function("native function [real first,real second] late_domain(real x){return x,x*x;}");native_functions::Callback late(*lateDefinition,settings);const std::vector<double> huge{1e200};output.fill(12345);const auto legacyError=error([&]{(void)late.evaluate(1,huge,order0);});expect(error([&]{late.evaluateValues(1,huge,std::span<double>(output.data(),2));})==legacyError&&legacyError=="N2005","late output overflow preserves domain error code");expect(output[0]==12345&&output[1]==12345,"late domain failure is transactional for all output entries");
 settings.build.cancellation->store(true);output.fill(12345);expect(error([&]{real.evaluateValues(1,realInput,std::span<double>(output.data(),1));})=="N2006","cached value-only callback observes cancellation");expect(output[0]==12345,"cancelled callback leaves output intact");settings.build.cancellation->store(false);
 auto budget=settings;budget.chargeOperations=[](std::size_t){throw std::runtime_error("native-values-quota");};native_functions::Callback limited(*realDefinition,budget);bool original=false;try{limited.evaluateValues(1,realInput,std::span<double>(output.data(),1));}catch(const std::runtime_error& e){original=std::string(e.what())=="native-values-quota";}expect(original,"value-only host quota failure preserves original exception");expect(output[0]==12345,"host quota failure leaves output intact");
 const auto parallelReference=tuple.evaluate(1,tupleInput,order0).values;std::atomic_bool parallelOkay{true};std::vector<std::thread> workers;for(int worker=0;worker<4;++worker)workers.emplace_back([&]{try{std::array<double,7> values{};for(int n=0;n<1000;++n){tuple.evaluateValues(1,tupleInput,values);for(std::size_t j=0;j<values.size();++j)if(!bits(values[j],parallelReference[j]))parallelOkay=false;}}catch(...){parallelOkay=false;}});for(auto& worker:workers)worker.join();expect(parallelOkay.load(),"concurrent read-only calls are safe with a synchronized host charge adapter");
 std::string parameters,body;std::vector<double> many(18);for(int n=0;n<18;++n){if(n){parameters+=",";body+="+";}parameters+="real x"+std::to_string(n);body+="x"+std::to_string(n);many[n]=.1*n;}native_functions::Callback linear(*function("native function real many_values("+parameters+"){return "+body+";}"),settings);std::array<double,1> fast{};constexpr int repeats=100000;double checksum=0;auto started=std::chrono::steady_clock::now();for(int n=0;n<repeats;++n)checksum+=linear.evaluate(1,many,order0).values[0];const auto legacySeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();started=std::chrono::steady_clock::now();for(int n=0;n<repeats;++n){linear.evaluateValues(1,many,fast);checksum+=fast[0];}const auto valuesSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();expect(std::isfinite(checksum),"warm benchmark results consumed");std::cout<<"100000 warm18-coordinate calls: order0="<<legacySeconds<<" seconds, evaluateValues="<<valuesSeconds<<" seconds\n";
 std::error_code ignored;std::filesystem::remove_all(settings.build.cacheDirectory,ignored);
}
}
int main(){try{run();}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}std::cout<<checks<<" native value adapter checks, "<<failures<<" failures\n";return failures?1:0;}
