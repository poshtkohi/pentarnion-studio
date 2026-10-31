#include "runtime/interpreter.hpp"
#include "native_functions/native_callback.hpp"
#include "native_functions/third_callback.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <future>
#include <iostream>

namespace {
using namespace penta;
int checks=0,failures=0;
void expect(bool okay,const std::string& text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
void show(const runtime::ExecutionResult& r){for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
std::shared_ptr<const language::FunctionDefinition> function(const std::string& source){
    const auto checked=language::analyse(source);if(!checked.ok()){for(const auto& d:checked.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';throw std::runtime_error("Native array fixture did not analyse.");}
    for(const auto& statement:checked.program.statements)if(statement.function)return statement.function;
    throw std::runtime_error("Native array fixture did not define a function.");
}
void native(const std::filesystem::path& directory){
    native_functions::Options options;options.build.cacheDirectory=directory/"native";
    const auto pureReal=function("native function pentarnionmatrix<1,2> lifted_pure(matrix<1,2> x){return pure(x);}");
    native_functions::Callback pureCallback(*pureReal,options);native_functions::DerivativeRequest pureRequest;pureRequest.order=1;const auto pureResult=pureCallback.evaluate(1,{2,3},pureRequest);
    expect(pureResult.values==std::vector<double>(10,0)&&pureResult.jacobian==std::vector<double>(20,0),"Native pure(real array) retains the complete checked zero Pentarnion result and derivatives.");
    const auto f=function("native function matrix<2,2> square_array(matrix<2,2> A){return A*A;}");
    native_functions::Callback callback(*f,options);native_functions::DerivativeRequest request;request.order=1;
    const auto value=callback.evaluate(1,{1,2,3,4},request);
    expect(value.values==std::vector<double>({7,10,15,22}),"Native row-major coordinate ABI preserves matrix multiplication.");
    const std::vector<double> jacobian{2,3,2,0, 2,5,0,2, 3,0,5,3, 0,3,2,8};
    expect(value.jacobian==jacobian,"Matrix product Jacobian agrees with independently expanded scalar polynomials.");
    request.order=2;request.direction={1,0,0,0};const auto second=callback.evaluate(1,{1,2,3,4},request);
    const std::vector<double> hessianDirection{2,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0};
    expect(second.second==hessianDirection,"Array AD2 retains output-major Hessian-vector products.");
    native_functions::Callback reused(*f,options);expect(reused.cacheHit()&&reused.key()==callback.key(),"Fixed-array body and shape reuse the native cache.");
    const auto reshaped=function("native function matrix<1,4> square_array(matrix<1,4> A){return A.*A;}");
    expect(native_functions::callbackFingerprint(*f)!=native_functions::callbackFingerprint(*reshaped),"Equal coordinate counts with different array shapes have distinct structural keys.");
    auto concurrent=[&callback]{native_functions::DerivativeRequest r;r.order=0;return callback.evaluate(1,{2,0,0,3},r).values;};
    auto first=std::async(std::launch::async,concurrent),other=std::async(std::launch::async,concurrent);
    expect(first.get()==std::vector<double>({4,0,0,9})&&other.get()==std::vector<double>({4,0,0,9}),"A shared array callback is reentrant with independent stack buffers.");
    const auto cubic=function("native function matrix<2,2> cube_array(matrix<2,2> A){return (A*A)*A;}");
    native_functions::ThirdCallback third(*cubic,options);native_functions::ThirdRequest directions;directions.u=directions.v=directions.w={1,0,0,0};
    const auto d3=third.evaluate(1,{1,2,3,4},directions);expect(d3.third==std::vector<double>({6,0,0,0}),"Array AD3 differentiates a cubic matrix product without truncation.");
    const auto edit=function("native function matrix<2,2> edited(matrix<2,2> A){matrix<2,2> B=A;B(2,1)=A(1,2);B(end)=A(1);return B.';}");
    native_functions::Callback indexed(*edit,options);native_functions::DerivativeRequest zeroth;zeroth.order=0;
    expect(indexed.evaluate(1,{1,2,3,4},zeroth).values==std::vector<double>({1,2,2,1}),"Literal and end indexing use MATLAB column-major indices over row-major ABI storage.");
    const auto q=function("algebra H=builtin_algebra(\"quaternion\");native function matrix<H,1,2> ordered(matrix<H,1,2> A){return A*H.i;}");
    native_functions::Callback quaternion(*q,options);const auto h=quaternion.evaluate(1,{0,0,1,0,0,0,0,1},zeroth);
    expect(h.values==std::vector<double>({0,0,0,-1,0,0,1,0}),"Native quaternion array scaling retains ordered products and all four coefficients per cell.");
}
void languageCases(const std::filesystem::path& directory){
    runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"odes";
    auto run=[&](const std::string& source,bool valid=true){auto result=runtime::execute(source,{},options);if(valid&&!result.ok())show(result);expect(result.ok()==valid,source);return result;};
    run("matrix dynamic=[1,2];dynamic=[1;2;3];assert(size(dynamic,1)==3);matrix<2,2> fixed=[1,2;3,4];fixed(1,2)=7;assert(fixed(1,2)==7);");
    run("matrix<2,2> fixed=[1,2;3,4];fixed=[1,2,3,4];",false);
    run("algebra H=builtin_algebra(\"quaternion\");matrix<H,2,2> A=[H.i,H.j;H.k,1];matrix<H,2,2> B=A*A;assert(B(1,1)==-1+H.i);assert(B(1,2)==H.k+H.j);assert(B(2,1)==H.j+H.k);assert(B(2,2)==1-H.i);assert(coeff(A,1)==[1,0;0,0]);assert(A(3)==H.j);A(2,1)=H.one;assert(A(2)==H.one);assert(A'(1,2)==H.one);");
    run("algebra H=builtin_algebra(\"quaternion\");matrix<H,2,2> A=[H.i,H.j;H.k,H.one];matrix<H,2,2> B=A;A(:)=B;assert(A==B);A([1,1,2,2])=B;assert(A(1)==H.k);assert(A(2)==H.one);assert(B(2)==H.k);A(:,1:2)=B;assert(A==B);matrix<H,2,2> realEntries=[1,2;3,4];A(:)=realEntries;assert(coeff(A,0)==[1,2;3,4]);");
    run("algebra H=builtin_algebra(\"quaternion\");matrix<H,2,2> A=[H.i,H.j;H.k,H.one];A(:,1:2)=[H.one,H.k,H.j,H.i];",false);
    run("algebra H=builtin_algebra(\"quaternion\");matrix A=[H.i];matrix emptyRow=A(:,[]);matrix emptyColumn=A([],:);assert(numel(emptyRow)==0&&length(emptyRow)==0);assert(numel(emptyColumn)==0&&length(emptyColumn)==0);");
    run("algebra H=builtin_algebra(\"quaternion\");element<H> small=1e-309*H.i;element<H> q=small/1e-309;element<H> left=1e-309\\small;assert(q==H.i&&left==H.i);matrix A=[small,0;0,small];matrix B=A/1e-309;assert(B(1)==H.i&&B(4)==H.i&&B(2)==0&&B(3)==0);matrix C=A./[1e-309,1e-309;1e-309,1e-309];assert(C==B);");
    run("algebra H=builtin_algebra(\"quaternion\");matrix A=[H.i];matrix invalid=A/0;",false);
    run("algebra H=builtin_algebra(\"quaternion\");element<H> big=element(H,[1e308,1e308,-1e308,1e308]);matrix A=[big,big;big,big];matrix columns=mean(A);matrix rows=mean(A,2);assert(size(columns,1)==1&&size(columns,2)==2);assert(size(rows,1)==2&&size(rows,2)==1);assert(columns(1)==big&&columns(2)==big&&rows(1)==big&&rows(2)==big);matrix cancelled=sum([big,big,-big]);assert(cancelled(1)==big);real tiny=4.9406564584124654e-324;element<H> small=tiny*H.i;matrix retained=mean([small,small,small]);assert(retained(1)==small);");
    run("algebra H=builtin_algebra(\"quaternion\");algebra Q=builtin_algebra(\"split_quaternion\");matrix<H,1,1> a=[H.i];matrix<Q,1,1> b=[Q.i];a=b;",false);
    run("algebra L=builtin_algebra(\"lie_so3\");matrix<L,1,2> A=[L.x,L.y];matrix<L,1,2> B=3*A;assert(B(1)==3*L.x);assert(B(2)==3*L.y);");
    const auto source="native function matrix<2,2> square_array(matrix<2,2> A){return A*A;}matrix<2,2> A=[1,2;3,4];matrix<2,2> B=square_array(A);assert(B==[7,10;15,22]);matrix J=jacobian(square_array,A);assert(J==[2,3,2,0;2,5,0,2;3,0,5,3;0,3,2,8]);record checked=derivative_check(square_array,A);assert(checked.passed);";
    run(source);const auto cached=run(source);if(cached.ok())expect(cached.nativeCompilation.compilerAttempts==0,"Repeated array function calls and derivative queries compile no new body.");
    run("native function real energy(matrix<2,2> A){return norm(A,\"fro\")^2;}matrix<2,2> A=[1,2;3,4];matrix direction=[1,0;0,0];matrix g=gradient(energy,A);assert(norm(g-[2;4;6;8])<1e-10);matrix h=hvp(energy,A,direction);assert(norm(h-[2;0;0;0])<1e-10);");
    run("ode decay(t,matrix<2,2> A;real rate){A'=-rate*A;}solution s=odesolve(decay,A=[1,2;3,4],rate=0.5,t0=0,t1=1,sample_step=0.05,rtol=1e-10,atol=1e-12);matrix<2,2> final=s.A(length(s.t));assert(norm(final-exp(-0.5)*[1,2;3,4],\"fro\")<1e-8);matrix<2,2> dense=deval(s,0.5);assert(norm(dense-exp(-0.25)*[1,2;3,4],\"fro\")<1e-8);matrix packed=deval(s,[0,0.5,1]);assert(size(packed,1)==3);assert(size(packed,2)==4);assert(abs(packed(2,4)-4*exp(-0.25))<1e-8);assert(abs(statecoord(s.A,3)(length(s.t))-4*exp(-0.5))<1e-8);save(\"array-states.pws\",\"s\",\"final\");");
    runtime::InterpreterSession restored;auto loaded=restored.submit("load(\"array-states.pws\");",{},options);if(!loaded.ok())show(loaded);expect(loaded.ok(),"Saved fixed-array solution loads into a separate session submission.");auto roundtrip=restored.submit("matrix<2,2> replay=s.A(length(s.t));assert(norm(replay-final,\"fro\")<1e-12);",{},options);if(!roundtrip.ok())show(roundtrip);expect(roundtrip.ok(),"Array-state persistence retains complete coordinates and shape.");
    run("algebra H=builtin_algebra(\"quaternion\");ode turn(t,matrix<H,1,2> A;real speed){A'=speed*(A*H.k);}solution s=odesolve(turn,A=[H.one,H.i],speed=1,t0=0,t1=0.5,sample_step=0.1,rtol=1e-10,atol=1e-12);matrix<H,1,2> last=s.A(length(s.t));assert(algebra_norm(last(1)-(cos(0.5)+sin(0.5)*H.k))<1e-8);assert(algebra_norm(last(2)-(cos(0.5)*H.i-sin(0.5)*H.j))<1e-8);");
    run("algebra O=builtin_algebra(\"octonion\");matrix<O,1,1> a=[O.e1];matrix<O,1,1> b=[O.e2];matrix<O,1,1> c=[O.e4];matrix d=a*b*c;",false);
    run("algebra O=builtin_algebra(\"octonion\");matrix<O,1,1> a=[O.e1];matrix<O,1,1> b=[O.e2];matrix<O,1,1> c=[O.e4];matrix<O,1,1> d=(a*b)*c;assert(algebra_norm(d(1)-((O.e1*O.e2)*O.e4))<1e-12);");
}
void massCases(const std::filesystem::path& directory){
    runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"odes";
    auto run=[&](const std::string& source,bool valid=true){auto result=runtime::execute(source,{},options);if(valid&&!result.ok())show(result);expect(result.ok()==valid,source);return result;};
    const std::string source="native function real rhs(real t,real y){return -y-y*y;}native function matrix<1,1> inertia(real t,real y){return [1+y];}record s=masssolve(rhs,inertia,1,-1,options(t1=1,sample_step=0.05,rtol=1e-10,atol=1e-12));assert(abs(s.values(length(s.t))-exp(-1))<1e-8);assert(s.algebraic(1)==0);assert(s.jacobian==\"analytic\");assert(s.classification==\"initial_exact_zero_columns\");";
    run(source);const auto cached=run(source);if(cached.ok())expect(cached.nativeCompilation.compilerAttempts==0,"Repeated state-dependent mass operators reuse both native modules.");
    run("native function real rhs(real t,real y){return -(1+t)*y;}native function matrix<1,1> inertia(real t,real y){return [1+t];}record s=masssolve(rhs,inertia,1,-1,options(t1=1,sample_step=0.1,rtol=1e-10,atol=1e-12));assert(abs(s.values(length(s.t))-exp(-1))<1e-8);");
    run("native function matrix<2,1> rhs(real t,matrix<2,1> y){return [-y(1);y(2)-y(1)^2];}native function matrix<2,2> inertia(real t,matrix<2,1> y){return [1,0;0,0];}record s=masssolve(rhs,inertia,[1;1],[-1;-2],options(t1=1,sample_step=0.05,rtol=1e-10,atol=1e-12));integer last=length(s.t);assert(s.algebraic==[0,1]);assert(abs(s.values(last,1)-exp(-1))<1e-8);assert(abs(s.values(last,2)-exp(-2))<1e-8);assert(s.maximum_sample_residual<1e-8);");
    run("native function matrix<2,1> rhs(real t,matrix<2,1> y){return [y(2);-y(1)];}native function matrix<2,2> inertia(real t,matrix<2,1> y){return [1,0;0,0];}record s=masssolve(rhs,inertia,[0;0],[0;0],options(t1=0.1));",false);
    run("native function matrix<2,1> rhs(real t,matrix<2,1> y){return [1;y(2)];}native function matrix<2,2> inertia(real t,matrix<2,1> y){return [1,0;0,y(1)];}record s=masssolve(rhs,inertia,[0;0],[1;0],options(t1=0.1));",false);
}
void unitCases(const std::filesystem::path& directory){
    runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"odes";
    auto run=[&](const std::string& source,bool valid=true){auto result=runtime::execute(source,{},options);if(valid&&!result.ok())show(result);expect(result.ok()==valid,source);};
    run("native function real energy(real mass,matrix<2,1> velocity){return 0.5*mass*norm(velocity)^2;}record u=unitcheck(energy,options(mass=\"kg\",velocity=\"m/s\"));assert(u.valid);assert(u.result_dimensions==[2,1,-2,0,0,0,0]);assert(u.parameter_dimensions(2,1)==1);assert(u.jacobian_dimensions(1,2)==0);");
    run("ode oscillator(t,real x,real v;real omega){x'=v;v'=-omega^2*x;}record u=unitcheck(oscillator,options(t=\"s\",x=\"m\",v=\"m/s\",omega=\"s^-1\"));assert(u.valid);assert(u.result_dimensions==[1,0,-1,0,0,0,0;1,0,-2,0,0,0,0]);");
    run("algebra H=builtin_algebra(\"quaternion\");native function element<H> bracket(element<H> a,element<H> b){return commutator(a,b);}record u=unitcheck(bracket,options(a=\"m\",b=\"s\"));assert(u.result_dimensions==[1,0,1,0,0,0,0]);");
    run("native function real f(real x){return 0;}record u=unitcheck(f,options(x=\"m\"));assert(u.valid);");
    run("native function real f(real x,real t){return x+t;}record u=unitcheck(f,options(x=\"m\",t=\"s\"));",false);
    run("native function real f(real x){return exp(x);}record u=unitcheck(f,options(x=\"m\"));",false);
    run("ode wrong(t,real x;real rate){x'=rate+x;}record u=unitcheck(wrong,options(t=\"s\",x=\"m\",rate=\"m/s\"));",false);
    run("native function real f(real x){return x;}record u=unitcheck(f,options(x=\"cm\"));",false);
    run("ode decay(t,real x;real rate){x'=-rate*x;}solution s=odesolve(decay,x=1,rate=1,t0=0,t1=0.1,sample_step=0.05,units=options(t=\"s\",x=\"m\",rate=\"s^-1\"));assert(s.x(length(s.t))>0);");
    run("ode stationary(t,real x){x'=0;}solution s=odesolve(stationary,x=2,t0=0,t1=0.1,sample_step=0.05,units=options(t=\"s\",x=\"m\"));assert(s.x(length(s.t))==2);");
    run("ode decay(t,real x;real rate){x'=-rate*x;}solution s=odesolve(decay,x=1,rate=1,t0=0,t1=0.1,sample_step=0.05,units=options(t=\"s\",x=\"m\",rate=\"s\"));",false);
}
void mechanicsCases(const std::filesystem::path& directory){
    runtime::ExecutionOptions options;options.dataDirectory=directory;options.odeCacheDirectory=directory/"odes";
    auto run=[&](const std::string& source,bool valid=true){auto result=runtime::execute(source,{},options);if(valid&&!result.ok())show(result);expect(result.ok()==valid,source);return result;};
    run("native function real applied(real t,real q){return 1.0;}record s=mechanics(applied,0,0,options(t1=1,time_step=0.01,sample_step=0.05));integer last=length(s.t);assert(abs(s.work(last)-0.5)<1e-10);assert(abs(s.kinetic_energy(last)-0.5)<1e-10);assert(abs(s.energy_balance(last))<1e-10);assert(s.diagnostic_evaluations>0);");
    const std::string moving="native function real L(real t,real q,real v){return 0.5*(1+q*q)*v*v;}native function real moving(real t,real q){return q-t;}record s=mechanicsdae(L,moving,0,1,options(t1=0.3,sample_step=0.01,rtol=1e-9,atol=1e-11));integer last=length(s.t);assert(abs(s.q(last)-0.3)<1e-7);assert(abs(s.v(last)-1)<1e-7);assert(abs(s.lambda(last)-0.3)<1e-6);assert(s.maximum_constraint_error<1e-7);assert(abs(s.work(last)-0.045)<1e-6);assert(abs(s.energy_balance(last))<1e-6);assert(s.jacobian==\"analytic\");";
    run(moving);const auto cached=run(moving);if(cached.ok())expect(cached.nativeCompilation.compilerAttempts==0,"Constrained AD2/AD3 bodies reuse their structural native modules.");
    run("native function real L(real t,matrix<2,1> q,matrix<2,1> v){return 0.5*(v(1)^2+v(2)^2);}native function real constraint(real t,matrix<2,1> q,matrix<2,1> v){return v(2)-q(1);}record s=mechanicsdae(L,constraint,[0;0],[1;0],options(constraint_mode=\"velocity\",t1=0.3,sample_step=0.01,rtol=1e-9,atol=1e-11));integer last=length(s.t);assert(abs(s.q(last,1)-0.3)<1e-7);assert(abs(s.q(last,2)-0.045)<1e-7);assert(abs(s.v(last,2)-0.3)<1e-7);assert(s.maximum_constraint_error<1e-7);assert(abs(s.energy_balance(last))<1e-6);");
    run("native function real L(real t,real q,real v){return 0.5*v*v;}native function real g(real t,real q){return q-t;}record s=mechanicsdae(L,g,0,0,options(t1=0.1));",false);
    run("native function real L(real t,real q,real v){return 0.5*v*v;}native function real g(real t,real q){return q*q;}record s=mechanicsdae(L,g,0,0,options(t1=0.1));",false);
}
}
int main(){const auto directory=std::filesystem::temp_directory_path()/("penta-native-arrays-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{std::filesystem::create_directories(directory);native(directory);languageCases(directory);massCases(directory);unitCases(directory);mechanicsCases(directory);}catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" native array language checks, "<<failures<<" failures\n";return failures?1:0;}
