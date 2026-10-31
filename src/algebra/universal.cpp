#include "universal_internal.hpp"
#include <Eigen/SVD>
#include <algorithm>
#include <cmath>
#include <limits>

namespace penta::algebra::universal {
Error::Error(std::string value,const std::string& message):std::runtime_error(message),code(std::move(value)){}
std::string_view evidenceName(Evidence evidence)noexcept{switch(evidence){case Evidence::Declared:return "declared";case Evidence::ProvedExact:return "proved_exact";case Evidence::NumericalEvidence:return "numerical_evidence";case Evidence::Failed:return "failed";default:return "unknown";}}
namespace detail {
double finite(long double value){
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<double>::max())throw Error("A4002","Algebra arithmetic produced a nonfinite/unrepresentable coefficient.");
    const auto result=static_cast<double>(value);
    if(value!=0&&result==0)throw Error("A4002","A nonzero algebra coefficient underflowed binary64.");
    return result;
}
std::size_t slot(std::size_t n,std::size_t i,std::size_t j,std::size_t k){return (i*n+j)*n+k;}
void finishDefinition(Definition::Data& d){
    using Json=nlohmann::json;Json products=Json::array();
    std::size_t currentLeft=std::numeric_limits<std::size_t>::max(),currentRight=currentLeft;
    for(const auto& term:d.terms){
        if(term.left!=currentLeft||term.right!=currentRight){products.push_back({{"left",term.left},{"right",term.right},{"terms",Json::array()}});currentLeft=term.left;currentRight=term.right;}
        products.back()["terms"].push_back(Json::array({term.output,term.exact.empty()?Json(term.coefficient):Json(term.exact)}));
    }
    auto vector=[&](const std::vector<double>& values,const std::vector<std::string>& exact){Json out=Json::array();for(std::size_t i=0;i<values.size();++i)out.push_back(exact.empty()?Json(values[i]):Json(exact[i]));return out;};
    auto matrix=[&](const std::vector<double>& values,const std::vector<std::string>& exact){if(values.empty())return Json(nullptr);Json rows=Json::array();for(std::size_t i=0;i<d.dimension;++i){Json row=Json::array();for(std::size_t j=0;j<d.dimension;++j){const auto k=i*d.dimension+j;row.push_back(exact.empty()?Json(values[k]):Json(exact[k]));}rows.push_back(std::move(row));}return rows;};
    const Json canonical={{"format","StudioAlgebraDefinition"},{"version",1},{"name",d.name},{"dimension",d.dimension},{"basis",d.basis},{"coefficient_domain",d.coefficientDomain},{"unit",d.unit.empty()?Json(nullptr):vector(d.unit,d.unitExact)},{"omitted_products","zero"},{"products",products},{"conjugation",matrix(d.conjugation,d.conjugationExact)},{"metric",matrix(d.metric,d.metricExact)},{"convention",d.convention}};
    d.hash=sha256(canonical.dump());d.canonical=canonical.dump(2);
}
}

