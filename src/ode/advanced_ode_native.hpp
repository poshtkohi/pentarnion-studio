#pragma once
#include "native_ode.hpp"
namespace penta::ode {
inline language::FunctionDefinition rhsFunction(const language::OdeDefinition& source) {
    language::FunctionDefinition definition;definition.name="rhs_"+source.name;definition.native=true;definition.span=source.span;
    definition.origin=source.origin;
    definition.parameters=source.states;definition.parameters.insert(definition.parameters.end(),source.parameters.begin(),source.parameters.end());
    definition.parameters.push_back({source.timeName,language::Type::Real,source.span});
    for(const auto& parameter:definition.parameters)definition.parameterSchemas.push_back(parameter.schema);
    language::Statement returned;returned.kind=language::StatementKind::Return;returned.span=source.span;
    if(source.states.size()==1){definition.result=source.states[0].type;definition.resultSchema=source.states[0].schema;returned.expression=source.derivatives[0];}
    else{definition.result=language::Type::Tuple;for(const auto& state:source.states)definition.results.push_back(state.type);
        auto schema=std::make_shared<language::ValueSchema>();for(const auto& state:source.states)schema->elements.push_back({{},state.type,state.schema});definition.resultSchema=schema;
        auto tuple=std::make_shared<language::Expr>();tuple->kind=language::ExprKind::TupleLiteral;tuple->type=language::Type::Tuple;tuple->span=source.span;tuple->schema=schema;tuple->children=source.derivatives;returned.expression=std::move(tuple);}
    definition.body={std::move(returned)};return definition;
}
}
