#include "runtime/interpreter.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace penta;
namespace {
int checks=0,failures=0;
void expect(bool value,const std::string& message) {
    ++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}
}
void diagnostics(const runtime::ExecutionResult& result) {
    for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
    for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
}
void values(const std::string& source,const std::vector<double>& expected,double tolerance=0) {
    const auto result=runtime::execute(source);
    if(!result.ok())diagnostics(result);
    expect(result.ok(),"matrix execution succeeds: "+source);
    expect(result.output.size()==expected.size(),"matrix output count");
    for(std::size_t index=0;index<std::min(result.output.size(),expected.size());++index){
        try {
            std::size_t consumed=0;const double actual=std::stod(result.output[index],&consumed);
            expect(consumed==result.output[index].size()&&std::isfinite(actual)&&
                std::abs(actual-expected[index])<=tolerance*(1+std::abs(expected[index])),
                "matrix scalar output "+std::to_string(index)+": "+result.output[index]);
        } catch(...) {expect(false,"matrix scalar output is numeric: "+result.output[index]);}
    }
}
void rejected(const std::string& source) {
    const auto result=runtime::execute(source);
    expect(!result.ok()&&(!result.analysis.diagnostics.empty()||!result.runtimeDiagnostics.empty()),
        "invalid matrix operation rejected: "+source);
}

void literalAndIndexing() {
    values("matrix A=[1,2;3,4];display(A(1,1));display(A(1,2));display(A(2,1));display(A(2,2));"
        "display(A(1));display(A(2));display(A(3));display(A(4));",{1,2,3,4,1,3,2,4});
    values("realmatrix A=[1,2;3,4];A(2,1)=9;A(3)=8;display(A(2,1));display(A(1,2));display(A(2,2));",{9,8,4});
    values("vector row=[1,2,3];vector col=[4;5;6];display(row(2));display(col(2));",{2,5});
    values("matrix A=[1,2,3;4,5,6];matrix shape=size(A);display(size(A,1));display(size(A,2));"
        "display(shape(1));display(shape(2));display(numel(A));display(length(A));",{2,3,2,3,6,3});
    values("matrix A=zeros(0,3);matrix B=transpose(A);matrix C=zeros(2,0)*zeros(0,3);"
        "display(size(A,1));display(size(A,2));display(numel(A));display(length(A));"
        "display(size(B,1));display(size(B,2));display(C(2,3));",{0,3,0,0,3,0,0});
    values("pentarnionmatrix P=[i,j;k,ell];P(1,2)=1+2*j;display(scalar(P(1,2)));display(coeff(P(1,2),2));display(coeff(P(2),3));",{1,2,1});
    rejected("matrix A=[1,2;3];");
    rejected("matrix A=[1,2;3,4];display(A(0));");
    rejected("matrix A=[1,2;3,4];display(A(5));");
    rejected("matrix A=[1,2;3,4];display(A(1.5));");
    rejected("matrix A=[1,2;3,4];A(3,1)=5;");
    rejected("matrix A=[1,2;3,4];A(1)=i;");
    rejected("matrix A=zeros(0,3);display(A(1));");
    values("matrix A=[1,2;3,4];matrix B=A;A(1)=9;display(A(1));display(B(1));",{9,1});
}

void arithmeticAndOrientation() {
    values("matrix A=[1,2;3,4];matrix B=[5,6;7,8];matrix C=A*B;"
        "display(C(1,1));display(C(1,2));display(C(2,1));display(C(2,2));",{19,22,43,50});
    values("matrix A=[1,2;3,4];vector c=[5;6];vector r=[5,6];vector x=A*c;vector y=r*A;"
        "display(x(1));display(x(2));display(y(1));display(y(2));",{17,39,23,34});
    values("matrix A=[1,2;3,4];matrix B=A+2;matrix C=2-A;matrix D=3*A;matrix E=A/2;"
        "display(B(2,2));display(C(2,1));display(D(1,2));display(E(2,2));",{6,-1,6,2});
    values("matrix A=[1,2;3,4];matrix B=[2,4;6,8];matrix C=A.*B;matrix D=B./A;"
        "display(C(1));display(C(2));display(C(3));display(C(4));display(D(1));display(D(4));",{2,18,8,32,2,2});
    values("matrix A=[1,2;3,4];matrix B=transpose(A);matrix C=A.';matrix D=A';"
        "display(B(1,2));display(C(1,2));display(D(1,2));display(B(2,1));",{3,3,3,2});
    values("pentarnionmatrix P=[i,j];pentarnionmatrix T=P.';pentarnionmatrix H=P';"
        "display(coeff(T(1),1));display(coeff(H(1),1));display(size(T,1));display(size(T,2));",{1,-1,2,1});
    rejected("vector r=[1,2];vector c=[1;2];vector v=r+c;");
    rejected("matrix A=[1,2;3,4];vector r=[1,2];vector v=A*r;");
    rejected("matrix A=[1,2;3,4];matrix B=[1,2,3];matrix C=A*B;");
    rejected("matrix A=[1,2;3,4];matrix B=[1,2,3];matrix C=A.*B;");
    rejected("matrix A=[1,2;3,4];matrix B=[1,0;1,1];matrix C=A./B;");
    rejected("pentarnionmatrix P=[i,j];pentarnionmatrix Q=[j,k];pentarnionmatrix R=P./Q;");
}

