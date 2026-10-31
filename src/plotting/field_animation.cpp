#include "field_animation.hpp"
#include "runtime/data_io.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace penta::plotting {
namespace {
void increasing(const std::vector<double>& values){if(values.empty())throw ExportError("Field axes/times must not be empty.");for(std::size_t k=0;k<values.size();++k)if(!std::isfinite(values[k])||(k&&values[k]<=values[k-1]))throw ExportError("Field axes and sample times must be finite and strictly increasing.");}
std::string scalar(double x){std::ostringstream out;out<<std::setprecision(10)<<x;return out.str();}
}
void validateFieldAnimation(FieldAnimationData& data){
    if(!std::isfinite(data.gamma)||data.gamma<0||data.gamma>1||(data.algebraId.empty()||data.algebraId.size()>512)||(data.dimension!=1&&data.dimension!=2)||data.nx<2||data.nx>4096||data.ny<1||data.ny>256||data.nx>16384/data.ny||data.x.size()!=data.nx||data.t.empty()||data.t.size()>100000||data.selectedFrames.empty()||data.selectedFrames.size()>240||(data.components<1||data.components>64)||data.coefficient>=data.components||!std::isfinite(data.fps)||data.fps<1||data.fps>60)throw ExportError("Field animation dimensions, coefficient, frame count or FPS exceed their bounded ranges.");
    if((data.dimension==1&&data.ny!=1)||(data.dimension==2&&data.y.size()!=data.ny))throw ExportError("Field grid dimensions do not match their coordinates.");
    const auto nodes=data.nx*data.ny;if(data.t.size()>8000000/(nodes*data.components)||data.values.size()!=data.t.size()*nodes*data.components)throw ExportError("Field animation must retain exactly the original 1..8000000 real coordinates.");
    if(!data.algebraHash.empty()&&(data.algebraHash.size()!=64||data.algebraHash.find_first_not_of("0123456789abcdef")!=std::string::npos))throw ExportError("Field definition hashes require64 lowercase hexadecimal digits.");
    if(data.basisLabels.empty()){if(data.components==5&&data.algebraId=="A_gamma_v1")data.basisLabels={"1","i","j","k","ell"};else for(std::size_t i=0;i<data.components;++i)data.basisLabels.push_back("coordinate"+std::to_string(i));}if(data.basisLabels.size()!=data.components)throw ExportError("Field basis labels must retain the complete coefficient layout.");for(const auto& label:data.basisLabels)if(label.empty()||label.size()>256)throw ExportError("Field basis labels exceed their text allowance.");
    increasing(data.x);increasing(data.t);if(data.dimension==2)increasing(data.y);
    for(const double value:data.values)if(!std::isfinite(value))throw ExportError("Field animation preserves only finite original samples.");
    std::array<double,64> low,high;low.fill(INFINITY);high.fill(-INFINITY);std::size_t previous=0;
    for(std::size_t k=0;k<data.selectedFrames.size();++k){const auto index=data.selectedFrames[k];if(index>=data.t.size()||(k&&index<=previous))throw ExportError("Selected field frames must be unique increasing original indices.");previous=index;for(std::size_t node=0;node<nodes;++node)for(std::size_t c=0;c<data.components;++c){const auto value=data.values[(index*nodes+node)*data.components+c];low[c]=std::min(low[c],value);high[c]=std::max(high[c],value);}}
    for(std::size_t c=0;c<data.components;++c){if(low[c]==high[c]){const double original=low[c],pad=std::max(1.,std::abs(original)*.05);low[c]=std::isfinite(original-pad)?original-pad:original;high[c]=std::isfinite(original+pad)?original+pad:original;if(!(low[c]<high[c])){low[c]=std::nextafter(original,-INFINITY);high[c]=original;if(!std::isfinite(low[c])){low[c]=original;high[c]=std::nextafter(original,INFINITY);}}}if(!std::isfinite(low[c])||!std::isfinite(high[c])||!(low[c]<high[c]))throw ExportError("Cannot represent a finite display range for an extreme field.");data.coefficientRanges[c]={low[c],high[c]};}
    data.scalarRange=data.coefficientRanges[data.coefficient];
}
NativePanel fieldAnimationFrame(const FieldAnimationData& data,std::size_t frame,std::optional<std::size_t> selected){
    const auto component=selected.value_or(data.coefficient);if((data.components<1||data.components>64)||component>=data.components||data.x.size()!=data.nx||(data.dimension==2&&data.y.size()!=data.ny)||data.nx<2||data.ny<1||data.nx>16384/data.ny||data.t.size()>8000000/(data.nx*data.ny*data.components)||data.values.size()!=data.t.size()*data.nx*data.ny*data.components)throw ExportError("Animation data is invalid or was not validated.");
    const auto scalarRange=data.coefficientRanges[component];
    if(frame>=data.selectedFrames.size())throw ExportError("Animation frame is outside the retained selection.");
    const auto sample=data.selectedFrames[frame],nodes=data.nx*data.ny;NativePanel plot;plot.basisLabels=data.basisLabels;plot.algebraId=data.algebraHash.empty()?data.algebraId:data.algebraHash;plot.gamma=data.gamma;plot.dimensions=data.dimension==1||!data.surface?2:3;plot.style.legendVisible=false;plot.style.colorLimits=scalarRange;plot.title="Field coefficient "+std::to_string(component)+" | t="+scalar(data.t.at(sample));plot.xLabel="x";plot.yLabel=data.dimension==1?"field":"y";plot.zLabel="field";plot.context=data.algebraId+" | gamma="+scalar(data.gamma)+(data.context.empty()?std::string():" | "+data.context)+" | original sample "+std::to_string(sample+1);plot.style.limits[0]=runtime::AxisLimits{data.x.front(),data.x.back()};
    if(data.dimension==1){plot.style.limits[1]=scalarRange;runtime::PlotCurve curve;curve.label="coefficient "+std::to_string(component);curve.points.reserve(data.nx);for(std::size_t i=0;i<data.nx;++i)curve.points.push_back({data.x[i],data.values[(sample*nodes+i)*data.components+component],0});plot.curves.push_back(std::move(curve));return plot;}
    plot.style.limits[1]=runtime::AxisLimits{data.y.front(),data.y.back()};if(data.surface)plot.style.limits[2]=scalarRange;
    runtime::PlotMesh mesh;mesh.label="field coefficient "+std::to_string(component);mesh.points.reserve(nodes);mesh.scalars.reserve(nodes);
    for(std::size_t j=0;j<data.ny;++j)for(std::size_t i=0;i<data.nx;++i){const double value=data.values[(sample*nodes+j*data.nx+i)*data.components+component];mesh.points.push_back({data.x[i],data.y[j],data.surface?value:0});mesh.scalars.push_back(value);}
    for(std::size_t j=0;j+1<data.ny;++j)for(std::size_t i=0;i+1<data.nx;++i){const auto at=static_cast<std::uint32_t>(j*data.nx+i),nx=static_cast<std::uint32_t>(data.nx);mesh.triangles.push_back({at,at+1,at+nx+1});mesh.triangles.push_back({at,at+nx+1,at+nx});}
    plot.mesh=std::move(mesh);return plot;
}
FrameExportReport exportFieldFrames(const FieldAnimationData& input,const std::filesystem::path& directory,const FrameExportOptions& options){
    auto data=input;validateFieldAnimation(data);
    if(!directory.is_absolute()||options.maximumFrames<1||options.maximumFrames>240||data.selectedFrames.size()>options.maximumFrames||options.maximumBytes<1024||options.maximumBytes>1024ull*1024*1024||options.prefix.empty()||options.prefix.size()>64)throw ExportError("Frame export requires an absolute directory and explicit bounded frame/disk settings.");
    for(char c:options.prefix)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))throw ExportError("Frame filename prefix accepts ASCII letters, digits, '_' and '-' only.");
    if(options.image.width<160||options.image.height<120||options.image.width>16000||options.image.height>16000)throw ExportError("Frame image dimensions are outside the native PNG range.");
    const auto pixels=static_cast<std::uint64_t>(options.image.width)*options.image.height;
    // The native encoder emits stored lossless DEFLATE blocks. This conservative
    // bound includes raw pixels, row filters, chunk headers and 1 MiB metadata.
    const auto perFrame=pixels*3+static_cast<std::uint64_t>(options.image.height)+pixels*3/65535*5+1024*1024;
    if(perFrame>options.maximumBytes/data.selectedFrames.size())throw ExportError("Requested PNG frame sequence exceeds max_bytes before any image is written.");
    std::vector<std::filesystem::path> paths;for(std::size_t k=0;k<data.selectedFrames.size();++k){std::ostringstream name;name<<options.prefix<<'-'<<std::setw(6)<<std::setfill('0')<<(k+1)<<".png";paths.push_back(directory/name.str());}
    const auto manifest=directory/(options.prefix+"-frames.csv");if(!options.image.overwrite){for(const auto& path:paths)if(std::filesystem::exists(path))throw ExportError("A selected frame already exists; enable overwrite explicitly.");if(std::filesystem::exists(manifest))throw ExportError("Frame manifest already exists; enable overwrite explicitly.");}
    if(!std::filesystem::exists(directory)){if(!options.createDirectory)throw ExportError("Frame directory does not exist; enable create_directory explicitly.");std::filesystem::create_directories(directory);}if(!std::filesystem::is_directory(directory))throw ExportError("Frame destination must be a directory.");
    FrameExportReport report;report.directory=directory;report.manifest=manifest;std::ostringstream catalogue;catalogue<<"frame,source_index,time,coefficient,filename,status\n";
    auto saveManifest=[&]{runtime::dataio::Options io;io.maxFileBytes=1048576;runtime::dataio::writeBytesAtomic(manifest,catalogue.str(),io);};
    try{for(std::size_t k=0;k<paths.size();++k){if(options.image.cancellation&&options.image.cancellation->load())throw ExportError("PNG frame sequence cancelled; completed images are listed in its manifest.");writePng({fieldAnimationFrame(data,k)},paths[k],options.image);report.bytes+=std::filesystem::file_size(paths[k]);++report.frames;catalogue<<(k+1)<<','<<(data.selectedFrames[k]+1)<<','<<std::setprecision(17)<<data.t[data.selectedFrames[k]]<<','<<data.coefficient<<','<<paths[k].filename().string()<<",complete\n";saveManifest();}}
    catch(...){if(report.frames)saveManifest();throw;}
    return report;
}
}
