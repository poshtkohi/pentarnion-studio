#include "algebra/universal.hpp"
#include "native_functions/native_callback.hpp"
#include "native_functions/third_callback.hpp"
#include "native_functions/universal_native.hpp"
#include "ode/native_internal.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <iostream>
#include <numeric>

using namespace penta;
namespace u=penta::algebra::universal;
namespace {
using Type=language::Type;
using Expr=language::ExprPtr;
using Schema=std::shared_ptr<const language::ValueSchema>;
int checks=0;
void require(bool yes,const std::string& message){++checks;if(!yes)throw std::runtime_error(message);}
bool close(double a,double b,double tolerance=2e-10){return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));}
void near(const std::vector<double>& a,const std::vector<double>& b,const std::string& label,double tolerance=2e-10){bool okay=a.size()==b.size();for(std::size_t k=0;k<std::min(a.size(),b.size());++k)okay=okay&&close(a[k],b[k],tolerance);require(okay,label);}
Schema schema(const u::DefinitionPtr& definition){auto result=std::make_shared<language::ValueSchema>();result->algebra=definition;return result;}
Expr node(language::ExprKind kind,Type type,std::string text={},std::vector<Expr> children={},Schema shape={}){auto result=std::make_shared<language::Expr>();result->kind=kind;result->type=type;result->text=std::move(text);result->children=std::move(children);result->schema=std::move(shape);return result;}
Expr ref(const std::string& name,Schema shape={}){return node(language::ExprKind::Reference,shape?Type::Element:Type::Real,name,{},shape);}
Expr integer(int number){return node(language::ExprKind::IntegerLiteral,Type::Integer,std::to_string(number));}
Expr real(double number){return node(language::ExprKind::RealLiteral,Type::Real,std::to_string(number));}
Expr binary(const std::string& op,Expr a,Expr b,Schema shape){return node(language::ExprKind::Binary,shape?Type::Element:Type::Real,op,{std::move(a),std::move(b)},shape);}
Expr call(const std::string& name,std::vector<Expr> args,Type type,Schema shape={}){return node(language::ExprKind::Call,type,name,std::move(args),std::move(shape));}
Expr basis(const u::DefinitionPtr& definition,std::size_t index){const auto shape=schema(definition);return node(language::ExprKind::Member,Type::Element,definition->basis().at(index),{node(language::ExprKind::Reference,Type::Algebra,"fixed",{},shape)},shape);}
language::FunctionDefinition function(const std::vector<language::OdeVariable>& parameters,Expr body,Schema resultSchema={}){language::FunctionDefinition result;result.name="fixture";result.parameters=parameters;for(const auto& parameter:parameters)result.parameterSchemas.push_back(parameter.schema);result.result=body->type;result.resultSchema=resultSchema?resultSchema:body->schema;result.native=true;language::Statement returned;returned.kind=language::StatementKind::Return;returned.expression=std::move(body);result.body.push_back(std::move(returned));return result;}
std::vector<double> hamilton(const std::vector<double>& a,const std::vector<double>& b){return {a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]};}
std::vector<double> coordinates(const u::Element& value){return value.coefficients();}
void derivatives(native_functions::Callback& callback,const std::vector<double>& input,std::vector<std::size_t> active,const std::string& label){native_functions::DerivativeRequest request;request.order=2;request.active=std::move(active);const auto exact=callback.evaluate(1,input,request);request.method=native_functions::DerivativeMethod::FiniteDifference;const auto fd=callback.evaluate(1,input,request);near(exact.jacobian,fd.jacobian,label+" Jacobian agrees with central finite differences",3e-6);near(exact.second,fd.second,label+" Hessian agrees with independent differences",3e-5);}
template<class F>void rejects(F action,const std::string& label){try{action();}catch(const ode::OdeError&){require(true,label);return;}throw std::runtime_error(label);}