void pentarnionOrder() {
    values("use_algebra(\"A_gamma\",gamma=1);pentarnionmatrix P=[i];pentarnionmatrix Q=[j];pentarnionmatrix R=[j];"
        "pentarnionmatrix L=(P*Q)*R;pentarnionmatrix M=P*(Q*R);"
        "display(coeff(L(1),3));display(coeff(M(1),1));",{.5,-1},1e-14);
    values("pentarnionmatrix P=[i,j];pentarnionmatrix Q=[j;k];pentarnionmatrix L=P*Q;"
        "pentarnion reference=i*j+j*k;display(coeff(L(1),0)-coeff(reference,0));"
        "display(coeff(L(1),1)-coeff(reference,1));display(coeff(L(1),2)-coeff(reference,2));"
        "display(coeff(L(1),3)-coeff(reference,3));display(coeff(L(1),4)-coeff(reference,4));",{0,0,0,0,0},1e-14);
}

void constructorsAndRandom() {
    values("matrix A=[1,2;3,4];matrix B=A.^2;display(B(1));display(B(2));display(B(3));display(B(4));",{1,9,4,16});
    values("pentarnionmatrix P=[i,j];matrix E=[2,-1];matrix R=P.^E;display(scalar(R(1)));display(coeff(R(2),2));",{-1,-1},1e-14);
    values("matrix T=sin([0,1]);display(T(1));display(T(2));",{0,std::sin(1.0)},1e-14);
    rejected("pentarnionmatrix P=[i];matrix E=[0.5];matrix R=P.^E;");
    rejected("pentarnionmatrix P=[i];matrix R=P.^100001;");
    values("matrix Z=zeros(2,3);matrix O=ones(2,3);matrix I=eye(3);"
        "display(Z(2,3));display(O(1,3));display(I(1,1));display(I(1,2));display(I(3,3));",{0,1,1,0,1});
    const std::string generate="matrix U=rand(2,3);matrix N=randn(2,3);matrix K=randi(7,2,3);"
        "display(U(1));display(U(6));display(N(1));display(N(6));display(K(1));display(K(6));";
    const auto a=runtime::execute("rng(12345);"+generate),b=runtime::execute("rng(12345);"+generate),c=runtime::execute("rng(67890);"+generate);
    if(!a.ok())diagnostics(a);
    expect(a.ok()&&b.ok()&&c.ok(),"seeded matrix random constructors run");
    expect(a.output.size()==6&&a.output==b.output,"same seed reproduces uniform/normal/integer matrix streams");
    expect(a.output!=c.output,"different seeds change generated matrix values");
    if(a.output.size()==6){
        for(auto index:{0,1}){const auto value=std::stod(a.output[index]);expect(value>=0&&value<1,"rand lies in [0,1)");}
        for(auto index:{2,3})expect(std::isfinite(std::stod(a.output[index])),"randn is finite");
        for(auto index:{4,5}){const auto value=std::stod(a.output[index]);expect(value>=1&&value<=7&&value==std::floor(value),"randi scalar bound is inclusive and integral");}
    }
    runtime::InterpreterSession session;
    expect(session.submit("rng(12345);").ok(),"console accepts rng seed");
    const auto first=session.submit("matrix U=rand(2,3);display(U(1));display(U(6));");
    const auto second=session.submit("matrix V=rand(2,3);display(V(1));display(V(6));");
    const auto reference=runtime::execute("rng(12345);matrix U=rand(2,3);matrix V=rand(2,3);display(U(1));display(U(6));display(V(1));display(V(6));");
    auto combined=first.output;combined.insert(combined.end(),second.output.begin(),second.output.end());
    expect(first.ok()&&second.ok()&&reference.ok()&&combined==reference.output,"console preserves random stream between submissions");
    expect(session.submit("clear;matrix W=rand(1,1);").ok(),"clear preserves the seeded random stream");
    const auto afterClear=session.submit("display(W(1));");
    const auto clearReference=runtime::execute("rng(12345);matrix U=rand(2,3);matrix V=rand(2,3);matrix W=rand(1,1);display(W(1));");
    expect(afterClear.ok()&&afterClear.output==clearReference.output,"clear does not reseed the console generator");
    const auto afterRun=session.runFile("matrix Z=rand(1,1);display(Z(1));");
    const auto runReference=runtime::execute("rng(12345);matrix U=rand(2,3);matrix V=rand(2,3);matrix W=rand(1,1);matrix Z=rand(1,1);display(Z(1));");
    expect(afterRun.ok()&&afterRun.output==runReference.output,"Run File preserves the session random stream");
    session.reset();runtime::InterpreterSession fresh;
    const auto reset=session.submit("matrix Z=rand(1,1);display(Z(1));"),defaultSeed=fresh.submit("matrix Z=rand(1,1);display(Z(1));");
    expect(reset.ok()&&defaultSeed.ok()&&reset.output==defaultSeed.output,"Reset Workspace restores initial random-generator state");
    runtime::InterpreterSession transaction;
    expect(transaction.submit("rng(9876);").ok(),"prepare cancellation random snapshot");
    runtime::ExecutionOptions cancel;cancel.maxOperations=100000000;cancel.cancellation=std::make_shared<std::atomic_bool>(false);
    std::thread interruption([flag=cancel.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(1));flag->store(true);});
    runtime::ExecutionResult cancelled;
    // Optimized GEMM can finish before the timer fires. Keep the request alive
    // until cancellation, rather than assuming a particular multiplication time.
    try{cancelled=transaction.submit("matrix prefix=rand(1,1);matrix A=rand(300,300);while(true){matrix B=A*A;}",{},cancel);}
    catch(...){interruption.join();throw;}
    interruption.join();
    expect(cancelled.status==runtime::ExecutionStatus::Cancelled,"cancel a request containing random generation and matrix work");
    const auto next=transaction.submit("matrix next=rand(1,1);display(next(1));");
    const auto restart=runtime::execute("rng(9876);matrix next=rand(1,1);display(next(1));");
    expect(next.ok()&&next.output==restart.output,"cancelled request restores random generator snapshot");
    rejected("matrix A=zeros(-1,2);");
    rejected("matrix A=ones(1.5,2);");
    rejected("matrix A=randi(0,2,3);");
    rejected("rng(1.5);");
}

