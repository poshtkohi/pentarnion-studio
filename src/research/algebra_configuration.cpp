#include "script_builder.hpp"
#include <set>
namespace penta::research {
std::string prepareAlgebras(const Configuration& configuration,language::AnalysisOptions& options){
    if(configuration.algebras.size()>8)throw ScriptError("At most8 explicitly named native algebra presets are supported in the Explorer.");
    std::string source;std::set<std::string> names;
    for(const auto& entry:configuration.algebras){if(entry.name.empty()||entry.name.size()>64||entry.preset.empty()||entry.preset.size()>64||entry.name.starts_with("research_"))throw ScriptError("Enter bounded unique algebra names outside the research_ prefix.");for(const auto& state:configuration.states)if(state.name==entry.name)throw ScriptError("An algebra and state cannot share a name.");for(const auto& parameter:configuration.parameters)if(parameter.name==entry.name)throw ScriptError("An algebra and parameter cannot share a name.");if(!names.insert(entry.name).second)throw ScriptError("Explorer algebra names must be unique.");source+="algebra "+entry.name+"=builtin_algebra("+quote(entry.preset)+");\n";}
    if(source.empty())return source;
    const auto checked=language::analyse(source,options);if(!checked.ok()||checked.program.statements.size()!=configuration.algebras.size()||checked.symbols.size()!=configuration.algebras.size())throw ScriptError("Native algebra controls require valid names and installed builtin presets.");for(const auto& symbol:checked.symbols){if(symbol.type!=language::Type::Algebra||!symbol.schema||!symbol.schema->algebra)throw ScriptError("Native algebra controls accept data definitions only.");options.initialSymbols.push_back(symbol);}return source;
}
language::OdeVariable numericControlType(const std::string& text,const language::AnalysisOptions& options){
    if(text.empty()||text.size()>128)throw ScriptError("Enter a bounded numeric type, with an explicit shape for arrays.");
    const auto checked=language::analyse("function void research_type_check("+text+" research_value){return;}",options);if(!checked.ok()||checked.program.statements.size()!=1||!checked.program.statements[0].function||checked.program.statements[0].function->parameters.size()!=1)throw ScriptError("Enter a checked scalar algebra or fixed-shape numerical array type.");const auto& function=*checked.program.statements[0].function;auto result=function.parameters[0];if(!function.parameterSchemas.empty())result.schema=function.parameterSchemas[0];if(!language::coordinateWidth(result.type,result.schema))throw ScriptError("This type has no fixed numerical coordinate layout.");return result;
}
}
