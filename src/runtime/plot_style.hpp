#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace penta::runtime {
struct AxisLimits {double lower=0,upper=1;bool operator==(const AxisLimits&)const=default;};
struct PlotStyle {
    bool hold=false,legendVisible=true,legendBox=true,equalUnits=false;
    double lineWidth=2.2,fontSize=12;
    std::string lineStyle="solid",fontFamily="sans",legendLocation="auto";
    std::vector<std::array<double,3>> colors;
    std::vector<std::string> legendLabels;
    std::array<std::optional<AxisLimits>,3> limits;
    std::optional<AxisLimits> colorLimits;
    bool operator==(const PlotStyle&)const=default;
};
// A property patch changes only explicitly supplied values. In limits, a set
// flag with an empty value restores automatic bounds for that coordinate.
struct PlotStylePatch {
    std::optional<bool> hold,legendVisible,legendBox,equalUnits;
    std::optional<double> lineWidth,fontSize;
    std::optional<std::string> lineStyle,fontFamily,legendLocation;
    std::optional<std::vector<std::array<double,3>>> colors;
    std::optional<std::vector<std::string>> legendLabels;
    std::array<bool,3> limitsSet{};
    std::array<std::optional<AxisLimits>,3> limits;
    bool colorLimitsSet=false;
    std::optional<AxisLimits> colorLimits;
};
inline void validatePlotStyle(const PlotStyle& s){
    if(!std::isfinite(s.lineWidth)||s.lineWidth<.1||s.lineWidth>20||!std::isfinite(s.fontSize)||s.fontSize<6||s.fontSize>72)throw std::invalid_argument("Plot line width must be 0.1..20 and font size 6..72.");
    if(s.lineStyle!="solid"&&s.lineStyle!="dash"&&s.lineStyle!="dot"&&s.lineStyle!="dashdot")throw std::invalid_argument("Line style must be solid, dash, dot or dashdot.");
    if(s.fontFamily!="sans"&&s.fontFamily!="serif"&&s.fontFamily!="mono")throw std::invalid_argument("Font family must be sans, serif or mono.");
    if(s.legendLocation!="auto"&&s.legendLocation!="northeast"&&s.legendLocation!="northwest"&&s.legendLocation!="southeast"&&s.legendLocation!="southwest"&&s.legendLocation!="outside")throw std::invalid_argument("Unsupported legend location.");
    if(s.colors.size()>256||s.legendLabels.size()>256)throw std::invalid_argument("Plot palettes and legend labels are limited to 256 entries.");
    for(const auto& c:s.colors)for(double value:c)if(!std::isfinite(value)||value<0||value>1)throw std::invalid_argument("RGB palette entries must be finite values from 0 to 1.");
    std::size_t textBytes=0;for(const auto& label:s.legendLabels){textBytes+=label.size();if(label.size()>4096||textBytes>65536)throw std::invalid_argument("Legend labels exceed their bounded text budget.");}
    for(const auto& limit:s.limits)if(limit&&(!std::isfinite(limit->lower)||!std::isfinite(limit->upper)||limit->lower>=limit->upper))throw std::invalid_argument("Axis limits require two finite increasing values.");
    if(s.colorLimits&&(!std::isfinite(s.colorLimits->lower)||!std::isfinite(s.colorLimits->upper)||s.colorLimits->lower>=s.colorLimits->upper))throw std::invalid_argument("Color limits require finite increasing values.");
}
inline PlotStyle patchedPlotStyle(PlotStyle s,const PlotStylePatch& p){
    if(p.hold)s.hold=*p.hold;
    if(p.legendVisible)s.legendVisible=*p.legendVisible;
    if(p.legendBox)s.legendBox=*p.legendBox;
    if(p.equalUnits)s.equalUnits=*p.equalUnits;
    if(p.lineWidth)s.lineWidth=*p.lineWidth;
    if(p.fontSize)s.fontSize=*p.fontSize;
    if(p.lineStyle)s.lineStyle=*p.lineStyle;
    if(p.fontFamily)s.fontFamily=*p.fontFamily;
    if(p.legendLocation)s.legendLocation=*p.legendLocation;
    if(p.colors)s.colors=*p.colors;
    if(p.legendLabels)s.legendLabels=*p.legendLabels;
    for(std::size_t k=0;k<3;++k)if(p.limitsSet[k])s.limits[k]=p.limits[k];
    if(p.colorLimitsSet)s.colorLimits=p.colorLimits;
    validatePlotStyle(s);return s;
}
inline std::array<double,3> plotColor(const PlotStyle& s,std::size_t index){
    static constexpr std::array<std::array<double,3>,8> defaults{{{.12,.36,.78},{.88,.30,.08},{.10,.58,.28},{.53,.22,.68},{.03,.57,.66},{.73,.14,.40},{.60,.43,.06},{.30,.34,.39}}};
    return s.colors.empty()?defaults[index%defaults.size()]:s.colors[index%s.colors.size()];
}
inline std::string plotLegendLabel(const PlotStyle& s,std::size_t index,const std::string& fallback){return index<s.legendLabels.size()?s.legendLabels[index]:fallback;}
struct FigureExportOptions {
    double widthCm=0,heightCm=0;int dpi=150;
    bool overwrite=false;int subplot=0;
};
}
