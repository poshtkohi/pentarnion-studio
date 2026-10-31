#include "universal_internal.hpp"
#include <Eigen/Cholesky>
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace penta::algebra::universal {
namespace {
using Json=nlohmann::json;
using Integer=boost::multiprecision::cpp_int;
using Rational=boost::multiprecision::cpp_rational;
struct ProofBudget {};
void cancelled(const ImportOptions& options){if(options.cancellation&&options.cancellation->load(std::memory_order_relaxed))throw Error("A4026","Algebra table import/verification was cancelled.");}
void require(bool okay,const std::string& message){if(!okay)throw Error("A4020",message);}
void fields(const Json& object,std::initializer_list<std::string_view> names,const char* where){require(object.is_object(),std::string(where)+" must be an object.");for(auto it=object.begin();it!=object.end();++it)require(std::find(names.begin(),names.end(),it.key())!=names.end(),std::string("Unknown ")+where+" field: "+it.key()+".");}
std::size_t index(const Json& value,std::size_t maximum,const char* what){require(value.is_number_integer()&&!value.is_boolean(),std::string(what)+" must be an integer.");if(value.is_number_unsigned()){const auto v=value.get<std::uint64_t>();require(v<maximum,std::string(what)+" is outside the declared basis.");return static_cast<std::size_t>(v);}const auto v=value.get<std::int64_t>();require(v>=0&&static_cast<std::uint64_t>(v)<maximum,std::string(what)+" is outside the declared basis.");return static_cast<std::size_t>(v);}
std::size_t bits(const Integer& integer){const Integer value=integer<0?-integer:integer;return value==0?0:static_cast<std::size_t>(boost::multiprecision::msb(value))+1;}
Integer integer(std::string_view text,const ImportOptions& options){
    require(!text.empty()&&text.size()<=8192,"Exact integer literal is empty or too long.");std::size_t i=0;bool negative=false;if(text[0]=='+'||text[0]=='-'){negative=text[0]=='-';++i;}require(i<text.size(),"Exact integer literal has no digits.");Integer result=0;
    for(;i<text.size();++i){const auto c=text[i];require(c>='0'&&c<='9',"Exact coefficients accept only integer or numerator/denominator strings.");result*=10;result+=c-'0';if((i&63u)==0){cancelled(options);require(bits(result)<=options.maxRationalBits,"Exact coefficient exceeds the rational bit allowance.");}}
    require(bits(result)<=options.maxRationalBits,"Exact coefficient exceeds the rational bit allowance.");return negative?-result:result;
}
Rational rational(std::string_view text,const ImportOptions& options){const auto slash=text.find('/');if(slash==std::string_view::npos)return Rational(integer(text,options));require(text.find('/',slash+1)==std::string_view::npos,"Exact rational coefficient has multiple division marks.");const auto numerator=integer(text.substr(0,slash),options),denominator=integer(text.substr(slash+1),options);require(denominator!=0,"Exact rational denominator cannot be zero.");return Rational(numerator)/denominator;}
std::string spelling(const Rational& value){const auto n=boost::multiprecision::numerator(value),d=boost::multiprecision::denominator(value);return n.str()+(d==1?std::string():"/"+d.str());}
struct Scalar {double value=0;std::string exact;};
Scalar scalar(const Json& value,bool exact,const ImportOptions& options){
    if(exact){require(value.is_string()||value.is_number_integer(),"Rational tables use integer JSON values or exact rational strings; decimal JSON numbers require coefficient_domain=floating.");const auto number=rational(value.is_string()?value.get<std::string>():value.dump(),options);const auto wide=number.convert_to<long double>();return {detail::finite(wide),spelling(number)};}
    require(value.is_number()&&!value.is_boolean(),"Floating tables require finite numeric coefficients.");const auto number=value.get<double>();require(std::isfinite(number),"Floating coefficients must be finite.");return {number==0?0:number,{}};
}
bool identifier(std::string_view text){if(text.empty()||text.size()>64)return false;const auto first=text[0];if(!((first>='A'&&first<='Z')||(first>='a'&&first<='z')||first=='_'))return false;for(const auto c:text)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return false;return true;}
bool label(std::string_view text){return !text.empty()&&text.size()<=256&&std::none_of(text.begin(),text.end(),[](unsigned char c){return c<32||c==127;});}
struct Budget {
    const ImportOptions& options;std::size_t maximum,used=0;
    void step(){if(used>=maximum)throw ProofBudget{};++used;if((used&255u)==0)cancelled(options);}
    template<class Number>void bound(const Number& number){if constexpr(std::is_same_v<Number,Rational>){if(bits(boost::multiprecision::numerator(number))>4*options.maxRationalBits||bits(boost::multiprecision::denominator(number))>4*options.maxRationalBits)throw ProofBudget{};}else if(!std::isfinite(number))throw ProofBudget{};}
};
template<class Number>struct Product {std::size_t output;Number value;};
template<class Number>class Check {
    const Definition::Data& data_;const ImportOptions& options_;std::size_t n_;
    std::vector<std::vector<Product<Number>>> table_;
    Number number(double value,const std::string& text)const{if constexpr(std::is_same_v<Number,Rational>)return rational(text,options_);else return static_cast<long double>(value);}
    std::vector<Number> metadata(const std::vector<double>& values,const std::vector<std::string>& exact)const{std::vector<Number> out;out.reserve(values.size());for(std::size_t i=0;i<values.size();++i)out.push_back(number(values[i],exact.empty()?std::string():exact[i]));return out;}
    bool equal(const Number& a,const Number& b)const{if constexpr(std::is_same_v<Number,Rational>)return a==b;else return std::abs(a-b)<=options_.identityTolerance*(1+std::abs(a)+std::abs(b));}
    void accumulate(Number& destination,const Number& term,Budget& budget,bool negative=false)const{budget.step();if(negative)destination-=term;else destination+=term;budget.bound(destination);}
    std::vector<Number> associator(std::size_t a,std::size_t b,std::size_t c,Budget& budget)const{std::vector<Number> out(n_,0);for(const auto& x:table_[a*n_+b])for(const auto& y:table_[x.output*n_+c])accumulate(out[y.output],x.value*y.value,budget);for(const auto& x:table_[b*n_+c])for(const auto& y:table_[a*n_+x.output])accumulate(out[y.output],x.value*y.value,budget,true);return out;}
    Property yes(const std::string& detail)const{return {true,std::is_same_v<Number,Rational>?Evidence::ProvedExact:Evidence::NumericalEvidence,detail};}
    Property no(const std::string& detail)const{return {false,Evidence::Failed,detail};}
public:
    Check(const Definition::Data& data,const ImportOptions& options):data_(data),options_(options),n_(data.dimension),table_(n_*n_){for(const auto& term:data.terms)table_[term.left*n_+term.right].push_back({term.output,number(term.coefficient,term.exact)});}
    Property unit()const{
        if(data_.unit.empty())return {false,Evidence::Unknown,"No unit vector was supplied; no global unit search is implied."};
        const auto u=metadata(data_.unit,data_.unitExact);std::vector<Number> left(n_*n_,0),right(n_*n_,0);Budget budget{options_,2*data_.terms.size()+1};
        try{for(const auto& term:data_.terms){const auto c=number(term.coefficient,term.exact);accumulate(left[term.right*n_+term.output],u[term.left]*c,budget);accumulate(right[term.left*n_+term.output],u[term.right]*c,budget);}}
        catch(const ProofBudget&){throw Error("A4021","Exact unit verification exceeded its intermediate rational budget.");}
        for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j)require(equal(left[i*n_+j],Number(i==j))&&equal(right[i*n_+j],Number(i==j)),"Declared unit vector fails left/right identity checks.");
        return yes("Declared unit checked on every basis vector.");
    }
    Property commutative(Budget& budget)const{
        for(std::size_t i=0;i<n_;++i)for(std::size_t j=i+1;j<n_;++j){std::vector<Number> difference(n_,0);for(const auto& term:table_[i*n_+j])accumulate(difference[term.output],term.value,budget);for(const auto& term:table_[j*n_+i])accumulate(difference[term.output],term.value,budget,true);for(const auto& value:difference)if(!equal(value,Number(0)))return no("Noncommuting basis pair("+std::to_string(i)+","+std::to_string(j)+").");}
        return yes("All ordered basis products agree after swapping operands.");
    }
    Property associative(Budget& budget)const{
        for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j)for(std::size_t k=0;k<n_;++k){budget.step();const auto value=associator(i,j,k,budget);for(const auto& x:value)if(!equal(x,Number(0)))return no("Nonzero basis associator("+std::to_string(i)+","+std::to_string(j)+","+std::to_string(k)+").");}
        return yes("All trilinear basis associators vanish.");
    }
    Property alternative(Budget& budget)const{
        // Full polarization, not merely [e_i,e_i,e_j]=[e_j,e_i,e_i]=0.
        for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j)for(std::size_t k=0;k<n_;++k){budget.step();const auto a=associator(i,j,k,budget),b=associator(j,i,k,budget),c=associator(i,k,j,budget);for(std::size_t q=0;q<n_;++q)if(!equal(a[q]+b[q],Number(0))||!equal(a[q]+c[q],Number(0)))return no("Polarized alternativity fails at("+std::to_string(i)+","+std::to_string(j)+","+std::to_string(k)+").");}
        return yes("Full polarized left/right alternativity identities hold.");
    }
    Property conjugation(Budget& general)const{
        if(data_.conjugation.empty())return {false,Evidence::Unknown,"No conjugation map was supplied."};
        const auto s=metadata(data_.conjugation,data_.conjugationExact);
        Budget mandatory{options_,n_*n_*n_+1};
        try{for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j){Number total=0;for(std::size_t k=0;k<n_;++k)accumulate(total,s[i*n_+k]*s[k*n_+j],mandatory);require(equal(total,Number(i==j)),"Declared conjugation map is not an involution.");}}
        catch(const ProofBudget&){throw Error("A4021","Conjugation involution verification exceeded its intermediate rational budget.");}
        for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j){std::vector<Number> lhs(n_,0),rhs(n_,0);for(const auto& term:table_[i*n_+j])for(std::size_t k=0;k<n_;++k)if(s[k*n_+term.output]!=0)accumulate(lhs[k],s[k*n_+term.output]*term.value,general);for(std::size_t a=0;a<n_;++a)if(s[a*n_+j]!=0)for(std::size_t b=0;b<n_;++b)if(s[b*n_+i]!=0)for(const auto& term:table_[a*n_+b])accumulate(rhs[term.output],s[a*n_+j]*s[b*n_+i]*term.value,general);for(std::size_t k=0;k<n_;++k)require(equal(lhs[k],rhs[k]),"Declared conjugation fails the product-reversing identity.");}
        return yes("Supplied map is an involutive anti-automorphism on all basis products.");
    }
    Property division()const{for(std::size_t i=0;i<n_;++i)for(std::size_t j=0;j<n_;++j)if(table_[i*n_+j].empty())return no("Nonzero basis vectors("+std::to_string(i)+","+std::to_string(j)+") have zero product.");return {false,Evidence::Unknown,"Absence of sampled zero divisors does not establish a division algebra."};}
};

