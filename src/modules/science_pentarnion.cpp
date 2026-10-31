#define PENTA_NUMERIC_MODULE_BUILD
#include "pent_symbolic_api.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using Rational=boost::multiprecision::cpp_rational;
using Integer=boost::multiprecision::cpp_int;
struct Failure:std::runtime_error{int status;Failure(int s,const std::string& message):std::runtime_error(message),status(s){}};
void message(char* out,uint64_t cap,const std::string& text){if(out&&cap){const auto n=std::min<uint64_t>(cap-1,text.size());std::memcpy(out,text.data(),n);out[n]=0;}}
struct Work {
 const penta_pent_symbolic_host_v1& host;uint64_t limit,used=0,pending=0;
 void flush(){if(host.cancelled&&host.cancelled(host.context))throw Failure(PENTA_NUMERIC_CANCELLED,"Exact symbolic calculation cancelled.");if(pending&&host.charge){char text[512]{};if(host.charge(host.context,pending,text,sizeof(text)))throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,text[0]?text:"Exact symbolic host budget exceeded.");}pending=0;}
 void spend(uint64_t n=1){if(n>limit-used)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact symbolic work budget exceeded.");used+=n;pending+=n;if(pending>=256)flush();}
};
void bounded(const Rational& value){Integer n=boost::multiprecision::numerator(value);if(n<0)n=-n;const auto d=boost::multiprecision::denominator(value);if((n!=0&&boost::multiprecision::msb(n)>4095)||boost::multiprecision::msb(d)>4095)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact scalar rational exceeds4096 bits.");}
std::string rationalText(const Rational& value){std::ostringstream out;out<<value;return out.str();}
struct Monomial {
 uint64_t radical=1;std::map<std::string,unsigned> powers;
 bool operator<(const Monomial& b)const{return radical<b.radical||(radical==b.radical&&powers<b.powers);}
};
using Scalar=std::map<Monomial,Rational>;
using Element=std::array<Scalar,5>;
Scalar constant(const Rational& value){bounded(value);return value==0?Scalar{}:Scalar{{Monomial{},value}};}
Scalar variable(const std::string& name){Monomial m;m.powers[name]=1;return {{std::move(m),Rational(1)}};}
void addTerm(Scalar& out,Monomial key,Rational value){if(value==0)return;bounded(value);auto [at,inserted]=out.try_emplace(std::move(key),0);(void)inserted;at->second+=value;bounded(at->second);if(at->second==0)out.erase(at);if(out.size()>4096)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact coefficient exceeds4096 monomials.");}
Scalar add(const Scalar& a,const Scalar& b,int sign,Work& work){Scalar out=a;for(const auto& [key,value]:b){work.spend();addTerm(out,key,sign*value);}return out;}
Scalar scale(Scalar a,const Rational& factor,Work& work){if(factor==0)return {};for(auto& [key,value]:a){(void)key;work.spend();value*=factor;bounded(value);}return a;}
Scalar multiply(const Scalar& a,const Scalar& b,Work& work){Scalar out;for(const auto& [ka,va]:a)for(const auto& [kb,vb]:b){work.spend();Monomial key=ka;const auto common=std::gcd(ka.radical,kb.radical),left=ka.radical/common,right=kb.radical/common;if(left&&right>std::numeric_limits<uint64_t>::max()/left)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact radical product exceeds64 bits.");key.radical=left*right;unsigned degree=0;for(const auto& [name,power]:kb.powers)key.powers[name]+=power;for(const auto& [name,power]:key.powers){(void)name;degree+=power;}if(degree>128)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact monomial degree exceeds128.");addTerm(out,std::move(key),va*vb*common);}return out;}
Scalar reciprocal(const Scalar& value,Work& work){work.spend();if(value.size()!=1||!value.begin()->first.powers.empty())throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact division requires a nonzero rational/radical scalar constant; variable denominators need explicit domain support.");const auto& [key,c]=*value.begin();return {{key,Rational(1)/(c*key.radical)}};}
Scalar root(uint64_t value,Work& work){if(value==0)return {};uint64_t remaining=value,radical=1;Integer outside=1;for(uint64_t factor=2;factor<=remaining/factor;++factor){work.spend();unsigned count=0;while(remaining%factor==0){remaining/=factor;++count;}for(unsigned n=0;n<count/2;++n)outside*=factor;if(count%2)radical*=factor;}if(remaining>1)radical*=remaining;Monomial key;key.radical=radical;return {{key,Rational(outside)}};}
Rational rationalConstant(const Scalar& a){if(a.empty())return 0;if(a.size()!=1||a.begin()->first.radical!=1||!a.begin()->first.powers.empty())throw Failure(PENTA_NUMERIC_ARGUMENT,"sqrt/gamma requires an exact rational constant.");return a.begin()->second;}
Scalar squareRoot(const Scalar& value,Work& work){const auto rational=rationalConstant(value);if(rational<0)throw Failure(PENTA_NUMERIC_DOMAIN,"Exact sqrt requires a nonnegative real rational.");const auto n=boost::multiprecision::numerator(rational),d=boost::multiprecision::denominator(rational);if(n>2000000000000ULL||d>2000000000000ULL)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact radical numerator/denominator exceeds2e12.");auto numerator=root(n.convert_to<uint64_t>(),work),denominator=root(d.convert_to<uint64_t>(),work);return multiply(numerator,reciprocal(denominator,work),work);}
std::string scalarText(const Scalar& value,bool native=false){if(value.empty())return "0";std::string out;for(const auto& [key,c]:value){if(!out.empty())out+='+';std::string term="("+rationalText(c)+")";if(key.radical!=1)term+="*sqrt("+std::to_string(key.radical)+")";for(const auto& [name,power]:key.powers){term+="*"+(native?"v_"+name:name);if(power!=1)term+="^"+std::to_string(power);}out+=term;if(out.size()>32768)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact coefficient text exceeds32768 bytes.");}return "("+out+")";}
Element real(const Scalar& value){Element out;out[0]=value;return out;}
bool isReal(const Element& value){return std::all_of(value.begin()+1,value.end(),[](const auto& c){return c.empty();});}
Element sum(Element a,const Element& b,int sign,Work& work){for(std::size_t n=0;n<5;++n)a[n]=add(a[n],b[n],sign,work);return a;}
bool identifier(const std::string& name){return !name.empty()&&(std::isalpha(static_cast<unsigned char>(name[0]))||name[0]=='_')&&std::all_of(name.begin()+1,name.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});}
std::set<std::string> names(const char* raw){const std::string text=raw?raw:"";if(text.size()>4096)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Symbol declarations exceed4096 bytes.");std::set<std::string> out;std::size_t at=0;while(at<text.size()){while(at<text.size()&&(text[at]==','||std::isspace(static_cast<unsigned char>(text[at]))))++at;const auto begin=at;while(at<text.size()&&text[at]!=','&&!std::isspace(static_cast<unsigned char>(text[at])))++at;if(at==begin)break;const auto name=text.substr(begin,at-begin);if(!identifier(name)||!out.insert(name).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Symbol names must be unique ASCII identifiers.");if(out.size()>64)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Declare at most64 symbols.");}return out;}
struct Algebra {
 Work& work;Rational gamma;std::set<std::string> reals,pentas;
 std::array<std::array<Element,5>,5> table;
 Algebra(Work& w,Rational g,const char* realNames,const char* pentNames):work(w),gamma(std::move(g)),reals(names(realNames)),pentas(names(pentNames)){
    const std::set<std::string> reserved{"i","j","k","ell","gamma","sqrt"};
    for(const auto& n:reals)if(reserved.contains(n)||pentas.contains(n))throw Failure(PENTA_NUMERIC_ARGUMENT,"Basis/gamma/sqrt names and conflicting scalar/Pentarnion declarations are reserved.");
    for(const auto& n:pentas){if(reserved.contains(n))throw Failure(PENTA_NUMERIC_ARGUMENT,"A Pentarnion symbol cannot be a basis/gamma/sqrt name.");for(unsigned c=0;c<5;++c){const auto coordinate=n+std::to_string(c);if(pentas.contains(coordinate)||!reals.insert(coordinate).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Pentarnion coordinate names conflict with another declaration.");}}
    for(unsigned a=0;a<5;++a){table[0][a][a]=constant(1);table[a][0][a]=constant(1);if(a)table[a][a][0]=constant(-1);}
    const Integer numerator=boost::multiprecision::numerator(gamma),denominator=boost::multiprecision::denominator(gamma);if(gamma<0||gamma>1||numerator>1000000||denominator>1000000)throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact gamma must lie in[0,1] with reduced numerator/denominator at most1e6.");
    const auto k=multiply(constant(1),reciprocal(squareRoot(constant(1+gamma*gamma),work),work),work);const auto s=scale(k,gamma,work),h=scale(root(2,work),Rational(1)/2,work);
    const auto put=[&](unsigned a,unsigned b,unsigned coordinate,const Scalar& value,int sign=1){table[a][b][coordinate]=sign==1?value:scale(value,-1,work);};
    put(1,2,3,k);put(1,2,4,s);put(1,3,2,k,-1);put(1,3,4,s,-1);put(1,4,2,h,-1);put(1,4,3,h,-1);
    put(2,1,3,k,-1);put(2,1,4,s,-1);put(2,3,1,k);put(2,3,4,s);put(2,4,1,h,-1);put(2,4,3,h,-1);
    put(3,1,2,k);put(3,1,4,s);put(3,2,1,k,-1);put(3,2,4,s,-1);put(3,4,1,h,-1);put(3,4,2,h,-1);
    put(4,1,2,h);put(4,1,3,h);put(4,2,1,h);put(4,2,3,h);put(4,3,1,h);put(4,3,2,h);
 }
 Element product(const Element& a,const Element& b){Element out;for(unsigned i=0;i<5;++i)for(unsigned j=0;j<5;++j)if(!a[i].empty()&&!b[j].empty()){const auto entry=multiply(a[i],b[j],work);for(unsigned c=0;c<5;++c)if(!table[i][j][c].empty())out[c]=add(out[c],multiply(entry,table[i][j][c],work),1,work);}return out;}
 Element atom(const std::string& name){Element out;const std::string basis[]{"1","i","j","k","ell"};for(unsigned c=1;c<5;++c)if(name==basis[c]){out[c]=constant(1);return out;}if(name=="gamma")return real(constant(gamma));if(reals.contains(name))return real(variable(name));if(pentas.contains(name)){for(unsigned c=0;c<5;++c)out[c]=variable(name+std::to_string(c));return out;}throw Failure(PENTA_NUMERIC_ARGUMENT,"Declare scalar or Pentarnion assumptions for symbol: "+name);}
};
struct Parser {
 std::string text;std::size_t at=0,depth=0,nodes=0;Algebra& algebra;
 Parser(const char* input,Algebra& a):text(input?input:""),algebra(a){if(text.empty()||text.size()>32768)throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact expansion requires1..32768 input bytes.");}
 void space(){while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;}
 bool take(char c){space();if(at<text.size()&&text[at]==c){++at;return true;}return false;}
 Element expression(){auto a=product();for(;;){if(take('+'))a=sum(std::move(a),product(),1,algebra.work);else if(take('-'))a=sum(std::move(a),product(),-1,algebra.work);else return a;}}
 Element product(){auto a=power();for(;;){if(take('*'))a=algebra.product(a,power());else if(take('/')){auto b=power();if(!isReal(b))throw Failure(PENTA_NUMERIC_ARGUMENT,"Symbolic equation division is not ordinary scalar division.");const auto inv=reciprocal(b[0],algebra.work);for(auto& coefficient:a)coefficient=multiply(coefficient,inv,algebra.work);}else return a;}}
 Element power(){auto a=primary();if(take('^')){auto exponent=primary();if(!isReal(exponent))throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact powers require a nonnegative integer literal.");const auto rational=rationalConstant(exponent[0]);if(rational<0||rational>32||boost::multiprecision::denominator(rational)!=1)throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact powers require an integer in0..32.");const auto n=rational.convert_to<unsigned>();auto result=real(constant(1));for(unsigned count=0;count<n;++count)result=algebra.product(result,a);return result;}return a;}
 Element primary(){algebra.work.spend();if(++nodes>8192||++depth>128)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact parser node/depth budget exceeded.");struct Guard{std::size_t& depth;~Guard(){--depth;}}guard{depth};space();
    if(take('+'))return primary();
    if(take('-'))return sum(Element{},primary(),-1,algebra.work);
    if(take('(')){auto value=expression();if(!take(')'))throw Failure(PENTA_NUMERIC_ARGUMENT,"Unclosed exact symbolic Group.");return value;}
    if(at<text.size()&&(std::isalpha(static_cast<unsigned char>(text[at]))||text[at]=='_')){const auto begin=at++;while(at<text.size()&&(std::isalnum(static_cast<unsigned char>(text[at]))||text[at]=='_'))++at;const auto name=text.substr(begin,at-begin);if(name=="sqrt"){if(!take('('))throw Failure(PENTA_NUMERIC_ARGUMENT,"sqrt requires parentheses.");auto value=expression();if(!take(')')||!isReal(value))throw Failure(PENTA_NUMERIC_ARGUMENT,"sqrt requires one nonnegative exact rational scalar.");return real(squareRoot(value[0],algebra.work));}return algebra.atom(name);}
    const auto begin=at;std::string digits;unsigned fractional=0;bool dot=false;while(at<text.size()&&(std::isdigit(static_cast<unsigned char>(text[at]))||text[at]=='.')){if(text[at]=='.'){if(dot)throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid exact decimal.");dot=true;}else{digits+=text[at];if(dot)++fractional;}++at;}if(begin==at||digits.empty()||digits.size()>256)throw Failure(PENTA_NUMERIC_ARGUMENT,"Expected an exact rational/decimal, declared symbol or basis element.");Integer numerator=0;for(char c:digits){numerator*=10;numerator+=c-'0';}Rational value=numerator;for(unsigned n=0;n<fractional;++n)value/=10;return real(constant(value));
 }
 Element parse(){auto out=expression();space();if(at!=text.size())throw Failure(PENTA_NUMERIC_ARGUMENT,"Unsupported exact symbolic syntax; use +,-,*,/,integer powers,sqrt and Groups.");return out;}
};
Scalar differentiate(const Scalar& input,const std::string& name,Work& work){Scalar out;for(const auto& [key,value]:input){work.spend();const auto found=key.powers.find(name);if(found==key.powers.end())continue;auto next=key;const auto power=found->second;if(--next.powers[name]==0)next.powers.erase(name);addTerm(out,std::move(next),value*power);}return out;}
Scalar substitute(const Scalar& input,const std::string& name,const Scalar& replacement,Work& work){Scalar out;for(const auto& [key,value]:input){work.spend();auto rest=key;const auto found=rest.powers.find(name);const auto count=found==rest.powers.end()?0:found->second;rest.powers.erase(name);Scalar term{{std::move(rest),value}};for(unsigned n=0;n<count;++n)term=multiply(term,replacement,work);out=add(out,term,1,work);}return out;}
double evaluate(const Scalar& input,const std::map<std::string,double>& bindings,Work& work){long double sum=0;for(const auto& [key,value]:input){work.spend();long double term=value.convert_to<long double>()*std::sqrt(static_cast<long double>(key.radical));for(const auto& [name,power]:key.powers){const auto found=bindings.find(name);if(found==bindings.end())throw Failure(PENTA_NUMERIC_ARGUMENT,"Missing real coordinate binding for "+name);for(unsigned n=0;n<power;++n)term*=found->second;}sum+=term;}if(!std::isfinite(sum)||std::abs(sum)>std::numeric_limits<double>::max())throw Failure(PENTA_NUMERIC_DOMAIN,"Exact symbolic numeric result is not finite binary64.");return static_cast<double>(sum);}
int execute(const penta_pent_symbolic_host_v1* host,const penta_pent_symbolic_request_v1* request,penta_pent_symbolic_report_v1* report,char* error,uint64_t capacity)noexcept{
 try{
    if(!host||host->struct_size<sizeof(*host)||host->abi_version!=PENTA_PENT_SYMBOLIC_ABI_V1||!request||request->struct_size<sizeof(*request)||!report||report->struct_size<sizeof(*report))throw Failure(PENTA_NUMERIC_VERSION,"Incompatible exact coefficient capability ABI.");
    Work work{*host,request->max_work};work.flush();if(!request->max_work)throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact work allowance is zero.");
    // Parse gamma in a separate scalar-only context without constructing a table.
    const std::string gammaText=request->gamma?request->gamma:"";if(gammaText.empty()||gammaText.size()>64)throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact gamma requires a short nonnegative rational string.");
    const auto slash=gammaText.find('/');const auto parseInteger=[](const std::string& text)->Integer{if(text.empty()||!std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isdigit(c);}))throw Failure(PENTA_NUMERIC_ARGUMENT,"Gamma uses exact integer or numerator/denominator syntax.");Integer out=0;for(char c:text){out*=10;out+=c-'0';}return out;};
    Rational gamma=parseInteger(gammaText.substr(0,slash));if(slash!=std::string::npos){const auto denominator=parseInteger(gammaText.substr(slash+1));if(denominator==0)throw Failure(PENTA_NUMERIC_ARGUMENT,"Exact gamma denominator cannot be zero.");gamma/=denominator;}
    Algebra algebra(work,gamma,request->real_symbols,request->pent_symbols);auto value=Parser(request->expression,algebra).parse();const std::string variableName=request->variable?request->variable:"";
    if(request->operation==PENTA_PENT_DIFF||request->operation==PENTA_PENT_SUBS){if(!algebra.reals.contains(variableName))throw Failure(PENTA_NUMERIC_ARGUMENT,"Differentiate/substitute one declared real coefficient coordinate, such as p0.");if(request->operation==PENTA_PENT_DIFF){for(auto& coefficient:value)coefficient=differentiate(coefficient,variableName,work);}else{const auto replacement=Parser(request->replacement,algebra).parse();if(!isReal(replacement))throw Failure(PENTA_NUMERIC_ARGUMENT,"Coefficient substitution requires a real expression.");for(auto& coefficient:value)coefficient=substitute(coefficient,variableName,replacement[0],work);}}
    else if(request->operation!=PENTA_PENT_EXPAND&&request->operation!=PENTA_PENT_NUMERIC&&request->operation!=PENTA_PENT_CODE)throw Failure(PENTA_NUMERIC_ARGUMENT,"Unknown exact coefficient operation.");
    std::array<double,5> numeric{};std::string output;
    if(request->operation==PENTA_PENT_NUMERIC){if(request->binding_count>320||(request->binding_count&&(!request->binding_names||!request->binding_values)))throw Failure(PENTA_NUMERIC_ARGUMENT,"Invalid coefficient bindings.");std::map<std::string,double> bindings;for(uint64_t n=0;n<request->binding_count;++n){const std::string name=request->binding_names[n]?request->binding_names[n]:"";if(!algebra.reals.contains(name)||!std::isfinite(request->binding_values[n])||!bindings.emplace(name,request->binding_values[n]).second)throw Failure(PENTA_NUMERIC_ARGUMENT,"Numeric bindings require unique declared real coordinates and finite values.");}for(unsigned c=0;c<5;++c)numeric[c]=evaluate(value[c],bindings,work);}
    else if(request->operation==PENTA_PENT_CODE){if(!identifier(variableName)||std::set<std::string>{"i","j","k","ell","gamma","sqrt"}.contains(variableName))throw Failure(PENTA_NUMERIC_ARGUMENT,"Native export requires a plain non-basis function identifier.");std::set<std::string> parameters;for(const auto& coefficient:value)for(const auto& [key,c]:coefficient){(void)c;for(const auto& [name,power]:key.powers){(void)power;parameters.insert(name);}}output="native function pentarnion "+variableName+"(";bool comma=false;for(const auto& name:parameters){if(comma)output+=", ";output+="real v_"+name;comma=true;}output+=") {\n    return pent([";for(unsigned c=0;c<5;++c){if(c)output+=", ";output+=scalarText(value[c],true);}output+="]);\n}\n";}
    else for(unsigned c=0;c<5;++c){if(c)output+='\n';output+=scalarText(value[c]);}
    if(request->operation==PENTA_PENT_CODE){std::ostringstream context;context<<std::setprecision(17)<<gamma.convert_to<double>();output="% Exact symbolic gamma = "+gammaText+"\nuse_algebra(\"A_gamma\", gamma="+context.str()+");\n"+output;}
    if(!output.empty()&&(!request->output||request->output_capacity<=output.size()))throw Failure(PENTA_PENT_SYMBOLIC_LIMIT,"Exact symbolic output buffer is too small.");
    work.flush();
    if(!output.empty())std::memcpy(request->output,output.c_str(),output.size()+1);
    std::copy(numeric.begin(),numeric.end(),report->coefficients);report->gamma=gamma.convert_to<double>();report->work_units=work.used;return PENTA_NUMERIC_OK;
 }catch(const Failure& failure){message(error,capacity,failure.what());return failure.status;}catch(const std::exception& failure){message(error,capacity,failure.what());return PENTA_NUMERIC_ARGUMENT;}catch(...){message(error,capacity,"Unknown exact symbolic coefficient failure.");return PENTA_NUMERIC_INTERNAL;}
}
const penta_pent_symbolic_api_v1 api{sizeof(penta_pent_symbolic_api_v1),PENTA_PENT_SYMBOLIC_ABI_V1,"exact-rational-radical-pentarnion-coefficients-v1",execute};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_pent_symbolic_api_v1* penta_science_pent_query_v1(uint32_t version){return version==PENTA_PENT_SYMBOLIC_ABI_V1?&api:nullptr;}
