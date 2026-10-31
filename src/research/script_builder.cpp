#include "script_builder.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace penta::research {
namespace {
std::string number(double value){if(!std::isfinite(value))throw ScriptError("Research settings must be finite.");std::ostringstream out;out<<std::setprecision(17)<<value;return out.str();}
std::string join(const std::vector<std::string>& values,const char* delimiter){std::string out;for(const auto& value:values){if(!out.empty())out+=delimiter;out+=value;}return out;}
std::string diagnostic(const language::AnalysisResult& analysis){for(const auto& d:analysis.diagnostics)if(d.severity==language::Severity::Error)return d.code+": "+d.message;return "Research source did not check.";}
bool pure(const language::ExprPtr& expression){
    if(!expression)return false;
    using language::ExprKind;
    switch(expression->kind){case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:case ExprKind::Reference:case ExprKind::Group:case ExprKind::Unary:case ExprKind::Binary:case ExprKind::PentConstructor:case ExprKind::MatrixLiteral:case ExprKind::Range:case ExprKind::TupleLiteral:case ExprKind::Transpose:break;
        case ExprKind::Member:if(expression->children.empty()||expression->children[0]->type!=language::Type::Algebra)return false;break;
        case ExprKind::Call:{static const std::set<std::string> allowed{"complex","element","algebra_basis","realpart","imagpart","sin","cos","tan","exp","log","sqrt","sinh","cosh","tanh","asin","acos","atan","atan2","abs","floor","ceil","round","sign","min","max","conj","scalar","pure","norm2","coeff","linspace","logspace","zeros","ones","eye"};if(!allowed.contains(expression->text))return false;break;}
        default:return false;
    }
    return std::all_of(expression->children.begin(),expression->children.end(),pure);
}
void expression(const std::string& type,const std::string& source,const language::AnalysisOptions& options){
    if(source.empty()||source.size()>4096||source.find('\0')!=std::string::npos)throw ScriptError("Enter a bounded nonempty initial/parameter/data expression.");
    const auto checked=language::analyse(type+" research_checked_expression="+source+";",options);
    if(!checked.ok())throw ScriptError(diagnostic(checked));
    if(checked.program.statements.size()!=1||checked.program.statements[0].kind!=language::StatementKind::Declaration||!pure(checked.program.statements[0].expression))throw ScriptError("Enter one pure numeric expression; extra statements, callbacks and file access are not accepted in a control.");
}
void identifier(const std::string& text,std::set<std::string>& names){
    if(text.empty()||text.size()>64||!((text[0]>='a'&&text[0]<='z')||(text[0]>='A'&&text[0]<='Z')||text[0]=='_'))throw ScriptError("State/parameter names require ASCII identifiers.");
    for(char c:text)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'))throw ScriptError("State/parameter names require ASCII identifiers.");
    if(!names.insert(text).second||text.rfind("research_",0)==0)throw ScriptError("Use unique state/parameter names outside the reserved research_ prefix and time variable t.");
}
std::vector<std::string> coordinateExpressions(const Configuration& c,const std::string& solution){
    language::AnalysisOptions options;options.gamma=c.gamma;(void)prepareAlgebras(c,options);std::vector<std::string> result;
    for(const auto& state:c.states){const auto numeric=numericControlType(state.type,options);const auto width=language::coordinateWidth(numeric.type,numeric.schema);if(width==1&&numeric.type!=language::Type::RealMatrix)result.push_back(solution+"."+state.name);else for(std::size_t k=0;k<width;++k)result.push_back((numeric.type==language::Type::RealMatrix||numeric.type==language::Type::PentarnionMatrix||numeric.type==language::Type::ElementMatrix?"statecoord(":"coeff(")+solution+"."+state.name+","+std::to_string(k)+")");}

    return result;
}
std::string plots(const Configuration& c){
    const auto coordinates=coordinateExpressions(c,"research_solution");const std::size_t columns=coordinates.size()==1?1:2,rows=(coordinates.size()+columns-1)/columns;
    std::string result="figure(60,\"Name\",\"Research Explorer: ODE coordinates\",\"NumberTitle\",\"off\");clf;\n";
    for(std::size_t i=0;i<coordinates.size();++i)result+="subplot("+std::to_string(rows)+","+std::to_string(columns)+","+std::to_string(i+1)+");plot(research_solution.t,"+coordinates[i]+");title(\"Full-state coordinate "+std::to_string(i+1)+"\");xlabel(\"t\");\n";
    if(coordinates.size()>=3)result+="figure(61,\"Name\",\"Research Explorer: three-coordinate projection\",\"NumberTitle\",\"off\");clf;plot3("+coordinates[0]+","+coordinates[1]+","+coordinates[2]+");title(\"Projection; every original coefficient is retained\");\n";
    return result;
}
}
std::string quote(const std::string& value){std::string out="\"";for(char c:value){if(c=='\0')throw ScriptError("Text controls do not accept NUL characters.");if(c=='\\'||c=='\"'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else out+=c;}return out+'\"';}
Configuration defaultConfiguration(Mode mode){
    Configuration c;c.mode=mode;c.states={{"real","x","1.0","v"},{"real","v","0.0","-frequency*x"}};c.parameters={{"real","frequency","1.0"}};c.sweepParameter="frequency";c.initialRows="[1,0;0,1]";
    if(mode==Mode::Chaos){c.states={{"real","x","1.0","sigma*(y-x)"},{"real","y","1.0","x*(rho-z)-y"},{"real","z","1.0","x*y-beta*z"}};c.parameters={{"real","sigma","10.0"},{"real","rho","28.0"},{"real","beta","8.0/3.0"}};c.t1=20;c.transient=2;}
    if(mode==Mode::Fitting)c.parameters={{"real","intercept","0.0"},{"real","slope","0.0"}};
    if(mode==Mode::Pde){c.states.clear();c.parameters={{"real","amplitude","1.0"},{"real","wave","3.141592653589793"}};c.t1=.1;c.sampleStep=.01;}
    if(mode==Mode::Geometry){c.states.clear();c.parameters={{"real","amplitude","0.5"}};}
    if(mode==Mode::Mechanics){c.states.clear();c.parameters={{"real","frequency","1.0"}};c.t1=4;c.sampleStep=.02;}
    return c;
}
std::string generateScript(const Configuration& c,bool preflight){
    if(c.mode==Mode::Pde||c.mode==Mode::Geometry||c.mode==Mode::Mechanics)return generateSpatialScript(c,preflight);
    for(double value:{c.gamma,c.t0,c.t1,c.sampleStep,c.rtol,c.atol,c.maximumSeconds,c.transient,c.qrStep,c.sweepLower,c.sweepUpper})if(!std::isfinite(value))throw ScriptError("Research settings must be finite.");
    if(c.gamma<0||c.gamma>1||c.t1<=c.t0||c.sampleStep<=0||c.rtol<=0||c.rtol>=1||c.atol<=0||c.maximumSeconds<=0||c.maximumSeconds>3600||c.threads<0||c.threads>64||c.parameters.size()>16)throw ScriptError("Check algebra, time interval, solver tolerances, thread limit and wall budget.");
    language::AnalysisOptions options;options.gamma=c.gamma;const auto algebraSource=prepareAlgebras(c,options);std::set<std::string> names{"t"};std::vector<std::string> declarations,bindings,initials,flatInitial;std::size_t coordinates=0;
    std::string initialDeclarations;
    if(c.mode!=Mode::Fitting){if(c.states.empty()||c.states.size()>16)throw ScriptError("Use1..16 checked numerical ODE states, with at most32 full coordinates in this panel.");for(const auto& state:c.states){const auto numeric=numericControlType(state.type,options);const auto width=language::coordinateWidth(numeric.type,numeric.schema);identifier(state.name,names);expression(state.type,state.initial,options);if(state.rhs.empty()||state.rhs.size()>4096)throw ScriptError("Enter a bounded derivative expression for each state.");declarations.push_back(state.type+" "+state.name);const auto initial="research_initial_"+state.name;initialDeclarations+=state.type+" "+initial+"="+state.initial+";\n";initials.push_back(initial);coordinates+=width;
        if(numeric.type==language::Type::RealMatrix||numeric.type==language::Type::PentarnionMatrix||numeric.type==language::Type::ElementMatrix){const auto rows=numeric.schema->shape[0],columns=numeric.schema->shape[1],components=width/(rows*columns);for(std::size_t row=0;row<rows;++row)for(std::size_t column=0;column<columns;++column)for(std::size_t component=0;component<components;++component){const auto cell=initial+"("+std::to_string(row+1)+","+std::to_string(column+1)+")";flatInitial.push_back(components==1?cell:"coeff("+cell+","+std::to_string(component)+")");}}
        else if(width==1)flatInitial.push_back(initial);else for(std::size_t k=0;k<width;++k)flatInitial.push_back("coeff("+initial+","+std::to_string(k)+")");}}
    if(coordinates>32||std::ceil((static_cast<long double>(c.t1)-c.t0)/c.sampleStep)*std::max<std::size_t>(1,coordinates)>500000)throw ScriptError("Research plot storage exceeds 500000 sample coordinates; reduce the horizon or increase sample step.");
    std::vector<std::string> parameterDeclarations,guesses;
    for(const auto& p:c.parameters){(void)numericControlType(p.type,options);identifier(p.name,names);expression(p.type,p.value,options);parameterDeclarations.push_back(p.type+" "+p.name);bindings.push_back(p.name+"=("+p.value+")");guesses.push_back(p.value);}
    const auto bindingRecord="options("+join(bindings,",")+")";
    std::string model;
    if(c.mode!=Mode::Fitting){model="ode research_model(t,"+join(declarations,",");if(!parameterDeclarations.empty())model+=";"+join(parameterDeclarations,",");model+="){\n";for(const auto& s:c.states)model+=s.name+"'="+s.rhs+";\n";model+="}\n";
        const auto checked=language::analyse(model,options);if(!checked.ok())throw ScriptError(diagnostic(checked));if(checked.program.statements.size()!=1||checked.program.statements[0].kind!=language::StatementKind::OdeDefinition||checked.program.statements[0].ode->states.size()!=c.states.size())throw ScriptError("Derivative controls accept expressions only, without injected statements or models.");}
    std::string source="% Generated by Research Explorer. Inspect before Run; native bodies are cached.\nuse_algebra(\"A_gamma\",gamma="+number(c.gamma)+");\nclear;\nrng(5489);\n"+algebraSource+initialDeclarations;
    const auto method=c.method;if(method!="bdf"&&method!="adams"&&method!="erk"&&method!="esdirk")throw ScriptError("ODE method must be bdf, adams, erk or esdirk.");
    const auto solver="t0="+number(c.t0)+",t1="+number(c.t1)+",sample_step="+number(c.sampleStep)+",method="+quote(method)+",jacobian=\"analytic\",rtol="+number(c.rtol)+",atol="+number(c.atol)+",max_seconds="+number(c.maximumSeconds)+(c.livePlots?",live_plot=options(figure=62,coordinate=1)":"");
    if(c.mode==Mode::Ode){std::vector<std::string> stateBindings;for(std::size_t i=0;i<c.states.size();++i)stateBindings.push_back(c.states[i].name+"="+initials[i]);stateBindings.insert(stateBindings.end(),bindings.begin(),bindings.end());source+=model+"tic;\nsolution research_solution=odesolve(research_model,"+join(stateBindings,",")+","+solver+");\ntoc;\ndisplay(research_solution);\n"+plots(c);}
    else if(c.mode==Mode::Chaos){if((method!="bdf"&&method!="adams")||c.transient<0||c.transient>=c.t1-c.t0||c.qrStep<=0||(c.t1-c.t0)/c.qrStep>4096)throw ScriptError("Tangent chaos analysis requires BDF/Adams, a valid transient and 1..4096 positive QR intervals.");const auto initial=initials.size()==1?initials.front():"("+join(initials,",")+")";
        source+=model+"record research_result=lyapunov(research_model,"+initial+",options(parameters="+bindingRecord+",t0="+number(c.t0)+",t1="+number(c.t1)+",transient="+number(c.transient)+",qr_step="+number(c.qrStep)+",method="+quote(method)+",rtol="+number(c.rtol)+",atol="+number(c.atol)+",max_seconds="+number(c.maximumSeconds)+(c.livePlots?",live_plot=options(figure=62,coordinate=1)":"")+"));\ndisplay(research_result);\nfigure(60,\"Name\",\"Research Explorer: finite-time tangent diagnostics\",\"NumberTitle\",\"off\");clf;subplot(2,1,1);plot(research_result.t,research_result.history(:,1));title(\"Largest finite-time Lyapunov estimate\");xlabel(\"t\");subplot(2,1,2);matrix research_modes=1:research_result.dimension;plot(research_modes,research_result.exponents);title(\"Full-state spectrum; validate horizon and numerical settings\");\n";
    }else if(c.mode==Mode::Fitting){if(c.parameters.empty()||c.maximumIterations<1||c.maximumIterations>1000||c.fitExpression.empty()||c.fitExpression.size()>4096||(c.fitMethod!="lm"&&c.fitMethod!="dogleg"))throw ScriptError("Fit a checked native numerical model with1..16 parameter groups, LM/Dogleg and1..1000 iterations.");(void)numericControlType(c.fitType,options);expression("matrix",c.xData,options);expression("matrix",c.yData,options);model="native function "+c.fitType+" research_model("+join(parameterDeclarations,",")+",real t){return "+c.fitExpression+";}\n";const auto checked=language::analyse(model,options);if(!checked.ok())throw ScriptError(diagnostic(checked));if(checked.program.statements.size()!=1||!checked.program.statements[0].function||checked.program.statements[0].function->body.size()!=1||checked.program.statements[0].function->body[0].kind!=language::StatementKind::Return)throw ScriptError("Fit model accepts one native expression without injected statements.");
        bool allReal=std::all_of(c.parameters.begin(),c.parameters.end(),[](const auto& p){return p.type=="real";});const auto point=guesses.size()==1?guesses[0]:allReal?"["+join(guesses,",")+"]":"("+join(guesses,",")+")";
        source+=model+"function integer research_monitor(matrix point,record info=iterationinfo(),string phase=\"iter\"){if(info.objective_available){figure(61,\"Name\",\"Research Explorer: live fitting history\",\"NumberTitle\",\"off\");plot(info.history(:,2),info.history(:,3));title(\"Native fitting objective history\");}return 0;}\nmatrix research_xdata="+c.xData+";matrix research_ydata="+c.yData+";\nrecord research_result=curvefit(research_model,"+point+",research_xdata,research_ydata,options(method="+quote(c.fitMethod)+",jacobian=\"analytic\",max_iterations="+std::to_string(c.maximumIterations)+",max_wall_seconds="+number(c.maximumSeconds)+",threads="+std::to_string(c.threads)+",OutputFcn=research_monitor,OutputEvery="+std::to_string(std::max(1,(c.maximumIterations+29)/30))+"));\ndisplay(research_result);\nfigure(60,\"Name\",\"Research Explorer: fitted samples and residuals\",\"NumberTitle\",\"off\");clf;subplot(2,1,1);plot(research_result.xdata,research_result.fitted(:,1));title(\"Fitted coordinate 1; every output coordinate retained\");subplot(2,1,2);matrix research_residuals=reshape(research_result.residuals,size(research_result.fitted,2),size(research_result.fitted,1))';plot(research_result.xdata,research_residuals(:,1));title(\"Residual coordinate 1\");\n";
    }else{
        if(c.transient<0||c.transient>=c.t1-c.t0||c.sweepSamples<1||c.sweepSamples>64||c.maximumRuns<1||c.maximumRuns>4096||c.sweepLower>c.sweepUpper)throw ScriptError("Check discovery range, sample count, transient and run cap.");
        auto sweeps=c.sweeps;if(sweeps.empty())sweeps.push_back({c.sweepParameter,c.sweepLower,c.sweepUpper,c.sweepSamples});
        if(sweeps.size()>16)throw ScriptError("At most16 simultaneous discovery parameters are supported.");
        std::set<std::string> selected;std::vector<std::string> sweepBindings;std::string domains;std::size_t combinations=1;
        for(std::size_t index=0;index<sweeps.size();++index){const auto& sweep=sweeps[index];const auto parameter=std::find_if(c.parameters.begin(),c.parameters.end(),[&](const auto& p){return p.name==sweep.parameter;});if(parameter==c.parameters.end()||parameter->type!="real"||!selected.insert(sweep.parameter).second)throw ScriptError("Select unique declared real parameters for simultaneous discovery sweeps.");
            if(!std::isfinite(sweep.lower)||!std::isfinite(sweep.upper)||sweep.lower>sweep.upper||sweep.samples<1||sweep.samples>64||combinations>4096/static_cast<std::size_t>(sweep.samples))throw ScriptError("Discovery ranges require finite ordered bounds and1..64 samples each, with at most4096 Cartesian combinations.");
            combinations*=static_cast<std::size_t>(sweep.samples);
            const auto domain=std::string("research_domain")+(index?std::to_string(index+1):"");domains+="record "+domain+"=searchinterval("+number(sweep.lower)+","+number(sweep.upper)+",options(samples="+std::to_string(sweep.samples)+",method=\"linspace\"));\n";
        }
        for(const auto& parameter:c.parameters){const auto at=std::find_if(sweeps.begin(),sweeps.end(),[&](const auto& sweep){return sweep.parameter==parameter.name;});const auto index=static_cast<std::size_t>(at-sweeps.begin());sweepBindings.push_back(parameter.name+"="+(at==sweeps.end()?"("+parameter.value+")":std::string("research_domain")+(index?std::to_string(index+1):"")));}
        const auto rows=c.initialRows.empty()?"["+join(flatInitial,",")+"]":c.initialRows;expression("matrix",rows,options);
        source+=model+"matrix research_initials="+rows+";\n"+domains+"record research_settings=options(parameters=options("+join(sweepBindings,",")+"),"+solver+",threads="+std::to_string(c.threads)+",transient="+number(c.transient)+",return_tolerance=0.02,min_return_time=0.5,max_runs="+std::to_string(c.maximumRuns)+",maximum_bytes=134217728,max_native_operations=100000000";
        if(!c.checkpoint.empty())source+=",checkpoint="+quote(c.checkpoint)+",resume="+(c.resume?"1":"0")+",overwrite="+(c.overwriteCheckpoint?"1":"0");
        source+=");\nrecord research_plan=discoverplan(research_model,research_initials,research_settings);display(research_plan);\n";
        if(!preflight)source+="record research_result=odeensemble(research_model,research_initials,research_settings);integer research_runs=research_result.runs;display(research_result);figure(60,\"Name\",\"Research Explorer: experiment summary\",\"NumberTitle\",\"off\");clf;matrix research_ids=1:research_result.runs;research_ids=research_ids';plot(research_ids,research_result.maximum_norm);title(\"Finite-horizon maximum norm by stable run ID\");\n";
    }
    auto finalOptions=options;finalOptions.initialSymbols.clear();const auto final=language::analyse(source,finalOptions);if(!final.ok())throw ScriptError(diagnostic(final));return source;
}
}