void boundsAndCancellation() {
    runtime::ExecutionOptions options;options.maxMatrixElements=6;
    auto result=runtime::execute("matrix A=zeros(3,3);",{},options);
    expect(!result.ok(),"matrix element budget rejects constructor before allocation");
    result=runtime::execute("matrix A=[1,2,3;4,5,6;7,8,9];",{},options);
    expect(!result.ok(),"matrix element budget rejects literals");
    options={};options.maxMatrixCoordinates=12;
    result=runtime::execute("pentarnionmatrix P=[i,j,k];",{},options);
    expect(!result.ok(),"Pentarnion matrices count all five retained coordinates");
    runtime::InterpreterSession alias;
    options={};options.maxMatrixCoordinates=4;
    expect(alias.submit("matrix A=ones(2,2);matrix B=A;",{},options).ok(),"shared matrix aliases count stored coordinates once");
    result=alias.submit("A(1)=2;",{},options);
    expect(!result.ok(),"indexed copy-on-write assignment checks expanded unique storage budget");
    const auto unchanged=alias.submit("display(A(1));display(B(1));",{},options);
    expect(unchanged.ok()&&unchanged.output==std::vector<std::string>{"1","1"},"failed indexed assignment preserves old aliases");
    options={};options.maxOperations=10;
    result=runtime::execute("matrix A=ones(40,40);matrix B=A*A;",{},options);
    expect(!result.ok(),"matrix arithmetic obeys operation budget");
    options={};options.cancellation=std::make_shared<std::atomic_bool>(true);
    result=runtime::execute("matrix A=rand(100,100);matrix B=A*A;",{},options);
    expect(result.status==runtime::ExecutionStatus::Cancelled,"matrix operations observe cancellation");
}
}
int main(){
    try{literalAndIndexing();arithmeticAndOrientation();pentarnionOrder();constructorsAndRandom();boundsAndCancellation();}
    catch(const std::exception& error){expect(false,std::string("unexpected matrix test exception: ")+error.what());}
    std::cout<<"Matrix runtime tests: "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
