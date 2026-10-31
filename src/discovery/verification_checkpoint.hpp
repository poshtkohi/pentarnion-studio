#pragma once
#include "verification.hpp"
#include "runtime/data_io.hpp"
#include "ode/native_internal.hpp"
#include <nlohmann/json.hpp>
#include <cmath>

namespace penta::discovery::verification::detail {
using Json=nlohmann::json;
inline Json rowData(const Row& r){
    return {{"run",r.runId},{"tier",r.tierId},{"status",r.status},{"screen",r.screenStatus},{"label",r.label},{"message",r.message},{"diagnostic",r.diagnosticStatus},
        {"gamma",r.gamma},{"maximum_norm",r.maximumNorm},{"return_error",r.returnError},{"period",r.period},{"rhs_norm",r.rhsNorm},{"stop_time",r.stopTime},
        {"largest",r.largest},{"window_drift",r.windowDrift},{"setting_drift",r.settingDrift},{"tier_drift",r.tierDrift},{"orthogonality",r.orthogonality},{"closure",r.closureResidual},{"neutral",r.neutralError},
        {"lyapunov_available",r.lyapunovAvailable},{"window_converged",r.windowConverged},{"settings_converged",r.settingsConverged},{"tier_converged",r.tierConverged},{"positive",r.positive},{"periodic_converged",r.periodicConverged},{"stable",r.stable},{"unstable",r.unstable},
        {"near_zero_exponents",r.nearZeroExponents},{"failure_reason",r.failureReason},{"quasiperiodic_candidate",r.quasiperiodicCandidate},{"state",r.state},{"exponents",r.exponents},{"reference_exponents",r.referenceExponents},{"floquet_moduli",r.floquetModuli},{"steps",r.steps},{"evaluations",r.evaluations},{"qr_intervals",r.qrIntervals}};
}
inline Row restoreRow(const Json& j,const ode::SolveOptions& settings,std::size_t run,std::size_t tier,std::size_t n,std::size_t p){
    Row r;r.nearZeroExponents=j.value("near_zero_exponents",std::size_t(0));r.failureReason=j.value("failure_reason",std::uint32_t(0));r.quasiperiodicCandidate=j.value("quasiperiodic_candidate",false);if(r.nearZeroExponents>p||r.failureReason>10)throw std::runtime_error("Invalid verification checkpoint classifier metadata.");r.settings=settings;r.runId=j.at("run");r.tierId=j.at("tier");r.status=j.at("status");r.screenStatus=j.at("screen");r.label=j.at("label");r.message=j.at("message");r.diagnosticStatus=j.at("diagnostic");
    if(r.runId!=run||r.tierId!=tier||r.status>11||r.screenStatus>9||r.label.size()>128||r.message.size()>65536||r.diagnosticStatus.size()>128)throw std::runtime_error("Invalid verification checkpoint row identity/status.");
    auto number=[&](const char* key,double& value){value=j.at(key).get<double>();if(!std::isfinite(value))throw std::runtime_error("Verification checkpoint contains a nonfinite diagnostic.");};
    number("gamma",r.gamma);number("maximum_norm",r.maximumNorm);number("return_error",r.returnError);number("period",r.period);number("rhs_norm",r.rhsNorm);number("stop_time",r.stopTime);number("largest",r.largest);number("window_drift",r.windowDrift);number("setting_drift",r.settingDrift);number("tier_drift",r.tierDrift);number("orthogonality",r.orthogonality);number("closure",r.closureResidual);number("neutral",r.neutralError);
    r.lyapunovAvailable=j.at("lyapunov_available");r.windowConverged=j.at("window_converged");r.settingsConverged=j.at("settings_converged");r.tierConverged=j.at("tier_converged");r.positive=j.at("positive");r.periodicConverged=j.at("periodic_converged");r.stable=j.at("stable");r.unstable=j.at("unstable");r.steps=j.at("steps");r.evaluations=j.at("evaluations");r.qrIntervals=j.at("qr_intervals");
    auto vector=[&](const char* key,std::vector<double>& values,std::size_t width){values=j.at(key).get<std::vector<double>>();if(values.size()!=width)throw std::runtime_error("Verification checkpoint coefficient dimensions changed.");for(const auto value:values)if(!std::isfinite(value))throw std::runtime_error("Verification checkpoint contains nonfinite coefficient data.");};
    vector("state",r.state,n);vector("exponents",r.exponents,p);vector("reference_exponents",r.referenceExponents,p);vector("floquet_moduli",r.floquetModuli,p);return r;
}
inline std::size_t restoreTiers(Result& result,const Options& options,const discovery::Options& input,std::size_t n,std::size_t p){
    if(options.resume&&(options.checkpoint.empty()||!std::filesystem::exists(options.checkpoint)))throw std::runtime_error("Verification resume requires an existing checkpoint.");
    if(!options.resume){if(!options.checkpoint.empty()&&std::filesystem::exists(options.checkpoint)&&!options.overwriteCheckpoint)throw std::runtime_error("Verification checkpoint already exists; select resume or explicit overwrite_checkpoint.");return 0;}
    if(std::filesystem::file_size(options.checkpoint)>32*1024*1024)throw std::runtime_error("Verification checkpoint exceeds32MiB.");
    const auto bytes=runtime::dataio::readBytes(options.checkpoint);if(bytes.size()>32*1024*1024)throw std::runtime_error("Verification checkpoint exceeds32MiB.");const auto j=Json::parse(bytes);const auto& payload=j.at("rows");
    if(j.at("format")!="PentarnionVerificationTiers"||j.at("version")!=1||j.at("fingerprint")!=result.fingerprint||j.at("payload_hash")!=ode::detail::sha256(payload.dump()))throw std::runtime_error("Verification checkpoint identity/integrity changed; previous evidence was retained.");
    const auto tiers=j.at("completed_tiers").get<std::size_t>();if(tiers>options.horizons.size()||!payload.is_array()||payload.size()!=tiers*input.runs)throw std::runtime_error("Verification checkpoint has a partial or invalid tier layout.");
    std::vector<Row> rows;rows.reserve(payload.size());for(std::size_t tier=0;tier<tiers;++tier){auto settings=input.solver;settings.t1=settings.t0+options.horizons[tier];settings.rtol*=std::pow(options.tightening,static_cast<double>(tier));settings.atol*=std::pow(options.tightening,static_cast<double>(tier));for(std::size_t run=0;run<input.runs;++run)rows.push_back(restoreRow(payload[tier*input.runs+run],settings,options.runIds[run],tier+1,n,p));}
    result.rows=std::move(rows);result.completed=result.rows.size();result.restoredTiers=tiers;return tiers;
}
inline void saveTiers(Result& result,const Options& options,std::size_t tiers){
    if(options.checkpoint.empty())return;
    try{Json rows=Json::array();for(const auto& row:result.rows)rows.push_back(rowData(row));const Json document{{"format","PentarnionVerificationTiers"},{"version",1},{"fingerprint",result.fingerprint},{"completed_tiers",tiers},{"payload_hash",ode::detail::sha256(rows.dump())},{"rows",std::move(rows)},{"restart_semantics","completed tiers reused; interrupted tier restarted"}};const auto bytes=document.dump();if(bytes.size()>32*1024*1024)throw std::runtime_error("Verification checkpoint exceeds32MiB.");if(!options.checkpoint.parent_path().empty())std::filesystem::create_directories(options.checkpoint.parent_path());runtime::dataio::writeBytesAtomic(options.checkpoint,bytes);}
    catch(const std::exception& error){result.checkpointOkay=false;result.checkpointMessage=error.what();}
}
}