Property unknown(const std::string& what){return {false,Evidence::Unknown,what+" verification was not completed within the explicit proof-work/rational allowance."};}
template<class Number>void properties(Definition::Data& data,const ImportOptions& options){
    Check<Number> checker(data,options);auto& p=data.properties;Budget budget{options,options.maxIdentityOperations};p.unital=checker.unit();
    try{p.commutative=checker.commutative(budget);}catch(const ProofBudget&){p.commutative=unknown("Commutativity");}
    try{p.associative=checker.associative(budget);}catch(const ProofBudget&){p.associative=unknown("Associativity");}
    if(p.associative.value){p.alternative={true,p.associative.evidence,"Associativity implies alternativity."};p.powerAssociative={true,p.associative.evidence,"Associativity implies power associativity."};}
    else{try{p.alternative=checker.alternative(budget);}catch(const ProofBudget&){p.alternative=unknown("Alternativity");}p.powerAssociative=p.alternative.value?Property{true,p.alternative.evidence,"Alternativity implies power associativity."}:Property{false,Evidence::Unknown,"General power associativity has not been established."};}
    try{p.conjugation=checker.conjugation(budget);}catch(const ProofBudget&){p.conjugation=unknown("Conjugation anti-automorphism");}
    p.division=checker.division();
}