void quaternion(native_functions::Options& options){
    const auto h=u::builtin("quaternion");const auto shape=schema(h);const auto p=ref("p",shape),omega=ref("omega");
    const auto polynomial=binary("+",binary("^",p,integer(3),shape),binary("*",omega,binary("*",basis(h,3),p,shape),shape),shape);
    const auto definition=function({{"p",Type::Element,{},shape},{"omega",Type::Real,{},{}}},polynomial);
    native_functions::Callback callback(definition,options);
    require(callback.inputWidths()==std::vector<std::size_t>({4,1})&&callback.outputWidths()==std::vector<std::size_t>({4}),"Callback publishes complete declared widths");
    const std::vector<double> p0{.4,.2,-.3,.15},input{.4,.2,-.3,.15,1.7};auto expected=hamilton(hamilton(p0,p0),p0);const auto rotate=hamilton({0,0,0,1},p0);for(unsigned k=0;k<4;++k)expected[k]+=1.7*rotate[k];
    const auto result=callback.evaluate(1,input,{});near(result.values,expected,"Quaternion native cubic matches independent Hamilton arithmetic");
    derivatives(callback,input,{},"Quaternion polynomial");
    native_functions::DerivativeRequest request;request.order=2;request.active={0,1,2,3};const auto full=callback.evaluate(1,input,request);request.direction={.1,.2,-.3,.4};const auto hvp=callback.evaluate(1,input,request);std::vector<double> product(16,0);for(unsigned o=0;o<4;++o)for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)product[o*4+row]+=full.second[o*16+row*4+col]*request.direction[col];near(hvp.second,product,"Generic Hessian-vector products preserve output-major orientation");
    native_functions::Callback cached(definition,options);require(cached.cacheHit()&&cached.key()==callback.key(),"Immutable generic function reuses native cache");auto changed=input;changed[4]=2.7;const auto changedResult=cached.evaluate(1,changed,{});for(unsigned k=0;k<4;++k)expected[k]+=rotate[k];near(changedResult.values,expected,"Changing runtime scalar parameter reuses the compiled equation");
    native_functions::ThirdCallback third(definition,options);require(third.inputWidths()==std::vector<std::size_t>({4,1})&&third.outputWidths()==std::vector<std::size_t>({4}),"AD3 publishes complete generic coordinate widths");native_functions::ThirdRequest thirdRequest;thirdRequest.active={0,1,2,3};thirdRequest.u={1,.2,-.1,.4};thirdRequest.v={.1,-.2,.6,.3};thirdRequest.w={-.3,.5,.4,-.2};const std::array<std::vector<double>,3> directions{thirdRequest.u,thirdRequest.v,thirdRequest.w};std::array<unsigned,3> permutation{0,1,2};std::vector<double> thirdExpected(4,0);do{const auto term=hamilton(hamilton(directions[permutation[0]],directions[permutation[1]]),directions[permutation[2]]);for(unsigned k=0;k<4;++k)thirdExpected[k]+=term[k];}while(std::next_permutation(permutation.begin(),permutation.end()));near(third.evaluate(1,input,thirdRequest).third,thirdExpected,"Native AD3 matches all six explicitly grouped Hamilton products");
}

void octonion(native_functions::Options& options){
    const auto o=u::builtin("octonion");const auto shape=schema(o);const auto a=ref("a",shape),b=ref("b",shape),c=ref("c",shape);
    const auto assoc=binary("-",binary("*",binary("*",a,b,shape),c,shape),binary("*",a,binary("*",b,c,shape),shape),shape);
    native_functions::Callback callback(function({{"a",Type::Element,{},shape},{"b",Type::Element,{},shape},{"c",Type::Element,{},shape}},assoc),options);
    std::vector<double> golden(24,0),expected(8,0);golden[1]=1;golden[8+3]=1;golden[16+4]=1;expected[6]=2;near(callback.evaluate(1,golden,{}).values,expected,"Golden octonion associator (e1,e3,e4)=2e6 retains grouping");
    std::vector<double> input(24);for(unsigned k=0;k<24;++k)input[k]=.03*(static_cast<int>(k%9)-4);derivatives(callback,input,{1,11,20},"Nonassociative octonion associator");
}

