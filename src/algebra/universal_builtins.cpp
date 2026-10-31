#include "universal_internal.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

namespace penta::algebra::universal::detail {
namespace {
using Wide=long double;
void conj(std::span<const Wide> value,std::span<Wide> result){result[0]=value[0];for(std::size_t i=1;i<value.size();++i)result[i]=-value[i];}
void hamilton(std::span<const Wide> a,std::span<const Wide> b,std::span<Wide> out){
    out[0]=a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3];
    out[1]=a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2];
    out[2]=a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1];
    out[3]=a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0];
}
// Hamilton basis at dimension4, then Baez's ordered pair convention. This
// fixes every basis sign explicitly; other Fano conventions require a basis map.
void cayley(std::span<const Wide> x,std::span<const Wide> y,std::span<Wide> out){
    const auto n=x.size();
    if(n==1){out[0]=x[0]*y[0];return;}
    if(n==2){out[0]=x[0]*y[0]-x[1]*y[1];out[1]=x[0]*y[1]+x[1]*y[0];return;}
    if(n==4){hamilton(x,y,out);return;}
    const auto h=n/2;std::array<Wide,8> ac{},db{},ad{},cb{},aStar{},bStar{};
    conj(x.first(h),std::span<Wide>(aStar).first(h));conj(x.subspan(h),std::span<Wide>(bStar).first(h));
    cayley(x.first(h),y.first(h),std::span<Wide>(ac).first(h));
    cayley(y.subspan(h),std::span<const Wide>(bStar).first(h),std::span<Wide>(db).first(h));
    cayley(std::span<const Wide>(aStar).first(h),y.subspan(h),std::span<Wide>(ad).first(h));
    cayley(y.first(h),x.subspan(h),std::span<Wide>(cb).first(h));
    for(std::size_t k=0;k<h;++k){out[k]=ac[k]-db[k];out[h+k]=ad[k]+cb[k];}
}
Property exact(bool value,std::string reason){return {value,Evidence::ProvedExact,std::move(reason)};}
std::string normalize(std::string_view name){std::string out;for(const char c:name)if(c!=' '&&c!='_'&&c!='-')out+=static_cast<char>(c>='A'&&c<='Z'?c+('a'-'A'):c);return out;}
std::string rationalInteger(double value){
    if(value==std::trunc(value))return std::to_string(static_cast<long long>(value));
    if(2*value==std::trunc(2*value))return std::to_string(static_cast<long long>(2*value))+"/2";
    throw Error("A4001","Builtin coefficient is outside its exact integer/half-integer construction.");
}
void matrixMetadata(Definition::Data& d){
    const auto n=d.dimension;d.metric.assign(n*n,0);d.metricExact.assign(n*n,"0");
    for(std::size_t i=0;i<n;++i){d.metric[i*n+i]=1;d.metricExact[i*n+i]="1";}
    d.unit.assign(n,0);d.unit[0]=1;d.unitExact.assign(n,"0");d.unitExact[0]="1";
    d.conjugation.assign(n*n,0);d.conjugationExact.assign(n*n,"0");
    for(std::size_t i=0;i<n;++i){double sign=i==0?1:-1;
        if(d.builtinKind=="dual_quaternion")sign=(i%4)==0?1:-1;
        if(d.builtinKind=="clifford"){const auto grade=std::popcount(static_cast<unsigned>(i));sign=((grade*(grade+1)/2)%2)?-1:1;}
        d.conjugation[i*n+i]=sign;d.conjugationExact[i*n+i]=rationalInteger(sign);
    }
    if(d.builtinKind=="matrix2"){
        d.unit={1,0,0,1};d.unitExact={"1","0","0","1"};
        std::fill(d.conjugation.begin(),d.conjugation.end(),0);std::fill(d.conjugationExact.begin(),d.conjugationExact.end(),"0");
        for(const auto& [row,column]:{std::pair{0u,0u},std::pair{1u,2u},std::pair{2u,1u},std::pair{3u,3u}}){d.conjugation[row*n+column]=1;d.conjugationExact[row*n+column]="1";}
    }else if(d.builtinKind=="lie_so3"){
        d.unit.clear();d.unitExact.clear();d.conjugation[0]=-1;d.conjugationExact[0]="-1";
    }else if(d.builtinKind=="jordan_sym2"){
        d.unit={1,0,1};d.unitExact={"1","0","1"};
        d.conjugation[4]=d.conjugation[8]=1;d.conjugationExact[4]=d.conjugationExact[8]="1";
        d.metric[4]=2;d.metricExact[4]="2";
    }
}
}

