#include "plot_template.hpp"
#include "runtime/data_io.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace penta::plotting {
namespace {
using Json=nlohmann::json;
void exactKeys(const Json& object,const std::set<std::string>& keys){if(!object.is_object()||object.size()!=keys.size())throw std::invalid_argument("Plot template fields differ from version1.");for(const auto& [key,value]:object.items()){(void)value;if(!keys.contains(key))throw std::invalid_argument("Unknown plot template field: "+key);}}
Json limit(const std::optional<runtime::AxisLimits>& bounds){return bounds?Json::array({bounds->lower,bounds->upper}):Json(nullptr);}
std::optional<runtime::AxisLimits> bounds(const Json& value){if(value.is_null())return {};if(!value.is_array()||value.size()!=2||!value[0].is_number()||!value[1].is_number())throw std::invalid_argument("Plot template bounds require two real numbers.");return runtime::AxisLimits{value[0].get<double>(),value[1].get<double>()};}
}
void validatePlotTemplate(const PlotTemplate& value){runtime::validatePlotStyle(value.style);std::size_t bytes=0;for(const auto& label:value.labels){bytes+=label.size();if(label.size()>4096||bytes>20480||label.find('\0')!=std::string::npos)throw std::invalid_argument("Plot template labels exceed their bounded UTF-8 text allowance.");}}
PlotTemplate readPlotTemplate(const std::filesystem::path& path){
    runtime::dataio::Options options;options.maxFileBytes=262144;const auto bytes=runtime::dataio::readBytes(path,options);const auto root=Json::parse(bytes,[](int depth,Json::parse_event_t,Json&){if(depth>16)throw std::invalid_argument("Plot template nesting exceeds16 levels.");return true;});
    exactKeys(root,{"format","version","style","labels","latex"});if(root.at("format")!="PentarnionPlotTemplate"||root.at("version")!=1)throw std::invalid_argument("Unsupported plot template format/version.");const auto& style=root.at("style");exactKeys(style,{"hold","legend_visible","legend_box","equal","line_width","font_size","line_style","font_family","legend_location","colors","legend_labels","limits","color_limits"});
    PlotTemplate result;auto& s=result.style;s.hold=style.at("hold").get<bool>();s.legendVisible=style.at("legend_visible").get<bool>();s.legendBox=style.at("legend_box").get<bool>();s.equalUnits=style.at("equal").get<bool>();s.lineWidth=style.at("line_width").get<double>();s.fontSize=style.at("font_size").get<double>();s.lineStyle=style.at("line_style").get<std::string>();s.fontFamily=style.at("font_family").get<std::string>();s.legendLocation=style.at("legend_location").get<std::string>();
    const auto& colors=style.at("colors"),labels=style.at("legend_labels");if(!colors.is_array()||colors.size()>256||!labels.is_array()||labels.size()>256)throw std::invalid_argument("Plot template palette/legend budget exceeded.");for(const auto& color:colors){if(!color.is_array()||color.size()!=3)throw std::invalid_argument("Plot template RGB triples require3 coordinates.");s.colors.push_back(color.get<std::array<double,3>>());}for(const auto& label:labels)s.legendLabels.push_back(label.get<std::string>());
    const auto& limits=style.at("limits");if(!limits.is_array()||limits.size()!=3)throw std::invalid_argument("Plot template requires3 optional axis limits.");for(std::size_t i=0;i<3;++i)s.limits[i]=bounds(limits[i]);s.colorLimits=bounds(style.at("color_limits"));
    if(!root.at("labels").is_array()||root.at("labels").size()!=5||!root.at("latex").is_array()||root.at("latex").size()!=5)throw std::invalid_argument("Plot template requires5 text labels and rendering flags.");
    result.labels=root.at("labels").get<std::array<std::string,5>>();result.latex=root.at("latex").get<std::array<bool,5>>();validatePlotTemplate(result);return result;
}
void writePlotTemplate(const PlotTemplate& value,const std::filesystem::path& path,bool overwrite){
    validatePlotTemplate(value);if(!path.is_absolute()||!std::filesystem::is_directory(path.parent_path())||(!overwrite&&std::filesystem::exists(path)))throw std::invalid_argument("Use an absolute plot template destination with an existing parent; overwrite must be explicit.");const auto& s=value.style;Json limits=Json::array();for(const auto& l:s.limits)limits.push_back(limit(l));Json data={{"format","PentarnionPlotTemplate"},{"version",1},{"style",{{"hold",s.hold},{"legend_visible",s.legendVisible},{"legend_box",s.legendBox},{"equal",s.equalUnits},{"line_width",s.lineWidth},{"font_size",s.fontSize},{"line_style",s.lineStyle},{"font_family",s.fontFamily},{"legend_location",s.legendLocation},{"colors",s.colors},{"legend_labels",s.legendLabels},{"limits",limits},{"color_limits",limit(s.colorLimits)}}},{"labels",value.labels},{"latex",value.latex}};const auto bytes=data.dump(2);if(bytes.size()>262144)throw std::invalid_argument("Plot template exceeds256KiB.");
    static std::atomic_uint64_t serial{0};auto temporary=path;temporary+=".penta-template-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial.fetch_add(1))+".tmp";
    try{std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));stream.close();if(!stream)throw std::runtime_error("Cannot write complete plot template.");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0)))throw std::runtime_error("Cannot atomically commit plot template; an existing destination was preserved.");
#else
        if(overwrite)std::filesystem::rename(temporary,path);else{std::filesystem::create_hard_link(temporary,path);std::filesystem::remove(temporary);}
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}
}
