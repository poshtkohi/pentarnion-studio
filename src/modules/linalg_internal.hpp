#pragma once
#include "linalg_api.h"
#include "blas_coordinator_scope.hpp"
#include "algebra/pentarnion.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace penta::linalg {
struct Error:std::runtime_error {int status;Error(int s,const std::string& text):std::runtime_error(text),status(s){}};
inline void require(bool condition,const std::string& message){if(!condition)throw Error(PENTA_NUMERIC_ARGUMENT,message);}
inline void message(char* output,uint64_t capacity,const std::string& text){if(output&&capacity){const auto length=std::min<uint64_t>(capacity-1,text.size());std::memcpy(output,text.data(),static_cast<size_t>(length));output[length]=0;}}
inline double finite(double x){if(!std::isfinite(x))throw Error(PENTA_NUMERIC_DOMAIN,"Linear algebra produced a nonfinite result.");return x;}
inline void cancelled(const penta_numeric_host_v1* host){if(host->cancelled(host->context))throw Error(PENTA_NUMERIC_CANCELLED,"Linear algebra cancelled.");}
template<class F>int protect(F action,char* error,uint64_t capacity)noexcept{try{action();return PENTA_NUMERIC_OK;}catch(const Error& e){message(error,capacity,e.what());return e.status;}catch(const algebra::AlgebraError& e){message(error,capacity,e.code+": "+e.what());return PENTA_NUMERIC_DOMAIN;}catch(const std::exception& e){message(error,capacity,e.what());return PENTA_NUMERIC_INTERNAL;}catch(...){message(error,capacity,"Unknown native linear algebra error.");return PENTA_NUMERIC_INTERNAL;}}
inline uint64_t entries(uint64_t rows,uint64_t cols){require(rows<=100000&&cols<=100000&&(!cols||rows<=1000000/cols),"Matrix exceeds the native element bound.");return rows*cols;}
inline void matrixValid(const penta_linalg_matrix_v1& matrix,uint64_t batches=1){const auto count=entries(matrix.rows,matrix.columns);require((matrix.width==1||matrix.width==5)&&batches>=1&&batches<=1000&&count<=1000000/batches&&matrix.count==count*batches,"Invalid matrix coefficient layout.");require(matrix.count==0||matrix.values,"Missing matrix coefficients.");for(uint64_t i=0;i<matrix.count*matrix.width;++i)finite(matrix.values[i]);}
inline void realMatrix(const penta_linalg_matrix_v1& matrix){matrixValid(matrix);require(matrix.width==1,"This operation requires real entries; use leftop/rightop for a Pentarnion multiplication operator.");require(matrix.rows<=2048&&matrix.columns<=2048,"Dense factorization dimension exceeds2048.");}
inline void outputValid(penta_linalg_output_v1& output,uint64_t rows,uint64_t cols,uint32_t width=1,uint64_t batches=1){const auto count=entries(rows,cols);require((width==1||width==5)&&count<=1000000/batches&&output.capacity>=count*batches*width&&(!count||output.values),"Output buffer is too small.");output.rows=rows;output.columns=cols;output.width=width;}
inline algebra::Pentarnion element(const penta_linalg_matrix_v1& matrix,uint64_t index,const algebra::Context& context){algebra::Coefficients values{};for(uint32_t c=0;c<matrix.width;++c)values[c]=matrix.values[index*matrix.width+c];return algebra::Pentarnion(context,values);}
inline void write(penta_linalg_output_v1& output,uint64_t index,const algebra::Pentarnion& value){for(uint32_t c=0;c<output.width;++c)output.values[index*output.width+c]=finite(value.coefficients()[c]);}
inline void copyFinite(const std::vector<double>& input,penta_linalg_output_v1& output){require(output.capacity>=input.size(),"Output coefficient capacity exceeded.");for(size_t i=0;i<input.size();++i)output.values[i]=finite(input[i]);}
inline void lapackStatus(int status,const char* operation){if(status<0)throw Error(PENTA_NUMERIC_ARGUMENT,std::string(operation)+" rejected a native argument.");if(status>0)throw Error(PENTA_NUMERIC_DOMAIN,std::string(operation)+" did not converge or encountered a singular/non-positive-definite matrix.");}
// Internal scope delegates to the shared linalg/PETSc configuration domain.
// Compatible thread grants overlap; setters require an idle/exclusive epoch.
// Sequential vendor builds stay exclusive unless recursively owned.
class BlasScope {
    std::optional<blas::Scope> scope_;
public:
    explicit BlasScope(unsigned threads,const penta_numeric_host_v1* host=nullptr);
    ~BlasScope();
    BlasScope(const BlasScope&)=delete;
    BlasScope& operator=(const BlasScope&)=delete;
};
void denseProduct(const penta_numeric_host_v1*,const penta_linalg_request_v1&,penta_linalg_result_v1&);
void denseLinear(const penta_numeric_host_v1*,const penta_linalg_request_v1&,penta_linalg_result_v1&);
void spectral(const penta_numeric_host_v1*,const penta_linalg_request_v1&,penta_linalg_result_v1&);
void sparse(const penta_numeric_host_v1*,const penta_linalg_request_v1&,penta_linalg_result_v1&);
}
