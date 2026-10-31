#pragma once
#include "numeric_module_loader.hpp"
#include "modules/linalg_api.h"
#include <filesystem>
namespace penta::runtime {
class LinalgModule final {
public:
    explicit LinalgModule(std::filesystem::path directory={});
    [[nodiscard]] bool loaded()const;
    void compute(penta_linalg_request_v1 request,penta_linalg_result_v1& result,
        const ParallelSettings& settings,const std::shared_ptr<std::atomic_bool>& cancellation,
        ParallelStatistics& statistics)const;
    [[nodiscard]] const std::filesystem::path& path()const noexcept{return path_;}
private:
    const penta_linalg_api_v1& api()const;
    std::filesystem::path path_;
};
}
