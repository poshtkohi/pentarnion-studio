#pragma once
#include "modules/vector_plot_api.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace penta::plotting {
struct VectorCommand {penta_vector_command_v1 data{};std::string text;};
std::vector<unsigned char> renderVector(const std::vector<VectorCommand>&,int width,int height,int dpi,
    bool pdf,const std::string& metadata,const std::filesystem::path& moduleDirectory,
    const std::shared_ptr<std::atomic_bool>& cancellation);
}
