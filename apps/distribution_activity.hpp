#pragma once
#if defined(PENTA_DISTRIBUTION_BUILD) && defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace penta {
// The real binaries also advertise activity, so bypassing the convenient root
// launcher cannot let an update interrupt a running calculation.
class DistributionActivity final {
#if defined(PENTA_DISTRIBUTION_BUILD) && defined(_WIN32)
    HANDLE handle_=CreateMutexW(nullptr,FALSE,L"PentarnionStudioManagedInstaller-v1");
#endif
public:
    DistributionActivity()=default;
    DistributionActivity(const DistributionActivity&)=delete;
    DistributionActivity& operator=(const DistributionActivity&)=delete;
    ~DistributionActivity(){
#if defined(PENTA_DISTRIBUTION_BUILD) && defined(_WIN32)
        if(handle_)CloseHandle(handle_);
#endif
    }
    bool valid()const{
#if defined(PENTA_DISTRIBUTION_BUILD) && defined(_WIN32)
        return handle_!=nullptr;
#else
        return true;
#endif
    }
};
}
