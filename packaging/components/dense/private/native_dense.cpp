#include "universal_dynamic.inc"
namespace {
unsigned width(BridgeEntry entry){if(entry.tag==-1)return 1;if(entry.tag==-2)return 5;if(!entry.definition||entry.definition->dimension<1||entry.definition->dimension>64)throw std::domain_error("Invalid native entry descriptor.");return entry.definition->dimension;}
}
unsigned bridge_array_index(unsigned rows,unsigned columns,unsigned row,unsigned column){if(row>=rows||column>=columns)throw std::domain_error("Native array index exceeds its fixed shape.");return row*columns+column;}
unsigned bridge_linear_index(unsigned rows,unsigned columns,unsigned oneBased){if(oneBased<1||oneBased>rows*columns)throw std::domain_error("Native array linear index exceeds its fixed shape.");const unsigned n=oneBased-1;return bridge_array_index(rows,columns,n%rows,n/rows);}
template<class S>void bridge_load_index(S* out,const S* in,unsigned index,unsigned cells,unsigned count){if(index>=cells)throw std::domain_error("Native cell index exceeds its immutable shape.");std::copy_n(in+index*count,count,out);}
template<class S>void bridge_store_index(S* out,const S* in,unsigned index,unsigned cells,unsigned count){if(index>=cells)throw std::domain_error("Native assignment exceeds its immutable shape.");std::copy_n(in,count,out+index*count);}
template<class S>void bridge_entry_divide(S* out,const S* in,const S* denominator,unsigned cells,unsigned count){for(unsigned n=0;n<cells;++n)for(unsigned k=0;k<count;++k)out[n*count+k]=divide(in[n*count+k],denominator[n]);}
template<class S>void bridge_fill(S* out,const S* cell,unsigned cells,unsigned count){for(unsigned n=0;n<cells;++n)std::copy_n(cell,count,out+n*count);}
template<class S>void bridge_tensor_from_array(S* out,const S* in,unsigned rows,unsigned columns,unsigned count,unsigned elements){if(rows*columns!=elements&&rows*columns!=1)throw std::domain_error("Tensor reshape must retain element count.");for(unsigned n=0;n<elements;++n){const auto cell=rows*columns==1?0:(n%rows)*columns+n/rows;std::copy_n(in+cell*count,count,out+n*count);}}
template<class S>void bridge_coefficient(S* out,const S* in,unsigned cells,unsigned count,unsigned index){if(index>=count)throw std::domain_error("Native tensor coefficient exceeds its entry algebra.");for(unsigned n=0;n<cells;++n)out[n]=in[n*count+index];}
template<class S>void bridge_copy(S* out,const S* in,std::size_t count){if(count&&(!out||!in))throw std::domain_error("Invalid native coefficient buffer.");std::copy_n(in,count,out);}
template<class S>bool bridge_equal(const S* a,const S* b,std::size_t count){for(std::size_t n=0;n<count;++n)if(!(a[n]==b[n]))return false;return true;}
template<class S>void bridge_cell(S* out,const S* a,const S* b,BridgeEntry entry,BridgeOp op,long long aux,double gamma){
 const auto n=width(entry);if(!out)throw std::domain_error("Missing native result buffer.");
 if(op==BridgeOp::Copy){bridge_copy(out,a,n);return;}
 if(op==BridgeOp::Negate){for(unsigned k=0;k<n;++k)out[k]=-a[k];return;}
 if(op==BridgeOp::Add||op==BridgeOp::Subtract){for(unsigned k=0;k<n;++k)out[k]=op==BridgeOp::Add?a[k]+b[k]:a[k]-b[k];return;}
 if(op==BridgeOp::Scale||op==BridgeOp::Divide){for(unsigned k=0;k<n;++k)out[k]=op==BridgeOp::Scale?a[k]*b[0]:divide(a[k],b[0]);return;}
 if(entry.tag==-1){switch(op){
 case BridgeOp::Lift:out[0]=a[0];return;case BridgeOp::Basis:if(aux!=0)throw std::domain_error("Invalid real basis index.");out[0]=S(1);return;
 case BridgeOp::Product:out[0]=a[0]*b[0];return;case BridgeOp::Conjugate:out[0]=a[0];return;case BridgeOp::Pure:out[0]=S(0);return;
 case BridgeOp::Norm2:case BridgeOp::CoordinateNorm2:out[0]=a[0]*a[0];return;
 case BridgeOp::Inverse:out[0]=divide(S(1),a[0]);return;case BridgeOp::Power:out[0]=cb_ipow(a[0],aux);return;
 case BridgeOp::Whole:switch(aux){case 1:out[0]=cb_sin(a[0]);break;case 2:out[0]=cb_cos(a[0]);break;case 3:out[0]=cb_exp(a[0]);break;case 4:out[0]=cb_sinh(a[0]);break;default:out[0]=cb_cosh(a[0]);}return;
 default:throw std::domain_error("Unsupported real native operation.");}}
 if(entry.tag==-2){Pent<S> p{},q{},r{};if(a&&op!=BridgeOp::Lift)std::copy_n(a,5,p.c.begin());if(b&&op==BridgeOp::Product)std::copy_n(b,5,q.c.begin());switch(op){
 case BridgeOp::Lift:r=lift<S>(a[0]);break;case BridgeOp::Basis:r=basis<S>(static_cast<unsigned>(aux));break;case BridgeOp::Product:r=product(p,q,gamma);break;
 case BridgeOp::Conjugate:r=conjugate(p);break;case BridgeOp::Pure:r=pure(p);break;case BridgeOp::Norm2:case BridgeOp::CoordinateNorm2:out[0]=norm2(p);return;
 case BridgeOp::Inverse:r=divide(conjugate(p),norm2(p));break;case BridgeOp::Power:r=cb_pent_power(p,aux,gamma);break;case BridgeOp::Whole:r=cb_whole(p,static_cast<unsigned>(aux));break;
 default:throw std::domain_error("Unsupported Pentarnion native operation.");}std::copy(r.c.begin(),r.c.end(),out);return;}
 private_dense::Scope scope(entry.definition);private_dense::Dynamic<S> p{},q{},r{};if(a&&op!=BridgeOp::Lift)std::copy_n(a,n,p.c.begin());if(b&&op==BridgeOp::Product)std::copy_n(b,n,q.c.begin());
 using namespace private_dense;switch(op){
 case BridgeOp::Lift:r=universal_lift<S>(a[0]);break;case BridgeOp::Basis:r=universal_basis<S>(static_cast<unsigned>(aux));break;case BridgeOp::Product:r=universal_product(p,q);break;
 case BridgeOp::Conjugate:r=universal_conjugate(p);break;case BridgeOp::Pure:r=universal_pure(p);break;case BridgeOp::Norm2:out[0]=universal_norm2(p);return;case BridgeOp::CoordinateNorm2:out[0]=universal_coordinate_norm2(p);return;
 case BridgeOp::Inverse:r=universal_inverse(p);break;case BridgeOp::Power:r=universal_power(p,aux);break;case BridgeOp::Whole:r=universal_whole(p,static_cast<unsigned>(aux));break;case BridgeOp::ComplexLog:r=universal_complex_log(p);break;
 default:throw std::domain_error("Unsupported compiled native algebra operation.");}std::copy(r.c.begin(),r.c.end(),out);
}
template<class S>void bridge_cells(S* out,const S* a,const S* b,std::size_t cells,BridgeEntry entry,BridgeOp op,long long aux,double gamma,bool broadcast,bool left){
 const auto n=width(entry);std::vector<S> temporary(n);for(std::size_t cell=0;cell<cells;++cell){const auto* second=b?(broadcast?b:b+cell*n):nullptr;const auto* first=a+cell*n;if(left)bridge_cell(temporary.data(),second,first,entry,op,aux,gamma);else bridge_cell(temporary.data(),first,second,entry,op,aux,gamma);std::copy(temporary.begin(),temporary.end(),out+cell*n);}}
