#pragma once
#include <mutex>
namespace penta::fem::detail {
// One configuration/reentry domain for all MFEM capabilities in this DLL.
inline std::mutex mfemMutex;
inline thread_local bool activeMfemSolve=false;
}
