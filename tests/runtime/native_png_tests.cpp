#include "plotting/native_export.hpp"
#include <zlib.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
std::vector<unsigned char> bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),{});}
std::uint32_t integer(const std::vector<unsigned char>& data,std::size_t at){return (std::uint32_t(data.at(at))<<24)|(std::uint32_t(data.at(at+1))<<16)|(std::uint32_t(data.at(at+2))<<8)|data.at(at+3);}
struct Decoded {int width=0,height=0,density=0;std::vector<unsigned char> pixels;std::string metadata;};
std::size_t colorCount(const Decoded& image,std::array<unsigned char,3> color){
    std::size_t count=0;
    for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){
        const auto at=static_cast<std::size_t>(y)*(image.width*3+1)+1+static_cast<std::size_t>(x)*3;
        if(image.pixels[at]==color[0]&&image.pixels[at+1]==color[1]&&image.pixels[at+2]==color[2])++count;
    }
    return count;
}
Decoded decode(const std::vector<unsigned char>& png){
    expect(png.size()>8&&png[0]==137&&png[1]==80&&png[2]==78&&png[3]==71,"valid native PNG signature");Decoded result;std::vector<unsigned char> compressed;
    for(std::size_t at=8;at+12<=png.size();){const auto size=integer(png,at);const std::string type(reinterpret_cast<const char*>(png.data()+at+4),4);if(size>png.size()-at-12)throw std::runtime_error("Malformed PNG chunk size.");const auto value=crc32(0L,png.data()+at+4,size+4);expect(value==integer(png,at+8+size),"independent zlib CRC validates PNG chunk");
        if(type=="IHDR"){result.width=static_cast<int>(integer(png,at+8));result.height=static_cast<int>(integer(png,at+12));expect(png[at+16]==8&&png[at+17]==2,"native PNG stores full 8-bit RGB pixels");}
        if(type=="pHYs")result.density=static_cast<int>(integer(png,at+8));
        if(type=="IDAT")compressed.insert(compressed.end(),png.begin()+static_cast<std::ptrdiff_t>(at+8),png.begin()+static_cast<std::ptrdiff_t>(at+8+size));
        if(type=="iTXt")result.metadata.assign(reinterpret_cast<const char*>(png.data()+at+8+16),size-16);
        at+=size+12;
    }
    uLongf count=static_cast<uLongf>(result.height)*(static_cast<uLongf>(result.width)*3+1);result.pixels.resize(count);
    expect(uncompress(result.pixels.data(),&count,compressed.data(),static_cast<uLong>(compressed.size()))==Z_OK&&count==result.pixels.size(),"independent zlib decoder accepts native lossless PNG stream");
    return result;
}
}
int main(){using namespace penta;using namespace penta::plotting;const auto folder=std::filesystem::temp_directory_path()/("penta-png-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    try{
        NativePanel p;p.title="Native publication test";p.xLabel="time";p.yLabel="value";p.context="A_gamma_v1 | gamma=1";p.style.fontSize=8;p.style.colors={{{1,0,0}}};p.curves.push_back({{{0,0,0},{1,1,0},{2,0,0}},"fitted model"});
        const auto unchanged=p.curves[0].points;PngOptions options;options.width=480;options.height=320;const auto path=folder/"curve.png";writePng({p},path,options);const auto original=bytes(path);const auto image=decode(original);
        expect(image.width==480&&image.height==320&&image.density==5906,"requested dimensions and physical DPI preserved");
        expect(image.metadata.find(p.title)!=std::string::npos&&image.metadata.find(p.context)!=std::string::npos,"original title and algebra provenance retained in PNG metadata");
        std::size_t red=0;for(int y=0;y<image.height;++y)for(int x=0;x<image.width;++x){const auto at=static_cast<std::size_t>(y)*(image.width*3+1)+1+static_cast<std::size_t>(x)*3;if(image.pixels[at]>240&&image.pixels[at+1]<10&&image.pixels[at+2]<10)++red;}
        expect(red>200,"configured red curve is present in independently decoded pixels");expect(p.curves[0].points==unchanged,"native export leaves original scientific samples unchanged");
        NativePanel decimal=p;decimal.title.clear();decimal.context.clear();decimal.style.fontSize=16;decimal.style.legendVisible=false;decimal.curves[0].points={{{0,-.6,0},{1,.6,0}}};writePng({decimal},folder/"decimal-labels.png",options);const auto decimalImage=decode(bytes(folder/"decimal-labels.png"));std::size_t trailingDigitInk=0;
        for(int y=160;y<265;++y)for(int x=40;x<51;++x){const auto at=static_cast<std::size_t>(y)*(decimalImage.width*3+1)+1+static_cast<std::size_t>(x)*3;if(decimalImage.pixels[at]<80&&decimalImage.pixels[at+1]<80&&decimalImage.pixels[at+2]<80)++trailingDigitInk;}
        expect(trailingDigitInk>25,"adaptive axis margin displays the complete trailing decimal digit instead of truncating numerical tick labels");
        auto reject=[&](auto action,const char* message){bool caught=false;try{action();}catch(const std::exception&){caught=true;}expect(caught,message);};
        reject([&]{writePng({p},path,options);},"existing image is protected without explicit overwrite");expect(bytes(path)==original,"rejected overwrite leaves existing image byte-identical");
        options.overwrite=true;options.width=500;writePng({p},path,options);expect(decode(bytes(path)).width==500,"explicit overwrite atomically replaces destination");
        auto stopped=options;stopped.cancellation=std::make_shared<std::atomic_bool>(true);reject([&]{writePng({p},folder/"cancelled.png",stopped);},"headless rendering supports cancellation");expect(!std::filesystem::exists(folder/"cancelled.png"),"cancelled export leaves no partial destination");
        auto bounded=options;bounded.maximumPixels=2;reject([&]{writePng({p},folder/"oversize.png",bounded);},"pixel budget rejected before allocating a frame");
        auto quota=options;quota.maximumRasterOperations=10;reject([&]{writePng({p},folder/"quota.png",quota);},"native raster work quota bounds expensive drawing");
        reject([&]{writePng({p},"relative.png",options);},"native destination must be an absolute caller-controlled path");
        NativePanel huge=p;huge.curves[0].points={{{-1e300,-1e300,0},{1e300,1e300,0}}};writePng({huge},folder/"huge.png",options);expect(std::filesystem::is_regular_file(folder/"huge.png"),"finite extreme coordinates normalize without GPU/float overflow");
        NativePanel mesh;mesh.title="Real level set mesh";mesh.style.fontSize=8;mesh.dimensions=3;mesh.mesh=runtime::PlotMesh{{{{0,0,0},{1,0,0},{0,1,0},{0,0,1}}},{{{0,1,2},{0,1,3},{0,2,3}}},{},{0,1,2,3},"tetrahedron"};writePng({mesh},folder/"mesh.png",options);expect(std::filesystem::is_regular_file(folder/"mesh.png"),"3D mesh scalar projection renders without Qt/OpenGL");
        NativePanel geometry=mesh;geometry.mesh->scalars.clear();geometry.style.colors={{{1,0,1}}};geometry.style.colorLimits=runtime::AxisLimits{-100,100};
        writePng({geometry},folder/"geometry-without-values.png",options);const auto geometryImage=decode(bytes(folder/"geometry-without-values.png"));
        expect(colorCount(geometryImage,{255,0,255})>100,"uncoloured surface geometry uses the configured mesh colour without requiring field samples");
        expect(geometry.mesh->scalars.empty(),"export does not invent or attach scalar values to pure geometry");
        NativePanel circle;circle.style.fontSize=8;circle.style.colors={{{1,0,1}}};circle.mesh=runtime::PlotMesh{{{{0,-1,0},{1,0,0},{0,1,0},{-1,0,0}}},{},{{{0,1},{1,2},{2,3},{3,0}}},{},"circle boundary"};
        writePng({circle},folder/"geometry-boundary-without-values.png",options);
        expect(colorCount(decode(bytes(folder/"geometry-boundary-without-values.png")),{255,0,255})>100,"segment-only geometry renders in its mesh colour without indexing an absent scalar field");
        NativePanel constant=geometry;constant.style.colorLimits.reset();constant.mesh->scalars.assign(constant.mesh->points.size(),7);
        writePng({constant},folder/"constant-field.png",options);
        expect(colorCount(decode(bytes(folder/"constant-field.png")),{0,0,128})>100,"a supplied constant field retains its scalar lookup colour rather than the geometry palette");
        auto malformed=geometry;malformed.mesh->scalars={1};reject([&]{rasterRgb({malformed},options);},"a nonempty scalar array still requires one value per mesh vertex");
        malformed=geometry;malformed.mesh->scalars.assign(malformed.mesh->points.size(),1);malformed.mesh->scalars[0]=std::numeric_limits<double>::infinity();reject([&]{rasterRgb({malformed},options);},"nonfinite supplied mesh field values are rejected");
        malformed=geometry;malformed.mesh->triangles[0][0]=99;reject([&]{rasterRgb({malformed},options);},"uncoloured surfaces retain triangle index validation");
        malformed=circle;malformed.mesh->segments[0][0]=99;reject([&]{rasterRgb({malformed},options);},"uncoloured boundaries retain segment index validation");
        malformed=geometry;malformed.mesh->points.clear();reject([&]{rasterRgb({malformed},options);},"connectivity cannot silently refer to an empty geometry mesh");
        NativeFigureScene scene;runtime::FigureAction select;select.kind=runtime::FigureActionKind::Select;select.figure=2;scene.ingest(select);runtime::FigureAction plot;plot.kind=runtime::FigureActionKind::Plot;plot.dimensions=2;plot.axes={-1,-1,-1};plot.algebraId="A_gamma_v1";plot.curves=p.curves;scene.ingest(plot);
        runtime::FigureAction hold;hold.kind=runtime::FigureActionKind::ConfigurePlot;hold.plotStyle=runtime::PlotStylePatch{};hold.plotStyle->hold=true;scene.ingest(hold);scene.ingest(plot);expect(scene.panels()[0].curves.size()==2,"headless hold preserves both original curve datasets");hold.plotStyle->hold=false;scene.ingest(hold);scene.ingest(plot);expect(scene.panels()[0].curves.size()==1,"headless hold off replaces plotted datasets");
        runtime::FigureAction subplot;subplot.kind=runtime::FigureActionKind::Subplot;subplot.rows=1;subplot.columns=2;subplot.subplotIndex=2;scene.ingest(subplot);scene.ingest(plot);options.width=800;scene.exportCurrentPng(folder/"subplots.png",options);expect(decode(bytes(folder/"subplots.png")).width==800,"headless scene exports allocated subplots in their original grid");scene.exportCurrentPng(folder/"single-subplot.png",options,0,2);expect(scene.panels(2,2).size()==1,"headless caller can select one subplot without exporting the others");
    }catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);std::cout<<checks<<" native PNG checks, "<<failures<<" failures\n";return failures?1:0;
}