Definition::Definition(std::shared_ptr<const Data> data):data_(std::move(data)){if(!data_||data_->dimension<1||data_->dimension>64)throw Error("A4001","Invalid immutable algebra definition.");}
const std::string& Definition::name()const noexcept{return data_->name;}
const std::string& Definition::id()const noexcept{return data_->id;}
const std::string& Definition::hash()const noexcept{return data_->hash;}
const std::string& Definition::sourceHash()const noexcept{return data_->sourceHash;}
const std::string& Definition::sourceJson()const noexcept{return data_->sourceJson;}
const std::string& Definition::builtinKind()const noexcept{return data_->builtinKind;}
const std::string& Definition::convention()const noexcept{return data_->convention;}
const std::string& Definition::coefficientDomain()const noexcept{return data_->coefficientDomain;}
std::size_t Definition::dimension()const noexcept{return data_->dimension;}
const std::vector<std::string>& Definition::basis()const noexcept{return data_->basis;}
const std::vector<Term>& Definition::terms()const noexcept{return data_->terms;}
const std::vector<double>& Definition::unit()const noexcept{return data_->unit;}
const std::vector<double>& Definition::conjugation()const noexcept{return data_->conjugation;}
const std::vector<double>& Definition::metric()const noexcept{return data_->metric;}
const Properties& Definition::properties()const noexcept{return data_->properties;}
std::size_t Definition::retainedBytes()const noexcept{
    const auto& d=*data_;std::size_t bytes=sizeof(Definition)+sizeof(Data);
    for(const auto* text:{&d.name,&d.id,&d.hash,&d.sourceHash,&d.sourceJson,&d.builtinKind,&d.convention,&d.coefficientDomain,&d.canonical})bytes+=text->capacity()+1;
    for(const auto* list:{&d.basis,&d.unitExact,&d.conjugationExact,&d.metricExact}){bytes+=list->capacity()*sizeof(std::string);for(const auto& text:*list)bytes+=text.capacity()+1;}
    bytes+=d.terms.capacity()*sizeof(Term);for(const auto& term:d.terms)bytes+=term.exact.capacity()+1;
    for(const auto* list:{&d.table,&d.unit,&d.conjugation,&d.metric})bytes+=list->capacity()*sizeof(double);
    for(const auto* property:{&d.properties.unital,&d.properties.commutative,&d.properties.associative,&d.properties.alternative,&d.properties.powerAssociative,&d.properties.conjugation,&d.properties.positiveMetric,&d.properties.division})bytes+=property->detail.capacity()+1;
    return bytes;
}
double Definition::coefficient(std::size_t i,std::size_t j,std::size_t k)const{const auto n=dimension();if(i>=n||j>=n||k>=n)throw Error("A4001","Algebra coefficient index is outside its basis.");return data_->table[detail::slot(n,i,j,k)];}
std::string Definition::json()const{return data_->canonical;}
DefinitionPtr builtin(std::string_view name,const BuiltinOptions& options){return std::make_shared<const Definition>(detail::makeBuiltin(name,options));}
std::vector<std::string> builtinNames(){return {"real","complex","quaternion","octonion","sedenion","dual","dual_quaternion","split_complex","split_quaternion","clifford","matrix2","lie_so3","jordan_sym2"};}