void builtinProduct(std::string_view kind,std::span<const double> x,std::span<const double> y,std::span<double> out){
    std::array<Wide,16> a{},b{},c{};for(std::size_t i=0;i<x.size();++i){a[i]=x[i];b[i]=y[i];}
    const auto n=x.size();
    if(kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion")cayley(std::span<const Wide>(a).first(n),std::span<const Wide>(b).first(n),std::span<Wide>(c).first(n));
    else if(kind=="dual"){c[0]=a[0]*b[0];c[1]=a[0]*b[1]+a[1]*b[0];}
    else if(kind=="split_complex"){c[0]=a[0]*b[0]+a[1]*b[1];c[1]=a[0]*b[1]+a[1]*b[0];}
    else if(kind=="dual_quaternion"){
        std::array<Wide,4> first{},second{},third{};hamilton(std::span<const Wide>(a).first(4),std::span<const Wide>(b).first(4),first);
        hamilton(std::span<const Wide>(a).first(4),std::span<const Wide>(b).subspan(4,4),second);hamilton(std::span<const Wide>(a).subspan(4,4),std::span<const Wide>(b).first(4),third);
        for(std::size_t i=0;i<4;++i){c[i]=first[i];c[4+i]=second[i]+third[i];}
    }else if(kind=="split_quaternion"){
        // i^2=-1,j^2=k^2=+1,ij=k; k=ij.
        c[0]=a[0]*b[0]-a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
        c[1]=a[0]*b[1]+a[1]*b[0]-a[2]*b[3]+a[3]*b[2];
        c[2]=a[0]*b[2]+a[2]*b[0]-a[1]*b[3]+a[3]*b[1];
        c[3]=a[0]*b[3]+a[3]*b[0]+a[1]*b[2]-a[2]*b[1];
    }else if(kind=="matrix2"){
        c[0]=a[0]*b[0]+a[1]*b[2];c[1]=a[0]*b[1]+a[1]*b[3];
        c[2]=a[2]*b[0]+a[3]*b[2];c[3]=a[2]*b[1]+a[3]*b[3];
    }else if(kind=="lie_so3"){
        c[0]=a[1]*b[2]-a[2]*b[1];c[1]=a[2]*b[0]-a[0]*b[2];c[2]=a[0]*b[1]-a[1]*b[0];
    }else if(kind=="jordan_sym2"){
        c[0]=a[0]*b[0]+a[1]*b[1];c[1]=(a[0]*b[1]+a[1]*b[2]+b[0]*a[1]+b[1]*a[2])/2;c[2]=a[1]*b[1]+a[2]*b[2];
    }else throw Error("A4001","Unknown specialized native algebra product.");
    for(std::size_t i=0;i<n;++i)out[i]=finite(c[i]);
}

std::shared_ptr<Definition::Data> makeBuiltin(std::string_view requested,const BuiltinOptions& options){
    auto d=std::make_shared<Definition::Data>();const auto key=normalize(requested);
    if(key=="real"||key=="r")d->builtinKind="real";
    else if(key=="complex"||key=="c")d->builtinKind="complex";
    else if(key=="quaternion"||key=="quaternions"||key=="h")d->builtinKind="quaternion";
    else if(key=="octonion"||key=="octonions"||key=="o")d->builtinKind="octonion";
    else if(key=="sedenion"||key=="sedenions"||key=="s")d->builtinKind="sedenion";
    else if(key=="dual"||key=="dualnumber"||key=="dualnumbers")d->builtinKind="dual";
    else if(key=="dualquaternion"||key=="dualquaternions")d->builtinKind="dual_quaternion";
    else if(key=="splitcomplex"||key=="hyperbolic")d->builtinKind="split_complex";
    else if(key=="splitquaternion"||key=="splitquaternions"||key=="coquaternion")d->builtinKind="split_quaternion";
    else if(key=="clifford"||key=="cl")d->builtinKind="clifford";
    else if(key=="matrix2"||key=="m2r")d->builtinKind="matrix2";
    else if(key=="lieso3"||key=="so3")d->builtinKind="lie_so3";
    else if(key=="jordansym2"||key=="symmetricjordan2")d->builtinKind="jordan_sym2";
    else throw Error("A4001","Unknown builtin algebra: "+std::string(requested)+".");
    const auto& kind=d->builtinKind;
    if(kind=="clifford"){
        if(options.positive>6||options.negative>6||options.positive+options.negative>6)throw Error("A4001","Clifford signatures require0..6 total generators (up to64 coefficients).");
        d->dimension=std::size_t(1)<<(options.positive+options.negative);d->name="Cl("+std::to_string(options.positive)+","+std::to_string(options.negative)+")";
        d->convention="orthogonal_clifford_bitmap_positive_then_negative_v1";
        for(std::size_t mask=0;mask<d->dimension;++mask){std::string name=mask?"e":"one";for(unsigned bit=0;bit<6;++bit)if(mask&(std::size_t(1)<<bit))name+=std::to_string(bit+1);d->basis.push_back(std::move(name));}
    }else{
        d->name=kind;
        if(kind=="real")d->dimension=1;
        else if(kind=="complex"||kind=="dual"||kind=="split_complex")d->dimension=2;
        else if(kind=="quaternion"||kind=="split_quaternion"||kind=="matrix2")d->dimension=4;
        else if(kind=="lie_so3"||kind=="jordan_sym2")d->dimension=3;
        else if(kind=="sedenion")d->dimension=16;
        else d->dimension=8;
        d->basis={"one"};
        if(kind=="dual")d->basis.push_back("eps");
        else if(kind=="matrix2")d->basis={"E11","E12","E21","E22"};
        else if(kind=="lie_so3")d->basis={"x","y","z"};
        else if(kind=="jordan_sym2")d->basis={"E11","S12","E22"};
        else if(kind=="split_complex")d->basis.push_back("j");
        else if(kind=="dual_quaternion")d->basis={"one","i","j","k","eps","eps_i","eps_j","eps_k"};
        else if(d->dimension<=4){if(d->dimension>1)d->basis.push_back("i");if(d->dimension==4){d->basis.push_back("j");d->basis.push_back("k");}}
        else{for(std::size_t i=1;i<d->dimension;++i)d->basis.push_back("e"+std::to_string(i));}
        d->convention=(kind=="octonion"||kind=="sedenion")?"cayley_dickson_baez_from_hamilton_v1":kind=="split_quaternion"?"hamilton_i_negative_j_positive_v1":"scalar_first_"+kind+"_v1";
    }
    d->coefficientDomain="rational";const auto n=d->dimension;d->table.assign(n*n*n,0);
    std::vector<double> left(n,0),right(n,0),out(n,0);
    for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j){
        if(kind=="clifford"){
            int sign=1;for(unsigned bit=0;bit<options.positive+options.negative;++bit)if(i&(std::size_t(1)<<bit)){
                if(std::popcount(static_cast<unsigned>(j&((std::size_t(1)<<bit)-1)))%2)sign=-sign;
                if((j&(std::size_t(1)<<bit))&&bit>=options.positive)sign=-sign;
            }
            d->table[slot(n,i,j,i^j)]=sign;
        }else{
            left[i]=1;right[j]=1;builtinProduct(kind,left,right,out);left[i]=0;right[j]=0;
            for(std::size_t k=0;k<n;++k)d->table[slot(n,i,j,k)]=out[k];
        }
    }
    for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j)for(std::size_t k=0;k<n;++k)if(const auto value=d->table[slot(n,i,j,k)];value!=0)d->terms.push_back({i,j,k,value,rationalInteger(value)});
    matrixMetadata(*d);
    const bool commutative=n<=2||kind=="jordan_sym2";
    const bool associative=kind!="octonion"&&kind!="sedenion"&&kind!="lie_so3"&&kind!="jordan_sym2";
    const bool alternative=associative||kind=="octonion";
    bool division=kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion";
    if(kind=="clifford")division=(options.positive==0&&options.negative<=2);
    auto& p=d->properties;p.unital=exact(kind!="lie_so3","Builtin unit construction or explicitly nonunital Lie product.");p.commutative=exact(commutative,"Builtin multiplication identities.");p.associative=exact(associative,"Builtin construction/theorem or exact counterexample.");p.alternative=exact(alternative,"Builtin construction/theorem or exact counterexample.");p.powerAssociative=exact(true,"Associative, quadratic Cayley-Dickson, monogenic Lie, or special Jordan construction.");p.conjugation=exact(true,"Builtin involutive anti-automorphism.");p.positiveMetric=exact(true,"Explicit positive coefficient/Frobenius metric; multiplicativity is not implied.");p.division=exact(division,"Division property from the selected builtin family/signature; reciprocal existence is distinct.");
    finishDefinition(*d);d->id="builtin/"+d->name+"/v1/"+d->hash;d->sourceHash=sha256("StudioBuiltinAlgebra-v1:"+d->hash);return d;
}
}
