#include "runtime/interpreter.hpp"
#include "algebra/universal.hpp"
#include "runtime/data_io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

namespace {
using namespace penta;
int checks=0,failures=0;
void expect(bool okay,const std::string& label){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<label<<'\n';}}
void diagnostics(const runtime::ExecutionResult& result){
    if(result.ok())return;
    for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';
    for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.code<<": "<<diagnostic.message<<'\n';
}
runtime::ExecutionResult valid(const std::string& source,const language::AnalysisOptions& analysis={},const runtime::ExecutionOptions& execution={}){
    auto result=runtime::execute(source,analysis,execution);diagnostics(result);expect(result.ok(),"execution: "+source);return result;
}
void invalid(const std::string& source,const language::AnalysisOptions& options={}){
    const auto result=language::analyse(source,options);expect(!result.ok(),"static rejection: "+source);
}
void rejectedAtRun(const std::string& source,const runtime::ExecutionOptions& execution={}){
    const auto result=runtime::execute(source,{},execution);expect(!result.ok()&&result.analysis.ok()&&!result.runtimeDiagnostics.empty(),"runtime rejection: "+source);
}
void write(const std::filesystem::path& path,const std::string& text){
    std::ofstream file(path,std::ios::binary);file<<text;if(!file)throw std::runtime_error("Could not write test algebra fixture.");
}
std::string dualJson(const std::string& square="0"){
    return R"({"format":"StudioAlgebraDefinition","version":1,"name":"Test dual table","dimension":2,"basis":["one","eps"],"coefficient_domain":"rational","unit":["1","0"],"omitted_products":"zero","products":[{"left":0,"right":0,"terms":[[0,"1"]]},{"left":0,"right":1,"terms":[[1,"1"]]},{"left":1,"right":0,"terms":[[1,"1"]]},{"left":1,"right":1,"terms":[[0,")"+square+R"("]]}],"conjugation":[["1","0"],["0","-1"]],"metric":[["1","0"],["0","1"]]})";
}
const char* matrixJson=R"({"format":"StudioAlgebraDefinition","version":1,"name":"Real two by two matrices","dimension":4,"basis":["E11","E12","E21","E22"],"coefficient_domain":"rational","unit":["1","0","0","1"],"omitted_products":"zero","products":[{"left":0,"right":0,"terms":[[0,"1"]]},{"left":0,"right":1,"terms":[[1,"1"]]},{"left":1,"right":2,"terms":[[0,"1"]]},{"left":1,"right":3,"terms":[[1,"1"]]},{"left":2,"right":0,"terms":[[2,"1"]]},{"left":2,"right":1,"terms":[[3,"1"]]},{"left":3,"right":2,"terms":[[2,"1"]]},{"left":3,"right":3,"terms":[[3,"1"]]}]})";
const char* numericMetadata=R"({"format":"StudioAlgebraDefinition","version":1,"name":"Numeric dual table","dimension":2,"basis":["one","eps"],"coefficient_domain":"floating","unit":[1,0],"omitted_products":"zero","products":[],"metric":[[1,0],[0,1]]})";

