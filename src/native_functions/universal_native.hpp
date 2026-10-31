#pragma once
#include "language/frontend.hpp"
#include "ode/native_ode.hpp"

namespace penta::native_functions {
using AlgebraDefinitionPtr=algebra::universal::DefinitionPtr;
inline AlgebraDefinitionPtr elementDefinition(language::Type type,const std::shared_ptr<const language::ValueSchema>& schema={}){
    if(type==language::Type::Complex)return algebra::universal::builtin("complex");
    if(type==language::Type::Element||type==language::Type::ElementMatrix){
        if(!schema||!schema->algebra)throw ode::OdeError("N2401","A native algebra element requires an immutable algebra definition in its checked type.");
        return schema->algebra;
    }
    return {};
}
inline std::size_t nativeCoordinateWidth(language::Type type,const std::shared_ptr<const language::ValueSchema>& schema={}){
    if(type==language::Type::RealMatrix||type==language::Type::PentarnionMatrix||type==language::Type::ElementMatrix||type==language::Type::Tensor){const auto width=language::coordinateWidth(type,schema);if(width)return width;throw ode::OdeError("N2401","Native array coordinates require an explicit checked fixed shape.");}
    if(type==language::Type::Real||type==language::Type::Integer)return 1;
    if(type==language::Type::Pentarnion)return 5;
    if(auto definition=elementDefinition(type,schema))return definition->dimension();
    throw ode::OdeError("N2401","Native coordinates require real, complex, Pentarnion or typed algebra elements.");
}
inline std::size_t coordinateWidth(const language::OdeVariable& variable){return nativeCoordinateWidth(variable.type,variable.schema);}
inline std::vector<std::shared_ptr<const language::ValueSchema>> callbackResultSchemas(const language::FunctionDefinition& function){
    if(function.result!=language::Type::Tuple)return {function.resultSchema};
    std::vector<std::shared_ptr<const language::ValueSchema>> schemas;
    for(std::size_t n=0;n<function.results.size();++n)schemas.push_back(function.resultSchema&&n<function.resultSchema->elements.size()?function.resultSchema->elements[n].schema:nullptr);
    return schemas;
}
inline std::vector<std::size_t> callbackInputWidths(const language::FunctionDefinition& function){
    std::vector<std::size_t> widths;for(std::size_t n=0;n<function.parameters.size();++n)widths.push_back(nativeCoordinateWidth(function.parameters[n].type,n<function.parameterSchemas.size()?function.parameterSchemas[n]:function.parameters[n].schema));return widths;
}
inline std::vector<std::size_t> callbackOutputWidths(const language::FunctionDefinition& function){
    const auto types=function.result==language::Type::Tuple?function.results:std::vector<language::Type>{function.result};
    const auto schemas=callbackResultSchemas(function);std::vector<std::size_t> widths;
    for(std::size_t n=0;n<types.size();++n)widths.push_back(nativeCoordinateWidth(types[n],n<schemas.size()?schemas[n]:nullptr));
    return widths;
}
inline bool usesUniversalCoordinates(const language::FunctionDefinition& function){
    const auto generic=[](language::Type type){return type==language::Type::Element||type==language::Type::Complex||type==language::Type::RealMatrix||type==language::Type::PentarnionMatrix||type==language::Type::ElementMatrix||type==language::Type::Tensor;};
    if(generic(function.result))return true;
    for(const auto type:function.results)if(generic(type))return true;
    for(const auto& parameter:function.parameters)if(generic(parameter.type))return true;
    std::function<bool(const language::ExprPtr&)> expression=[&](const auto& e){if(!e)return false;if(generic(e->type))return true;for(const auto& child:e->children)if(expression(child))return true;return false;};
    std::function<bool(const std::vector<language::Statement>&)> statements=[&](const auto& body){for(const auto& statement:body)if(generic(statement.declaredType)||expression(statement.expression)||statements(statement.body)||statements(statement.alternative))return true;return false;};
    return statements(function.body);
}
inline bool usesUniversalCoordinates(const language::OdeDefinition& model){
    const auto generic=[](language::Type type){return type==language::Type::Element||type==language::Type::Complex||type==language::Type::RealMatrix||type==language::Type::PentarnionMatrix||type==language::Type::ElementMatrix||type==language::Type::Tensor;};
    for(const auto& variable:model.states)if(generic(variable.type))return true;
    for(const auto& variable:model.parameters)if(generic(variable.type))return true;
    std::function<bool(const language::ExprPtr&)> expression=[&](const auto& e){if(!e)return false;if(generic(e->type))return true;for(const auto& child:e->children)if(expression(child))return true;return false;};
    for(const auto& derivative:model.derivatives)if(expression(derivative))return true;
    return false;
}
}
