#include "ode/native_ode.hpp"
#include "language/frontend.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using V=std::array<double,5>;
using M=std::array<V,5>;
int checks=0;
void require(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
M left(const V& q){
    const auto [a,b,c,d,e]=q;const double r=std::sqrt(2.);
    return {{{a,-b,-c,-d,-e},{b,a,(-d+e)/r,(c+e)/r,(-c-d)/r},
      {c,(d+e)/r,a,(-b+e)/r,(-b-d)/r},{d,(-c+e)/r,(b+e)/r,a,(-b-c)/r},
      {e,(-c+d)/r,(b-d)/r,(-b+c)/r,a}}};
}
M right(V q){for(int c=1;c<5;++c)q[c]=-q[c];M m=left(q);for(int r=0;r<5;++r)for(int c=0;c<5;++c)if((r==0)!=(c==0))m[r][c]=-m[r][c];return m;}
V mv(const M& a,const V& b){V x{};for(int r=0;r<5;++r)for(int c=0;c<5;++c)x[r]+=a[r][c]*b[c];return x;}
M mm(const M&a,const M&b){M x{};for(int r=0;r<5;++r)for(int c=0;c<5;++c)for(int k=0;k<5;++k)x[r][c]+=a[r][k]*b[k][c];return x;}
M plus(M a,const M&b){for(int r=0;r<5;++r)for(int c=0;c<5;++c)a[r][c]+=b[r][c];return a;}
V A{{-.5,-2.,.5,-1.,-.5}},B{{1.,0.,2.,0.,-.5}},p0{{.5,-.5,-.5,.5,-.5}};
std::vector<double> params(double eta){std::vector<double>x(A.begin(),A.end());x.insert(x.end(),B.begin(),B.end());x.push_back(eta);return x;}
const std::string model=R"(
ode bracketing(t, pentarnion p; pentarnion A, pentarnion B, real eta) {
    p' = A*p + (1-eta)*(B*(p*p)) + eta*((B*p)*p);
}
)";
void run(){
    const auto checked=penta::language::analyse(model);
    for(const auto& d:checked.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
    require(checked.ok(),"paper ODE must pass static validation");
    std::shared_ptr<penta::language::OdeDefinition> definition;
    for(const auto& statement:checked.program.statements)if(statement.ode)definition=statement.ode;
    require(bool(definition),"paper ODE definition missing");
    penta::ode::NativeOptions native;
    native.cacheDirectory=std::filesystem::temp_directory_path()/
        ("penta-paper-reference-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::string cacheKey;
    for(double eta:{0.,.37,1.})for(int n=0;n<5;++n){
        V p=p0;for(int j=0;j<5;++j)p[j]+=.13*n*(j-2);
        const auto evaluation=penta::ode::evaluateNative(*definition,1.,.7,{p.begin(),p.end()},params(eta),native);
        if(cacheKey.empty())cacheKey=evaluation.key;
        require(evaluation.key==cacheKey,"parameter values must reuse the same native kernel");
        const auto lp=left(p),rp=right(p),lb=left(B),la=left(A);
        const auto lbp=left(mv(lb,p)),first=mm(lb,plus(lp,rp)),last=plus(mm(rp,lb),lbp);
        V expected=mv(la,p),pp=mv(lp,p),bp=mv(lb,p);
        const auto lquad=mv(lb,pp),rquad=mv(left(bp),p);
        for(int r=0;r<5;++r){
            expected[r]+=(1-eta)*lquad[r]+eta*rquad[r];
            require(std::abs(expected[r]-evaluation.rhs[r])<5e-13,"native RHS disagrees with paper coordinate matrices");
            for(int c=0;c<5;++c){
                const double j=la[r][c]+(1-eta)*first[r][c]+eta*last[r][c];
                require(std::abs(j-evaluation.jacobian[r*5+c])<5e-13,"automatic native Jacobian disagrees with paper tangent equation");
            }
        }
    }
    const std::array<std::pair<double,const char*>,3> references{{{0.,"eta0"},{1.,"eta1"},{.37,"eta037"}}};
    for(const auto& [eta,name]:references){
        penta::ode::SolveOptions options;options.t0=0;options.t1=5;options.sampleStep=.1;options.method=penta::ode::Method::Bdf;options.rtol=1e-12;options.atol=1e-14;
        const auto solution=penta::ode::solve(*definition,1.,{p0.begin(),p0.end()},params(eta),options,native);
        require(solution->stats.cacheHit,"integration should reuse evaluated native model");
        require(solution->stats.key==cacheKey,"solver settings must preserve native identity");
        require(solution->times.size()==51,"paper reference sampling count");
        std::ifstream file(std::filesystem::path(PENTA_SOURCE_DIR)/"tests/ode/reference"/(std::string("paper_bracketing_")+name+".csv"));
        require(bool(file),"independent DOP853 reference missing");
        std::string line;std::getline(file,line);std::size_t row=0;double maximum=0.;
        while(std::getline(file,line)){
            std::replace(line.begin(),line.end(),',',' ');std::istringstream input(line);double t;V v{};input>>t;for(auto& x:v)input>>x;
            require(bool(input),"reference row parse failure");require(row<solution->times.size(),"reference sample overflow");
            require(std::abs(t-solution->times[row])<1e-13,"reference output time mismatch");
            for(int c=0;c<5;++c)maximum=std::max(maximum,std::abs(v[c]-solution->values[row*5+c]));
            ++row;
        }
        std::cout<<"paper "<<name<<" max absolute error vs DOP853: "<<maximum<<std::endl;
        require(row==51,"reference count mismatch");require(maximum<2e-7,"CVODE disagrees with independent DOP853 reference");
        options.jacobian="finite_difference";
        const auto finite=penta::ode::solve(*definition,1.,{p0.begin(),p0.end()},params(eta),options,native);
        require(finite->stats.key==cacheKey && finite->stats.cacheHit,"Jacobian selection must preserve kernel identity");
        for(std::size_t c=0;c<finite->values.size();++c)require(std::abs(finite->values[c]-solution->values[c])<2e-7,"analytic/FD integration mismatch");
        std::cout<<"paper "<<name<<" max absolute error vs DOP853: "<<maximum<<'\n';
    }
    const V periodic{{.352211456319145,.280943768469983,.580647718066596,.274403530427830,.442578560477541}};
    penta::ode::SolveOptions cycle;cycle.t0=0;cycle.t1=5.795118818486838;cycle.sampleStep=cycle.t1;cycle.rtol=1e-11;cycle.atol=1e-13;cycle.method=penta::ode::Method::Bdf;
    const auto orbit=penta::ode::solve(*definition,1.,{periodic.begin(),periodic.end()},params(1.),cycle,native);
    double closure=0.;for(int c=0;c<5;++c)closure+=std::pow(orbit->values[5+c]-periodic[c],2);
    require(orbit->times.size()==2 && std::sqrt(closure)<2e-8,"paper eta1 periodic orbit closure");
    std::cout<<"paper eta1 periodic closure: "<<std::sqrt(closure)<<'\n';
    std::cout<<checks<<" paper/reference checks passed\n";
}
}
int main(){try{run();return 0;}catch(const std::exception& e){std::cerr<<"FAILED: "<<e.what()<<'\n';return 1;}}
