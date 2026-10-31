#pragma once
#include "field_animation.hpp"

namespace penta::plotting {
struct VideoExportOptions {
    PngOptions image;
    double fps=0,maximumSeconds=60; // fps=0 uses the validated field display rate.
    std::size_t maximumFrames=240;
    std::uint64_t maximumBytes=256*1024*1024;
};
struct VideoExportReport {
    std::size_t frames=0;
    std::uint64_t bytes=0;
    double fps=0,duration=0,sourceDuration=0;
    std::filesystem::path path;
};
// Native single-stream AVI / BI_RGB24, no compression or external codec.
// One rendered RGB frame and one padded BGR scanline are streamed at a time.
VideoExportReport exportFieldVideo(const FieldAnimationData&,const std::filesystem::path& absolutePath,
                                  const VideoExportOptions& options={});
}
