#include "runtime/interpreter.hpp"
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int checks=0,failures=0;
void expect(bool condition,const std::string& message){++checks;if(!condition){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
void diagnostics(const penta::runtime::ExecutionResult& result) {
    for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';
    for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';
}
void values(const std::string& source,const std::vector<double>& expected,double tolerance=0) {
    auto result=penta::runtime::execute(source);
    if(!result.ok())diagnostics(result);
    expect(result.ok(),"array language execution succeeds: "+source);
    expect(result.output.size()==expected.size(),"expected scalar output count");
    for(std::size_t index=0;index<std::min(result.output.size(),expected.size());++index) {
        try{
            std::size_t consumed=0;const auto actual=std::stod(result.output[index],&consumed);
            expect(consumed==result.output[index].size()&&std::isfinite(actual)&&std::abs(actual-expected[index])<=tolerance*(1+std::abs(expected[index])),
                "output "+std::to_string(index)+" equals "+std::to_string(expected[index])+", got "+result.output[index]);
        }catch(...){expect(false,"numeric scalar output expected");}
    }
}
void rejects(const std::string& source) {
    const auto result=penta::runtime::execute(source);
    expect(!result.ok(),"invalid array language operation rejected: "+source);
}
void rangesAndConcatenation() {
    values("vector v=1:5;display(length(v));display(v(1));display(v(end));"
        "vector w=2:-0.5:0;display(w(2));display(w(end));"
        "matrix empty=5:1;display(numel(empty));display(size(empty,1));display(size(empty,2));",{5,1,5,1.5,0,0,1,0});
    values("matrix v=0:0.1:0.3;display(length(v));display(v(end));"
        "matrix w=3:-1:1;display(w(2));matrix a=[1,2:3;4:6];"
        "display(size(a,1));display(size(a,2));display(a(2,1));display(a(2,3));",{4,.3,2,2,3,4,6},1e-14);
    values("matrix A=[1,2;3,4];matrix B=[A,zeros(2,1);ones(1,3)];"
        "display(size(B,1));display(size(B,2));display(B(2,2));display(B(3,3));"
        "matrix C=(1:4).*2;display(C(3));matrix D=sin(0:1:2);display(D(2));",{3,3,4,1,6,std::sin(1.)},1e-14);
    rejects("matrix v=1:0:5;");rejects("matrix v=1:100001;");
    rejects("matrix A=[1:3;4:5];");rejects("matrix A=[ones(2,1),ones(3,1)];");
}
void slicesAndEnd() {
    values("matrix A=[1,2,3;4,5,6;7,8,9];vector c=A(:,2);"
        "display(size(c,1));display(size(c,2));display(c(1));display(c(end));"
        "matrix B=A(1:2,end-1:end);display(B(1,1));display(B(2,2));"
        "matrix C=A([2,1],[3,1]);display(C(1,1));display(C(1,2));display(C(2,1));display(C(2,2));",
        {3,1,2,8,2,6,6,4,3,1});
    values("matrix A=[1,2,3;4,5,6];matrix c=A(:);display(size(c,1));display(size(c,2));display(c(2));display(c(3));"
        "vector r=[10,20,30,40];matrix s=r(2:2:end);display(size(s,1));display(size(s,2));display(s(2));"
        "vector v=[10;20;30;40];matrix t=v([1,3]);display(size(t,1));display(size(t,2));display(t(2));",{6,1,4,2,1,2,40,2,1,30});
    values("matrix A=[1,2,3;4,5,6;7,8,9];display(A(end,A(1,1)));display(A(1,A(1,end)-end+3));"
        "matrix e=A(2:1,:);display(size(e,1));display(size(e,2));",{7,3,0,3});
    rejects("matrix A=[1,2;3,4];display(A(0:2));");
    rejects("matrix A=[1,2;3,4];display(A([1,1.5]));");
    rejects("matrix A=[1,2;3,4];display(A([1,2;3,4]));");
    rejects("display(end);");rejects("matrix A=[1,2];display(A(end+1));");
}
void sliceAssignmentAndAliases() {
    values("matrix A=[1,2,3;4,5,6;7,8,9];matrix B=A;A(1:2,2:3)=[20,30;50,60];"
        "display(A(1,2));display(A(2,3));display(B(2,3));A(:,1)=0;display(A(3,1));"
        "A([1,3])=[100;300];display(A(1,1));display(A(3,1));"
        "A([1,1])=[2,3];display(A(1,1));A(1:0)=99;display(A(1,1));",{20,60,6,0,100,300,3,3});
    values("pentarnionmatrix P=[i,j;k,ell];pentarnionmatrix Q=P;P(:,end)=1+ell;"
        "display(scalar(P(1,2)));display(coeff(P(2,2),4));display(coeff(Q(1,2),2));"
        "P(1,:)=2:3;display(scalar(P(1,1)));display(scalar(P(1,2)));",{1,1,1,2,3});
    rejects("matrix A=[1,2;3,4];A(:,1)=[9,10];");
    rejects("matrix A=[1,2;3,4];A(:,1)=i;");
    rejects("matrix A=[1,2;3,4];A(1:3)=1:2;");
    rejects("matrix A=[1,2;3,4];A(3,:)=0;");
}
void reductions() {
    values("matrix A=[1,2,3;4,5,6];matrix s=sum(A);matrix r=sum(A,2);"
        "display(size(s,1));display(size(s,2));display(s(1));display(s(3));display(r(1));display(r(2));"
        "matrix m=mean(A);display(m(1));display(m(3));matrix mr=mean(A,2);display(mr(2));"
        "matrix lo=min(A);matrix hi=max(A,[],2);display(lo(2));display(hi(1));display(hi(2));",{1,3,5,9,6,15,2.5,4.5,5,2,3,6});
    values("matrix A=[1,7;5,-2];matrix B=min(A,3);matrix C=max(0,A);"
        "display(B(1,2));display(B(2,1));display(C(2,2));display(min(3,7));display(max(3,7));"
        "matrix D=min(A,[0,8;6,-3]);display(D(1,1));display(D(1,2));display(D(2,2));"
        "matrix E=sum(1:5);display(E(1));matrix F=mean(2:2:6);display(F(1));",{3,3,0,3,7,0,7,-3,15,4});
    values("pentarnionmatrix P=[1+i,2+j;3+k,4+ell];matrix S=sum(P);matrix M=mean(P,2);"
        "display(scalar(S(1)));display(coeff(S(1),1));display(coeff(S(1),3));"
        "display(scalar(M(2)));display(coeff(M(2),4));display(sum(4));display(mean(4));",{4,1,1,3.5,.5,4,4});
    values("matrix A=zeros(0,3);matrix S=sum(A);matrix M=min(A);"
        "display(size(S,1));display(size(S,2));display(S(2));display(size(M,1));display(size(M,2));"
        "matrix Z=sum([]);display(Z(1));matrix T=sum([1,2],3);display(T(2));",{1,3,0,0,3,0,2});
    rejects("matrix A=mean(zeros(0,3));");rejects("matrix A=min([i,j]);");
    rejects("matrix A=sum([1,2],0);");rejects("matrix A=max([1,2],[],0);");
    rejects("matrix A=min([1,2],[1;2]);");
}
void boundsAndCancellation() {
    penta::runtime::ExecutionOptions small;small.maxMatrixElements=3;
    auto result=penta::runtime::execute("matrix A=1:4;",{},small);
    expect(!result.ok(),"range materialization obeys configured element budget");
    auto cancelled=std::make_shared<std::atomic_bool>(true);penta::runtime::ExecutionOptions options;options.cancellation=cancelled;
    result=penta::runtime::execute("matrix A=1:1000;matrix B=A(:);",{},options);
    expect(result.status==penta::runtime::ExecutionStatus::Cancelled,"array work observes cancellation");
    penta::runtime::ExecutionOptions workspace;workspace.maxMatrixCoordinates=5;
    result=penta::runtime::execute("function real allocate(real x){matrix B=ones(2,2);return B(1)+x;}"
        "matrix A=ones(2,2);real z=allocate(1);",{},workspace);
    expect(result.status==penta::runtime::ExecutionStatus::RuntimeError,"retained matrix budgets include caller environments");
    result=penta::runtime::execute("function real same(realmatrix B){realmatrix C=B;return C(1);}"
        "matrix A=ones(2,2);real z=same(A);display(z);",{},workspace);
    if(!result.ok())diagnostics(result);
    expect(result.ok()&&result.output==std::vector<std::string>{"1"},"caller/local aliases count only once toward retained coordinate limits");
}
void solutionSeries() {
    const std::string source="ode decay(t,pentarnion p;real rate){p'=-rate*p;}"
        "solution s=odesolve(decay,p=1+i,rate=1,t0=0,t1=1,sample_step=0.25,rtol=1e-10,atol=1e-12);"
        "matrix t=s.t(2:end);matrix p=s.p(1:2:end);matrix c=coeff(s.p,1)(end-1:end);"
        "display(size(t,1));display(t(1));display(t(end));display(size(p,1));"
        "display(coeff(p(1),1));display(coeff(p(end),1));display(c(1));display(length(s.t));";
    penta::runtime::ExecutionOptions options;
    options.odeCacheDirectory=std::filesystem::temp_directory_path()/("penta-series-slice-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto result=penta::runtime::execute(source,{},options);
    std::error_code ignored;std::filesystem::remove_all(options.odeCacheDirectory,ignored);
    if(!result.ok())diagnostics(result);
    expect(result.ok(),"ODE sampled series support bounded indexing and slicing");
    const std::vector<double> expected{4,.25,1,3,1,std::exp(-1.),std::exp(-.75),5};
    expect(result.output.size()==expected.size(),"series slice output count");
    for(std::size_t index=0;index<std::min(result.output.size(),expected.size());++index)
        expect(std::abs(std::stod(result.output[index])-expected[index])<1e-7,"series slice value");
}
}
int main(){rangesAndConcatenation();slicesAndEnd();sliceAssignmentAndAliases();reductions();boundsAndCancellation();solutionSeries();std::cout<<checks<<" array tools checks, "<<failures<<" failures\n";return failures?1:0;}
