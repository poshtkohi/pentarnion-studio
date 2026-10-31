#include "script_builder.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace penta::research {
namespace {
std::string number(double x){if(!std::isfinite(x))throw ScriptError("Spatial inputs must be finite.");std::ostringstream s;s<<std::setprecision(17)<<x;return s.str();}
std::string join(const std::vector<std::string>& values){std::string result;for(const auto& value:values){if(!result.empty())result+=",";result+=value;}return result;}
std::string reason(const language::AnalysisResult& checked){for(const auto& d:checked.diagnostics)if(d.severity==language::Severity::Error)return d.code+": "+d.message;return "The generated native model did not check.";}
bool pure(const language::ExprPtr& e){
    if(!e)return false;
    using language::ExprKind;
    switch(e->kind){
    case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:case ExprKind::Reference:case ExprKind::Group:case ExprKind::Unary:case ExprKind::Binary:case ExprKind::PentConstructor:case ExprKind::MatrixLiteral:case ExprKind::Range:case ExprKind::TupleLiteral:case ExprKind::Index:case ExprKind::Transpose:case ExprKind::AllIndices:case ExprKind::EndIndex:break;
    case ExprKind::Member:if(e->children.empty()||e->children[0]->type!=language::Type::Algebra)return false;break;
    case ExprKind::Call:{static const std::set<std::string> allowed{"complex","element","algebra_basis","realpart","imagpart","sin","cos","tan","exp","log","sqrt","sinh","cosh","tanh","asin","acos","atan","atan2","abs","floor","ceil","round","sign","min","max","conj","scalar","pure","norm2","coeff","zeros","ones","eye"};if(!allowed.contains(e->text))return false;break;}
    default:return false;
    }
    return std::all_of(e->children.begin(),e->children.end(),pure);
}
void bounded(const std::string& value){if(value.empty()||value.size()>4096||value.find('\0')!=std::string::npos)throw ScriptError("Enter one bounded native mathematical expression.");}
void value(const std::string& type,const std::string& expression,const language::AnalysisOptions& options){bounded(expression);const auto checked=language::analyse(type+" research_value="+expression+";",options);if(!checked.ok())throw ScriptError(reason(checked));if(checked.program.statements.size()!=1||checked.program.statements[0].kind!=language::StatementKind::Declaration||!pure(checked.program.statements[0].expression))throw ScriptError("Value controls require one pure expression without injected statements or external access.");}
void model(const std::string& source,const language::AnalysisOptions& options){const auto checked=language::analyse(source,options);if(!checked.ok())throw ScriptError(reason(checked));if(checked.program.statements.size()!=1||!checked.program.statements[0].function||checked.program.statements[0].function->body.size()!=1||checked.program.statements[0].function->body[0].kind!=language::StatementKind::Return||!pure(checked.program.statements[0].function->body[0].expression))throw ScriptError("A model control requires one pure native return expression.");}
}
std::optional<std::string> generateExtendedSpatialScript(const Configuration& c,bool preflight){
    const bool pde=c.mode==Mode::Pde&&c.spatial.pde.integrator=="implicit";
    const bool mechanics=c.mode==Mode::Mechanics&&(c.spatial.mechanics.dimensions!=1||c.spatial.mechanics.form=="constrained");
    if(!pde&&!mechanics)return {};
    for(double x:{c.gamma,c.t0,c.t1,c.sampleStep,c.rtol,c.atol,c.maximumSeconds})if(!std::isfinite(x))throw ScriptError("Check finite algebra, solver and time controls.");
    if(c.gamma<0||c.gamma>1||c.t1<=c.t0||c.sampleStep<=0||c.rtol<=0||c.rtol>=1||c.atol<=0||c.maximumSeconds<=0||c.maximumSeconds>3600||c.threads<0||c.threads>64||c.parameters.size()>16)throw ScriptError("Check the algebra, positive time interval/tolerances and native resource budgets.");
    language::AnalysisOptions options;options.gamma=c.gamma;const auto algebras=prepareAlgebras(c,options);std::set<std::string> names{"t","x","y","u","v","q","p","domain_x","domain_y"};std::vector<std::string> declarations,bindings;
    for(const auto& parameter:c.parameters){(void)numericControlType(parameter.type,options);if(parameter.name.empty()||parameter.name.size()>64||parameter.name.starts_with("research_")||!names.insert(parameter.name).second)throw ScriptError("Use unique native parameter names outside reserved model coordinates.");value(parameter.type,parameter.value,options);declarations.push_back(parameter.type+" "+parameter.name);bindings.push_back(parameter.name+"=("+parameter.value+")");}
    const auto suffix=declarations.empty()?std::string():","+join(declarations),parameters="options("+join(bindings)+")";
    std::string source="% Generated checked native spatial model. Inspect before Run.\nuse_algebra(\"A_gamma\",gamma="+number(c.gamma)+");\nclear;\nrng(5489);\n"+algebras;
    if(pde){
        const auto& p=c.spatial.pde;const auto type=numericControlType(p.valueType,options);const auto width=language::coordinateWidth(type.type,type.schema);
        for(double x:{p.lengthX,p.lengthY,p.diffusion,p.timeStep,p.robinAlpha})if(!std::isfinite(x))throw ScriptError("PDE grid, diffusion, Robin and time controls must be finite.");
        if((p.dimension!=1&&p.dimension!=2)||p.method!="fd"||p.nx<3||p.nx>65||p.ny<3||p.ny>65||p.lengthX<=0||p.lengthY<=0||p.diffusion<0||p.timeStep<=0||width<1||width>64||p.component<0||static_cast<std::size_t>(p.component)>=width||(p.boundary!="dirichlet"&&p.boundary!="neumann"&&p.boundary!="flux"&&p.boundary!="robin"&&p.boundary!="periodic"))throw ScriptError("Implicit PDE Explorer requires a checked1..64-coordinate field, FD1/2 grid and supported boundary data.");
        const auto frames=std::ceil((static_cast<long double>(c.t1)-c.t0)/c.sampleStep)+2,nodes=static_cast<long double>(p.nx)*(p.dimension==2?p.ny:1);if(frames>240||frames*nodes*width>500000||(c.t1-c.t0)/p.timeStep>200000)throw ScriptError("Reduce the implicit PDE grid/horizon or increase the sample step within240 frames/500000 coefficients/200000 proposed steps.");
        bounded(p.initial);bounded(p.reaction);if(p.boundary!="periodic")value(p.valueType,p.boundaryValue,options);
        // A 1D initial expression retains y=0 as a trailing fixed parameter.
        const auto initial="native function "+p.valueType+" research_initial_profile(real t,real x"+(p.dimension==2?",real y":"")+",real domain_x,real domain_y"+(p.dimension==1?",real y":"")+suffix+"){return "+p.initial+";}\n";
        const auto reaction="native function "+p.valueType+" research_reaction("+p.valueType+" u,real t,real x"+(p.dimension==2?",real y":"")+",real domain_x,real domain_y"+suffix+"){return "+p.reaction+";}\n";model(initial,options);model(reaction,options);source+=initial+reaction;
        if(preflight)source+="display(\"Implicit PDE native expressions checked; no solve requested.\","+std::to_string(width)+","+number(static_cast<double>(frames*nodes*width))+");\n";
        else{
            auto grid=[&](double length,int n){return "linspace(0.0,"+number(p.boundary=="periodic"?length*(n-1)/n:length)+","+std::to_string(n)+")";};source+="matrix research_x="+grid(p.lengthX,p.nx)+";\n";if(p.dimension==2)source+="matrix research_y="+grid(p.lengthY,p.ny)+";\n";
            auto fixed=bindings;fixed.push_back("domain_x="+number(p.lengthX));fixed.push_back("domain_y="+number(p.lengthY));auto initialFixed=fixed;if(p.dimension==1)initialFixed.push_back("y=0.0");
            source+="record research_result=pdeimplicit(research_reaction,"+(p.dimension==2?std::string("options(x=research_x,y=research_y)"):"research_x")+",research_initial_profile,options(parameters=options("+join(fixed)+"),initial_parameters=options("+join(initialFixed)+"),diffusion="+number(p.diffusion)+",boundary="+quote(p.boundary)+",t0="+number(c.t0)+",t1="+number(c.t1)+",time_step="+number(p.timeStep)+",sample_step="+number(c.sampleStep)+",rtol="+number(c.rtol)+",atol="+number(c.atol)+",max_steps=200000,max_seconds="+number(c.maximumSeconds)+",threads="+std::to_string(c.threads);
            if(p.boundary!="periodic")source+=",boundary_value=("+p.boundaryValue+")";
            if(p.boundary=="robin")source+=",robin_alpha="+number(p.robinAlpha);
            if(c.livePlots)source+=",live_plot=options(figure=62,coordinate="+std::to_string(p.component+1)+")";
            source+="));\ndisplay(\"Adaptive native implicit PDE steps/threads:\",research_result.steps,research_result.threads_used);\nfigure(60,\"Name\",\"Research Explorer: implicit PDE\",\"NumberTitle\",\"off\");clf;\n";
            if(p.dimension==1)source+="matrix research_coordinate_field=pdeimplicitcoeff(research_result,"+std::to_string(p.component)+");subplot(1,2,1);plot(research_x,research_coordinate_field(end,:));title(\"Final stored field coefficient\");subplot(1,2,2);imagesc(research_x,research_result.t,research_coordinate_field);title(\"Retained space-time field\");\n";
            else source+="matrix research_final_slice=pdeimplicitslice(research_result,length(research_result.t),"+std::to_string(p.component)+");subplot(1,2,1);imagesc(research_x,research_y,research_final_slice);subplot(1,2,2);surf(research_x,research_y,research_final_slice);\n";
            source+="figure(61,\"Name\",\"Research Explorer: full-coefficient playback\",\"NumberTitle\",\"off\");clf;animatefield(research_result,options(component="+std::to_string(p.component)+",fps=10,max_frames=240));\n";
        }
    }else{
        const auto& m=c.spatial.mechanics;const bool constrained=m.form=="constrained";const auto n=m.dimensions;
        if(n<1||n>8||m.timeStep<=0||!std::isfinite(m.timeStep)||(c.t1-c.t0)/m.timeStep>200000||(std::ceil((c.t1-c.t0)/c.sampleStep)+2)*(8*n+16)>500000||(m.form!="newton"&&m.form!="lagrangian"&&m.form!="hamiltonian"&&!constrained))throw ScriptError("Mechanics Explorer accepts1..8 real configuration coordinates within bounded step/history allowances.");
        if((m.form=="newton"&&m.method!="verlet"&&m.method!="rk4")||(m.form=="lagrangian"&&m.method!="rk4")||(m.form=="hamiltonian"&&m.method!="midpoint")||(constrained&&(m.method!="idas"||m.constraints<1||m.constraints>n||(m.constraintMode!="position"&&m.constraintMode!="velocity"))))throw ScriptError("Select a compatible native mechanics method and regular constraint layout.");
        const auto qType=n==1?std::string("real"):"matrix<"+std::to_string(n)+",1>";value(qType,m.initialQ,options);value(qType,m.initialSecond,options);value("real",m.mass,options);bounded(m.expression);
        const auto signature="real t,"+qType+" q"+(m.form=="newton"?std::string():","+qType+(m.form=="hamiltonian"?" p":" v"));const auto definition="native function "+(m.form=="newton"?qType:std::string("real"))+" research_model("+signature+suffix+"){return "+m.expression+";}\n";model(definition,options);source+=definition;
        if(constrained){bounded(m.constraintExpression);const auto result=m.constraints==1?std::string("real"):"matrix<"+std::to_string(m.constraints)+",1>";const auto constraint="native function "+result+" research_constraint(real t,"+qType+" q"+(m.constraintMode=="velocity"?","+qType+" v":std::string())+suffix+"){return "+m.constraintExpression+";}\n";model(constraint,options);source+=constraint;}
        if(preflight)source+="display(\"Native mechanics expressions checked; no integration requested.\");\n";
        else{
            source+=qType+" research_q0="+m.initialQ+";"+qType+" research_second0="+m.initialSecond+";\nrecord research_result=";
            if(constrained)source+="mechanicsdae(research_model,research_constraint,research_q0,research_second0,options(parameters="+parameters+",constraint_parameters="+parameters+",constraint_mode="+quote(m.constraintMode)+",rtol="+number(c.rtol)+",atol="+number(c.atol)+",maximum_step="+number(m.timeStep);
            else source+="mechanics(research_model,research_q0,research_second0,options(parameters="+parameters+",form="+quote(m.form)+",method="+quote(m.method)+",time_step="+number(m.timeStep)+(m.form=="newton"?",mass=("+m.mass+")":std::string());
            source+=",t0="+number(c.t0)+",t1="+number(c.t1)+",sample_step="+number(c.sampleStep)+",max_wall_seconds="+number(c.maximumSeconds)+"));\ndisplay(\"Native mechanics method and steps:\",research_result.method,research_result.steps);\nfigure(60,\"Name\",\"Research Explorer: complete mechanics coordinates\",\"NumberTitle\",\"off\");clf;\n";
            for(int k=1;k<=n;++k)source+="subplot("+std::to_string(n)+",2,"+std::to_string(2*k-1)+");plot(research_result.t,research_result.q(:,"+std::to_string(k)+"));title(\"Configuration "+std::to_string(k)+"\");subplot("+std::to_string(n)+",2,"+std::to_string(2*k)+");plot(research_result.q(:,"+std::to_string(k)+"),research_result.v(:,"+std::to_string(k)+"));title(\"Position / velocity "+std::to_string(k)+"\");\n";
            if(constrained)source+="display(\"Constraint residual and retained-sample energy balance:\",research_result.maximum_constraint_error,research_result.energy_balance(end));\n";
        }
    }
    auto finalOptions=options;finalOptions.initialSymbols.clear();const auto checked=language::analyse(source,finalOptions);if(!checked.ok())throw ScriptError(reason(checked));return source;
}
}
