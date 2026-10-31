#pragma once
#include "runtime/plot_style.hpp"
#include <filesystem>
namespace penta::plotting {
struct PlotTemplate {
    runtime::PlotStyle style;
    std::array<std::string,5> labels{}; // title,x,y,z,caption
    std::array<bool,5> latex{{true,true,true,true,true}};
};
void validatePlotTemplate(const PlotTemplate&);
PlotTemplate readPlotTemplate(const std::filesystem::path&);
void writePlotTemplate(const PlotTemplate&,const std::filesystem::path&,bool overwrite=false);
}
