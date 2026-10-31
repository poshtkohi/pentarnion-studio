#pragma once
#include <pentarnion/native_base.hpp>
#include <span>
namespace penta_generated {
struct BridgeTerm {unsigned left,right,output;long double coefficient;};
struct BridgeDefinition {
 unsigned dimension;bool has_unit,has_conjugation,has_metric,scalar_first,canonical_reciprocal,quadratic,associative_exact,power_associative_exact;
 std::span<const long double> unit,conjugation,metric,pure_square;
 std::span<const BridgeTerm> terms;long double product_bound;
};
struct BridgeEntry {int tag;const BridgeDefinition* definition;};
PENTA_NATIVE_API unsigned bridge_array_index(unsigned rows,unsigned columns,unsigned row,unsigned column);
PENTA_NATIVE_API unsigned bridge_linear_index(unsigned rows,unsigned columns,unsigned oneBased);
template<class S>PENTA_NATIVE_API void bridge_load_index(S* out,const S* in,unsigned index,unsigned cells,unsigned width);
template<class S>PENTA_NATIVE_API void bridge_store_index(S* out,const S* in,unsigned index,unsigned cells,unsigned width);
template<class S>PENTA_NATIVE_API void bridge_entry_divide(S* out,const S* in,const S* denominator,unsigned cells,unsigned width);
template<class S>PENTA_NATIVE_API void bridge_fill(S* out,const S* cell,unsigned cells,unsigned width);
template<class S>PENTA_NATIVE_API void bridge_tensor_from_array(S* out,const S* in,unsigned rows,unsigned columns,unsigned width,unsigned elements);
template<class S>PENTA_NATIVE_API void bridge_coefficient(S* out,const S* in,unsigned cells,unsigned width,unsigned index);
enum class BridgeOp:unsigned {Copy,Negate,Add,Subtract,Product,Scale,Divide,Conjugate,Pure,Norm2,CoordinateNorm2,Inverse,Power,Whole,ComplexLog,Lift,Basis};
template<class S>PENTA_NATIVE_API void bridge_copy(S* out,const S* in,std::size_t count);
template<class S>PENTA_NATIVE_API bool bridge_equal(const S* a,const S* b,std::size_t count);
template<class S>PENTA_NATIVE_API void bridge_cell(S* out,const S* a,const S* b,BridgeEntry entry,BridgeOp op,long long aux,double gamma);
template<class S>PENTA_NATIVE_API void bridge_cells(S* out,const S* a,const S* b,std::size_t cells,BridgeEntry entry,BridgeOp op,long long aux,double gamma,bool broadcast,bool left);
template<class S>PENTA_NATIVE_API void bridge_matmul(S* out,const S* a,const S* b,unsigned rows,unsigned inner,unsigned columns,BridgeEntry entry,double gamma);
template<class S>PENTA_NATIVE_API void bridge_matrix_power(S* out,const S* a,unsigned dimension,BridgeEntry entry,unsigned exponent,double gamma);
template<class S>PENTA_NATIVE_API void bridge_transpose(S* out,const S* a,unsigned rows,unsigned columns,BridgeEntry entry,bool conjugation);
template<class S>PENTA_NATIVE_API void bridge_reduce(S* out,const S* a,unsigned rows,unsigned columns,BridgeEntry entry,unsigned axis,bool mean);
template<class S>PENTA_NATIVE_API S bridge_norm(const S* values,std::size_t count);
template<class S>PENTA_NATIVE_API void bridge_map(S* out,const S* in,unsigned cells,unsigned inputWidth,unsigned outputWidth,void(*callback)(S*,const S*,void*),void* context);
}
