#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace penta::runtime {
struct ModuleInfo {std::string name;std::filesystem::path path;bool available=false,loaded=false;std::uint32_t abi=0;std::string status;};
// Inspection never loads libraries. Loading is limited to the built-in catalogue
// and the host-controlled module directory, and is process-resident like solvers.
std::vector<ModuleInfo> moduleInventory(const std::filesystem::path& directory={});
ModuleInfo loadModule(const std::string& name,const std::filesystem::path& directory={});
}
