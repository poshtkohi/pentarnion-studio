#include "native_functions/dependency_pattern.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
namespace {
int checks=0;void require(bool yes,const char* text){++checks;if(!yes)throw std::runtime_error(text);}penta::native_functions::DependencyPattern pattern(const std::string& source){auto result=penta::language::analyse(source);if(!result.ok()){for(const auto& error:result.diagnostics)std::cerr<<error.code<<' '<<error.message<<'\n';throw std::runtime_error("Pattern fixture did not typecheck");}for(const auto& statement:result.program.statements)if(statement.function)return penta::native_functions::dependencyPattern(*statement.function);throw std::runtime_error("No fixture function");}
}
int main(){const auto cache=std::filesystem::temp_directory_path()/("penta-pattern-ad-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));try{
    const auto atZero=pattern("native function [real a,real b] sample(real x,real y){return x*x,sin(y);}");require(atZero.rows==2&&atZero.columns==2&&atZero.columnIndices.size()==2,"structural zeros not numerical zeros");require(atZero.contains(0,0)&&!atZero.contains(0,1)&&atZero.contains(1,1),"scalar structural pattern");
    const auto branch=pattern("native function real sample(real x,real y){real z=0.0;if(x>0){z=y;}else{z=2.0;}return z;}");require(branch.includesControlDependencies&&branch.contains(0,0)&&branch.contains(0,1),"branch control and both paths");
    const auto early=pattern("native function real sample(real x,real y){if(x>0){return y;}return 2.0;}");require(early.contains(0,0)&&early.contains(0,1),"early return guards");
    const auto literal=pattern("native function matrix<1,2> sample(real x,real y){return [x,y];}");require(literal.rows==2&&literal.contains(0,0)&&!literal.contains(0,1)&&literal.contains(1,1),"fixed-array result coordinate isolation");
    const auto indexing=pattern("native function real sample(matrix<2,2> x){return x(2,1);}");require(indexing.columns==4&&indexing.columnIndices==std::vector<std::size_t>{2},"row-major ABI versus MATLAB literal indices");
    const auto changed=pattern("native function real sample(matrix<2,2> x){x(2,1)=x(1,2);return x(2,1);}");require(changed.columnIndices==std::vector<std::size_t>{1},"indexed assignment dependency replaces prior cell");
    const auto groups=pattern("native function real sample(pentarnion x,real y){return coeff(x,3)+y;}");require(groups.columns==6&&groups.columnIndices==std::vector<std::size_t>({3,5}),"whole coordinate groups retain full widths");
    const auto scalarEntries=pattern("native function matrix<1,2> sample(pentarnionmatrix<1,2> x){return scalar(x);}");require(scalarEntries.columnIndices==std::vector<std::size_t>({0,5}),"scalar array intrinsic retains every entry's scalar coordinate");
    const auto pureEntries=pattern("native function real sample(pentarnionmatrix<1,2> x){pentarnionmatrix<1,2> y=pure(x);return coeff(y(2),3);}");require(pureEntries.columnIndices==std::vector<std::size_t>{8},"pure array intrinsic retains later entry coefficients");
    const auto dimensions=pattern("native function matrix<1,2> sample(matrix<3,4> x){return size(x);}");require(dimensions.rows==2&&dimensions.columnIndices.empty(),"native fixed dimensions are static constants");
    const auto transpose=pattern("native function real sample(matrix<2,3> x){matrix<3,2> y=transpose(x);return y(3,2);}");require(transpose.columnIndices==std::vector<std::size_t>{5},"transposed array coordinate incidence");
    const auto shadow=pattern("native function real sample(real x,real y){if(x>0){real z=y;}return x;}");require(shadow.contains(0,0)&&!shadow.contains(0,1),"block-local result does not change returned coordinate dependency");
    const auto cancellation=pattern("native function real sample(real x){return x-x;}");require(cancellation.contains(0,0),"no symbolic cancellation guessed");
    const std::vector<std::string> containmentFixtures{
        "native function pentarnionmatrix<1,2> sample(pentarnionmatrix<1,2> x,real w){return x.*pure(x)*w;}",
        "native function matrix<2,2> sample(matrix<2,3> x,matrix<3,2> y){return x*y;}",
        "native function real sample(real x,real y){real z=sin(x);if(x>0){z=y*y;}return z;}",
        "native function matrix<1,2> sample(pentarnionmatrix<1,2> x){return scalar(x);}",
        "native function real sample(pentarnionmatrix<1,2> x){return coeff(pure(x)(2),3);}",
        "algebra H=builtin_algebra(\"quaternion\");native function element<H> sample(element<H> x,element<H> y){return x*y+conj(x);}",
        "algebra H=builtin_algebra(\"quaternion\");native function real sample(element<H> x){element y=x;return coeff(y,3);}",
        "native function real sample(real x,real y){matrix<1,2> z=[x,y];z(1)=y;return z(1);}"
        ,"native function real sample(real x){return coeff(pure(x),3);}"
        ,"native function real sample(tensor<2,2,2> x){return x(2,1,2);}"
        ,"native function real sample(tensor<2,2,2> x){x(2,1,2)=x(1,2,1);return x(2,1,2);}"
        ,"native function real sample(tensor<pentarnion,2,1,2> x){return coeff(x(2,1,2),4);}"
        ,"native function tensor<2,1,1> sample(tensor<complex,2,1,1> x){return coeff(x,1);}"
        ,"native function matrix<8,1> sample(tensor<2,2,2> x){return tensorvalues(x);}"
        ,"native function real sample(real x,real y){tensor<2,1,1> z=tensor([x,y],[2,1,1]);return z(2);}"
        ,"algebra H=builtin_algebra(\"quaternion\");native function tensor<H,2,1,1> sample(tensor<H,2,1,1> x,real w){return x.*conj(x)+H.i*x*w;}"
    };
    penta::native_functions::Options native;native.build.cacheDirectory=cache;std::mt19937_64 random(831);std::uniform_real_distribution<double> distribution(-.8,.8);
    for(const auto& source:containmentFixtures){auto checked=penta::language::analyse(source);if(!checked.ok()){for(const auto& diagnostic:checked.diagnostics)std::cerr<<diagnostic.code<<' '<<diagnostic.message<<'\n';throw std::runtime_error("Native containment fixture did not typecheck");}std::shared_ptr<penta::language::FunctionDefinition> function;for(const auto& statement:checked.program.statements)if(statement.function)function=statement.function;const auto dependency=penta::native_functions::dependencyPattern(*function);penta::native_functions::Callback callback(*function,native);for(unsigned trial=0;trial<4;++trial){std::vector<double> input(callback.inputCount());for(auto& value:input)value=distribution(random);const auto derivative=callback.evaluate(1,input);require(derivative.values.size()==dependency.rows&&derivative.columns==dependency.columns,"pattern and native ABI dimensions");for(std::size_t row=0;row<dependency.rows;++row)for(std::size_t col=0;col<dependency.columns;++col)require(dependency.contains(row,col)||derivative.jacobian[row*dependency.columns+col]==0,"every omitted structural edge is numerically zero in independent native AD probes");}}
    const auto same=pattern("native function [real a,real b] sample(real x,real y) { return x*x, sin(y); }");require(same.key==atZero.key,"whitespace independent identity");require(branch.key!=atZero.key,"body changes change pattern identity");std::cout<<"PASS "<<checks<<" compiler structural-pattern checks\n";
}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}std::error_code ignored;std::filesystem::remove_all(cache,ignored);}
