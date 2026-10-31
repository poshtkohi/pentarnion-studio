#pragma once
#include "evolution_module.hpp"
#include "modules/evolution_session_api.h"
namespace penta::evolution {
struct SessionOptions {
    std::size_t islands=1,migrationInterval=5,migrants=1,maximumBytes=256*1024*1024;
    std::vector<std::uint8_t> integerCoordinates;
    std::size_t equalityCount=0,inequalityCount=0;
    void* equalityContext=nullptr;penta_evolution_evaluate_v1 equality=nullptr;
    void* inequalityContext=nullptr;penta_evolution_evaluate_v1 inequality=nullptr;
    double constraintTolerance=1e-8;
    std::filesystem::path checkpoint;
    bool resume=false,overwriteCheckpoint=false;
    std::string modelIdentity;
};
struct SessionResult {
    Result evolution;
    std::vector<double> constraints;
    std::size_t migrations=0,restoredGeneration=0,islands=1;
    double constraintViolation=0;
    bool feasible=false,checkpointOkay=true;
    std::string fingerprint,checkpointMessage;
};
class SessionModule {
public:
    explicit SessionModule(std::filesystem::path directory={});
    SessionResult run(const penta_evolution_request_v1&,const SessionOptions&,std::size_t threads=0,
        const std::shared_ptr<std::atomic_bool>& cancellation={},
        const std::function<void(std::size_t)>& charge={},
        const std::function<bool(const penta_iteration_snapshot_v1&)>& observer={})const;
private:std::filesystem::path path_;
};
}
