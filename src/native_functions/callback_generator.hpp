#pragma once
#include "ode/native_internal.hpp"
namespace penta::native_functions::detail {
// Internal mode of the existing checked callback emitter; no second DSL/parser.
ode::detail::Generated generateThirdCallback(const language::FunctionDefinition&);
}
