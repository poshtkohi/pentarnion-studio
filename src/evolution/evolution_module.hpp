#pragma once
#include "modules/evolution_api.h"
#include "runtime/thread_budget.hpp"
#include <filesystem>
#include <stdexcept>
#include <vector>
namespace penta::evolution {
class Error final:public std::runtime_error{public:int status;Error(int s,const std::string& text):std::runtime_error(text),status(s){}};
struct Result{std::vector<double> solution,history,populationX,populationF,paretoX,paretoF;penta_evolution_result_v1 report{};runtime::ParallelStatistics statistics;};
class Module{public:explicit Module(std::filesystem::path directory={});[[nodiscard]] bool loaded()const;[[nodiscard]] Result solve(const penta_evolution_request_v1&,std::size_t threads=0,const std::shared_ptr<std::atomic_bool>& cancellation={},const std::function<void(std::size_t)>& charge={},const std::function<bool(const penta_iteration_snapshot_v1&)>& observer={})const;private:std::filesystem::path path_;const penta_evolution_api_v1& api()const;};
}

#include "evolution_session.hpp"