Element::Element(DefinitionPtr definition,std::vector<double> coefficients):definition_(std::move(definition)),coefficients_(std::move(coefficients)){
    if(!definition_||coefficients_.size()!=definition_->dimension())throw Error("A4001","An algebra element must contain every coefficient of its definition.");
    if(!std::all_of(coefficients_.begin(),coefficients_.end(),[](double value){return std::isfinite(value);}))throw Error("A4002","Algebra coefficients must be finite real binary64 values.");
}
Element Element::scalar(DefinitionPtr definition,double value){if(!definition||definition->unit().empty())throw Error("A4003","Real scalar embedding requires a declared validated unit vector.");std::vector<double> result;result.reserve(definition->dimension());for(const auto x:definition->unit())result.push_back(detail::finite(static_cast<long double>(x)*value));return Element(std::move(definition),std::move(result));}
Element Element::basis(DefinitionPtr definition,std::size_t index){if(!definition||index>=definition->dimension())throw Error("A4001","Basis index is outside the algebra dimension.");std::vector<double> result(definition->dimension(),0);result[index]=1;return Element(std::move(definition),std::move(result));}
namespace {
void compatible(const Element& a,const Element& b){if(a.definition()->hash()!=b.definition()->hash())throw Error("A4004","Arithmetic cannot mix different immutable algebra definitions. Use an explicit checked coordinate map.");}
Element combine(const Element& a,const Element& b,int sign){compatible(a,b);std::vector<double> result(a.coefficients().size());for(std::size_t i=0;i<result.size();++i)result[i]=detail::finite(static_cast<long double>(a.coefficients()[i])+sign*static_cast<long double>(b.coefficients()[i]));return Element(a.definition(),std::move(result));}
}
Element add(const Element& a,const Element& b){return combine(a,b,1);}
Element subtract(const Element& a,const Element& b){return combine(a,b,-1);}
Element scale(const Element& a,double factor){if(!std::isfinite(factor))throw Error("A4002","Algebra scaling requires a finite factor.");std::vector<double> result(a.coefficients().size());for(std::size_t i=0;i<result.size();++i)result[i]=detail::finite(static_cast<long double>(a.coefficients()[i])*factor);return Element(a.definition(),std::move(result));}
Element divideByReal(const Element& a,double divisor){
    if(!std::isfinite(divisor)||divisor==0)throw Error("A4002","Algebra real division requires a finite nonzero divisor.");
    std::vector<double> result(a.coefficients().size());
    for(std::size_t i=0;i<result.size();++i)result[i]=detail::finite(static_cast<long double>(a.coefficients()[i])/divisor);
    return Element(a.definition(),std::move(result));
}
Element multiply(const Element& a,const Element& b){
    compatible(a,b);const auto n=a.definition()->dimension();std::vector<double> result(n,0);const auto& kind=a.definition()->builtinKind();
    if(!kind.empty()&&kind!="clifford")detail::builtinProduct(kind,a.coefficients(),b.coefficients(),result);
    else{std::vector<long double> sum(n,0);for(const auto& term:a.definition()->terms())sum[term.output]+=static_cast<long double>(a.coefficients()[term.left])*b.coefficients()[term.right]*term.coefficient;for(std::size_t i=0;i<n;++i)result[i]=detail::finite(sum[i]);}
    return Element(a.definition(),std::move(result));
}
Element conjugate(const Element& value){const auto n=value.definition()->dimension();const auto& matrix=value.definition()->conjugation();if(matrix.empty())throw Error("A4003","This algebra has no explicitly supplied conjugation map.");std::vector<double> result(n);for(std::size_t i=0;i<n;++i){long double sum=0;for(std::size_t j=0;j<n;++j)sum+=static_cast<long double>(matrix[i*n+j])*value.coefficients()[j];result[i]=detail::finite(sum);}return Element(value.definition(),std::move(result));}
Element commutator(const Element& a,const Element& b){return subtract(multiply(a,b),multiply(b,a));}
Element associator(const Element& a,const Element& b,const Element& c){return subtract(multiply(multiply(a,b),c),multiply(a,multiply(b,c)));}
double coordinateNorm(const Element& value){double result=0;for(const auto x:value.coefficients())result=std::hypot(result,x);if(!std::isfinite(result))throw Error("A4002","The coefficient Euclidean norm is unrepresentable.");return result;}
double metricNormSquared(const Element& value){const auto n=value.definition()->dimension();const auto& metric=value.definition()->metric();if(metric.empty())throw Error("A4003","This algebra has no explicitly supplied positive metric.");long double result=0;for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j)result+=static_cast<long double>(value.coefficients()[i])*metric[i*n+j]*value.coefficients()[j];if(result<0)throw Error("A4002","Metric norm evaluation lost nonnegative numerical accuracy.");return detail::finite(result);}
std::vector<double> leftMatrix(const Element& a){const auto n=a.definition()->dimension();std::vector<long double> sum(n*n,0);for(const auto& t:a.definition()->terms())sum[t.output*n+t.right]+=static_cast<long double>(t.coefficient)*a.coefficients()[t.left];std::vector<double> result(n*n);for(std::size_t i=0;i<result.size();++i)result[i]=detail::finite(sum[i]);return result;}
std::vector<double> rightMatrix(const Element& a){const auto n=a.definition()->dimension();std::vector<long double> sum(n*n,0);for(const auto& t:a.definition()->terms())sum[t.output*n+t.left]+=static_cast<long double>(t.coefficient)*a.coefficients()[t.right];std::vector<double> result(n*n);for(std::size_t i=0;i<result.size();++i)result[i]=detail::finite(sum[i]);return result;}

