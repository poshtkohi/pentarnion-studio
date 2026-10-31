#include "runtime/interpreter.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int checks=0,failures=0;
void expect(bool value,const std::string& message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
void diagnostics(const penta::runtime::ExecutionResult& result) {
    for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
    for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';
}
void values(const std::string& source,const std::vector<std::string>& expected) {
    const auto result=penta::runtime::execute(source);
    if(!result.ok())diagnostics(result);
    expect(result.ok(),"valid language program: "+source);
    expect(result.output==expected,"language output: "+source);
}
void rejected(const std::string& source,bool staticError=true) {
    const auto result=penta::runtime::execute(source);
    expect(!result.ok(),"invalid program rejected: "+source);
    if(staticError)expect(!result.analysis.ok(),"invalid program rejected before execution: "+source);
}
void control() {
    values("integer total=0;for(n=1:5){total=total+n;}display(total);",{"15"});
    values("integer total=0;for(n=5:-2:1){total=total+n;}display(total);",{"9"});
    values("integer count=0;for(n=9223372036854775806:1:9223372036854775807){count=count+1;}display(count);",{"2"});
    values("integer count=0;for(n=9223372036854775807:-9223372036854775808:-9223372036854775808){count=count+1;}display(count);",{"2"});
    values("integer count=0;for(n=0:-9223372036854775808:-9223372036854775808){count=count+1;}display(count);",{"2"});
    values("real total=0;for(n=0:0.25:1){total=total+n;}display(total);",{"2.5"});
    values("real total=0;for(n=[1;2;3]){total=total+n;}display(total);",{"6"});
    values("integer total=0;for(n=1:8){if(n==2){continue;}if(n>=5){break;}total=total+n;}display(total);",{"8"});
    values("integer count=0;integer total=0;while(count<8){count=count+1;if(count==2){continue;}if(count==5){break;}total=total+count;}display(count);display(total);",{"5","8"});
    values("integer total=0;for(a=1:3){for(b=1:3){if(b==2){break;}total=total+1;}}display(total);",{"3"});
    values("integer n=99;integer total=0;for(n=1:3){integer local=n*2;total=total+local;}display(n);display(total);",{"99","12"});
    values("integer x=2;if(x<1){display(0);}else if(x==2){display(2);}else{display(3);}",{"2"});
    values("integer x=0;while(false){x=x+1;}for(n=3:1){x=x+1;}display(x);",{"0"});
    rejected("if(1){integer local=2;}display(local);");
    rejected("for(n=1:3){}display(n);");
    rejected("break;");rejected("continue;");rejected("return 1;");
    rejected("if(i){display(1);}");
    rejected("for(n=[1,2;3,4]){}",false);
    rejected("if(1){use_algebra(\"A_gamma\",gamma=1);}");
    rejected("if(1){function real f(real x){return x;}}");
}
void comparison() {
    values("display(1<2);display(2<=2);display(3>2);display(3>=4);display(1==1);display(1!=2);display(1~=1);display(!0);display(~1);",{"1","1","1","0","1","1","0","1","0"});
    values("display(false && sqrt(-1));display(true || sqrt(-1));display(1 && 5);display(0 || 2);",{"0","1","1","1"});
    values("display(1+2<4 && 3^2==9);display(1 || 0 && 0);display(!(3<4));",{"1","1","0"});
    values("pentarnion p=1+i;pentarnion q=1+j;display(p==p);display(p!=q);display([1,2]==[1,2]);display([1,2]!=[1;2]);",{"1","1","1","1"});
    values("string message=\"hello\";message=\"world\";display(message);display(message==\"world\");",{"world","1"});
    values("display(9007199254740993==9007199254740992.0);display(9223372036854775807>9223372036854775806);",{"0","1"});
    values("display((1:3)==[1,2,3]);",{"1"});
    rejected("display(i<j);");values("logicalmatrix mask=[1,2]<2;display(mask);",{"[1, 0]"});rejected("display(i&&j);");
}
void functions() {
    values("function real square(real x){return x*x;}display(square(3));",{"9"});
    values("function integer factorial(integer n){if(n<=1){return 1;}return n*factorial(n-1);}display(factorial(10));",{"3628800"});
    values("function pentarnion ordered(pentarnion p,pentarnion q){return p*q;}display(ordered(i,j)==i*j);display(ordered(i,j)!=j*i);",{"1","1"});
    values("function real totalTo(integer n){integer total=0;for(a=1:n){total=total+a;}return total;}display(totalTo(5));",{"15"});
    values("function matrix twice(matrix A){matrix B=A.*2;return B;}matrix A=[1,2;3,4];matrix B=twice(A);display(B(2,2));display(A(2,2));",{"8","4"});
    values("function string greet(string name){return name;}string result=greet(\"Susanne\");display(result);",{"Susanne"});
    values("function void show(real x){display(x);return;}show(2);",{"2"});
    values("function real f(real x){real local=3;return x+local;}real local=99;display(f(2));display(local);",{"5","99"});
    values("function real first(real x){return x+1;}function real second(real x){return first(x)*2;}display(second(3));",{"8"});
    rejected("function real f(real x){return x;}display(f());");
    rejected("function real f(real x){return x;}display(f(i));");
    rejected("function integer f(real x){return x;}");
    rejected("real global=2;function real f(real x){return x+global;}");
    rejected("function real f(real x){return x;}display(x);");
    rejected("integer x=1;if(1){clear x;integer x=2;}");
    rejected("function real f(real x){if(x>0){return x;}}display(f(-1));",false);
    rejected("function real recurse(real x){return recurse(x);}display(recurse(1));",false);
    penta::runtime::InterpreterSession session;
    auto result=session.submit("function real square(real x){return x*x;}");expect(result.ok(),"function definition console persistence");
    result=session.submit("square(4)");expect(result.ok()&&result.output==std::vector<std::string>{"16"},"function call across submissions");
    result=session.submit("help(\"square\");");expect(result.ok()&&result.output.size()==1&&result.output[0].find("function real square(real x)")!=std::string::npos,"function help metadata persists");
    result=session.submit("clear square;");expect(result.ok(),"clear function");
    result=session.submit("square(4)");expect(!result.ok(),"cleared function unavailable");
    result=session.submit("function real invalidRoot(real x){real local=x-3;return sqrt(local);}");
    expect(result.ok(),"persist function with runtime-domain check");
    result=session.submit("\ninvalidRoot(2)");
    expect(result.status==penta::runtime::ExecutionStatus::RuntimeError&&result.runtimeDiagnostics.size()==1,"prior-request function failure is reported");
    if(!result.runtimeDiagnostics.empty()) {
        expect(result.runtimeDiagnostics[0].span.offset==1&&result.runtimeDiagnostics[0].span.length==14,"function failure points to current request's call span");
        expect(result.runtimeDiagnostics[0].message.find("In function invalidRoot:")!=std::string::npos,"function failure retains function context");
    }
}
void diagnosticsAndBudgets() {
    values("assert(2>1);assert(0==0,\"fine\");warning(\"notice\");",{"Warning: notice"});
    rejected("assert(false,\"failed expectation\");",false);rejected("error(\"stop\");display(1);",false);
    const auto result=penta::runtime::execute("integer n=2;matrix A=[1,2];who;whos;");
    expect(result.ok(),"introspection succeeds");
    expect(std::find(result.output.begin(),result.output.end(),"A  realmatrix  1x2")!=result.output.end(),"whos lists matrix type and shape");
    expect(std::find(result.output.begin(),result.output.end(),"n  integer  1x1")!=result.output.end(),"whos lists scalar type");
    const auto help=penta::runtime::execute("help;help(\"for\");help(\"unknown_topic\");");expect(help.ok()&&help.output.size()==3,"help and unknown topic");
    penta::runtime::ExecutionOptions options;options.maxOperations=200;
    const auto bounded=penta::runtime::execute("integer n=0;while(1){n=n+1;}",{},options);
    expect(bounded.status==penta::runtime::ExecutionStatus::RuntimeError,"infinite loop bounded by operation budget");
    options.maxCallDepth=3;
    const auto recursion=penta::runtime::execute("function real f(real x){return f(x);}display(f(1));",{},options);
    expect(recursion.status==penta::runtime::ExecutionStatus::RuntimeError,"function recursion bounded");
    options.maxWorkspaceVariables=1;
    const auto iterators=penta::runtime::execute("integer x=0;for(n=1:2){x=x+1;}",{},options);
    expect(iterators.status==penta::runtime::ExecutionStatus::RuntimeError,"loop iterator counts toward workspace limit");
    const auto parameters=penta::runtime::execute("function real f(real x){return x;}display(f(1));",{},options);
    expect(parameters.status==penta::runtime::ExecutionStatus::RuntimeError,"function parameter counts toward workspace limit");
    options.maxWorkspaceVariables=50000;
    options.maxMatrixCoordinates=10;
    const auto retained=penta::runtime::execute("function real f(integer n){matrix local=ones(1,4);if(n>0){return f(n-1);}return 0;}display(f(3));",{},options);
    expect(retained.status==penta::runtime::ExecutionStatus::RuntimeError&&std::any_of(retained.runtimeDiagnostics.begin(),retained.runtimeDiagnostics.end(),[](const auto& d){return d.code=="R1902";}),"matrix budget includes suspended recursive caller scopes");
    penta::runtime::InterpreterSession session;expect(session.submit("integer x=7;").ok(),"cancellation baseline");
    options.maxOperations=1000000000;options.cancellation=std::make_shared<std::atomic_bool>(false);
    std::thread cancel([flag=options.cancellation]{std::this_thread::sleep_for(std::chrono::milliseconds(5));flag->store(true);});
    const auto cancelled=session.submit("while(1){x=x+1;}",{},options);cancel.join();
    expect(cancelled.status==penta::runtime::ExecutionStatus::Cancelled,"loop cancellation");
    const auto preserved=session.submit("x");expect(preserved.ok()&&preserved.output==std::vector<std::string>{"7"},"cancelled loop rolls workspace back");
    const auto legacy=penta::language::analyse("integer if=1;integer true=2;integer x=true;",[]{penta::language::AnalysisOptions o;o.languageVersion="0.1";return o;}());
    expect(legacy.ok(),"legacy identifiers remain supported");
}
}
int main(){control();comparison();functions();diagnosticsAndBudgets();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
