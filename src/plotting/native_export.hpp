#pragma once
#include "runtime/interpreter.hpp"
#include <map>
#include <stdexcept>

namespace penta::plotting {
class ExportError final:public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct NativePanel {
    int dimensions=2;
    std::array<int,3> axes{{-1,-1,-1}};
    std::vector<runtime::PlotVector> vectors;
    std::vector<runtime::PlotCurve> curves;
    std::optional<runtime::PlotMesh> mesh;
    std::optional<runtime::PlotGraph> graph;
    runtime::PlotStyle style;
    std::string title,xLabel,yLabel,zLabel,caption,context;
    double cameraAzimuth=-45,cameraElevation=35.264389682754654;
    int row=0,column=0,rowSpan=1,columnSpan=1;
    // Empty basisLabels retains the legacy five-coordinate naming convention.
    // Dynamic vectors keep all coefficients in PlotVector::allCoefficients().
    std::vector<std::string> basisLabels;
    std::string algebraId;
    double gamma=1;
    std::array<bool,5> latex{{true,true,true,true,true}};
};
struct PngOptions {
    int width=1600,height=1000,dpi=150,rows=0,columns=0;
    bool overwrite=false;
    std::size_t maximumPixels=16000000,maximumPoints=2000000;
    std::uint64_t maximumRasterOperations=250000000;
    std::shared_ptr<std::atomic_bool> cancellation;
};
// Deterministic native CPU projection/rasterization. No Qt/OpenGL, subprocess or
// Python. PNG metadata retains UTF-8 labels; the built-in bitmap font displays
// printable ASCII. Full original vector coefficients, basis/identity and chosen
// axes are retained in bounded JSON PNG metadata. LaTeX/Unicode typography
// remains the native GUI export path.
void writeVector(const std::vector<NativePanel>&,const std::filesystem::path&,const PngOptions& options={},const std::filesystem::path& moduleDirectory={});
void writePng(const std::vector<NativePanel>& panels,const std::filesystem::path& absolutePath,
              const PngOptions& options={});
// Top-down RGB24 pixels from the same native projection, for streaming video.
std::vector<unsigned char> rasterRgb(const std::vector<NativePanel>& panels,const PngOptions& options={});
class NativeFigureScene {
public:
    void ingest(const runtime::FigureAction&);
    void ingest(const std::vector<runtime::FigureAction>& actions){for(const auto& action:actions)ingest(action);}
    std::vector<NativePanel> panels(int figure=0,int subplot=0)const;
    PlotTemplate currentTemplate(int figure=0,int subplot=0)const;
    void exportCurrentVector(const std::filesystem::path&,PngOptions options={},int figure=0,int subplot=0,const std::filesystem::path& moduleDirectory={})const;
    void exportCurrentPng(const std::filesystem::path&,PngOptions options={},int figure=0,int subplot=0)const;
private:
    struct Figure {int rows=1,columns=1,active=1;std::map<int,NativePanel> panels;std::map<int,std::shared_ptr<const FieldAnimationData>> animations;std::map<int,std::size_t> animationFrames;};
    std::map<int,Figure> figures_;int selected_=0;
};
}