template<class S>void bridge_matmul(S* out,const S* a,const S* b,unsigned rows,unsigned inner,unsigned columns,BridgeEntry entry,double gamma){
 const auto n=width(entry);std::vector<S> result(static_cast<std::size_t>(rows)*columns*n),term(n),sum(n),next(n);for(unsigned row=0;row<rows;++row)for(unsigned column=0;column<columns;++column){std::fill(sum.begin(),sum.end(),S(0));for(unsigned k=0;k<inner;++k){bridge_cell(term.data(),a+(row*inner+k)*n,b+(k*columns+column)*n,entry,BridgeOp::Product,0,gamma);bridge_cell(next.data(),sum.data(),term.data(),entry,BridgeOp::Add,0,gamma);sum.swap(next);}std::copy(sum.begin(),sum.end(),result.data()+(row*columns+column)*n);}std::copy(result.begin(),result.end(),out);}
template<class S>void bridge_matrix_power(S* out,const S* a,unsigned dimension,BridgeEntry entry,unsigned exponent,double gamma){
 if(exponent>64){throw std::domain_error("Native matrix powers are bounded to0..64.");}const auto n=width(entry);const auto count=static_cast<std::size_t>(dimension)*dimension*n;std::vector<S> result(count),next(count);
 if(!exponent){S one(1);for(unsigned k=0;k<dimension;++k)bridge_cell<S>(result.data()+(k*dimension+k)*n,&one,nullptr,entry,BridgeOp::Lift,0,gamma);}else{std::copy_n(a,count,result.begin());for(unsigned k=1;k<exponent;++k){bridge_matmul(next.data(),result.data(),a,dimension,dimension,dimension,entry,gamma);result.swap(next);}}std::copy(result.begin(),result.end(),out);}
