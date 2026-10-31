#define PENTA_NUMERIC_MODULE_BUILD
#include "science_api.h"
#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
extern "C" int penta_science_numeric(const penta_numeric_host_v1*,const penta_science_request_v1*,penta_science_report_v1*,char*,uint64_t);
namespace {
using Rational=boost::multiprecision::cpp_rational;
using Integer=boost::multiprecision::cpp_int;
struct Node;using Tree=std::shared_ptr<const Node>;
struct Node {char kind='c';Rational value=0;std::string atom;Tree left,right;};
Tree constant(Rational value){auto numerator=boost::multiprecision::numerator(value);if(numerator<0)numerator=-numerator;const auto denominator=boost::multiprecision::denominator(value);if((numerator!=0&&boost::multiprecision::msb(numerator)>16383)||boost::multiprecision::msb(denominator)>16383)throw std::invalid_argument("Exact rational coefficient exceeds16384 bits.");auto n=std::make_shared<Node>();n->value=std::move(value);return n;}
Tree atom(std::string text){auto n=std::make_shared<Node>();n->kind='v';n->atom=std::move(text);return n;}
bool is(const Tree& n,int value){return n->kind=='c'&&n->value==value;}
Tree operation(char kind,Tree a,Tree b){
 if(kind=='/'&&(b->kind!='c'||is(b,0)))throw std::invalid_argument("Symbolic division requires a nonzero exact rational scalar denominator.");
 if(a->kind=='c'&&b->kind=='c'){Rational result;if(kind=='+')result=a->value+b->value;else if(kind=='-')result=a->value-b->value;else if(kind=='*')result=a->value*b->value;else result=a->value/b->value;return constant(result);}
 if(kind=='+'&&is(a,0))return b;
 if((kind=='+'||kind=='-')&&is(b,0))return a;
 if(kind=='*'&&(is(a,0)||is(b,0)))return constant(0);
 if(kind=='*'&&is(a,1))return b;
 if((kind=='*'||kind=='/')&&is(b,1))return a;
 auto n=std::make_shared<Node>();n->kind=kind;n->left=std::move(a);n->right=std::move(b);return n;
}
struct Parser {
 std::string text;std::size_t at=0,nodes=0,depth=0;
 explicit Parser(const char* input):text(input?input:""){if(text.empty()||text.size()>32768)throw std::invalid_argument("Symbolic expressions require1..32768 characters.");}
 void space(){while(at<text.size()&&std::isspace(static_cast<unsigned char>(text[at])))++at;}
 bool match(char c){space();if(at<text.size()&&text[at]==c){++at;return true;}return false;}
 Tree expr(){auto a=product();for(;;){if(match('+'))a=operation('+',a,product());else if(match('-'))a=operation('-',a,product());else return a;}}
 Tree product(){auto a=primary();for(;;){if(match('*'))a=operation('*',a,primary());else if(match('/'))a=operation('/',a,primary());else return a;}}
 Tree primary(){if(++nodes>8192||++depth>128)throw std::invalid_argument("Symbolic expression node/depth budget exceeded.");struct Guard{std::size_t& n;~Guard(){--n;}} guard{depth};space();
  if(match('+'))return primary();
  if(match('-'))return operation('-',constant(0),primary());
  if(match('(')){auto a=expr();if(!match(')'))throw std::invalid_argument("Unclosed symbolic parenthesis.");return a;}
  space();if(at==text.size())throw std::invalid_argument("Expected a symbolic operand.");
  if(std::isalpha(static_cast<unsigned char>(text[at]))||text[at]=='_'){const auto begin=at++;while(at<text.size()&&(std::isalnum(static_cast<unsigned char>(text[at]))||text[at]=='_'))++at;return atom(text.substr(begin,at-begin));}
  if(std::isdigit(static_cast<unsigned char>(text[at]))||text[at]=='.'){
   std::string digits;int fractional=0;bool dot=false;
   while(at<text.size()&&(std::isdigit(static_cast<unsigned char>(text[at]))||text[at]=='.')){if(text[at]=='.'){if(dot)throw std::invalid_argument("Invalid exact decimal.");dot=true;}else{digits+=text[at];if(dot)++fractional;}++at;}
   if(digits.empty()||digits.size()>1024)throw std::invalid_argument("Exact numeric literal exceeds1024 digits.");
   int exponent=0;if(at<text.size()&&(text[at]=='e'||text[at]=='E')){++at;bool negative=false;if(at<text.size()&&(text[at]=='+'||text[at]=='-'))negative=text[at++]=='-';const auto begin=at;while(at<text.size()&&std::isdigit(static_cast<unsigned char>(text[at]))){exponent=exponent*10+(text[at++]-'0');if(exponent>1024)throw std::invalid_argument("Exact decimal exponent exceeds1024.");}if(at==begin)throw std::invalid_argument("Invalid exact decimal exponent.");if(negative)exponent=-exponent;}
   Integer num=0;for(const auto c:digits){num*=10;num+=c-'0';}Rational value=num;const int power=exponent-fractional;for(int j=0;j<std::abs(power);++j){if(power>0)value*=10;else value/=10;}return constant(value);
  }
  throw std::invalid_argument("Symbolic syntax supports identifiers, exact integer/decimal/rational scalars, +,-,*,/, and parentheses only.");
 }
 Tree parse(){auto tree=expr();space();if(at!=text.size())throw std::invalid_argument("Unexpected symbolic character or unsupported operator.");return tree;}
};
Tree derivative(const Tree& n,const std::string& variable,std::size_t depth=0){if(depth>128)throw std::invalid_argument("Symbolic derivative depth budget exceeded.");if(n->kind=='c')return constant(0);if(n->kind=='v')return constant(n->atom==variable?1:0);auto a=derivative(n->left,variable,depth+1);if(n->kind=='/')return operation('/',a,n->right);auto b=derivative(n->right,variable,depth+1);if(n->kind=='*')return operation('+',operation('*',a,n->right),operation('*',n->left,b));return operation(n->kind,a,b);}
Tree substitute(const Tree& n,const std::string& variable,const Tree& replacement,std::size_t depth=0){if(depth>128)throw std::invalid_argument("Symbolic substitution depth budget exceeded.");if(n->kind=='v')return n->atom==variable?replacement:n;if(n->kind=='c')return n;return operation(n->kind,substitute(n->left,variable,replacement,depth+1),substitute(n->right,variable,replacement,depth+1));}
std::string serialize(const Tree& n,std::size_t depth=0){if(depth>128)throw std::invalid_argument("Symbolic result depth budget exceeded.");std::string s;if(n->kind=='c'){std::ostringstream stream;stream<<n->value;s=stream.str();if(s.find('/')!=std::string::npos)s="("+s+")";}else if(n->kind=='v')s=n->atom;else s="("+serialize(n->left,depth+1)+std::string(1,n->kind)+serialize(n->right,depth+1)+")";if(s.size()>32768)throw std::invalid_argument("Symbolic result exceeds32768 characters.");return s;}
double numeric(const Tree& n,std::size_t depth=0){if(depth>128)throw std::invalid_argument("Symbolic evaluation depth budget exceeded.");if(n->kind=='v')throw std::invalid_argument("Substitute every symbolic atom before requesting a real numeric value.");if(n->kind=='c')return n->value.convert_to<double>();const double a=numeric(n->left,depth+1),b=numeric(n->right,depth+1);return n->kind=='+'?a+b:n->kind=='-'?a-b:n->kind=='*'?a*b:a/b;}
void message(char* p,uint64_t n,const char* s){if(p&&n){const auto k=std::min<std::size_t>(n-1,std::strlen(s));std::memcpy(p,s,k);p[k]=0;}}
int symbolic(const penta_numeric_host_v1* host,const penta_symbolic_request_v1* r,double* value,char* error,uint64_t cap){try{
 if(!host||host->abi_version!=PENTA_NUMERIC_ABI_V1||!r||r->struct_size<sizeof(*r))throw std::invalid_argument("Invalid symbolic module request.");
 if(host->cancelled&&host->cancelled(host->context))return PENTA_NUMERIC_CANCELLED;
 auto tree=Parser(r->expression).parse();const std::string variable=r->variable?r->variable:"";
 if(r->operation==PENTA_SYM_DIFF||r->operation==PENTA_SYM_SUBS){const auto v=Parser(variable.c_str()).parse();if(v->kind!='v')throw std::invalid_argument("Symbolic variable must be one identifier.");if(r->operation==PENTA_SYM_DIFF)tree=derivative(tree,variable);else tree=substitute(tree,variable,Parser(r->replacement).parse());}
 else if(r->operation!=PENTA_SYM_PARSE&&r->operation!=PENTA_SYM_SIMPLIFY&&r->operation!=PENTA_SYM_NUMERIC)throw std::invalid_argument("Unknown symbolic operation.");
 if(r->operation==PENTA_SYM_NUMERIC){if(!value)throw std::invalid_argument("Missing numeric output.");const double result=numeric(tree);if(!std::isfinite(result))throw std::domain_error("Symbolic conversion is not a finite real value.");*value=result;}
 else {const auto text=serialize(tree);if(!r->output||r->output_capacity<=text.size())throw std::invalid_argument("Symbolic output capacity insufficient.");std::memcpy(r->output,text.c_str(),text.size()+1);}
 return PENTA_NUMERIC_OK;
 }catch(const std::exception& e){message(error,cap,e.what());return PENTA_NUMERIC_ARGUMENT;}catch(...){message(error,cap,"Unknown symbolic failure.");return PENTA_NUMERIC_INTERNAL;}}
const penta_science_api_v1 api{sizeof(penta_science_api_v1),PENTA_SCIENCE_ABI_V1,"penta_science","eigen-kissfft-boost-exact-v1",penta_science_numeric,symbolic};
}
extern "C" PENTA_NUMERIC_EXPORT const penta_science_api_v1* penta_science_query_v1(uint32_t version){return version==PENTA_SCIENCE_ABI_V1?&api:nullptr;}
