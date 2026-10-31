#include "code_export.hpp"
#include <algorithm>
#include <iomanip>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
namespace penta::exporting {
namespace {
using namespace language;
#ifndef PENTA_DISTRIBUTION_BUILD
const char* pythonRuntime=
#include "python_runtime.inc"
#include "python_scientific.inc"
#include "python_optimize.inc"
#include "python_graph.inc"
#include "python_pde.inc"
#include "python_geometry.inc"
#include "python_mechanics.inc"
#include "python_advanced_ode.inc"
#include "python_hybrid.inc"
#include "python_reverse.inc"
;
const char* matlabRuntime=
#include "matlab_runtime.inc"
#include "matlab_scientific.inc"
#include "matlab_optimize.inc"
#include "matlab_graph.inc"
#include "matlab_pde.inc"
#include "matlab_geometry.inc"
#include "matlab_mechanics.inc"
#include "matlab_advanced_ode.inc"
#include "matlab_hybrid.inc"
#include "matlab_reverse.inc"
;
const char* matlabDual=
#include "matlab_dual.inc"
;
#endif
std::string quote(std::string_view text){std::string out="\"";for(unsigned char c:text){if(c=='\\'||c=='\"'){out+='\\';out+=char(c);}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else if(c<32){std::ostringstream hex;hex<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c);out+=hex.str();}else out+=char(c);}return out+'"';}
std::string identifier(std::string_view name){std::string out="u_";for(unsigned char c:name){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9'))out+=char(c);else{std::ostringstream hex;hex<<"_x"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<'_';out+=hex.str();}}return out;}
bool matrix(Type type){return type==Type::RealMatrix||type==Type::PentarnionMatrix||type==Type::LogicalMatrix||type==Type::RealSeries||type==Type::PentarnionSeries||type==Type::IntegerRange||type==Type::RealRange;}
struct Unsupported {std::string text;SourceSpan span;};
struct Function {std::shared_ptr<const FunctionDefinition> definition;std::string source,name;};
struct Model {std::shared_ptr<const OdeDefinition> definition;std::string source,name;};
class Emitter {
    Target target_;Options options_;Result result_;std::ostringstream stream_;std::size_t line_=1,temporary_=0;int depth_=0;std::string source_;
    std::vector<Function> functions_;std::vector<Model> models_;std::set<const Program*> imports_;std::set<std::string> bound_;
    std::map<std::string,std::string> moduleAliases_;
    bool figures_=false,inModel_=false;const FunctionDefinition* activeFunction_=nullptr;
    bool python() const{return target_==Target::Python;}
    [[noreturn]] void unsupported(std::string text,SourceSpan span) const{throw Unsupported{std::move(text),span};}
    std::string q(std::string_view text) const {
        if(python())return quote(text);
        std::string out="'";for(char c:text){if(c=='\'')out+="''";else if(c=='\n'||c=='\r')unsupported("MATLAB export does not yet support multiline string literals.",{});else out+=c;}return out+'\'';
    }
    std::string list(const std::vector<std::string>& values,bool cells=false) const {std::string out=python()?"[":cells?"{":"[";for(std::size_t n=0;n<values.size();++n){if(n)out+=",";out+=values[n];}return out+(python()?"]":cells?"}":"]");}
    std::string helper(const std::string& name,const std::vector<std::string>& values) const {
        std::string out=python()?"p_"+name+"(":"penta_runtime("+q(name);for(std::size_t n=0;n<values.size();++n){if(n||!python())out+=",";out+=values[n];}return out+")";
    }
    std::string boolean(bool value)const{return python()?(value?"True":"False"):(value?"true":"false");}
    std::string call(const std::string& name,const std::vector<std::string>& args)const{std::vector<std::string> values{q(name)};values.insert(values.end(),args.begin(),args.end());return helper("call",values);}
    void emit(std::string text,SourceSpan span={}){const bool control=text=="end"||text=="else"||text.starts_with("function ")||text.starts_with("for ")||text.starts_with("if ")||text.starts_with("while ")||text.starts_with("%");stream_<<std::string(depth_*4,' ')<<text<<(python()||control?"\n":";\n");result_.sourceMap.push_back({python()?"exported.py":"exported.m",source_,line_,span});line_+=std::count(text.begin(),text.end(),'\n')+1;}
    std::string functionName(const std::shared_ptr<const FunctionDefinition>& function,SourceSpan span)const{for(const auto& entry:functions_)if(entry.definition==function)return entry.name;unsupported("The resolved function is outside the export bundle.",span);}
    std::shared_ptr<const FunctionDefinition> resolvedFunction(const Expr& value)const{
        for(const auto& entry:functions_)if(entry.definition==value.function)return entry.definition;
        std::string name=value.text,source=source_;
        if((value.kind==ExprKind::Invoke&&!value.children.empty())||value.kind==ExprKind::Member){
            const auto& handle=value.kind==ExprKind::Member?value:*value.children[0];name=handle.text;
            if(handle.kind==ExprKind::Member&&handle.children[0]->kind==ExprKind::Reference){const auto found=moduleAliases_.find(source_+"::"+handle.children[0]->text);if(found==moduleAliases_.end())return {};source=found->second;}
            else if(handle.kind!=ExprKind::FunctionHandle)return {};
        }
        for(const auto& entry:functions_)if(entry.source==source&&entry.definition->name==name)return entry.definition;
        return {};
    }
    std::string modelName(const std::shared_ptr<const OdeDefinition>& model,SourceSpan span)const{for(const auto& entry:models_)if(entry.definition==model)return entry.name;unsupported("The resolved ODE model is outside the export bundle.",span);}
    std::string cast(std::string value,Type type)const{return type==Type::Pentarnion||type==Type::PentarnionMatrix?helper("cast",{std::move(value),q(type==Type::Pentarnion?"pentarnion":"pentarnionmatrix")}):value;}
    std::string selectors(const std::string& base,const std::vector<ExprPtr>& arguments){std::vector<std::string> values;for(std::size_t n=0;n<arguments.size();++n)values.push_back(expression(arguments[n],base,n,arguments.size()));return list(values,true);}
    void validateAnalyticFunction(const std::shared_ptr<const FunctionDefinition>& definition){
        std::set<const FunctionDefinition*> inspected;std::function<void(const std::shared_ptr<const FunctionDefinition>&)> function;std::function<void(const ExprPtr&)> expr;std::function<void(const std::vector<Statement>&)> statements;
        expr=[&](const ExprPtr&pointer){if(!pointer)return;const auto&node=*pointer;if(node.type==Type::Complex||node.type==Type::Algebra||node.type==Type::Element||node.type==Type::ElementMatrix)unsupported("This target analytic callback adapter requires real/Pentarnion coefficient calculus.",node.span);if(node.kind==ExprKind::Binary&&(node.text=="\\"||((node.text=="/"||node.text=="./")&&(node.children[1]->type==Type::Pentarnion||node.children[1]->type==Type::PentarnionMatrix||(node.text=="/"&&matrix(node.children[1]->type))))))unsupported("Target analytic callbacks do not differentiate ordered/numerical equation-solver calls.",node.span);if(node.kind==ExprKind::Call||node.kind==ExprKind::Invoke){static const std::set<std::string> unavailable{"linsolve","solveleft","solveright","leftop","rightop","inv","det","complex"};if(unavailable.contains(node.text))unsupported("No validated target analytic derivative for call "+node.text+".",node.span);if(node.function)function(node.function);else if(node.schema&&node.schema->function)function(node.schema->function);}for(const auto&child:node.children)expr(child);};
        statements=[&](const std::vector<Statement>&body){for(const auto&statement:body){expr(statement.expression);for(const auto&argument:statement.arguments)expr(argument);statements(statement.body);statements(statement.alternative);}};
        function=[&](const std::shared_ptr<const FunctionDefinition>&f){if(!f)return;auto complete=f;for(const auto&entry:functions_)if(entry.definition->name==f->name&&!entry.definition->body.empty()&&(entry.source==source_||(f->origin&&entry.definition->origin&&f->origin->source==entry.definition->origin->source))){complete=entry.definition;break;}if(inspected.insert(complete.get()).second)statements(complete->body);};function(definition);
    }
    std::string scientific(const Expr& value){
        if(!options_.allowScientificAdapters)unsupported("Scientific solver export requires explicit --scientific-adapters opt-in and review of target algorithm mappings.",value.span);
        const bool curve=value.text=="curvefit";const std::size_t required=curve?4:2;if(value.children.size()!=required&&value.children.size()!=required+1)unsupported("Incorrect scientific target adapter argument count.",value.span);
        if(value.children[1]->type!=Type::Real&&value.children[1]->type!=Type::Integer&&value.children[1]->type!=Type::RealMatrix&&value.children[1]->type!=Type::Tuple)unsupported("This target fitting adapter requires an explicitly real initial point or real tuple.",value.children[1]->span);
        const auto& callable=value.children[0];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;if(!function||!function->captureNames.empty())unsupported("Target fitting requires a resolved pure callback without captured values.",callable->span);validateAnalyticFunction(function);std::vector<std::string> names;for(const auto& parameter:function->parameters){if(parameter.type!=Type::Real)unsupported("This target fitting adapter currently binds real scalar parameter groups; algebra/fixed-array unknown ABI mappings are separate.",parameter.span);names.push_back(q(parameter.name));}
        const auto allowedResult=[](Type t,const std::shared_ptr<const ValueSchema>&schema){return t==Type::Real||t==Type::Pentarnion||((t==Type::RealMatrix||t==Type::PentarnionMatrix)&&schema&&!schema->shape.empty());};if(function->results.empty()){if(!allowedResult(function->result,function->resultSchema))unsupported("Target fitting residuals require real/Pentarnion scalars or explicit fixed arrays.",function->span);}else for(std::size_t k=0;k<function->results.size();++k){const auto schema=function->resultSchema&&k<function->resultSchema->elements.size()?function->resultSchema->elements[k].schema:nullptr;if(!allowedResult(function->results[k],schema))unsupported("Target fitting tuple result has no checked coordinate layout.",function->span);}
        static const std::set<std::string> allowed{"parameters","method","loss","loss_scale","jacobian","weights","lower","upper","parameter_scale","max_iterations","max_evaluations","max_wall_seconds","ftol","gtol","xtol","covariance","noise_variance","threads","history_limit","observation_weights","coefficient_weights"};if(value.children.size()>required&&value.children.back()->schema)for(const auto& field:value.children.back()->schema->fields)if(!allowed.contains(field.name)||(!curve&&(field.name=="observation_weights"||field.name=="coefficient_weights")))unsupported("No target fitting mapping for option "+field.name+".",value.children.back()->span);
        std::vector<std::string> args{q(value.text)};for(const auto& child:value.children)args.push_back(expression(child));if(value.children.size()==required)args.push_back(helper("record",{list({},true),list({},true)}));args.push_back(list(names,true));return helper("scientific",args);
    }
    std::string scientificMinimize(const Expr& value){
        if(!options_.allowScientificAdapters)unsupported("Optimization export requires explicit --scientific-adapters opt-in.",value.span);
        if(value.children.size()!=2&&value.children.size()!=3)unsupported("Use minimize(objective,real_initial[,options]).",value.span);
        const auto point=value.children[1];if(point->type!=Type::Integer&&point->type!=Type::Real&&point->type!=Type::RealMatrix&&point->type!=Type::Tuple)unsupported("This target optimization adapter requires an explicitly real initial point.",point->span);
        auto signature=[&](const std::shared_ptr<const FunctionDefinition>& function,bool objective){if(!function||!function->captureNames.empty())unsupported("Target optimization requires resolved pure callback definitions without captures.",value.span);std::vector<std::string> names;for(const auto& parameter:function->parameters){if(parameter.type!=Type::Real)unsupported("This optimization target currently maps real scalar parameter groups.",parameter.span);names.push_back(q(parameter.name));}const auto outputs=function->results.empty()?std::vector<Type>{function->result}:function->results;if(outputs.empty()||!std::all_of(outputs.begin(),outputs.end(),[](Type type){return type==Type::Real;})||(objective&&outputs.size()!=1))unsupported(objective?"A target objective returns one real scalar.":"Target constraints return real scalars or real tuples.",function->span);return list(names,true);};
        const auto& callable=value.children[0];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;std::vector<std::string> metadataNames{q("objective")},metadataValues{signature(function,true)};
        static const std::set<std::string> allowed{"parameters","algorithm","jacobian","lower","upper","parameter_scale","inequality","equality","inequality_parameters","equality_parameters","constraint_tolerance","ftol","ftol_abs","xtol","xtol_abs","max_evaluations","max_wall_seconds","history_limit","initial_step","threads"};
        if(value.children.size()==3){const auto& settings=value.children[2];if(settings->schema)for(const auto&field:settings->schema->fields){if(!allowed.contains(field.name))unsupported("No target optimization mapping for option "+field.name+".",settings->span);if(field.name=="inequality"||field.name=="equality"){metadataNames.push_back(q(field.name));metadataValues.push_back(signature(field.schema?field.schema->function:nullptr,false));}}if(settings->kind==ExprKind::Call&&settings->text=="options")for(std::size_t k=0;k<settings->children.size();++k)if(k<settings->argumentNames.size()&&settings->argumentNames[k]=="algorithm"&&settings->children[k]->kind==ExprKind::StringLiteral){const auto& algorithm=settings->children[k]->text;const std::set<std::string> algorithms=python()?std::set<std::string>{"lbfgs","nelder_mead","slsqp","cobyla","bobyqa","direct"}:std::set<std::string>{"lbfgs","nelder_mead","slsqp"};if(!algorithms.contains(algorithm))unsupported("No explicit target mapping for optimization algorithm "+algorithm+".",settings->children[k]->span);}}
        std::vector<std::string> arguments{q("minimize"),expression(callable),expression(point),value.children.size()==3?expression(value.children[2]):helper("record",{list({},true),list({},true)}),helper("record",{list(metadataNames,true),list(metadataValues,true)})};return helper("scientific",arguments);
    }
    std::string pde(const Expr& value){
        if(!options_.allowScientificAdapters)unsupported("Portable PDE export requires explicit --scientific-adapters opt-in.",value.span);
        if(value.text=="pdecoeff")return helper("pde",{q(value.text),expression(value.children[0]),expression(value.children[1])});
        const auto& callable=value.children[0];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;
        if(!function||!function->captureNames.empty()||function->parameters.size()<3||(function->result!=Type::Real&&function->result!=Type::Pentarnion)||!function->results.empty())unsupported("PDE target reaction requires a closed real/Pentarnion scalar callback.",callable->span);
        std::vector<std::string> names,kinds;for(std::size_t n=0;n<function->parameters.size();++n){const auto&p=function->parameters[n];if((n==0&&p.type!=function->result)||(n>0&&n<3&&p.type!=Type::Real)||(p.type!=Type::Real&&p.type!=Type::Pentarnion))unsupported("PDE target callback begins(state,real time,real x), then scalar real/Pentarnion parameters.",p.span);names.push_back(q(p.name));kinds.push_back(q(typeName(p.type)));}
        static const std::set<std::string> allowed{"parameters","method","boundary","length","diffusion","t0","t1","time_step","sample_step","left","right","max_steps","max_reaction_evaluations","max_wall_seconds","threads","integrator"};if(value.children.size()==4&&value.children[3]->schema)for(const auto&field:value.children[3]->schema->fields)if(!allowed.contains(field.name))unsupported("No portable PDE target mapping for option "+field.name+".",value.children[3]->span);
        return helper("pde",{q(value.text),expression(callable),expression(value.children[1]),expression(value.children[2]),value.children.size()==4?expression(value.children[3]):helper("record",{list({},true),list({},true)}),list(names,true),list(kinds,true)});
    }
    std::string mechanics(const Expr& value){
        if(!options_.allowScientificAdapters)unsupported("Mechanics target export requires explicit --scientific-adapters opt-in.",value.span);
        const auto& callable=value.children[0];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;
        if(!function||!function->captureNames.empty())unsupported("Target mechanics requires a closed native-compatible callback.",callable->span);
        validateAnalyticFunction(function);
        std::vector<std::string> names;for(const auto&parameter:function->parameters){if(parameter.type!=Type::Real)unsupported("Target Newton coordinates and parameters currently use real scalar groups.",parameter.span);names.push_back(q(parameter.name));}
        if(function->results.empty()){if(function->result!=Type::Real&&!(function->result==Type::RealMatrix&&function->resultSchema&&!function->resultSchema->shape.empty()))unsupported("Target Newton force needs checked real coordinates.",function->span);}else if(!std::all_of(function->results.begin(),function->results.end(),[](Type type){return type==Type::Real;}))unsupported("Target Newton force tuple contains non-real coordinates.",function->span);
        for(std::size_t k=1;k<3;++k)if(value.children[k]->type!=Type::Real&&value.children[k]->type!=Type::Integer&&value.children[k]->type!=Type::RealMatrix)unsupported("Target Newton initial data require real scalars/vectors.",value.children[k]->span);
        static const std::set<std::string> allowed{"form","method","parameters","mass","force_layout","t0","t1","time_step","sample_step","max_wall_seconds","max_steps","max_evaluations","newton_tolerance","regularity_tolerance","max_newton_iterations"};
        if(value.children.size()==4){const auto&settings=value.children[3];if(settings->schema)for(const auto&field:settings->schema->fields)if(!allowed.contains(field.name))unsupported("No portable mechanics target mapping for option "+field.name+".",settings->span);}
        return helper("mechanics",{expression(callable),expression(value.children[1]),expression(value.children[2]),value.children.size()==4?expression(value.children[3]):helper("record",{list({},true),list({},true)}),list(names,true)});
    }
    std::string massSolve(const Expr&value){
        if(!options_.allowScientificAdapters||(python()?!options_.allowPythonBdfAdapter:!options_.allowMatlabOde15sAdapter))unsupported("Regular state/time mass export requires --scientific-adapters and the target BDF adapter opt-in.",value.span);
        std::vector<std::string> metadata;for(unsigned k=0;k<2;++k){const auto&callable=value.children[k];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;if(!function||!function->captureNames.empty())unsupported("Regular mass target callbacks must be closed checked functions.",callable->span);std::vector<std::string> names;for(const auto&p:function->parameters){if(p.type!=Type::Real)unsupported("Regular mass target callbacks currently bind real scalar state/parameter groups.",p.span);names.push_back(q(p.name));}if(k==1&&(function->result!=Type::RealMatrix||!function->resultSchema||function->resultSchema->shape.size()!=2))unsupported("Mass target callback requires an explicit matrix<n,n> result.",callable->span);metadata.push_back(list(names,true));}
        for(unsigned k=2;k<4;++k)if(value.children[k]->type!=Type::Real&&value.children[k]->type!=Type::Integer&&value.children[k]->type!=Type::RealMatrix)unsupported("Regular mass target initial data require real scalar/vector coordinates.",value.children[k]->span);
        static const std::set<std::string> allowed{"parameters","mass_parameters","algebraic","initialization","jacobian","t0","t1","sample_step","rtol","atol","initial_residual_tolerance","index_tolerance","max_wall_seconds","initial_step","maximum_step","max_steps","max_evaluations"};if(value.children[4]->schema)for(const auto&field:value.children[4]->schema->fields)if(!allowed.contains(field.name))unsupported("No regular mass target mapping for option "+field.name+".",value.children[4]->span);
        std::vector<std::string> args;for(const auto&child:value.children)args.push_back(expression(child));args.push_back(helper("record",{list({q("rhs"),q("mass")},true),list(metadata,true)}));return helper("masssolve",args);
    }
    std::string modelMetadata(const OdeDefinition& model,bool indexedReset,SourceSpan span,bool indexedEventCost=false){
        std::vector<std::string> names,widths,parameters,parameterWidths;for(const auto&state:model.states){if(state.type!=Type::Real&&state.type!=Type::Pentarnion)unsupported("Advanced target states require real/Pentarnion scalar groups.",span);names.push_back(q(state.name));widths.push_back(state.type==Type::Real?"1":"5");}for(const auto&p:model.parameters){if(p.type!=Type::Real&&p.type!=Type::Pentarnion)unsupported("Advanced target parameters require real/Pentarnion scalar groups.",span);parameters.push_back(q(p.name));parameterWidths.push_back(p.type==Type::Real?"1":"5");}
        return helper("record",{list({q("states"),q("widths"),q("parameters"),q("parameter_widths"),q("indexed_reset"),q("indexed_event_cost")},true),list({list(names,true),list(widths),list(parameters,true),list(parameterWidths),boolean(indexedReset),boolean(indexedEventCost)},true)});
    }
    bool costSignature(const std::shared_ptr<const FunctionDefinition>& function,const OdeDefinition&model,bool allowIndex,SourceSpan span){
        if(!function||!function->captureNames.empty()||function->result!=Type::Real||function->results.size()>1)unsupported("Target objective/cost requires a closed one-real-result callback.",span);
        std::vector<Type> signature{Type::Real};for(const auto&state:model.states)signature.push_back(state.type);for(const auto&parameter:model.parameters)signature.push_back(parameter.type);const std::size_t skip=allowIndex&&function->parameters.size()==signature.size()+1?1:0;if(function->parameters.size()!=signature.size()+skip||(skip&&function->parameters[0].type!=Type::Real))unsupported("Target cost callback arity/index layout differs from the ODE.",span);for(std::size_t k=0;k<signature.size();++k)if(function->parameters[k+skip].type!=signature[k])unsupported("Target cost callback must match(time,states...,parameters...).",span);validateAnalyticFunction(function);return skip!=0;
    }
    std::string adjoint(const Expr&value){
        if(!options_.allowScientificAdapters||(python()?!options_.allowPythonBdfAdapter:!options_.allowMatlabOde15sAdapter))unsupported("Continuous adjoint export requires --scientific-adapters and target BDF adapter opt-in.",value.span);
        const auto model=value.ode?value.ode:value.children[0]->ode;if(!model)unsupported("Target adjoint requires its resolved ODE model.",value.span);const auto&settings=value.children[2];bool objective=false;static const std::set<std::string> allowed{"parameters","objective","running_cost","t0","t1","sample_step","rtol","atol","method","jacobian","mass","initial_step","max_step","max_seconds"};if(settings->schema)for(const auto&field:settings->schema->fields){if(!allowed.contains(field.name))unsupported("No target continuous-adjoint mapping for option "+field.name+".",settings->span);if(field.name=="objective"||field.name=="running_cost"){costSignature(field.schema?field.schema->function:nullptr,*model,false,settings->span);objective=objective||field.name=="objective";}}if(!objective)unsupported("Target adjoint requires an explicitly checked objective option.",settings->span);
        return helper("ode_adjoint",{(python()?"":"@")+modelName(model,value.span),expression(value.children[1]),expression(settings),modelMetadata(*model,false,value.span)});
    }
    std::string hybrid(const Expr&value){
        if(value.text=="hybridval"){std::vector<std::string> args;for(const auto&child:value.children)args.push_back(expression(child));return helper("hybridval",args);}
        const bool reverse=value.text=="hybridadjoint";const std::size_t initialIndex=reverse?4:3,settingsIndex=initialIndex+1;
        if(!options_.allowScientificAdapters||(python()?!options_.allowPythonBdfAdapter:!options_.allowMatlabOde15sAdapter))unsupported("Hybrid target export requires --scientific-adapters and the target BDF adapter opt-in.",value.span);
        const auto model=value.children[0]->ode;if(!model)unsupported("Hybrid target requires its resolved ODE model.",value.span);
        std::vector<Type> signature{Type::Real};for(const auto&state:model->states)signature.push_back(state.type);for(const auto&p:model->parameters)signature.push_back(p.type);bool indexedReset=false;
        for(unsigned k=1;k<3;++k){const auto&callable=value.children[k];const auto function=callable->function?callable->function:callable->schema?callable->schema->function:nullptr;if(!function||!function->captureNames.empty())unsupported("Hybrid target requires closed event/reset callbacks.",callable->span);const std::size_t skip=k==2&&function->parameters.size()==signature.size()+1?1:0;indexedReset=skip!=0;if(function->parameters.size()!=signature.size()+skip||(skip&&function->parameters[0].type!=Type::Real))unsupported("Hybrid callback arity/index layout differs from its ODE.",callable->span);for(std::size_t i=0;i<signature.size();++i)if(function->parameters[i+skip].type!=signature[i])unsupported("Hybrid callback types/order must match(time,states...,parameters...).",callable->span);const auto outputs=function->results.empty()?std::vector<Type>{function->result}:function->results;if(k==1){if(outputs.empty()||!std::all_of(outputs.begin(),outputs.end(),[](Type type){return type==Type::Real;}))unsupported("Hybrid events require real scalar/tuple roots.",callable->span);}else{std::vector<Type> states;for(const auto&state:model->states)states.push_back(state.type);if(outputs!=states)unsupported("Hybrid reset must return the complete declared scalar state groups.",callable->span);}}
        bool indexedCost=false;if(reverse){const auto&objective=value.children[3];costSignature(objective->function?objective->function:objective->schema?objective->schema->function:nullptr,*model,false,objective->span);}
        static const std::set<std::string> allowed{"parameters","t0","t1","sample_step","method","jacobian","rtol","atol","max_steps","max_samples","max_seconds","max_bytes","event_direction","event_terminal","minimum_event_interval","grazing_tolerance","max_events","event_priority","simultaneous","event_time_tolerance","event_gradient_tolerance","sensitivities","sensitivity_parameters","sensitivity_initial"};if(value.children.size()>settingsIndex&&value.children[settingsIndex]->schema)for(const auto&field:value.children[settingsIndex]->schema->fields){if(reverse&&(field.name=="running_cost"||field.name=="event_cost")){const bool indexed=costSignature(field.schema?field.schema->function:nullptr,*model,field.name=="event_cost",value.children[settingsIndex]->span);if(field.name=="event_cost")indexedCost=indexed;}else if(!allowed.contains(field.name))unsupported("No hybrid target mapping for option "+field.name+".",value.children[settingsIndex]->span);}
        std::vector<std::string> args{(python()?"":"@")+modelName(model,value.span),expression(value.children[1]),expression(value.children[2])};if(reverse)args.push_back(expression(value.children[3]));args.push_back(expression(value.children[initialIndex]));args.push_back(value.children.size()>settingsIndex?expression(value.children[settingsIndex]):helper("record",{list({},true),list({},true)}));args.push_back(modelMetadata(*model,indexedReset,value.span,indexedCost));return helper(reverse?"hybrid_adjoint":"hybrid",args);
    }
    std::string ode(const Expr& value){
        if((python()&&!options_.allowPythonBdfAdapter)||(!python()&&!options_.allowMatlabOde15sAdapter))unsupported("ODE export requires explicit opt-in to the SciPy BDF or MATLAB ode15s adapter.",value.span);
        const auto& model=*value.ode;std::map<std::string,ExprPtr> args;for(std::size_t n=0;n<value.children.size();++n)args[value.argumentNames[n]]=value.children[n];
        std::vector<std::string> names,widths,initial,parameters,settings;
        for(const auto& state:model.states){if(state.type!=Type::Real&&state.type!=Type::Pentarnion)unsupported("This ODE export adapter supports real and Pentarnion scalar states; array/generic/complex state adapters require separate coefficient mappings.",state.span);names.push_back(q(state.name));widths.push_back(state.type==Type::Pentarnion?"5":"1");initial.push_back(cast(expression(args.at(state.name)),state.type));args.erase(state.name);}
        for(const auto& parameter:model.parameters){parameters.push_back(cast(expression(args.at(parameter.name)),parameter.type));args.erase(parameter.name);}
        const std::set<std::string> allowed{"t0","t1","sample_step","method","jacobian","rtol","atol","events","event_direction","event_terminal","sensitivities","sensitivity_parameters","sensitivity_initial","sensitivity_method","sensitivity_error_control","mass","initial_step","max_step"};
        for(const auto& [key,argument]:args){
            if(!allowed.contains(key))unsupported("No validated export mapping for ODE option "+key+".",argument->span);
            if((key=="method"&&!(argument->kind==ExprKind::StringLiteral&&argument->text=="bdf"))||(key=="jacobian"&&!(argument->kind==ExprKind::StringLiteral&&argument->text=="analytic")))unsupported("This ODE target adapter requires explicit method=bdf and compiler-generated analytic Jacobians.",argument->span);
            if(key=="events"){const auto callback=argument->function?argument->function:argument->schema?argument->schema->function:nullptr;std::vector<Type> types{Type::Real};for(const auto&state:model.states)types.push_back(state.type);for(const auto&p:model.parameters)types.push_back(p.type);if(!callback||!callback->captureNames.empty()||callback->parameters.size()!=types.size())unsupported("Event target callback must be closed and match(time,states...,parameters...).",argument->span);for(std::size_t k=0;k<types.size();++k)if(callback->parameters[k].type!=types[k])unsupported("Event target callback coordinate types do not match the ODE.",argument->span);const auto outputs=callback->results.empty()?std::vector<Type>{callback->result}:callback->results;if(!std::all_of(outputs.begin(),outputs.end(),[](Type type){return type==Type::Real;}))unsupported("Target events require real scalar/tuple roots.",argument->span);}
            settings.push_back(q(key)+(python()?":":",")+expression(argument));
        }
        std::string config=python()?"{":"struct(";for(std::size_t n=0;n<settings.size();++n){if(n)config+=",";config+=settings[n];}config+=python()?"}":")";
        return helper("ode",{(python()?"":"@")+modelName(value.ode,value.span),list(names,true),list(widths),list(initial,true),list(parameters,true),config});
    }
    std::string expression(const ExprPtr& pointer,const std::string& indexBase={},std::size_t axis=0,std::size_t total=0){
        if(!pointer)unsupported("Missing checked expression.",{});
        const auto& value=*pointer;
        if(inModel_&&matrix(value.type))unsupported("Target ODE analytic AD currently supports scalar real/Pentarnion expressions; array subexpressions require a separate checked derivative adapter.",value.span);
        const auto sub=[&](std::size_t n){return expression(value.children.at(n),indexBase,axis,total);};
        switch(value.kind){
        case ExprKind::IntegerLiteral:case ExprKind::RealLiteral:return value.text;
        case ExprKind::StringLiteral:if(!python()&&value.text.find_first_of("\n\r")!=std::string::npos)unsupported("MATLAB multiline string export is not supported.",value.span);return q(value.text);
        case ExprKind::Reference:
            if(value.text=="i"||value.text=="j"||value.text=="k"||value.text=="ell")return helper("basis",{value.text=="i"?"1":value.text=="j"?"2":value.text=="k"?"3":"4"});
            if(value.type==Type::Function)if(const auto function=resolvedFunction(value))return (python()?"":"@")+functionName(function,value.span);
            if(value.type==Type::Algebra||value.type==Type::Element||value.type==Type::ElementMatrix||value.type==Type::ElementSeries||value.type==Type::ArraySeries)unsupported("Generic algebra and array-state export requires an explicit immutable multiplication-table/shape adapter.",value.span);
            return identifier(value.text);
        case ExprKind::Group:return "("+sub(0)+")";
        case ExprKind::Unary:
            if(python())return value.text=="+"?"(+"+sub(0)+")":value.text=="-"?"(-"+sub(0)+")":matrix(value.children[0]->type)?"np.logical_not("+sub(0)+")":"(not p_truth("+sub(0)+"))";
            return helper("unary",{q(value.text),sub(0)});
        case ExprKind::Binary:{
            if(!inModel_&&value.text=="\\")return call("solveleft",{sub(0),sub(1)});
            if(!inModel_&&value.text=="/"&&value.children[1]->type==Type::Pentarnion)return call("solveright",{sub(1),sub(0)});
            if(value.text=="\\"||((value.text=="/"||value.text=="./")&&(value.children[1]->type==Type::Pentarnion||value.children[1]->type==Type::PentarnionMatrix||(value.text=="/"&&matrix(value.children[1]->type)))))unsupported("Equation division requires a validated target adapter.",value.span);
            if(value.text=="^"&&matrix(value.children[0]->type))unsupported("Matrix powers require a validated target adapter.",value.span);
            if((value.text=="^"||value.text==".^")&&(value.children[0]->type==Type::Pentarnion||value.children[0]->type==Type::PentarnionMatrix)&&value.children[1]->kind!=ExprKind::IntegerLiteral)unsupported("Portable Pentarnion powers currently require a nonnegative integer literal.",value.span);
            if(value.text=="&&"||value.text=="||")return "("+helper("truth",{sub(0)})+(python()?(value.text=="&&"?" and ":" or "):value.text)+helper("truth",{sub(1)})+")";
            return helper("bin",{q(value.text=="~="?"!=":value.text),sub(0),sub(1),boolean(matrix(value.children[0]->type)),boolean(matrix(value.children[1]->type))});
        }
        case ExprKind::PentConstructor:{std::vector<std::string> args;for(std::size_t n=0;n<5;++n)args.push_back(sub(n));return python()?"Penta("+list(args)+")":helper("coefficients",args);}
        case ExprKind::MatrixLiteral:{std::vector<std::string> rows;std::size_t at=0;for(const auto width:value.rowWidths){std::vector<std::string> cells;for(std::size_t c=0;c<width;++c)cells.push_back(sub(at++));rows.push_back(list(cells,true));}return helper("mat",{list(rows,true)});}
        case ExprKind::Range:return helper("range",{sub(0),sub(1),sub(2)});
        case ExprKind::EndIndex:if(indexBase.empty())unsupported("end requires an exported index context.",value.span);return helper("end",{indexBase,std::to_string(axis),std::to_string(total)});
        case ExprKind::AllIndices:return python()?"None":q("all");
        case ExprKind::Index:{const auto base=sub(0);return helper("get",{base,selectors(base,{value.children.begin()+1,value.children.end()})});}
        case ExprKind::Transpose:return helper("transpose",{sub(0),boolean(value.text=="'")});
        case ExprKind::OdeSolve:return ode(value);
        case ExprKind::Member:
            if(value.type==Type::Function)if(const auto function=resolvedFunction(value))return (python()?"":"@")+functionName(function,value.span);
            if(value.children[0]->type!=Type::Solution&&value.children[0]->type!=Type::Record)unsupported("This member export requires a record or solution adapter.",value.span);
            return helper("member",{sub(0),q(value.text)});
        case ExprKind::FunctionHandle:return (python()?"":"@")+functionName(resolvedFunction(value),value.span);
        case ExprKind::TupleLiteral:{std::vector<std::string> values;for(std::size_t n=0;n<value.children.size();++n)values.push_back(sub(n));return python()?"tuple("+list(values)+")":list(values,true);}
        case ExprKind::Lambda:{if(!value.function||value.function->body.size()!=1||value.function->body[0].kind!=StatementKind::Return)unsupported("Only a checked expression lambda has a portable adapter.",value.span);const auto&f=*value.function;std::string parameters;std::vector<std::string>kinds;for(const auto&p:f.parameters){if(!parameters.empty())parameters+=",";parameters+=identifier(p.name);kinds.push_back(q(typeName(p.type)));}const auto body=expression(f.body[0].expression);std::vector<std::string> captures;for(const auto&name:f.captureNames){if(!parameters.empty())parameters+=",";parameters+=identifier(name);captures.push_back(identifier(name));}return helper("closure",{python()?"lambda "+parameters+": "+body:"@("+parameters+")"+body,list(kinds,true),list(captures,true)});}
        case ExprKind::Call:case ExprKind::Invoke:{
            if(const auto function=resolvedFunction(value)){
                std::vector<std::string> args;const std::size_t start=value.kind==ExprKind::Invoke?1:0;
                std::vector<std::string> suppliedNames,parameterNames,parameterKinds,defaults;
                for(std::size_t n=start;n<value.children.size();++n){args.push_back(sub(n));suppliedNames.push_back(q(n<value.argumentNames.size()?value.argumentNames[n]:""));}
                for(std::size_t n=0;n<function->parameters.size();++n){parameterNames.push_back(q(function->parameters[n].name));parameterKinds.push_back(q(typeName(function->parameters[n].type)));defaults.push_back(n<function->parameterDefaults.size()&&function->parameterDefaults[n]?expression(function->parameterDefaults[n]):(python()?"None":"[]"));}
                return helper("invoke",{(python()?"":"@")+functionName(function,value.span),list(parameterNames,true),list(parameterKinds,true),list(suppliedNames,true),list(args,true),list(defaults,true)});
            }
            if(value.kind==ExprKind::Invoke){if(std::any_of(value.argumentNames.begin(),value.argumentNames.end(),[](const auto&name){return !name.empty();}))unsupported("Dynamic named/default callback export requires a statically resolved function signature.",value.span);std::string out=sub(0)+"(";for(std::size_t n=1;n<value.children.size();++n){if(n>1)out+=",";out+=sub(n);}return out+")";}
            if(value.function||bound_.contains(value.text)){if(std::any_of(value.argumentNames.begin(),value.argumentNames.end(),[](const auto&name){return !name.empty();}))unsupported("A dynamic callback requires normalized positional arguments in the target adapter.",value.span);std::string out=identifier(value.text)+"(";for(std::size_t n=0;n<value.children.size();++n){if(n)out+=",";out+=sub(n);}return out+")";}
            if(value.text=="lsqfit"||value.text=="lsqnonlin"||value.text=="curvefit")return scientific(value);
            if(value.text=="minimize")return scientificMinimize(value);
            if(value.text=="pdesolve"||value.text=="pdecoeff")return pde(value);
            if(value.text=="mechanics")return mechanics(value);
            if(value.text=="masssolve")return massSolve(value);
            if(value.text=="hybridsolve"||value.text=="hybridval"||value.text=="hybridadjoint")return hybrid(value);
            if(value.text=="odeadjoint")return adjoint(value);
            static const std::set<std::string> odeQueries{"odesens","odeevents","odestats","deval"};
            if(odeQueries.contains(value.text)){std::vector<std::string> values{q(value.text)};for(std::size_t n=0;n<value.children.size();++n)values.push_back(sub(n));return helper("ode_query",values);}
            static const std::set<std::string> geometryCalls{"curvegeometry","surf","imagesc","contour","isosurface"};
            if(geometryCalls.contains(value.text)){std::vector<std::string> values{q(value.text)};for(std::size_t n=0;n<value.children.size();++n)values.push_back(sub(n));return helper("geometry",values);}
            static const std::set<std::string> graphCalls{"graph","digraph","graphinfo","graphcomponents","components","shortestpath","spanningtree","toposort","centrality","graphlaplacian","graphadjacency","graphlayout","graphcycle","plotgraph"};
            if(graphCalls.contains(value.text)){if(!options_.allowScientificAdapters)unsupported("Graph target adapters require explicit --scientific-adapters opt-in.",value.span);std::vector<std::string> values{q(value.text)};for(std::size_t n=0;n<value.children.size();++n)values.push_back(sub(n));return helper("graph",values);}
            if(value.text=="options"){std::vector<std::string> fields,values;for(std::size_t n=0;n<value.children.size();++n){if(n>=value.argumentNames.size()||value.argumentNames[n].empty())unsupported("Portable records require checked named fields.",value.span);fields.push_back(q(value.argumentNames[n]));values.push_back(sub(n));}return helper("record",{list(fields,true),list(values,true)});}
            const std::set<std::string> supported{"sin","cos","exp","sinh","cosh","sqrt","log","tan","tanh","abs","floor","ceil","assert","coeff","scalar","pure","conj","norm2","norm","reshape","zeros","ones","eye","linspace","size","numel","length","transpose","find","logical","diag","sum","mean","toc","commutator","associator","complex","realpart","imagpart","linsolve","solveleft","solveright","leftop","rightop","inv","det"};
            if(!supported.contains(value.text))unsupported("No validated export adapter for call "+value.text+".",value.span);
            if(inModel_&&(value.text=="linsolve"||value.text=="solveleft"||value.text=="solveright"||value.text=="leftop"||value.text=="rightop"||value.text=="inv"||value.text=="det"))unsupported("Target ODE Jacobians do not substitute numerical equation/factorization calls for compiled scalar AD.",value.span);
            if((value.text=="inv"||value.text=="det")&&!value.children.empty()&&(value.children[0]->type==Type::Pentarnion||value.children[0]->type==Type::PentarnionMatrix))unsupported("Export determinants/inverses require real matrices or explicit leftop/rightop coefficient operators.",value.span);
            if(!python()&&(value.text=="sum"||value.text=="mean")&&!value.children.empty()&&value.children[0]->type==Type::PentarnionMatrix)unsupported("MATLAB Pentarnion array reductions are not validated.",value.span);
            std::vector<std::string> args;for(std::size_t n=0;n<value.children.size();++n)args.push_back(sub(n));return call(value.text=="scalar"?"scalarpart":value.text=="pure"?"purepart":value.text=="realpart"?"real":value.text=="imagpart"?"imag":value.text,args);
        }
        default:unsupported("Unsupported checked expression kind in portable export.",value.span);
        }
    }
    void block(const std::vector<Statement>& statements){for(const auto& statement:statements)one(statement);}
    void one(const Statement& statement){
        const auto expr=[&]{return expression(statement.expression);};std::vector<std::string> args;
        switch(statement.kind){
        case StatementKind::Directive:case StatementKind::Import:case StatementKind::FunctionDefinition:case StatementKind::OdeDefinition:return;
        case StatementKind::Declaration:case StatementKind::Assignment:
            if(statement.declaredType==Type::Algebra||statement.declaredType==Type::Element||statement.declaredType==Type::ElementMatrix)unsupported("Generic algebra declarations require a multiplication-table export adapter.",statement.span);
            emit(identifier(statement.name)+" = "+cast(expr(),statement.declaredType),statement.span);bound_.insert(statement.name);return;
        case StatementKind::IndexedAssignment:{const auto base=identifier(statement.name);emit(base+" = "+helper("set",{base,selectors(base,statement.arguments),expr()}),statement.span);return;}
        case StatementKind::Expression:emit(expr(),statement.span);return;
        case StatementKind::Destructure:{const auto values="p_tuple_"+std::to_string(temporary_++);emit(values+" = "+expr(),statement.span);for(std::size_t n=0;n<statement.names.size();++n)if(statement.names[n]!="_"){emit(identifier(statement.names[n])+" = "+(python()?values+"["+std::to_string(n)+"]":values+"{"+std::to_string(n+1)+"}"),statement.span);bound_.insert(statement.names[n]);}return;}
        case StatementKind::Display:for(const auto& arg:statement.arguments)args.push_back(expression(arg));emit(call("display",args),statement.span);return;
        case StatementKind::Clear:{std::vector<std::string> names=statement.names;if(names.empty())names={bound_.begin(),bound_.end()};for(const auto& name:names)if(bound_.contains(name)){emit((python()?"del ":"clear ")+identifier(name),statement.span);bound_.erase(name);}return;}
        case StatementKind::ClearConsole:emit(python()?"# clc: transcript clearing is not meaningful in a standalone export":"% clc: transcript clearing is not meaningful in a standalone export",statement.span);return;
        case StatementKind::If:case StatementKind::While:{
            emit((statement.kind==StatementKind::If?"if ":"while ")+helper("truth",{expr()})+(python()?":":""),statement.span);++depth_;if(statement.body.empty()&&python())emit("pass");block(statement.body);--depth_;
            if(!statement.alternative.empty()){emit(python()?"else:":"else",statement.span);++depth_;block(statement.alternative);--depth_;}if(!python())emit("end");return;
        }
        case StatementKind::For:{
            const auto name=identifier(statement.name),saved="p_saved_"+std::to_string(temporary_++),values="p_values_"+std::to_string(temporary_++),counter="p_index_"+std::to_string(temporary_++);const bool restore=bound_.contains(statement.name);
            if(restore)emit(saved+" = "+name,statement.span);
            emit(values+" = "+expr(),statement.span);
            emit(python()?"for "+name+" in np.asarray("+values+").ravel(order='F'):":"for "+counter+" = 1:numel("+values+")",statement.span);++depth_;
            if(!python())emit(name+" = "+helper("get",{values,list({counter},true)}),statement.span);
            if(statement.body.empty()&&python())emit("pass");
            block(statement.body);--depth_;if(!python())emit("end");
            if(restore)emit(name+" = "+saved,statement.span);else emit(python()?"if "+q(name)+" in locals(): del "+name:"clear "+name,statement.span);return;
        }
        case StatementKind::Break:emit("break",statement.span);return;
        case StatementKind::Continue:emit("continue",statement.span);return;
        case StatementKind::Return:{std::string output=statement.expression?expr():(python()?"None":"[]");if(activeFunction_&&statement.expression){if(activeFunction_->results.size()>1&&statement.expression->kind==ExprKind::TupleLiteral){std::vector<std::string> values;for(std::size_t k=0;k<activeFunction_->results.size();++k)values.push_back(cast(expression(statement.expression->children[k]),activeFunction_->results[k]));output=python()?"tuple("+list(values)+")":list(values,true);}else output=cast(output,activeFunction_->result);}emit(python()?"return "+output:(statement.expression?"p_result = "+output+"; return":"return"),statement.span);return;}
        case StatementKind::Tic:emit(call("tic",{}),statement.span);return;
        case StatementKind::Toc:emit(call("display",{call("toc",{})}),statement.span);return;
        case StatementKind::Figure:emit(python()?"p_figure("+(statement.expression?expr():"None")+","+(statement.figureName?q(*statement.figureName):"None")+")":"figure("+(statement.expression?expr():"")+")",statement.span);return;
        case StatementKind::Subplot:for(const auto& a:statement.arguments)args.push_back(expression(a));emit((python()?"plt.subplot(":"subplot(")+args[0]+","+args[1]+","+args[2]+")",statement.span);return;
        case StatementKind::Plot:{
            for(const auto& a:statement.arguments)args.push_back(expression(a));
            if(python())emit("p_plot("+std::to_string(statement.dimensions)+","+list({std::to_string(statement.axes[0]),std::to_string(statement.axes[1]),std::to_string(statement.axes[2])})+","+list(args)+")",statement.span);
            else if(statement.axes[0]<0){std::string text=statement.dimensions==2?"plot(":"plot3(";for(std::size_t n=0;n<args.size();++n){if(n)text+=",";text+=args[n];}emit(text+")",statement.span);}
            else {emit("hold on",statement.span);for(const auto& a:args){std::vector<std::string> coords;for(int axis=0;axis<statement.dimensions;++axis)coords.push_back(call("coeff",{a,std::to_string(statement.axes[axis])}));std::string text=statement.dimensions==2?"quiver(0,0,":"quiver3(0,0,0,";for(std::size_t n=0;n<coords.size();++n){if(n)text+=",";text+=coords[n];}emit(text+",0)",statement.span);}emit("hold off",statement.span);}return;
        }
        case StatementKind::SetTitle:case StatementKind::SetXLabel:case StatementKind::SetYLabel:case StatementKind::SetZLabel:case StatementKind::SetCaption:{
            if(statement.kind==StatementKind::SetCaption)unsupported("Caption placement has no validated portable figure adapter.",statement.span);
            const std::string name=statement.kind==StatementKind::SetTitle?"title":statement.kind==StatementKind::SetXLabel?"xlabel":statement.kind==StatementKind::SetYLabel?"ylabel":"zlabel";
            if(python()&&name=="zlabel")emit("plt.gca().set_zlabel("+q(statement.text)+")",statement.span);else emit((python()?"plt.":"")+name+"("+q(statement.text)+")",statement.span);return;
        }
        case StatementKind::CloseAllFigures:emit(python()?"plt.close('all')":"close all",statement.span);return;
        case StatementKind::CloseFigure:emit((python()?"plt.close(":"close(")+expr()+")",statement.span);return;
        case StatementKind::ClearFigure:emit(python()?"plt.clf()":"clf",statement.span);return;
        default:unsupported("This Studio statement has no validated portable export adapter.",statement.span);
        }
    }
    void collect(const std::vector<Statement>& statements,const std::string& source){
        const auto plotCall=[&](const auto& self,const ExprPtr&expression)->bool{if(!expression)return false;static const std::set<std::string> names{"surf","imagesc","contour","isosurface","plotgraph"};if(expression->kind==ExprKind::Call&&names.contains(expression->text))return true;return std::any_of(expression->children.begin(),expression->children.end(),[&](const auto&child){return self(self,child);});};
        for(const auto& statement:statements){
            if(plotCall(plotCall,statement.expression)||std::any_of(statement.arguments.begin(),statement.arguments.end(),[&](const auto&argument){return plotCall(plotCall,argument);}))figures_=true;
            if(statement.kind==StatementKind::Figure||statement.kind==StatementKind::Plot||statement.kind==StatementKind::Subplot||statement.kind==StatementKind::CloseAllFigures||statement.kind==StatementKind::CloseFigure||statement.kind==StatementKind::ClearFigure||statement.kind==StatementKind::SetTitle||statement.kind==StatementKind::SetXLabel||statement.kind==StatementKind::SetYLabel||statement.kind==StatementKind::SetZLabel)figures_=true;
            if(statement.kind==StatementKind::Import&&statement.module){moduleAliases_[source+"::"+statement.name]=statement.moduleId;if(imports_.insert(statement.module.get()).second)collect(statement.module->statements,statement.moduleId);}
            if(statement.function&&std::none_of(functions_.begin(),functions_.end(),[&](const auto& f){return f.definition==statement.function;}))functions_.push_back({statement.function,source,"p_function_"+std::to_string(functions_.size())+"_"+identifier(statement.function->name)});
            if(statement.ode&&statement.kind==StatementKind::OdeDefinition)models_.push_back({statement.ode,source,"p_model_"+std::to_string(models_.size())});
            collect(statement.body,source);collect(statement.alternative,source);
        }
    }
    void functions(){
        for(const auto& entry:functions_){source_=entry.source;const auto& f=*entry.definition;if(!f.captureNames.empty())unsupported("Captured named functions require an explicit closure declaration adapter; expression lambdas are supported.",f.span);
            std::string signature;for(const auto& parameter:f.parameters){if(!signature.empty())signature+=",";signature+=identifier(parameter.name);}
            emit(python()?"def "+entry.name+"("+signature+"):":"function p_result = "+entry.name+"("+signature+")",f.span);++depth_;bound_.clear();for(const auto& parameter:f.parameters)bound_.insert(parameter.name);if(f.body.empty()&&python())emit("pass");activeFunction_=&f;block(f.body);activeFunction_=nullptr;--depth_;if(!python())emit("end");
        }
        for(const auto& entry:models_){source_=entry.source;const auto& m=*entry.definition;emit(python()?"def "+entry.name+"(p_time,p_state,p_parameters):":"function p_result = "+entry.name+"(p_time,p_state,p_parameters)",m.span);++depth_;emit(identifier(m.timeName)+" = p_time",m.span);std::size_t offset=0;
            for(const auto& state:m.states){if(state.type!=Type::Real&&state.type!=Type::Pentarnion)unsupported("Export ODE state types require explicit real/Pentarnion scalar groups.",state.span);const auto width=state.type==Type::Pentarnion?5u:1u;const auto initial=python()?(width==1?"p_state["+std::to_string(offset)+"]":"Penta(p_state["+std::to_string(offset)+":"+std::to_string(offset+5)+"])"):(width==1?"p_state("+std::to_string(offset+1)+")":"struct('c',reshape(p_state("+std::to_string(offset+1)+":"+std::to_string(offset+5)+"),1,5))");emit(identifier(state.name)+" = "+initial,state.span);offset+=width;}
            for(std::size_t n=0;n<m.parameters.size();++n){if(m.parameters[n].type!=Type::Real&&m.parameters[n].type!=Type::Pentarnion)unsupported("Export ODE parameter types require real/Pentarnion scalar groups.",m.parameters[n].span);emit(identifier(m.parameters[n].name)+" = p_parameters"+(python()?"["+std::to_string(n)+"]":"{"+std::to_string(n+1)+"}"),m.parameters[n].span);}
            std::vector<std::string> derivatives;inModel_=true;for(std::size_t n=0;n<m.derivatives.size();++n)derivatives.push_back(cast(expression(m.derivatives[n]),m.states[n].type));inModel_=false;emit((python()?"return ":"p_result = ")+helper("pack",{list(derivatives,true)}),m.span);--depth_;if(!python())emit("end");
        }
    }
public:
    Emitter(Target target,Options options):target_(target),options_(std::move(options)),source_(options_.sourceName){}
    Result run(const AnalysisResult& checked){
        try{
            collect(checked.program.statements,source_);
            emit(python()?"# Exported from checked Pentarnion AST; explicit operand order retained.":"% Exported from checked Pentarnion AST; explicit operand order retained.");
#ifdef PENTA_DISTRIBUTION_BUILD
            const std::vector<std::string> required=python()?std::vector<std::string>{"penta_export_runtime.pyd"}:std::vector<std::string>{"penta_runtime_native.mexw64","penta_runtime.m","penta_dual.m","penta_runtime_bind.m","penta_runtime_context.m"};
            for(const auto& name:required)if(!options_.compiledRuntimeFiles.contains(name)||options_.compiledRuntimeFiles.at(name).empty())unsupported("Export requires the matching installed compiled target runtime component: "+name,{});
            if(python()){emit("import os as _penta_os, sys as _penta_sys");emit("_penta_sys.path.insert(0, _penta_os.path.dirname(_penta_os.path.abspath(__file__)))");}
#endif
            if(python()){emit("import penta_export_runtime as p_runtime");emit("from penta_export_runtime import *");if(figures_)emit("import matplotlib.pyplot as plt");}
            std::ostringstream gamma;gamma<<std::setprecision(17)<<checked.program.gamma;emit(python()?"p_runtime.GAMMA = "+gamma.str():helper("init",{gamma.str()}));
            if(python())functions();
            source_=options_.sourceName;bound_.clear();block(checked.program.statements);if(!python())functions();
            if(python()&&figures_)emit("if __name__ == '__main__': plt.show()");
            result_.files.push_back({python()?"exported.py":"exported.m",stream_.str()});
#ifdef PENTA_DISTRIBUTION_BUILD
            for(const auto& name:required)result_.files.push_back({name,options_.compiledRuntimeFiles.at(name)});
#else
            result_.files.push_back({python()?"penta_export_runtime.py":"penta_runtime.m",python()?pythonRuntime:matlabRuntime});
            if(!python()&&(!models_.empty()||options_.allowScientificAdapters))result_.files.push_back({"penta_dual.m",matlabDual});
#endif
        }catch(const Unsupported& error){result_.diagnostics.push_back({Severity::Error,"X1601",error.text,error.span});result_.files.clear();result_.sourceMap.clear();}
        std::ostringstream report;
        report<<"{\n  \"schema\":1,\n  \"source\":"<<quote(options_.sourceName)
            <<",\n  \"target\":"<<quote(python()?"python":"matlab")
            <<",\n  \"supported\":"<<(result_.diagnostics.empty()?"true":"false")
            <<",\n  \"algebra\":\"A_gamma_v1\",\n  \"gamma\":"<<std::setprecision(17)<<checked.program.gamma
            <<",\n  \"integer_policy\":\"portable subset; exact int64 overflow parity not guaranteed\""
            <<",\n  \"numeric_policy\":\"binary64 target operations; Studio extended-accumulator overflow checks are not reproduced\""
            <<",\n  \"dependencies\":"<<(python()?"[\"Python 3.10+\",\"NumPy\",\"Matplotlib\""+((options_.allowPythonBdfAdapter||options_.allowScientificAdapters)?std::string(",\"SciPy\""):std::string())+"]":options_.allowScientificAdapters?"[\"MATLAB with script local functions\",\"Optimization Toolbox\"]":"[\"MATLAB with script local functions\"]")
#ifdef PENTA_DISTRIBUTION_BUILD
            <<",\n  \"runtime_delivery\":\"Compiled Windows x86-64 target support supplied alongside generated user code; first-party numerical implementation sources are excluded. Python uses an abi3 extension; MATLAB uses a MEX runtime. Target language, solver and toolbox requirements remain.\""
#endif
            <<",\n  \"solver_mapping\":"<<quote(python()&&options_.allowPythonBdfAdapter?"Explicit opt-in: CVODE BDF -> scipy.integrate.solve_ivp BDF; generated AD1/AD2 analytic Jacobians, localized events, complete scalar real/Pentarnion trajectory sensitivities, independent adaptive trajectories. With scientific opt-in: regular state/time mass, hybrid indexed priority resets/saltation and actual continuous reverse cost adjoints using retained adaptive dense output.":!python()&&options_.allowMatlabOde15sAdapter?"Explicit opt-in: CVODE BDF -> MATLAB ode15s with BDF on; bundled AD1/AD2 analytic Jacobians, localized events and complete scalar real/Pentarnion trajectory sensitivities. With scientific opt-in: regular state/time mass, hybrid indexed priority resets/saltation and actual continuous reverse cost adjoints using retained adaptive dense output. Target adaptive and stopping policies differ.":"No ODE solver mapping enabled")
            <<",\n  \"scientific_mapping\":"<<quote(!options_.allowScientificAdapters?"No scientific solver mapping enabled":python()?"Explicit fitting: Ceres LM -> SciPy least_squares TRF; dogleg -> dogbox. NLopt lbfgs/nelder_mead/slsqp/cobyla/bobyqa/direct -> SciPy L-BFGS-B/Nelder-Mead/SLSQP/COBYLA/COBYQA/DIRECT. Scalar real unknown groups, target analytic AD or selected finite differences, robust losses/bounds/scales. Target iteration and stopping policies differ.":"Explicit fitting: Ceres LM -> MATLAB lsqnonlin LM without bounds, otherwise trust-region-reflective; dogleg -> trust-region-reflective. NLopt lbfgs -> fminunc quasi-newton or bounded fmincon interior-point; slsqp -> fmincon SQP; unbounded nelder_mead -> fminsearch. Bundled target AD or selected finite differences. Target iteration and stopping policies differ.")
            <<",\n  \"graph_mapping\":"<<quote(options_.allowScientificAdapters?"Checked real weighted directed/undirected multigraphs, complete edge IDs, loops, components, paths, cycle/forest/topological analysis, centralities, dense matrices, circular layouts and plotting. Canonical component labels; equal-cost path/tree choices may differ from Boost. MATLAB Dijkstra uses a bounded linear scan; Python uses a heap. Portable operation work allowance20million; dense matrices<=1million entries. No Studio worker scheduling.":"No graph mapping enabled")
            <<",\n  \"pde_mapping\":"<<quote(options_.allowScientificAdapters?"Explicit1D real/Pentarnion FD or P1 consistent-mass FEM; RK4 or IMEX Euler with constant scalar diffusion and periodic/constant Dirichlet/homogeneous Neumann boundaries. Target sparseLU factors and serial callback scheduling, complete coefficient fields. No adaptive2D/3D FEM, PETSc, native cache or native-operation quota mapping.":"No PDE mapping enabled")
            <<",\n  \"mechanics_mapping\":"<<quote(options_.allowScientificAdapters?"Real scalar coordinate groups: Newton RK4/position-force Verlet; Lagrangian RK4 with generated regular velocity-Hessian mass solve; Hamiltonian RK4/implicit midpoint with generated Hessian Newton matrix. Complete q/v/p, defined energy and retained-sample work diagnostics. Constraint/generalized-force/unit-aware and generic/fixed-array coordinate target mechanics remain unsupported.":"No mechanics mapping enabled")
            <<",\n  \"geometry_mapping\":\"Sampled curve estimates retain their discrete tangent/curvature formulas. Surf/imagesc/contour/isosurface map to target graphics; Python isosurface uses bounded marching tetrahedra, MATLAB its isosurface routine. Triangulations, contour interpolation, image shading and camera state can differ. No intrinsic geometry/atlas/chart/field differential-operator target adapter.\""
            <<",\n  \"limitations\":\"No native-cache/MEX reuse, shared Studio CPU scheduling, GPU/MPI mapping, GUI dock/tile mapping, captured named functions, generic-table/native-array-state exports, singular/general DAE or non-BDF advanced solver adapters, or arbitrary language import. Target hybrid max_bytes bounds retained numeric results; target solver dense storage/object overhead differs. Reverse gradients are continuous-adjoint approximations, not exact discrete adaptive-step derivatives. Unsupported AST nodes fail export. Target arithmetic is binary64, not Studio long-double accumulators.\"\n}\n";
        result_.compatibilityReport=report.str();
        if(result_.diagnostics.empty()){
            std::ostringstream map;map<<"{\"schema\":1,\"mappings\":[";for(std::size_t n=0;n<result_.sourceMap.size();++n){const auto& item=result_.sourceMap[n];if(n)map<<',';map<<"{\"file\":"<<quote(item.file)<<",\"line\":"<<item.line<<",\"source\":"<<quote(item.source)<<",\"offset\":"<<item.span.offset<<",\"length\":"<<item.span.length<<'}';}map<<"]}\n";
            result_.files.push_back({"source-map.json",map.str()});result_.files.push_back({"compatibility.json",result_.compatibilityReport});
        }
        return std::move(result_);
    }
};
}
bool Result::ok()const noexcept{return std::none_of(diagnostics.begin(),diagnostics.end(),[](const auto& d){return d.severity==language::Severity::Error;});}
Result exportProgram(const language::AnalysisResult& checked,Target target,const Options& options){
    if(!checked.ok()){Result result;result.diagnostics=checked.diagnostics;if(result.diagnostics.empty())result.diagnostics.push_back({language::Severity::Error,"X1600","Export requires a successful checked AST.",{}});return result;}
    return Emitter(target,options).run(checked);
}
}
