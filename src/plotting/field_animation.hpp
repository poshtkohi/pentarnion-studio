#pragma once
#include "native_export.hpp"

namespace penta::plotting {
struct FieldAnimationData {
    int dimension=1;
    std::size_t nx=0,ny=1,components=1,coefficient=0;
    std::vector<double> x,y,t,values; // sample-major, y then x then coefficient.
    std::vector<std::size_t> selectedFrames; // zero-based indices in original t.
    bool surface=true,loop=false;
    double fps=15;
    double gamma=1;
    std::string algebraId="A_gamma_v1";
    runtime::AxisLimits scalarRange;
    std::array<runtime::AxisLimits,64> coefficientRanges;
    std::string context,algebraHash;
    std::vector<std::string> basisLabels;
};
void validateFieldAnimation(FieldAnimationData&); // derives one fixed scalar range.
NativePanel fieldAnimationFrame(const FieldAnimationData&,std::size_t selectedFrame,
                               std::optional<std::size_t> coefficient={});
struct FrameExportOptions {
    PngOptions image;
    std::string prefix="field";
    std::size_t maximumFrames=240;
    std::uint64_t maximumBytes=256*1024*1024;
    bool createDirectory=false;
};
struct FrameExportReport {
    std::size_t frames=0;
    std::uint64_t bytes=0;
    std::filesystem::path directory,manifest;
};
FrameExportReport exportFieldFrames(const FieldAnimationData&,const std::filesystem::path& absoluteDirectory,
                                  const FrameExportOptions& options={});
}
