#include "runtime/interpreter.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& message){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
void diagnostics(const penta::runtime::ExecutionResult& r){for(const auto& d:r.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:r.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}
void values(const std::string& source,const std::vector<double>& expected,double tolerance=1e-10){const auto result=penta::runtime::execute(source);if(!result.ok())diagnostics(result);expect(result.ok(),"valid numerical source executes");expect(result.output.size()==expected.size(),"scalar output count");for(std::size_t n=0;n<std::min(result.output.size(),expected.size());++n){try{const auto actual=std::stod(result.output[n]);expect(std::abs(actual-expected[n])<=tolerance*(1+std::abs(expected[n])),"output "+std::to_string(n)+" is "+std::to_string(expected[n])+", got "+result.output[n]);}catch(...){expect(false,"numeric output expected");}}}
void rejects(const std::string& source){expect(!penta::runtime::execute(source).ok(),"invalid numeric operation rejected: "+source);}
}
int main(){
    values("matrix A=[1,2,3;4,5,6];matrix R=reshape(A,3,2);display(R(1,1));display(R(1,2));display(R(2,1));display(R(2,2));display(R(3,1));display(R(3,2));matrix B=reshape(R,2,[]);assert(B==A);matrix C=reshape(R,[2,3]);assert(C==A);",{1,5,4,3,2,6});
    values("matrix D=diag([1,2],1);display(size(D,1));display(D(1,2));display(D(2,3));matrix v=diag(D,1);display(v(1));display(v(2));matrix P=diag([i,j]);display(coeff(P(1,1),1));display(coeff(P(2,2),2));",{3,1,2,1,2,1,1});
    values("[matrix X,matrix Y]=meshgrid([1,2],[10,20,30]);display(size(X,1));display(size(X,2));display(X(3,2));display(Y(3,2));[matrix U,matrix V]=ndgrid([1,2],[10,20,30]);display(size(U,1));display(size(U,2));display(U(2,3));display(V(2,3));matrix Z=ndgrid([1,2]);display(size(Z,1));",{3,2,2,30,2,3,2,30,2});
    values("matrix A=[10,20;30,40];matrix B=A;matrix s=A(A>20);display(s(1));display(s(2));matrix f=find(A>20);display(f(1));display(f(2));A(A>20)=0;display(A(2,1));display(A(2,2));display(B(2,2));matrix q=find(logical([0,1;1,0]));display(q(1));display(q(2));",{30,40,2,4,0,0,40,2,3});
    values("matrix A=[1,2;3,4];matrix mask=A>2;matrix reshaped=reshape(mask,1,4);matrix selected=A(reshaped);display(selected(1));display(selected(2));matrix diagonal=diag(logical([0,1]));matrix D=[1,2;3,4];matrix entry=D(diagonal);display(entry(1));matrix previous=mask;mask(1,1)=3;display(mask(1,1));display(previous(1,1));matrix column=A(:,logical([0,1]));display(column(1));display(column(2));",{3,4,4,1,0,2,4});
    values("display(dot([1,2,3],[4;5;6]));display(norm([3,4]));display(norm([3,4],1));display(norm([3,4],\"inf\"));display(norm([1,2;3,4],\"fro\"));",{32,5,7,4,std::sqrt(30.)});
    values("matrix x=0:0.25:1;matrix y=2*x+1;display(trapz(x,y));matrix c=cumtrapz(x,y);display(c(1));display(c(end));matrix a=trapz([0,0.5,1],[1,1;2,2;3,3],1);display(a(1));display(a(2));matrix p=1+2*x*i;pentarnion z=trapz(x,p);display(scalar(z));display(coeff(z,1));display(trapz([]));",{2,0,2,2,2,1,1,0});
    values("matrix x=[0,1,2];matrix y=1+2*x;display(interp1(x,y,0.5));matrix q=interp1(x,y,[0.25,1.75]);display(q(1));display(q(2));display(interp1(x,y,3,\"linear\",\"extrap\"));matrix p=1+x*i;pentarnion a=interp1(x,p,0.5);display(scalar(a));display(coeff(a,1));",{2,1.5,4.5,7,1,.5});
    values("matrix d=diff([1,4,9,16]);display(d(1));display(d(3));matrix e=diff([1,4,9,16],2);display(e(1));display(e(2));[matrix X,matrix Y]=meshgrid([-1,0,2],[0,0.5,2,3]);matrix F=X.^2+3*Y.^2;[matrix gx,matrix gy]=gradient(F,[-1,0,2],[0,0.5,2,3]);display(gx(1,1));display(gx(4,3));display(gy(1,1));display(gy(4,3));matrix L=laplacian(F,[-1,0,2],[0,0.5,2,3]);display(L(1,1));display(L(3,2));matrix div=divergence(2*X,3*Y,[-1,0,2],[0,0.5,2,3]);display(div(2,2));",{3,7,2,2,-2,4,0,18,8,8,5});
    values("[matrix X,matrix Y]=meshgrid(0:0.5:1);matrix D=divergence(2*X,3*Y,0.5);display(D(2,2));",{5});
    values("function real cubic(real x){return x*x*x;}display(integral(cubic,0,1));display(integral(cubic,1,0));function real wave(real x){return sin(x);}display(integral(wave,0,3.141592653589793));function pentarnion poly(real x){return 1+x*i+x*x*j;}pentarnion q=integral(poly,0,1);display(scalar(q));display(coeff(q,1));display(coeff(q,2));",{.25,-.25,2,1,.5,1./3},1e-8);
    rejects("matrix A=reshape([1,2],3,2);");rejects("matrix A=reshape([1,2],[1,1,2]);");rejects("matrix A=diag(1:100000);");
    rejects("matrix A=[1,2];matrix q=A(logical([1,0,1]));");rejects("real x=dot([i,j],[1,2]);");rejects("real x=norm([1,2;3,4]);");
    rejects("real q=interp1([0,0],[1,2],0.5);");rejects("real q=interp1([0,1],[1,2],2);");rejects("matrix g=gradient([1,2,3],0);");rejects("matrix g=gradient([1,2,3],[0,1,1]);");
    rejects("function real singular(real x){return 1/x;}real a=integral(singular,-1,1);");
    for(const auto* source:{"matrix L=laplacian([1,4]);","matrix L=laplacian([1;4]);","matrix L=laplacian([i,j]);","matrix L=laplacian([1,4;9,16;25,36]);"}){
        const auto insufficient=penta::runtime::execute(source);
        expect(!insufficient.ok(),"second derivative rejects an undersampled dimension");
        expect(std::any_of(insufficient.runtimeDiagnostics.begin(),insufficient.runtimeDiagnostics.end(),[](const auto& diagnostic){return diagnostic.message.find("Insufficient data")!=std::string::npos;}),"second derivative reports insufficient data");
    }
    values("[matrix g,matrix unused]=gradient([1,5],[0,2]);display(g(1));display(g(2));matrix L=laplacian([1,0,4],[-1,0,2]);display(L(1));display(L(3));",{2,2,2,2});
    penta::runtime::ExecutionOptions small;small.maxOperations=1000;expect(!penta::runtime::execute("matrix a=ones(20,20);matrix d=diff(a,10,1);",{},small).ok(),"calculus work charges operation budget");
    penta::runtime::ExecutionOptions ode;ode.odeCacheDirectory=std::filesystem::temp_directory_path()/("penta-hermite-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto dense=penta::runtime::execute("ode decay(t,real y;real rate){y'=-rate*y;}solution a=odesolve(decay,y=1,rate=1,t0=0,t1=1,sample_step=0.25,dense_output=0,rtol=1e-12,atol=1e-14);solution b=odesolve(decay,y=1,rate=1,t0=0,t1=1,sample_step=0.125,dense_output=0,rtol=1e-12,atol=1e-14);display(deval(a,0.125));display(deval(b,0.0625));display(deval(a,0));display(deval(a,1));",{},ode);
    if(!dense.ok())diagnostics(dense);
    expect(dense.ok(),"cubic Hermite solution evaluation");
    if(dense.output.size()==4){const auto e1=std::abs(std::stod(dense.output[0])-std::exp(-.125)),e2=std::abs(std::stod(dense.output[1])-std::exp(-.0625));expect(e1>e2*8&&e1<e2*30,"Hermite midpoint error shows fourth-order refinement");expect(std::abs(std::stod(dense.output[2])-1)<1e-12,"dense left endpoint exact");expect(std::abs(std::stod(dense.output[3])-std::exp(-1))<1e-9,"dense right endpoint matches solve");}else expect(false,"dense output count");
    const auto sourceRoot=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    for(const auto& category:std::vector<std::pair<std::string,std::size_t>>{{"array-shapes",12},{"numerical-calculus",504}}){
        const auto exampleFile=sourceRoot/"examples"/category.first/"main.p";
        std::ifstream stream(exampleFile);const std::string source((std::istreambuf_iterator<char>(stream)),{});
        expect(!source.empty(),category.first+" runnable example exists");
        penta::language::AnalysisOptions analysis;analysis.sourceDirectory=exampleFile.parent_path();
        const auto example=penta::runtime::execute(source,analysis,ode);
        if(!example.ok())diagnostics(example);
        expect(example.ok(),category.first+" original example executes including figure commands");
        std::size_t plotCount=0,pointCount=0;
        for(const auto& action:example.figureActions)if(action.kind==penta::runtime::FigureActionKind::Plot){++plotCount;for(const auto& curve:action.curves)pointCount+=curve.points.size();}
        expect(plotCount==(category.first=="array-shapes"?2:4),category.first+" emits expected subplot plots");
        expect(pointCount==category.second,category.first+" retains every plotted sample");
    }
    std::error_code ignored;std::filesystem::remove_all(ode.odeCacheDirectory,ignored);
    std::cout<<checks<<" numerical foundation checks, "<<failures<<" failures\n";return failures?1:0;
}