template<class S>void bridge_transpose(S* out,const S* a,unsigned rows,unsigned columns,BridgeEntry entry,bool conjugation){const auto n=width(entry);std::vector<S> result(static_cast<std::size_t>(rows)*columns*n);for(unsigned row=0;row<rows;++row)for(unsigned column=0;column<columns;++column)bridge_cell<S>(result.data()+(column*rows+row)*n,a+(row*columns+column)*n,nullptr,entry,conjugation?BridgeOp::Conjugate:BridgeOp::Copy,0,0);std::copy(result.begin(),result.end(),out);}
template<class S>void bridge_reduce(S* out,const S* a,unsigned rows,unsigned columns,BridgeEntry entry,unsigned axis,bool mean){
 const auto n=width(entry);const unsigned groups=axis==1?columns:axis==2?rows:1;const unsigned terms=axis==1?rows:axis==2?columns:rows*columns;
 std::vector<S> result(static_cast<std::size_t>(groups)*n),sum(n),next(n);for(unsigned group=0;group<groups;++group){std::fill(sum.begin(),sum.end(),S(0));for(unsigned k=0;k<terms;++k){const auto index=axis==1?k*columns+group:axis==2?group*columns+k:k;bridge_cell(next.data(),sum.data(),a+index*n,entry,BridgeOp::Add,0,0);sum.swap(next);}for(unsigned c=0;c<n;++c)result[group*n+c]=mean?divide(sum[c],S(terms)):sum[c];}std::copy(result.begin(),result.end(),out);}
template<class S>S bridge_norm(const S* values,std::size_t count){S sum(0);for(std::size_t k=0;k<count;++k)sum=sum+values[k]*values[k];return cb_sqrt(sum);}
template<class S>void bridge_map(S* out,const S* in,unsigned cells,unsigned inputWidth,unsigned outputWidth,void(*callback)(S*,const S*,void*),void* context){if(!callback)throw std::domain_error("Missing compiled native map callback.");for(unsigned n=0;n<cells;++n)callback(out+n*outputWidth,in+n*inputWidth,context);}
#define PENTA_DENSE_INSTANCE(S) \
template void bridge_fill<S>(S*,const S*,unsigned,unsigned);template void bridge_tensor_from_array<S>(S*,const S*,unsigned,unsigned,unsigned,unsigned);template void bridge_coefficient<S>(S*,const S*,unsigned,unsigned,unsigned); \
template void bridge_load_index<S>(S*,const S*,unsigned,unsigned,unsigned);template void bridge_store_index<S>(S*,const S*,unsigned,unsigned,unsigned);template void bridge_entry_divide<S>(S*,const S*,const S*,unsigned,unsigned); \
template void bridge_copy<S>(S*,const S*,std::size_t);template bool bridge_equal<S>(const S*,const S*,std::size_t); \
template void bridge_cell<S>(S*,const S*,const S*,BridgeEntry,BridgeOp,long long,double); \
template void bridge_cells<S>(S*,const S*,const S*,std::size_t,BridgeEntry,BridgeOp,long long,double,bool,bool); \
template void bridge_matmul<S>(S*,const S*,const S*,unsigned,unsigned,unsigned,BridgeEntry,double); \
template void bridge_matrix_power<S>(S*,const S*,unsigned,BridgeEntry,unsigned,double); \
template void bridge_transpose<S>(S*,const S*,unsigned,unsigned,BridgeEntry,bool); \
template void bridge_reduce<S>(S*,const S*,unsigned,unsigned,BridgeEntry,unsigned,bool); \
template S bridge_norm<S>(const S*,std::size_t); \
template void bridge_map<S>(S*,const S*,unsigned,unsigned,unsigned,void(*)(S*,const S*,void*),void*);
PENTA_DENSE_INSTANCE(Jet2)
PENTA_DENSE_INSTANCE(Jet3)
#undef PENTA_DENSE_INSTANCE
} // penta_generated
