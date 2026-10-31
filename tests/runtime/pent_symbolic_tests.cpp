#include "symbolic/pent_symbolic.hpp"
#include "algebra/pentarnion.hpp"
#include "language/frontend.hpp"
#ifndef PENTA_PENT_SYMBOLIC_NATIVE_ONLY
#include "runtime/interpreter.hpp"
#endif
#include <cmath>
#include <chrono>
#include <iostream>
#include <string>
namespace {
int checks=0,failures=0;
void expect(bool okay,const std::string& text){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
template<class F>void rejects(F action,int status,const char* text){bool rejected=false;try{action();}catch(const penta::symbolic::Error& e){rejected=e.status==status;}expect(rejected,text);}
penta::symbolic::Result evaluate(const std::string& input,const std::string& gamma="1",const std::string& reals="",const std::string& pentas="",const std::map<std::string,double>& bindings={}){return penta::symbolic::PentModule().execute(PENTA_PENT_NUMERIC,input,reals,pentas,gamma,"","",bindings,100000000);}
void native(){
    using penta::algebra::Pentarnion;using penta::algebra::multiply;const std::string basis[]{"1","i","j","k","ell"};
    for(const auto& gamma:{std::pair{"0",0.0},std::pair{"1/2",.5},std::pair{"1",1.0}}){const penta::algebra::Context context(gamma.second);
        for(unsigned a=0;a<5;++a)for(unsigned b=0;b<5;++b){const auto exact=evaluate(basis[a]+"*"+basis[b],gamma.first);const auto numerical=multiply(Pentarnion::basis(context,a),Pentarnion::basis(context,b));for(unsigned c=0;c<5;++c)expect(std::abs(exact.coefficients[c]-numerical.coefficients()[c])<2e-15,"Exact25-entry multiplication table vs independent numerical core.");}
        const auto p=Pentarnion(context,{.5,1,-.3,.2,.7}),q=Pentarnion(context,{-.2,.1,.6,-.4,.3}),r=Pentarnion(context,{.4,-.1,.2,.3,-.2});std::map<std::string,double> bindings;
        for(unsigned c=0;c<5;++c){bindings["p"+std::to_string(c)]=p.coefficients()[c];bindings["q"+std::to_string(c)]=q.coefficients()[c];bindings["r"+std::to_string(c)]=r.coefficients()[c];}
        const auto left=evaluate("(p*q)*r",gamma.first,"","p,q,r",bindings),right=evaluate("p*(q*r)",gamma.first,"","p,q,r",bindings);const auto nativeLeft=multiply(multiply(p,q),r),nativeRight=multiply(p,multiply(q,r));double difference=0;
        for(unsigned c=0;c<5;++c){expect(std::abs(left.coefficients[c]-nativeLeft.coefficients()[c])<1e-14,"Left grouping retained in exact expansion.");expect(std::abs(right.coefficients[c]-nativeRight.coefficients()[c])<1e-14,"Right grouping retained in exact expansion.");difference+=std::abs(left.coefficients[c]-right.coefficients[c]);}expect(difference>.001,"Exact expansion does not erase nonassociativity.");
    }
    penta::symbolic::PentModule module;auto expand=[&](const std::string& input,const std::string& reals="",const std::string& pentas=""){return module.execute(PENTA_PENT_EXPAND,input,reals,pentas,"1","","",{},100000000);};
    const auto radical=expand("i*j");expect(radical.text=="0\n0\n0\n((1/2)*sqrt(2))\n((1/2)*sqrt(2))","Normalized exact rational/radical coefficients at gamma1.");
    expect(expand("sqrt(8)-2*sqrt(2)").text=="0\n0\n0\n0\n0","Exact positive-radical normalization.");expect(expand("(i*j)*ell-i*(j*ell)").text!="0\n0\n0\n0\n0","Exact nonassociative counterexample stays nonzero.");
    const auto complexProduct=evaluate("(1/2+2*i)*(3/4-i/3)");expect(std::abs(complexProduct.coefficients[0]-25.0/24)<1e-15&&std::abs(complexProduct.coefficients[1]-4.0/3)<1e-15,"Independent real/complex subcase rational product.");
    const auto derivative=module.execute(PENTA_PENT_DIFF,"p*p","","p","1","p0","",{},100000000);const auto differentiated=evaluate("("+derivative.text.substr(0,derivative.text.find('\n'))+")","1","","p",{{"p0",2}});expect(std::abs(differentiated.coefficients[0]-4)<1e-15,"Real-coordinate derivative of p squared.");
    const auto replaced=module.execute(PENTA_PENT_SUBS,"a*i+a*a","a","","1","a","1/2",{},100000000);expect(replaced.text=="((1/4))\n((1/2))\n0\n0\n0","Exact rational substitution.");
    const auto code=module.execute(PENTA_PENT_CODE,"(p*q)*r","","p,q,r","1","ordered_product","",{},100000000);expect(penta::language::analyse(code.text).ok(),"Inspectable generated native function passes the real compiler frontend.");expect(code.text.find("real v_p0")!=std::string::npos,"Native scalar coordinates use safe parameter names.");
    rejects([&]{expand("a*i");},PENTA_NUMERIC_ARGUMENT,"Undeclared real/Pentarnion assumptions rejected.");rejects([&]{expand("sqrt(-1)");},PENTA_NUMERIC_DOMAIN,"No implicit complex radical domain.");rejects([&]{expand("1/a","a");},PENTA_NUMERIC_ARGUMENT,"No cancellation through possibly zero variable denominators.");rejects([&]{expand("i/j");},PENTA_NUMERIC_ARGUMENT,"No implicit equation division.");
    rejects([&]{module.execute(PENTA_PENT_EXPAND,"p*p","","p","2","","",{},100000);},PENTA_NUMERIC_ARGUMENT,"Gamma domain is explicit.");rejects([&]{module.execute(PENTA_PENT_EXPAND,"i*j","","","1","","",{},1);},PENTA_PENT_SYMBOLIC_LIMIT,"Native exact work budget.");
    auto cancellation=std::make_shared<std::atomic_bool>(true);rejects([&]{module.execute(PENTA_PENT_EXPAND,"i*j","","","1","","",{},1000,cancellation);},PENTA_NUMERIC_CANCELLED,"Exact cancellation before allocation.");
}
#ifndef PENTA_PENT_SYMBOLIC_NATIVE_ONLY
void language(){
    penta::runtime::ExecutionOptions settings;settings.dataDirectory=std::filesystem::temp_directory_path()/("penta-exact-symbolic-native-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(settings.dataDirectory);
    const auto run=[&](const std::string& text,bool good=true){const auto result=penta::runtime::execute(text,{},settings);expect(result.ok()==good,text);if(good&&!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.message<<'\n';}return result;};
    run("record s=symexpand(\"i*j\");assert(symcoeff(s,3)==\"((1/2)*sqrt(2))\");pentarnion n=sympentnumeric(s);assert(norm2(n-i*j)<1e-28);assert(s.source_expression==\"i*j\");");
    run("record s=symexpand(\"(p*q)*r-p*(q*r)\",options(pentarnions=\"p,q,r\"));string native_code=sympentcode(s,\"associator_kernel\");assert(native_code!=\"\");");
    run("record s=symexpand(\"a*a+i*a\",options(reals=\"a\"));record d=sympentdiff(s,\"a\");pentarnion value=sympentnumeric(d,options(a=2));assert(norm2(value-(4+i))<1e-28);record replaced=sympentsubs(s,\"a\",\"1/2\");assert(norm2(sympentnumeric(replaced)-(0.25+0.5*i))<1e-28);");
    run("record half=symexpand(\"i*j\",options(gamma=\"1/2\"));pentarnion value=sympentnumeric(half);",false);
    run("use_algebra(\"A_gamma\",gamma=0.5);record half=symexpand(\"i*j\",options(gamma=\"1/2\"));assert(norm2(sympentnumeric(half)-i*j)<1e-28);");
    run("record s=symexpand(\"a*i\");",false);run("record s=symexpand(\"i*j\");string c=symcoeff(s,5);",false);
    const auto generated=penta::symbolic::PentModule().execute(PENTA_PENT_CODE,"a*(i*j)","a","","1","exact_table_native","",{},1000000);const auto evaluated=run(generated.text+"\npentarnion generated_value=exact_table_native(0.7);assert(norm2(generated_value-0.7*(i*j))<1e-28);");if(evaluated.ok())expect(evaluated.nativeCompilation.compilerAttempts>0,"Generated exact coefficient function compiled and executed numerically against independent native algebra.");
}
#endif
}
int main(){try{native();
#ifndef PENTA_PENT_SYMBOLIC_NATIVE_ONLY
language();
#endif
}catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}std::cout<<checks<<" exact Pentarnion symbolic checks, "<<failures<<" failures\n";return failures?1:0;}