void sedenion(native_functions::Options& options){
    const auto s=u::builtin("sedenion");const auto shape=schema(s);const auto a=ref("a",shape);const auto partner=binary("+",basis(s,4),basis(s,15),shape);
    native_functions::Callback callback(function({{"a",Type::Element,{},shape}},binary("*",a,partner,shape)),options);
    std::vector<double> divisor(16,0);divisor[1]=1;divisor[10]=1;const auto result=callback.evaluate(1,divisor,{});near(result.values,std::vector<double>(16,0),"Convention-specific sedenion zero-divisor product retains all sixteen coordinates");
    std::vector<double> nullImage(16,0);for(unsigned row=0;row<16;++row)for(unsigned col=0;col<16;++col)nullImage[row]+=result.jacobian[row*16+col]*divisor[col];near(nullImage,std::vector<double>(16,0),"Automatic Jacobian retains the genuine zero-divisor null direction");
    derivatives(callback,divisor,{1,10,15},"Sedenion zero-divisor linear map");
}

void constructorsAndTables(native_functions::Options& options){
    const auto matrix=u::builtin("matrix2");const auto shape=schema(matrix);const auto x=ref("x",shape);
    native_functions::Callback callback(function({{"x",Type::Element,{},shape}},binary("+",x,real(2),shape)),options);
    near(callback.evaluate(1,{1,2,3,4},{}).values,{3,2,3,6},"Native scalar promotion uses the complete matrix-algebra unit");
    const auto make=[](const char* coefficient,const char* unit){return u::fromJson(std::string(R"({"format":"StudioAlgebraDefinition","version":1,"name":"OneBasis","dimension":1,"basis":["e"],"coefficient_domain":"rational","unit":[")")+unit+R"("],"omitted_products":"zero","products":[{"left":0,"right":0,"terms":[[0,")"+coefficient+R"("]]}]})");};
    const auto first=make("2","1/2"),second=make("3","1/3");const auto firstSchema=schema(first),secondSchema=schema(second);
    const auto f=function({{"x",Type::Element,{},firstSchema}},binary("*",ref("x",firstSchema),ref("x",firstSchema),firstSchema));const auto g=function({{"x",Type::Element,{},secondSchema}},binary("*",ref("x",secondSchema),ref("x",secondSchema),secondSchema));
    require(native_functions::callbackFingerprint(f)!=native_functions::callbackFingerprint(g),"Multiplication-table edits invalidate the native generated cache identity");native_functions::Callback a(f,options),b(g,options);near(a.evaluate(1,{2},{}).values,{8},"First imported exact table executes its product");near(b.evaluate(1,{2},{}).values,{12},"Changed imported table executes its changed product");
    const auto lie=u::builtin("lie_so3");const auto l=schema(lie);native_functions::Callback cross(function({{"x",Type::Element,{},l}},binary("*",ref("x",l),basis(lie,1),l)),options);near(cross.evaluate(1,{1,2,3},{}).values,{-3,0,1},"Native Lie product supports a nonunital algebra");
    rejects([&]{native_functions::validateCallback(function({{"x",Type::Element,{},l}},binary("+",ref("x",l),real(1),l)));},"Nonunital algebra rejects implicit scalar embedding during native checking");
}

void elementary(native_functions::Options& options){
    const auto h=u::builtin("quaternion");const auto shape=schema(h);native_functions::Callback exponential(function({{"x",Type::Element,{},shape}},call("exp",{ref("x",shape)},Type::Element,shape)),options);
    const std::vector<double> point{.2,.3,-.1,.4};const double radius=std::sqrt(.26),scale=std::exp(.2);near(exponential.evaluate(1,point,{}).values,{scale*std::cos(radius),scale*std::sin(radius)*.3/radius,-scale*std::sin(radius)*.1/radius,scale*std::sin(radius)*.4/radius},"Quaternion exponential agrees with the one-element radial formula");derivatives(exponential,point,{0,1,2,3},"Quaternion exponential");
    native_functions::DerivativeRequest request;request.order=2;const auto atReal=exponential.evaluate(1,{.2,0,0,0},request);require(std::all_of(atReal.second.begin(),atReal.second.end(),[](double x){return std::isfinite(x);}),"One-element Hessian is smooth at zero pure part");
    const auto z=node(language::ExprKind::Reference,Type::Complex,"z");const auto square=node(language::ExprKind::Binary,Type::Complex,"*",{z,z});const auto sum=node(language::ExprKind::Binary,Type::Complex,"+",{square,call("exp",{z},Type::Complex)});native_functions::Callback complex(function({{"z",Type::Complex,{},{}}},sum),options);const std::complex<double> value(.3,-.4),reference=value*value+std::exp(value);near(complex.evaluate(1,{.3,-.4},{}).values,{reference.real(),reference.imag()},"Explicit complex native expressions retain both real coordinates");derivatives(complex,{.3,-.4},{},"Complex map real-coordinate AD");
    const auto matrix=u::builtin("matrix2");const auto m=schema(matrix);native_functions::Callback matrixExp(function({{"x",Type::Element,{},m}},call("exp",{ref("x",m)},Type::Element,m)),options);near(matrixExp.evaluate(1,{0,1,-1,0},{}).values,{std::cos(1.),std::sin(1.),-std::sin(1.),std::cos(1.)},"Certified associative matrix-algebra exponential uses its non-scalar-first unit",3e-10);
    const auto split=schema(u::builtin("split_complex"));const auto splitDefinition=function({{"x",Type::Element,{},split}},call("exp",{ref("x",split)},Type::Element,split));
    native_functions::Callback splitExp(splitDefinition,options);std::vector<double> splitValue(2);splitExp.evaluateValues(1,{-20000,20000},splitValue);near(splitValue,{.5,.5},"Native split exponential combines finite idempotent exponents before radial scaling");
    native_functions::DerivativeRequest splitRequest;splitRequest.order=2;const auto splitAd=splitExp.evaluate(1,{-20000,20000},splitRequest);near(splitAd.values,{.5,.5},"Split exponential AD2 primal retains its finite idempotent value");near(splitAd.jacobian,std::vector<double>(4,.5),"Split exponential Jacobian matches independent exp(a+b) idempotent differentiation");near(splitAd.second,std::vector<double>(8,.5),"Split exponential Hessian retains every mixed scalar/pure partial");
    native_functions::ThirdCallback splitThird(splitDefinition,options);native_functions::ThirdRequest splitDirections;splitDirections.u={1,0};splitDirections.v={0,1};splitDirections.w={1,0};const auto splitAd3=splitThird.evaluate(1,{-20000,20000},splitDirections);near(splitAd3.values,{.5,.5},"Split exponential AD3 primal retains finite idempotent values");near(splitAd3.third,{.5,.5},"Split exponential mixed AD3 follows the independent idempotent exponential derivative");
}

void remainingPresetsAndContracts(native_functions::Options& options){
    for(const std::string name:{"real","dual","dual_quaternion","split_complex","split_quaternion","clifford","jordan_sym2"}){
        const auto definition=u::builtin(name);const auto shape=schema(definition);const auto n=definition->dimension();
        native_functions::Callback callback(function({{"x",Type::Element,{},shape},{"y",Type::Element,{},shape}},binary("*",ref("x",shape),ref("y",shape),shape)),options);
        std::vector<double> x(n),y(n),input;for(std::size_t k=0;k<n;++k){x[k]=.02*(k+1);y[k]=.03*(static_cast<int>(k%3)-1);}input=x;input.insert(input.end(),y.begin(),y.end());near(callback.evaluate(1,input,{}).values,coordinates(u::multiply(u::Element(definition,x),u::Element(definition,y))),"Native "+name+" product matches the independent core implementation");
    }
    const auto h=u::builtin("quaternion");const auto shape=schema(h);
    auto vector=node(language::ExprKind::MatrixLiteral,Type::RealMatrix,{}, {ref("x"),real(1),real(2),real(3)});vector->rows=1;vector->columns=4;vector->rowWidths={4};
    auto constructor=call("element",{node(language::ExprKind::Reference,Type::Algebra,"fixed",{},shape),vector},Type::Element,shape);
    native_functions::Callback constructed(function({{"x",Type::Real,{},{}}},constructor),options);near(constructed.evaluate(1,{.25},{}).values,{.25,1,2,3},"A native vector-literal constructor retains every algebra coefficient");
    native_functions::Callback inverse(function({{"x",Type::Element,{},shape}},call("inv",{ref("x",shape)},Type::Element,shape)),options);const std::vector<double> value{.8,.2,-.1,.3};near(inverse.evaluate(1,value,{}).values,coordinates(u::inverse(u::Element(h,value))),"Canonical native quaternion inverse agrees with its explicit core contract");derivatives(inverse,value,{},"Quaternion inverse");
    const auto m=u::builtin("matrix2");const auto matrixShape=schema(m);native_functions::Callback matrixInverse(function({{"x",Type::Element,{},matrixShape}},call("inv",{ref("x",matrixShape)},Type::Element,matrixShape)),options);near(matrixInverse.evaluate(1,{2,1,1,3},{}).values,{.6,-.2,-.2,.4},"Certified associative native inverse preserves a non-scalar-first unit");derivatives(matrixInverse,{2,1,1,3},{0,1},"Matrix-algebra inverse");rejects([&]{(void)matrixInverse.evaluate(1,{1,2,2,4},{});},"Singular algebra multiplication operators reject native inverse");rejects([&]{(void)matrixInverse.evaluate(1,{1,0,0,1e-13},{});},"Ill-conditioned associative operator inverse is rejected before unreliable AD");
    const auto lie=u::builtin("lie_so3");const auto lieShape=schema(lie);rejects([&]{native_functions::validateCallback(function({{"x",Type::Element,{},lieShape}},call("exp",{ref("x",lieShape)},Type::Element,lieShape)));},"Nonunital or uncertified algebras reject whole-element calculus during native checking");
    rejects([&]{native_functions::validateCallback(function({{"x",Type::Element,{},shape}},binary("^",ref("x",shape),integer(65),shape)));},"Generic native power bounds diagnose excessive generated work");
    rejects([&]{native_functions::validateCallback(function({{"x",Type::Element,{},lieShape}},binary("^",ref("x",lieShape),integer(0),lieShape)));},"Native nonunital zeroth power rejects rather than inventing a scalar basis");
    auto cancelled=options;cancelled.build.cancellation=std::make_shared<std::atomic_bool>(true);native_functions::Callback cancelledInverse(function({{"x",Type::Element,{},shape}},call("inv",{ref("x",shape)},Type::Element,shape)),cancelled);rejects([&]{(void)cancelledInverse.evaluate(1,value,{});},"Cached generic callbacks observe cancellation before entering native computation");
    auto limited=options;limited.chargeOperations=[](std::size_t){throw std::runtime_error("universal-quota");};native_functions::Callback budget(function({{"x",Type::Element,{},shape}},call("inv",{ref("x",shape)},Type::Element,shape)),limited);std::vector<double> output(4,123);bool rejected=false;try{budget.evaluateValues(1,value,output);}catch(const std::runtime_error& error){rejected=std::string(error.what())=="universal-quota";}require(rejected&&output==std::vector<double>(4,123),"Generic native quota failure preserves the original exception and leaves output unmodified");
}

void odeModel(native_functions::Options& options){
    const auto s=u::builtin("sedenion");const auto shape=schema(s);language::OdeDefinition model;model.name="decay";model.timeName="t";model.states.push_back({"q",Type::Element,{},shape});model.parameters.push_back({"rate",Type::Real,{},{}});model.derivatives.push_back(binary("*",node(language::ExprKind::Unary,Type::Real,"-",{ref("rate")}),ref("q",shape),shape));
    ode::PreparedModel native(model,options.build);require(native.coordinateCount()==16&&native.parameterCount()==1,"Sedenion ODE native ABI retains sixteen coordinates");std::vector<double> initial(16);for(unsigned k=0;k<16;++k)initial[k]=.1*(k+1);const auto evaluated=native.evaluate(1,.3,initial,{.7});std::vector<double> rhs(16),jacobian(256,0);for(unsigned k=0;k<16;++k){rhs[k]=-.7*initial[k];jacobian[k*16+k]=-.7;}near(evaluated.rhs,rhs,"Native sixteen-coordinate ODE RHS matches independent reference");near(evaluated.jacobian,jacobian,"Compiler emits complete analytic state Jacobian for generic ODE");
    ode::PreparedModel cached(model,options.build);require(cached.cacheHit()&&cached.key()==native.key(),"ODE parameter edits reuse the existing structural shared library");const auto changed=cached.evaluate(1,.6,initial,{1.2});for(unsigned k=0;k<16;++k)rhs[k]=-1.2*initial[k];near(changed.rhs,rhs,"Cached generic ODE accepts changed runtime parameters");
    ode::SolveOptions settings;settings.t1=.2;settings.sampleStep=.05;settings.rtol=1e-10;settings.atol=1e-12;settings.method=ode::Method::Bdf;const auto solved=native.solve(1,initial,{.7},settings,options.build);require(solved->coordinateCount()==16&&solved->stateWidths==std::vector<std::size_t>{16},"Generic ODE solution layout preserves the declared element dimension");std::vector<double> expected=initial;for(auto& x:expected)x*=std::exp(-.7*.2);near(std::vector<double>(solved->values.end()-16,solved->values.end()),expected,"SUNDIALS generic ODE solution agrees with all sixteen analytic components",2e-8);
    auto mixed=model;mixed.states.push_back({"accumulator",Type::Real,{},{}});mixed.derivatives.push_back(call("coeff",{ref("q",shape),integer(3)},Type::Real));ode::PreparedModel mixedNative(mixed,options.build);auto mixedInitial=initial;mixedInitial.push_back(0);const auto mixedValue=mixedNative.evaluate(1,0,mixedInitial,{.7});require(mixedValue.rhs.size()==17&&mixedValue.jacobian.size()==289&&close(mixedValue.rhs[16],initial[3])&&close(mixedValue.jacobian[16*17+3],1),"Mixed real/algebra ODE states preserve their distinct widths and cross-state Jacobian blocks");
    language::OdeDefinition realModel;realModel.name="real_via_algebra";realModel.timeName="t";realModel.states={{"x",Type::Real,{},{}}};const auto h=u::builtin("quaternion");const auto hSchema=schema(h);const auto lifted=binary("*",ref("x"),basis(h,1),hSchema);realModel.derivatives={call("norm2",{lifted},Type::Real)};ode::PreparedModel scalarNative(realModel,options.build);const auto scalarValue=scalarNative.evaluate(1,0,{2},{});near(scalarValue.rhs,{4},"A real-state ODE with generic intermediate expressions selects descriptor-aware native generation");near(scalarValue.jacobian,{4},"Real-state ODE analytic Jacobian differentiates generic intermediate expressions");
}
}
int main(){
    native_functions::Options options;options.build.cacheDirectory=std::filesystem::temp_directory_path()/("penta-universal-native-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::size_t compilationCount=0;options.build.compilationFinished=[&](double){++compilationCount;};
    try{quaternion(options);octonion(options);sedenion(options);constructorsAndTables(options);elementary(options);remainingPresetsAndContracts(options);odeModel(options);require(compilationCount>=10,"Native tests actually compile and load descriptor-aware shared libraries");}
    catch(const std::exception& error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
    std::error_code ignored;std::filesystem::remove_all(options.build.cacheDirectory,ignored);std::cout<<checks<<" universal native checks passed ("<<compilationCount<<" actual shared-library compilations).\n";
}