void builtinFamilies(){
    for(const auto& [name,dimension]:std::map<std::string,int>{{"real",1},{"complex",2},{"quaternion",4},{"octonion",8},{"sedenion",16},{"dual",2},{"dual_quaternion",8},{"split_complex",2},{"split_quaternion",4},{"matrix2",4},{"jordan_sym2",3}}){
        valid("algebra A=builtin_algebra(\""+name+"\");record info=algebra_info(A);assert(info.dimension=="+std::to_string(dimension)+");assert(info.has_unit);assert(info.has_metric);assert(info.properties.unital.value);assert(info.properties.unital.evidence==\"proved_exact\");");
    }
    valid("algebra Cl=builtin_algebra(\"clifford\",options(p=2,q=1));assert(algebra_info(Cl).dimension==8);assert(Cl.e1*Cl.e1==Cl.one);assert(Cl.e3*Cl.e3==-Cl.one);assert(Cl.e1*Cl.e2==-Cl.e2*Cl.e1);");
    valid("algebra H=builtin_algebra(\"H\");algebra Q=builtin_algebra(\"quaternion\");element a=H.i+Q.j;assert(a==element(H,[0,1,1,0]));");
    invalid("algebra A=builtin_algebra(\"not_an_algebra\");");
    invalid("algebra A=builtin_algebra(\"clifford\",options(p=6,q=1));");
    invalid("integer p=2;algebra A=builtin_algebra(\"clifford\",options(p=p));");
    invalid("algebra A=builtin_algebra(\"clifford\",options(p=1,positive=2));");
    invalid("string name=\"quaternion\";algebra A=builtin_algebra(name);");
    valid("algebra M=builtin_algebra(\"matrix2\");element<M> identity=1;assert(identity==M.E11+M.E22);assert(M.E12*M.E21==M.E11);assert(M.E21*M.E12==M.E22);assert(algebra_norm(inv(identity)-identity)<1e-12);");
    valid("algebra L=builtin_algebra(\"lie_so3\");assert(algebra_info(L).dimension==3);assert(!algebra_info(L).has_unit);assert(L.x*L.y==L.z);assert(L.y*L.x==-L.z);assert(algebra_norm(L.x*L.x)==0);assert((L.x^3)==0*L.x);");
    valid("algebra J=builtin_algebra(\"jordan_sym2\");element<J> identity=1;assert(identity==J.E11+J.E22);assert(J.S12*J.S12==identity);assert(J.E11*J.S12==0.5*J.S12);assert(norm2(J.S12)==2);assert(associator(J.E11,J.S12,J.S12)==-0.5*J.E11+0.5*J.E22);");
    invalid("algebra L=builtin_algebra(\"lie_so3\");element<L> wrong=1;");
    invalid("algebra L=builtin_algebra(\"lie_so3\");element wrong=L.x+1;");
}
void quaternionArithmetic(){
    valid("algebra H=builtin_algebra(\"quaternion\");element<H> p=1+2*H.i-3*H.j+4*H.k;"
        "element q=element(H,1,2,-3,4);assert(p==q);assert(H.i*H.j==H.k);assert(H.j*H.i==-H.k);"
        "assert(H.i*H.i==-H.one);assert(commutator(H.i,H.j)==2*H.k);assert(associator(p,H.j,H.k)==0*H.one);"
        "assert(conj(q)==1-2*H.i+3*H.j-4*H.k);assert(norm2(q)==30);assert(abs(algebra_norm(q)-sqrt(30))<1e-12);"
        "assert(scalar(q)==1);assert(pure(q)==2*H.i-3*H.j+4*H.k);assert(coeff(q,3)==4);"
        "matrix c=algebra_coefficients(q);assert(size(c,1)==1);assert(size(c,2)==4);assert(c(3)==-3);"
        "element<H> real_embedding=2;assert(real_embedding==2*H.one);assert(q/2==0.5*q);assert(H.i^2==-H.one);");
    valid("algebra H=builtin_algebra(\"quaternion\");element a=1+0.5*H.i-0.25*H.k;"
        "element target=2+H.i-H.j;assert(algebra_norm(solveleft(a,a*target)-target)<1e-11);"
        "assert(algebra_norm(solveright(a,target*a)-target)<1e-11);"
        "record r=algebra_solveleft(a,a*target,options(condition_limit=1e8));"
        "assert(r.has_solution);assert(r.status==\"unique\");assert(r.rank==4);assert(r.condition_available);"
        "assert(r.relative_residual<1e-11);matrix target_column=transpose(algebra_coefficients(target));"
        "assert(norm(r.solution-target_column)<1e-11);assert(algebra_norm(a*inv(a)-H.one)<1e-11);"
        "matrix L=algebra_leftop(a);matrix R=rightop(a);assert(norm(L*target_column-transpose(algebra_coefficients(a*target)))<1e-12);"
        "assert(norm(R*target_column-transpose(algebra_coefficients(target*a)))<1e-12);");
    valid("algebra H=builtin_algebra(\"quaternion\");element x=0.3+0.4*H.i;assert(algebra_norm(exp(x)-exp(0.3)*(cos(0.4)+sin(0.4)*H.i))<1e-12);assert(algebra_norm(sin(x)*sin(x)+cos(x)*cos(x)-H.one)<1e-11);assert(algebra_norm(cosh(x)*cosh(x)-sinh(x)*sinh(x)-H.one)<1e-11);");
    valid("algebra D=builtin_algebra(\"dual\");element x=0.3+0.4*D.eps;assert(algebra_norm(exp(x)-(exp(0.3)+0.4*exp(0.3)*D.eps))<1e-12);assert(algebra_norm(sin(x)-(sin(0.3)+0.4*cos(0.3)*D.eps))<1e-12);");
    invalid("algebra L=builtin_algebra(\"lie_so3\");display(exp(L.x));");
    invalid("algebra H=builtin_algebra(\"quaternion\");element bad=H.unknown;");
    invalid("algebra H=builtin_algebra(\"quaternion\");display(coeff(H.i,4));");
    invalid("algebra H=builtin_algebra(\"quaternion\");display(algebra_basis(H,-1));");
    invalid("algebra H=builtin_algebra(\"quaternion\");element bad=element(H,1,2,3);");
    rejectedAtRun("algebra H=builtin_algebra(\"quaternion\");element bad=element(H,[1,2]);");
    rejectedAtRun("algebra H=builtin_algebra(\"quaternion\");integer index=4;display(coeff(H.i,index));");
    rejectedAtRun("algebra H=builtin_algebra(\"quaternion\");display(algebra_solveleft(H.i,H.j,options(unrecognized=1)));");
}
void nonassociativeAndDegenerate(){
    valid("algebra O=builtin_algebra(\"octonion\");element a=O.e1; element b=O.e3; element c=O.e4;"
        "assert(associator(a,b,c)==2*O.e6);assert((a*b)*c!=a*(b*c));assert(associator(a,a,c)==0*O.one);"
        "assert(algebra_info(O).properties.alternative.value);assert(!algebra_info(O).properties.associative.value);");
    valid("algebra S=builtin_algebra(\"sedenion\");element a=S.e1+S.e10; element b=S.e4+S.e15;"
        "assert(algebra_norm(a)>0);assert(algebra_norm(b)>0);assert(a*b==0*S.one);"
        "record equation=algebra_solveleft(a,0*S.one);assert(!equation.has_solution);assert(equation.rank<16);"
        "assert(equation.status==\"singular_consistent\");assert(!algebra_info(S).properties.division.value);"
        "assert(!algebra_info(S).properties.alternative.value);assert(algebra_norm(a*inv(a)-S.one)<1e-12);");
    valid("algebra D=builtin_algebra(\"dual\");assert(D.eps*D.eps==0*D.one);"
        "element x=2+3*D.eps;assert(x*x==4+12*D.eps);"
        "record inconsistent=algebra_solveleft(D.eps,D.one);assert(!inconsistent.has_solution);"
        "assert(inconsistent.status==\"singular_inconsistent\");assert(!inconsistent.condition_available);"
        "algebra DQ=builtin_algebra(\"dual_quaternion\");assert(DQ.eps*DQ.eps==0*DQ.one);assert(DQ.i*DQ.eps==DQ.eps_i);assert(DQ.eps*DQ.i==DQ.eps_i);");
    rejectedAtRun("algebra D=builtin_algebra(\"dual\");display(solveleft(D.eps,D.one));");
    rejectedAtRun("algebra D=builtin_algebra(\"dual\");display(inv(D.eps));");
    rejectedAtRun("algebra S=builtin_algebra(\"sedenion\");element a=S.e1+S.e10;display(a/a);");
    invalid("algebra O=builtin_algebra(\"octonion\");element a=O.e1*O.e3*O.e4;");
}
void schemaAndScope(){
    valid("real element=2;real algebra=3;element=element+algebra;assert(element==5);function real old_name(real x){real element=x+1;return element;}assert(old_name(2)==3);");
    valid("function real element(real x){return x*x;}assert(element(3)==9);");
    invalid("algebra H=builtin_algebra(\"quaternion\");algebra O=builtin_algebra(\"octonion\");element a=H.i+O.e1;");
    invalid("algebra H=builtin_algebra(\"quaternion\");algebra Q=builtin_algebra(\"split_quaternion\");element<H> a=Q.i;");
    invalid("algebra H=builtin_algebra(\"quaternion\");algebra O=builtin_algebra(\"octonion\");H=O;");
    invalid("algebra H=builtin_algebra(\"quaternion\");pentarnion p=H.i;");
    invalid("algebra H=builtin_algebra(\"quaternion\");element a=i+H.i;");
    invalid("element unresolved=2;");
    valid("algebra H=builtin_algebra(\"quaternion\");function element<H> rotate(element<H> x){return H.i*x*H.j;}"
        "element y=rotate(H.k);assert(y==H.one);function element<H> constructor(real x){return element(H,[x,1,0,0]);}"
        "assert(constructor(2)==2+H.i);");
    valid("algebra H=builtin_algebra(\"quaternion\");algebra O=builtin_algebra(\"octonion\");"
        "function [element<H> quaternion_value,element<O> octonion_value] pair(element<H> a,element<O> b){return a*H.j,b*O.e3;}"
        "[element<H> a,element<O> b]=pair(H.i,O.e1);assert(a==H.k);assert(b==-O.e2);"
        "callback right_i=@(element<H> x)=>x*H.i;element input=H.j;clear H;assert(coeff(right_i(input),3)==-1);");
    invalid("algebra H=builtin_algebra(\"quaternion\");algebra O=builtin_algebra(\"octonion\");function element<H> f(element<O> x){return x;}");
    valid("complex z=complex(0,1);pentarnion p=i;algebra C=builtin_algebra(\"complex\");element w=C.i;assert(z*z==complex(-1));assert(p*p==-1);assert(w*w==-C.one);");
    language::AnalysisOptions legacy;legacy.languageVersion="0.1";
    expect(language::analyse("integer algebra=1;integer element=2;integer builtin_algebra=3;integer total=algebra+element+builtin_algebra;",legacy).ok(),"0.1 identifier compatibility");
    runtime::InterpreterSession session;auto result=session.submit("algebra H=builtin_algebra(\"quaternion\");element a=H.i;");diagnostics(result);expect(result.ok(),"algebra bindings retained in session");
    result=session.submit("a*H.j");diagnostics(result);expect(result.ok()&&!result.output.empty(),"console submission retains element descriptor");
    runtime::InterpreterSession legacyName;expect(legacyName.submit("real element=2;").ok(),"contextual element name remains a workspace identifier");result=legacyName.submit("element=element+1;element");diagnostics(result);expect(result.ok()&&result.output==std::vector<std::string>{"3"},"contextual element identifier remains assignable across submissions");
}
void persistenceAndOdes(const std::filesystem::path& directory){
    runtime::ExecutionOptions execution;execution.dataDirectory=directory;execution.odeCacheDirectory=directory/".penta-cache/odes";
    language::AnalysisOptions analysis;analysis.sourceDirectory=directory;
    runtime::InterpreterSession saved;auto result=saved.runFile(
        "algebra H=builtin_algebra(\"quaternion\");algebra Cl=builtin_algebra(\"clifford\",options(p=1,q=2));"
        "element q=1+2*H.i-H.j;tuple packed=(q,H,Cl);record bundle=options(value=q,data=packed);"
        "save(\"native-algebras.pws\");",analysis,execution);
    diagnostics(result);expect(result.ok(),"save algebra definitions and nested element records/tuples");
    runtime::InterpreterSession restored;result=restored.submit("load(\"native-algebras.pws\");",analysis,execution);diagnostics(result);expect(result.ok(),"restore frozen native algebra workspace");
    result=restored.submit("assert(q==1+2*H.i-H.j);assert(bundle.value==q);assert(Cl.e1*Cl.e1==Cl.one);assert(Cl.e2*Cl.e2==-Cl.one);assert(algebra_info(H).properties.associative.evidence==\"proved_exact\");",analysis,execution);diagnostics(result);expect(result.ok(),"restored descriptors retain factory semantics and exact signatures");
    result=restored.submit("algebra Other=builtin_algebra(\"split_quaternion\");q=Other.i;",analysis,execution);expect(!result.ok(),"same-width split quaternion assignment does not alter restored quaternion schema");
    valid("ode complex_rotation(t,complex z;real omega){z'=omega*(complex(0,1)*z);}"
        "solution s=odesolve(complex_rotation,z=complex(1),omega=2.0,t0=0,t1=0.5,sample_step=0.1,rtol=1e-10,atol=1e-12,jacobian=\"analytic\");"
        "complex final=s.z(end);assert(abs(final-complex(cos(1),sin(1)))<1e-8);assert(abs(coeff(s.z,1)(end)-sin(1))<1e-8);",analysis,execution);
    const std::string genericOde=
        "algebra H=builtin_algebra(\"quaternion\");element omega=0.2+0.3*H.k;"
        "ode generic_rotation(t,element<H> q;element<H> omega){q'=omega*q;}"
        "solution s=odesolve(generic_rotation,q=H.one,omega=omega,t0=0,t1=0.5,sample_step=0.1,rtol=1e-10,atol=1e-12,jacobian=\"analytic\",sensitivities=\"parameters\");"
        "element expected=exp(0.1)*(cos(0.15)+sin(0.15)*H.k);assert(algebra_norm(s.q(end)-expected)<1e-8);"
        "matrix S0=odesens(s,1);matrix S3=odesens(s,4);assert(size(S0,2)==4);"
        "assert(abs(S0(end,1)-0.5*coeff(expected,0))<1e-8);assert(abs(S0(end,4)-0.5*coeff(expected,3))<1e-8);"
        "assert(abs(S3(end,1)+0.5*exp(0.1)*sin(0.15))<1e-8);assert(abs(S3(end,4)-0.5*exp(0.1)*cos(0.15))<1e-8);"
        "record first_stats=odestats(s);assert(first_stats.sensitivity_count==4);"
        "solution changed=odesolve(generic_rotation,q=H.one,omega=0.25+0.3*H.k,t0=0,t1=0.5,sample_step=0.1,jacobian=\"analytic\");"
        "record changed_stats=odestats(changed);assert(changed_stats.cache_hit);assert(first_stats.cache_key==changed_stats.cache_key);"
        "record retained=options(series=s.q,last=s.q(end));save(\"typed-ode-series.pws\",\"H\",\"s\",\"retained\");";
    valid(genericOde,analysis,execution);
    runtime::InterpreterSession series;result=series.submit("load(\"typed-ode-series.pws\");",analysis,execution);diagnostics(result);expect(result.ok(),"restore typed generic ODE state and aliased series metadata");
    result=series.submit("assert(algebra_norm(s.q(end)-retained.last)<1e-12);assert(algebra_norm(retained.series(end)-retained.last)<1e-12);assert(abs(coeff(s.q,3)(end)-exp(0.1)*sin(0.15))<1e-8);",analysis,execution);diagnostics(result);expect(result.ok(),"restored generic solution/series coefficient projection is complete");
    valid("algebra C=builtin_algebra(\"complex\");ode element_complex_rotation(t,element<C> z){z'=C.i*z;}"
        "solution s=odesolve(element_complex_rotation,z=C.one,t0=0,t1=0.5,sample_step=0.1,jacobian=\"analytic\");"
        "element<C> final=s.z(end);assert(algebra_norm(final-(cos(0.5)+sin(0.5)*C.i))<1e-6);",analysis,execution);
}
void importsAndBudgets(const std::filesystem::path& directory){
    const auto dual=dualJson();write(directory/"dual table.json",dual);write(directory/"matrices.json",matrixJson);
    language::AnalysisOptions analysis;analysis.sourceDirectory=directory;runtime::ExecutionOptions execution;execution.dataDirectory=directory;
    valid("algebra D=import_algebra(\"dual table.json\");assert(D.eps*D.eps==0*D.one);assert(algebra_info(D).dimension==2);"
        "assert(algebra_info(D).properties.associative.value);assert(algebra_info(D).properties.associative.evidence==\"proved_exact\");"
        "assert(!algebra_info(D).properties.division.value);assert(algebra_info(D).properties.division.evidence==\"failed\");matrix products=algebra_products(D);assert(size(products,2)==4);"
        "assert(products(1,1)==1);assert(products(1,2)==1);assert(products(1,3)==1);assert(products(1,4)==1);"
        "string json=algebra_json(D);assert(json!=\"\");assert(algebra_norm(exp(D.eps)-(1+D.eps))<1e-12);",analysis,execution);
    valid("algebra M=import_algebra(\"matrices.json\");element<M> identity=1;assert(identity==M.E11+M.E22);"
        "assert(2+M.E12==2*M.E11+M.E12+2*M.E22);assert(M.E12*M.E21==M.E11);"
        "assert(M.E21*M.E12==M.E22);record info=algebra_info(M);assert(!info.has_metric);assert(!info.has_conjugation);",analysis,execution);
    invalid("algebra M=import_algebra(\"matrices.json\");display(norm2(M.E11));",analysis);
    invalid("algebra M=import_algebra(\"matrices.json\");display(conj(M.E11));",analysis);
    invalid("algebra M=import_algebra(\"matrices.json\");display(scalar(M.E11));",analysis);
    const auto checked=language::analyse("algebra D=import_algebra(\"dual table.json\");algebra E=import_algebra(\"dual table.json\");",analysis);
    expect(checked.ok()&&checked.dependencies.size()==1&&checked.dependencies.front().source&&*checked.dependencies.front().source==dual,"table dependency retains one exact immutable byte snapshot");
    if(!checked.ok()||checked.program.statements.empty()||!checked.program.statements.front().expression||!checked.program.statements.front().expression->schema)return;
    const auto original=checked.program.statements.front().expression->schema->algebra;
    write(directory/"dual table.json",dualJson("-1"));const auto changed=language::analyse("algebra D=import_algebra(\"dual table.json\");",analysis);
    expect(changed.ok()&&original&&changed.program.statements.front().expression->schema->algebra->hash()!=original->hash(),"table product edits change native structural descriptor hash");
    expect(original&&original->coefficient(1,1,0)==0,"previous checked descriptor survives external file edits");
    write(directory/"dual table.json",dual);write(directory/"bad.json","error(\"table source must never execute\");");invalid("algebra A=import_algebra(\"bad.json\");",analysis);
    invalid("algebra A=import_algebra(\"missing.json\");",analysis);
    invalid("string p=\"dual table.json\";algebra A=import_algebra(p);",analysis);
    auto limited=analysis;limited.maximumImportBytes=dual.size()-1;invalid("algebra A=import_algebra(\"dual table.json\");",limited);
    limited=analysis;limited.importBudget=std::make_shared<language::ImportBudget>();limited.importBudget->bytes=limited.maximumImportBytes+1;invalid("algebra A=import_algebra(\"dual table.json\");",limited);
    auto cancellation=analysis;cancellation.cancellation=std::make_shared<std::atomic_bool>(true);expect(language::analyse("algebra D=import_algebra(\"dual table.json\");",cancellation).cancelled,"table static analysis observes cancellation");
    auto small=execution;small.maxOperations=20;rejectedAtRun("algebra S=builtin_algebra(\"sedenion\");display(algebra_info(S));",small);
    auto nativeSmall=execution;nativeSmall.maxNativeOperations=2;rejectedAtRun("algebra H=builtin_algebra(\"quaternion\");display(algebra_solveleft(H.i,H.j));",nativeSmall);
    auto seriesBudget=execution;seriesBudget.maxOperations=1000;rejectedAtRun("algebra M=builtin_algebra(\"matrix2\");display(exp(M.E11));",seriesBudget);
    valid("algebra H=builtin_algebra(\"quaternion\");assert(algebra_norm(exp(H.i)-(cos(1)+sin(1)*H.i))<1e-12);",{},seriesBudget);
    const auto help=valid("help(\"builtin_algebra\");help(\"algebra_solveleft\");");expect(help.output.size()==2&&help.output[0].find("multiplication table")!=std::string::npos&&help.output[1].find("unique")!=std::string::npos,"native algebra topics are discoverable through help");
    write(directory/"numeric metadata.json",numericMetadata);
    const runtime::dataio::NumericTable rows{3,4,{1,1,1,1, 1,2,2,1, 2,1,2,1}};
    runtime::dataio::writeMatrix(directory/"dual products.csv",rows);
    runtime::dataio::writeMatrix(directory/"dual products.xlsx",rows,"Products");
    for(const auto* extension:{"csv","xlsx"}){
        const std::string filename=std::string("dual products.")+extension;
        const auto source="algebra D=import_algebra(\"numeric metadata.json\",\""+filename+"\""+(std::string(extension)=="xlsx"?",\"Products\"":"")+");assert(D.eps*D.eps==0*D.one);assert((2+3*D.eps)^2==4+12*D.eps);";
        valid(source,analysis,execution);const auto captured=language::analyse(source,analysis);
        expect(captured.ok()&&captured.dependencies.size()==2,"numeric table import captures metadata and product bytes");
        const auto bytes=runtime::dataio::readBytes(directory/filename);
        const auto decoded=runtime::dataio::readMatrixBytes(bytes,std::filesystem::path(std::string("nonexistent.")+extension),std::string(extension)=="xlsx"?"Products":"");
        expect(decoded.rows==rows.rows&&decoded.columns==rows.columns&&decoded.values==rows.values,"captured CSV/XLSX snapshot parses without opening a file");
    }
    invalid("algebra D=import_algebra(\"dual table.json\",\"dual products.csv\");",analysis);
    invalid("algebra D=import_algebra(\"numeric metadata.json\",\"dual products.csv\",\"Products\");",analysis);
    invalid("algebra D=import_algebra(\"numeric metadata.json\",\"dual products.csv\");display(exp(D.eps));",analysis);
    write(directory/"fractional.csv","1,1,1,1\n1.5,2,2,1\n");invalid("algebra D=import_algebra(\"numeric metadata.json\",\"fractional.csv\");",analysis);
    write(directory/"duplicate.csv","1,1,1,1\n1,1,1,1\n");invalid("algebra D=import_algebra(\"numeric metadata.json\",\"duplicate.csv\");",analysis);
    write(directory/"invalid-index.csv","3,1,1,1\n");invalid("algebra D=import_algebra(\"numeric metadata.json\",\"invalid-index.csv\");",analysis);
    write(directory/"bad-columns.csv","1,1,1\n");invalid("algebra D=import_algebra(\"numeric metadata.json\",\"bad-columns.csv\");",analysis);
    const auto firstNumeric=language::analyse("algebra D=import_algebra(\"numeric metadata.json\",\"dual products.csv\");",analysis);
    auto changedRows=rows;changedRows.rows=4;changedRows.values.insert(changedRows.values.end(),{2,2,1,-1});runtime::dataio::writeMatrix(directory/"dual products.csv",changedRows);
    const auto changedNumeric=language::analyse("algebra D=import_algebra(\"numeric metadata.json\",\"dual products.csv\");",analysis);
    expect(firstNumeric.ok()&&changedNumeric.ok()&&firstNumeric.program.statements.front().expression->schema->algebra->hash()!=changedNumeric.program.statements.front().expression->schema->algebra->hash(),"CSV structure constants change the immutable native descriptor hash");
}
void examples(const std::filesystem::path& directory){
#ifdef PENTA_SOURCE_DIR
    const auto sourceDirectory=std::filesystem::path(PENTA_SOURCE_DIR)/"examples/native-algebras";
    language::AnalysisOptions analysis;analysis.sourceDirectory=sourceDirectory;
    runtime::ExecutionOptions execution;execution.dataDirectory=directory;execution.odeCacheDirectory=directory/".penta-cache/odes";
    for(const auto* filename:{"quaternion-rotations-and-products.p","complex-and-real-algebras.p","octonion-bracketing.p","sedenion-zero-divisors.p","dual-numbers-and-rigid-motion.p","clifford-and-split-algebras.p","associative-lie-and-jordan-presets.p","import-custom-multiplication-table.p","native-quaternion-jacobians-and-odes.p","import-csv-and-excel-products.p","prepare-csv-xlsx-tables.p","whole-element-calculus.p","algebra-vector-projections.p","quaternion-roots-maps-and-fitting.p"}){
        std::ifstream file(sourceDirectory/filename,std::ios::binary);const std::string source((std::istreambuf_iterator<char>(file)),{});
        expect(!source.empty(),std::string("example source exists: ")+filename);if(!source.empty()){const auto result=runtime::execute(source,analysis,execution);diagnostics(result);expect(result.ok(),std::string("example runs: ")+filename);}
    }
#else
    (void)directory;
#endif
}
}
int main(){
    const auto directory=std::filesystem::temp_directory_path()/("penta-universal-language-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{std::filesystem::create_directories(directory);builtinFamilies();quaternionArithmetic();nonassociativeAndDegenerate();schemaAndScope();importsAndBudgets(directory);persistenceAndOdes(directory);examples(directory);}
    catch(const std::exception& error){++failures;std::cerr<<"Unexpected exception: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);
    std::cout<<checks<<" universal algebra language checks, "<<failures<<" failures\n";return failures?1:0;
}