void claims(Definition::Data& data,const Json& input){
    if(!input.contains("properties"))return;
    const auto& declared=input.at("properties");fields(declared,{"unital","commutative","associative","alternative","power_associative","conjugation","positive_metric","division"},"properties");
    std::map<std::string,Property*> values{{"unital",&data.properties.unital},{"commutative",&data.properties.commutative},{"associative",&data.properties.associative},{"alternative",&data.properties.alternative},{"power_associative",&data.properties.powerAssociative},{"conjugation",&data.properties.conjugation},{"positive_metric",&data.properties.positiveMetric},{"division",&data.properties.division}};
    for(auto it=declared.begin();it!=declared.end();++it){require(it.value().is_boolean(),"Declared algebra properties must be booleans.");auto& result=*values.at(it.key());const bool expected=it.value().get<bool>();if(result.evidence==Evidence::Unknown){result.evidence=Evidence::Declared;result.value=expected;result.detail="User declaration only; "+result.detail;}else require(result.value==expected,"Declared property contradicts verification: "+it.key()+".");}
}
}

DefinitionPtr fromJson(std::string_view bytes,const ImportOptions& options){
    require(options.maxBytes>=1&&options.maxBytes<=64*1024*1024&&options.maxDimension>=1&&options.maxDimension<=64&&options.maxTerms<=262144&&options.maxIdentityOperations<=1000000000&&options.maxRationalBits>=16&&options.maxRationalBits<=16384&&std::isfinite(options.identityTolerance)&&options.identityTolerance>0&&options.identityTolerance<1,"Invalid algebra importer bounds.");
    require(bytes.size()<=options.maxBytes,"Algebra file exceeds its byte allowance.");cancelled(options);
    try{
        std::vector<std::set<std::string>> keys;std::size_t nodes=0;
        const auto input=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){cancelled(options);require(depth<=64,"Algebra JSON nesting exceeds64 levels.");require(++nodes<=2000000,"Algebra JSON node allowance exceeded.");if(event==Json::parse_event_t::object_start)keys.emplace_back();else if(event==Json::parse_event_t::object_end){require(!keys.empty(),"Invalid JSON object nesting.");keys.pop_back();}else if(event==Json::parse_event_t::key){require(!keys.empty(),"Invalid JSON key nesting.");const auto key=value.get<std::string>();require(keys.back().insert(key).second,"Duplicate JSON object field: "+key+".");}return true;});
        fields(input,{"format","version","name","dimension","basis","coefficient_domain","unit","products","tensor","omitted_products","conjugation","metric","convention","properties"},"algebra");
        require(input.contains("format")&&input.at("format")=="StudioAlgebraDefinition","Expected format StudioAlgebraDefinition.");require(input.contains("version")&&input.at("version").is_number_integer()&&input.at("version")==1,"Unsupported algebra definition version.");
        require(input.contains("name")&&input.at("name").is_string()&&label(input.at("name").get<std::string>()),"Algebra name must be a bounded printable label.");
        require(input.contains("dimension"),"Algebra dimension is required.");const auto n=index(input.at("dimension"),options.maxDimension+1,"Algebra dimension");require(n>0,"Algebra dimension must be positive.");
        auto data=std::make_shared<Definition::Data>();data->name=input.at("name").get<std::string>();data->dimension=n;
        require(input.contains("basis")&&input.at("basis").is_array()&&input.at("basis").size()==n,"Basis list must match the declared dimension.");std::set<std::string> unique;
        for(const auto& basis:input.at("basis")){require(basis.is_string(),"Basis names must be strings.");const auto name=basis.get<std::string>();require(identifier(name)&&unique.insert(name).second,"Basis names must be unique bounded ASCII identifiers.");data->basis.push_back(name);}
        require(input.contains("coefficient_domain")&&input.at("coefficient_domain").is_string(),"A coefficient_domain of rational or floating is required.");data->coefficientDomain=input.at("coefficient_domain").get<std::string>();require(data->coefficientDomain=="rational"||data->coefficientDomain=="floating","Unsupported coefficient domain; use rational or floating.");const bool exact=data->coefficientDomain=="rational";
        require(input.contains("omitted_products")&&input.at("omitted_products").is_string(),"Explicit omitted_products=zero or error is required.");const auto omitted=input.at("omitted_products").get<std::string>();require(omitted=="zero"||omitted=="error","Invalid omitted-product policy.");
        data->convention="user_ordered_structure_constants_v1";if(input.contains("convention")){require(input.at("convention").is_string()&&label(input.at("convention").get<std::string>()),"Convention must be a bounded printable label.");data->convention=input.at("convention").get<std::string>();}
        data->table.assign(n*n*n,0);std::set<std::size_t> seenTerms,seenProducts;
        auto term=[&](std::size_t i,std::size_t j,std::size_t k,const Json& coefficient){cancelled(options);const auto id=detail::slot(n,i,j,k);require(seenTerms.insert(id).second,"A multiplication coefficient occurs more than once.");require(seenTerms.size()<=options.maxTerms,"Multiplication table exceeds its term allowance.");const auto number=scalar(coefficient,exact,options);data->table[id]=number.value;if(number.value!=0)data->terms.push_back({i,j,k,number.value,number.exact});};
        require(input.contains("products")!=input.contains("tensor"),"Supply exactly one sparse products list or dense tensor.");
        if(input.contains("products")){
            require(input.at("products").is_array()&&input.at("products").size()<=n*n,"Sparse product list exceeds the ordered basis-pair count.");
            for(const auto& product:input.at("products")){fields(product,{"left","right","terms"},"product");require(product.contains("left")&&product.contains("right")&&product.contains("terms"),"A product needs left,right and terms.");const auto i=index(product.at("left"),n,"Left basis index"),j=index(product.at("right"),n,"Right basis index");require(seenProducts.insert(i*n+j).second,"An ordered basis product occurs more than once.");require(product.at("terms").is_array()&&product.at("terms").size()<=n,"A product must be a bounded list of output/coefficient pairs.");for(const auto& entry:product.at("terms")){require(entry.is_array()&&entry.size()==2,"A product term is [output_index,coefficient].");term(i,j,index(entry[0],n,"Output basis index"),entry[1]);}}
            require(omitted=="zero"||seenProducts.size()==n*n,"omitted_products=error requires every ordered product, including explicit zero products.");
        }else{
            const auto& tensor=input.at("tensor");require(tensor.is_array()&&tensor.size()==n,"Dense tensor shape must be n-by-n-by-n in left/right/output order.");for(std::size_t i=0;i<n;++i){require(tensor[i].is_array()&&tensor[i].size()==n,"Dense tensor second dimension is invalid.");for(std::size_t j=0;j<n;++j){require(tensor[i][j].is_array()&&tensor[i][j].size()==n,"Dense tensor output dimension is invalid.");for(std::size_t k=0;k<n;++k)term(i,j,k,tensor[i][j][k]);}}
        }
        std::sort(data->terms.begin(),data->terms.end(),[](const Term& a,const Term& b){return std::tie(a.left,a.right,a.output)<std::tie(b.left,b.right,b.output);});
        auto metadata=[&](const char* key,std::vector<double>& values,std::vector<std::string>& spellings,bool matrix){if(!input.contains(key)||input.at(key).is_null())return;const auto& array=input.at(key);require(array.is_array()&&array.size()==n,std::string(key)+" shape must match the algebra dimension.");for(std::size_t i=0;i<n;++i){if(matrix){require(array[i].is_array()&&array[i].size()==n,std::string(key)+" must be an n-by-n matrix.");for(std::size_t j=0;j<n;++j){const auto number=scalar(array[i][j],exact,options);values.push_back(number.value);if(exact)spellings.push_back(number.exact);}}else{const auto number=scalar(array[i],exact,options);values.push_back(number.value);if(exact)spellings.push_back(number.exact);}}};
        metadata("unit",data->unit,data->unitExact,false);metadata("conjugation",data->conjugation,data->conjugationExact,true);metadata("metric",data->metric,data->metricExact,true);
        if(!data->metric.empty()){
            Eigen::MatrixXd matrix(n,n);double maximum=0;for(std::size_t i=0;i<n;++i)for(std::size_t j=0;j<n;++j){const auto a=data->metric[i*n+j],b=data->metric[j*n+i];if(exact)require(data->metricExact[i*n+j]==data->metricExact[j*n+i],"Metric must be exactly symmetric.");else require(std::abs(static_cast<long double>(a)-b)<=options.identityTolerance*(1+std::abs(static_cast<long double>(a))+std::abs(static_cast<long double>(b))),"Metric must be symmetric within the declared numerical tolerance.");matrix(static_cast<Eigen::Index>(i),static_cast<Eigen::Index>(j))=a;maximum=std::max(maximum,std::abs(a));}
            require(maximum>0,"Metric must be positive definite.");matrix/=maximum;Eigen::LLT<Eigen::MatrixXd> factor(matrix);require(factor.info()==Eigen::Success,"Metric is not numerically positive definite.");data->properties.positiveMetric={true,Evidence::NumericalEvidence,"Supplied symmetric metric passed scaled binary64 Cholesky; no composition-norm identity is inferred."};
        }else data->properties.positiveMetric={false,Evidence::Unknown,"No metric was supplied."};
        if(exact)properties<Rational>(*data,options);else properties<long double>(*data,options);
        claims(*data,input);detail::finishDefinition(*data);data->id="table/"+data->hash;data->sourceHash=detail::sha256(bytes);data->sourceJson=bytes;return std::make_shared<const Definition>(std::move(data));
    }catch(const Json::exception& error){throw Error("A4020",std::string("Invalid algebra JSON: ")+error.what());}
}
}