std::string_view solveStatusName(SolveStatus status)noexcept{switch(status){case SolveStatus::Unique:return "unique";case SolveStatus::IllConditioned:return "ill_conditioned";case SolveStatus::SingularConsistent:return "singular_consistent";case SolveStatus::SingularInconsistent:return "singular_inconsistent";case SolveStatus::ResidualRejected:return "residual_rejected";default:return "nonfinite";}}
namespace {
void validateSolve(const SolveOptions& options){if(!std::isfinite(options.rankRelativeTolerance)||options.rankRelativeTolerance<=0||options.rankRelativeTolerance>=1||!std::isfinite(options.conditionLimit)||options.conditionLimit<1||!std::isfinite(options.residualTolerance)||options.residualTolerance<=0||options.residualTolerance>=1)throw Error("A4005","Invalid algebra equation-solve safeguards.");}
SolveResult solve(const Element& a,const Element& b,bool left,const SolveOptions& options){
    compatible(a,b);validateSolve(options);SolveResult result;const auto n=a.definition()->dimension();const auto raw=left?leftMatrix(a):rightMatrix(a);
    Eigen::MatrixXd matrix(n,n);Eigen::VectorXd rhs(n);double largest=0;for(std::size_t i=0;i<n;++i){rhs[static_cast<Eigen::Index>(i)]=b.coefficients()[i];for(std::size_t j=0;j<n;++j){const auto value=raw[i*n+j];largest=std::max(largest,std::abs(value));matrix(static_cast<Eigen::Index>(i),static_cast<Eigen::Index>(j))=value;}}
    if(largest==0){result.status=rhs.isZero(0)?SolveStatus::SingularConsistent:SolveStatus::SingularInconsistent;result.condition=std::numeric_limits<double>::infinity();result.relativeResidual=rhs.isZero(0)?0:1;return result;}
    matrix/=largest;rhs/=largest;if(!rhs.allFinite()){result.status=SolveStatus::NonFinite;return result;}
    for(std::size_t i=0;i<n;++i)if(b.coefficients()[i]!=0&&rhs[static_cast<Eigen::Index>(i)]==0){result.status=SolveStatus::NonFinite;return result;}
    Eigen::JacobiSVD<Eigen::MatrixXd> factor(matrix,Eigen::ComputeThinU|Eigen::ComputeThinV);factor.setThreshold(options.rankRelativeTolerance);result.rank=static_cast<std::size_t>(factor.rank());const auto& singular=factor.singularValues();result.condition=singular[singular.size()-1]>0?singular[0]/singular[singular.size()-1]:std::numeric_limits<double>::infinity();
    const Eigen::VectorXd x=factor.solve(rhs);if(!x.allFinite()){result.status=SolveStatus::NonFinite;return result;}
    long double residualSquared=0,matrixSquared=0,xSquared=0,rhsSquared=0;
    for(std::size_t i=0;i<n;++i){long double row=0;const auto ii=static_cast<Eigen::Index>(i);xSquared+=static_cast<long double>(x[ii])*x[ii];rhsSquared+=static_cast<long double>(b.coefficients()[i])*b.coefficients()[i];for(std::size_t j=0;j<n;++j){const auto value=raw[i*n+j];row+=static_cast<long double>(value)*x[static_cast<Eigen::Index>(j)];matrixSquared+=static_cast<long double>(value)*value;}const auto difference=row-b.coefficients()[i];residualSquared+=difference*difference;}
    const auto residual=std::sqrt(residualSquared),denominator=std::sqrt(matrixSquared)*std::sqrt(xSquared)+std::sqrt(rhsSquared);result.relativeResidual=static_cast<double>(denominator?residual/denominator:residual);
    if(result.rank<n){result.status=result.relativeResidual<=options.residualTolerance?SolveStatus::SingularConsistent:SolveStatus::SingularInconsistent;return result;}
    if(result.condition>options.conditionLimit){result.status=SolveStatus::IllConditioned;return result;}
    if(!std::isfinite(result.relativeResidual)||result.relativeResidual>options.residualTolerance){result.status=SolveStatus::ResidualRejected;return result;}
    std::vector<double> coefficients(n);for(std::size_t i=0;i<n;++i)coefficients[i]=x[static_cast<Eigen::Index>(i)];result.solution=Element(a.definition(),std::move(coefficients));result.status=SolveStatus::Unique;return result;
}
long double wideNorm(const Element& a){long double result=0;for(const auto x:a.coefficients())result=std::hypot(result,static_cast<long double>(x));return result;}
bool reciprocalResidual(const Element& a,const Element& candidate,double tolerance){const auto unit=Element::scalar(a.definition(),1);const auto left=subtract(multiply(a,candidate),unit),right=subtract(multiply(candidate,a),unit);const auto scale=1+wideNorm(a)*wideNorm(candidate);return wideNorm(left)<=tolerance*scale&&wideNorm(right)<=tolerance*scale;}
}
SolveResult solveLeft(const Element& a,const Element& b,const SolveOptions& options){return solve(a,b,true,options);}
SolveResult solveRight(const Element& a,const Element& b,const SolveOptions& options){return solve(a,b,false,options);}
Element inverse(const Element& value,const SolveOptions& options){
    validateSolve(options);const auto& kind=value.definition()->builtinKind();const auto& x=value.coefficients();const auto n=x.size();std::vector<double> result(n);bool candidate=false;
    if(kind=="real"||kind=="complex"||kind=="quaternion"||kind=="octonion"||kind=="sedenion"){
        long double norm=0;for(const auto component:x)norm+=static_cast<long double>(component)*component;if(norm==0)throw Error("A4006","Zero has no algebra reciprocal.");const auto conjugated=conjugate(value);for(std::size_t i=0;i<n;++i)result[i]=detail::finite(static_cast<long double>(conjugated.coefficients()[i])/norm);candidate=true;
    }else if(kind=="dual"){
        if(x[0]==0)throw Error("A4006","A dual number with zero real part has no reciprocal.");
        result[0]=detail::finite(1.0L/x[0]);result[1]=detail::finite(-static_cast<long double>(x[1])/(static_cast<long double>(x[0])*x[0]));candidate=true;
    }else if(kind=="split_complex"||kind=="split_quaternion"){
        long double norm=static_cast<long double>(x[0])*x[0];if(n==2)norm-=static_cast<long double>(x[1])*x[1];else norm+=static_cast<long double>(x[1])*x[1]-static_cast<long double>(x[2])*x[2]-static_cast<long double>(x[3])*x[3];if(norm==0)throw Error("A4006","This split-algebra element has zero quadratic determinant.");const auto conjugated=conjugate(value);for(std::size_t i=0;i<n;++i)result[i]=detail::finite(static_cast<long double>(conjugated.coefficients()[i])/norm);candidate=true;
    }else if(kind=="dual_quaternion"){
        auto h=builtin("quaternion");Element primal(h,std::vector<double>(x.begin(),x.begin()+4)),dual(h,std::vector<double>(x.begin()+4,x.end()));const auto pi=inverse(primal,options);const auto di=scale(multiply(multiply(pi,dual),pi),-1);std::copy(pi.coefficients().begin(),pi.coefficients().end(),result.begin());std::copy(di.coefficients().begin(),di.coefficients().end(),result.begin()+4);candidate=true;
    }else if(kind=="matrix2"){
        const long double determinant=static_cast<long double>(x[0])*x[3]-static_cast<long double>(x[1])*x[2];if(determinant==0)throw Error("A4006","A singular2-by2 matrix has no reciprocal.");result={detail::finite(x[3]/determinant),detail::finite(-x[1]/determinant),detail::finite(-x[2]/determinant),detail::finite(x[0]/determinant)};candidate=true;
    }else if(kind=="jordan_sym2"){
        const long double determinant=static_cast<long double>(x[0])*x[2]-static_cast<long double>(x[1])*x[1];if(determinant==0)throw Error("A4006","A singular symmetric2-by2 Jordan element has no reciprocal.");result={detail::finite(x[2]/determinant),detail::finite(-x[1]/determinant),detail::finite(x[0]/determinant)};candidate=true;
    }
    if(candidate){Element answer(value.definition(),std::move(result));if(!reciprocalResidual(value,answer,options.residualTolerance))throw Error("A4006","The canonical two-sided reciprocal failed residual validation.");return answer;}
    if(value.definition()->unit().empty())throw Error("A4003","Reciprocals require an explicitly validated unit.");
    const auto unit=Element::scalar(value.definition(),1);const auto left=solveLeft(value,unit,options),right=solveRight(value,unit,options);if(!left.solution||!right.solution)throw Error("A4006","A unique two-sided reciprocal was not established by the left/right operators.");if(coordinateNorm(subtract(*left.solution,*right.solution))>options.residualTolerance*(1+coordinateNorm(*left.solution))||!reciprocalResidual(value,*left.solution,options.residualTolerance))throw Error("A4006","Left/right reciprocal candidates disagree or fail residual validation.");return *left.solution;
}
Element power(const Element& value,std::int64_t exponent){
    if(exponent>4096||exponent< -4096)throw Error("A4001","Algebra powers are bounded to absolute exponent4096.");
    if(exponent==0)return Element::scalar(value.definition(),1);
    const Element base=exponent<0?inverse(value):value;const auto count=static_cast<std::uint64_t>(exponent<0?-exponent:exponent);Element answer=base;
    for(std::uint64_t i=1;i<count;++i)answer=multiply(answer,base);
    return answer;
}
}
