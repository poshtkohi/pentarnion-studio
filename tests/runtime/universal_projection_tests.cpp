#include "plotting/native_export.hpp"
#include "algebra/universal.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
using namespace penta;
int checks=0,failures=0;
void expect(bool okay,const char* message){++checks;if(!okay){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
template<class F>void reject(F action,const char* message){bool rejected=false;try{action();}catch(const plotting::ExportError&){rejected=true;}expect(rejected,message);}
auto bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::vector<unsigned char>(std::istreambuf_iterator<char>(file),{});}
std::uint32_t bigEndian(const std::vector<unsigned char>& data,std::size_t at){return (std::uint32_t(data.at(at))<<24)|(std::uint32_t(data.at(at+1))<<16)|(std::uint32_t(data.at(at+2))<<8)|data.at(at+3);}
nlohmann::json vectorMetadata(const std::vector<unsigned char>& png){
    for(std::size_t at=8;at+12<=png.size();){const auto length=bigEndian(png,at);if(length>png.size()-at-12)throw std::runtime_error("Invalid PNG chunk bounds.");const std::string type(reinterpret_cast<const char*>(png.data()+at+4),4);if(type=="iTXt"&&length>=16){const std::string text(reinterpret_cast<const char*>(png.data()+at+24),length-16);std::istringstream lines(text);std::string line;while(std::getline(lines,line))if(line.starts_with("{")&&line.find("algebra_projection")!=std::string::npos)return nlohmann::json::parse(line);}at+=length+12;}
    throw std::runtime_error("No full algebra projection metadata in PNG.");
}
}
int main(){
    using namespace penta;using namespace penta::plotting;
    const auto directory=std::filesystem::temp_directory_path()/("penta-universal-projection-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        std::filesystem::create_directories(directory);PngOptions options;options.width=600;options.height=420;
        const auto sed=algebra::universal::builtin("sedenion");NativePanel panel;panel.dimensions=2;panel.axes={10,15,-1};panel.basisLabels=sed->basis();panel.algebraId=sed->id();panel.xLabel="a";panel.yLabel="b";panel.style.legendVisible=false;panel.style.colors={{{1,0,0}}};
        runtime::PlotVector vector;vector.label="full sedenion";vector.originalCoefficients.resize(16);vector.originalCoefficients[10]=1;vector.originalCoefficients[15]=1;vector.originalCoefficients[5]=1e250;panel.vectors.push_back(vector);
        auto legacy=panel;legacy.axes={1,2,-1};legacy.basisLabels.clear();legacy.algebraId="A_gamma_v1";legacy.vectors.clear();runtime::PlotVector old;old.label=vector.label;old.coefficients={0,1,1,0,0};legacy.vectors.push_back(old);
        const auto projected=rasterRgb({panel},options),reference=rasterRgb({legacy},options);expect(projected==reference,"High-dimensional coordinate projection renders the same arrow geometry as an independent legacy plane.");
        std::size_t red=0;for(std::size_t k=0;k<projected.size();k+=3)if(projected[k]>240&&projected[k+1]<10&&projected[k+2]<10)++red;expect(red>100,"Projected vector produces a visible colored shaft and arrow head.");
        const auto original=panel.vectors[0].originalCoefficients;writePng({panel},directory/"sedenion.png",options);const auto metadata=vectorMetadata(bytes(directory/"sedenion.png"));expect(metadata.at("basis_labels").size()==16&&metadata.at("algebra_id")==sed->id(),"PNG preserves full basis and immutable algebra identity.");expect(metadata.at("axes")==nlohmann::json({10,15,-1}),"PNG records the exact selected projection indices.");expect(metadata.at("vectors")[0].at("coefficients").get<std::vector<double>>()==original,"PNG retains every original coefficient, including unprojected huge values.");expect(panel.vectors[0].originalCoefficients==original,"Export leaves original algebra vector coefficients unchanged.");
        const auto cl=algebra::universal::builtin("clifford",{6,0});auto wide=panel;wide.dimensions=3;wide.axes={16,32,63};wide.basisLabels=cl->basis();wide.algebraId=cl->id();wide.vectors[0].originalCoefficients.assign(64,0);wide.vectors[0].originalCoefficients[16]=1;wide.vectors[0].originalCoefficients[32]=2;wide.vectors[0].originalCoefficients[63]=3;writePng({wide},directory/"clifford64.png",options);const auto widest=vectorMetadata(bytes(directory/"clifford64.png"));expect(widest.at("vectors")[0].at("coefficients").size()==64&&widest.at("axes")[2]==63,"3D native projection and export support the final coefficient of a64-dimensional algebra.");
        NativeFigureScene scene;runtime::FigureAction plot;plot.kind=runtime::FigureActionKind::Plot;plot.dimensions=3;plot.axes=wide.axes;plot.vectors=wide.vectors;plot.basisLabels=wide.basisLabels;plot.algebraId=wide.algebraId;scene.ingest(plot);const auto retained=scene.panels();expect(retained.size()==1&&retained[0].basisLabels==cl->basis()&&retained[0].vectors[0].coefficient(63)==3,"Native scene retains full vectors and dynamic basis labels from runtime figure actions.");
        runtime::FigureAction hold;hold.kind=runtime::FigureActionKind::ConfigurePlot;hold.plotStyle=runtime::PlotStylePatch{};hold.plotStyle->hold=true;scene.ingest(hold);scene.ingest(plot);expect(scene.panels()[0].vectors.size()==2,"Holding the same algebra preserves multiple projected arrows.");auto mixed=plot;mixed.algebraId="different immutable algebra hash";reject([&]{scene.ingest(mixed);},"Held projections cannot mix different immutable multiplication tables.");
        auto invalid=wide;invalid.axes[2]=64;reject([&]{(void)rasterRgb({invalid},options);},"An axis outside the full retained basis is rejected safely.");invalid=wide;invalid.axes[2]=32;reject([&]{(void)rasterRgb({invalid},options);},"Repeated algebra projection axes are rejected.");invalid=wide;invalid.vectors[0].originalCoefficients[0]=std::numeric_limits<double>::quiet_NaN();reject([&]{writePng({invalid},directory/"invalid.png",options);},"An unprojected nonfinite coefficient is rejected before export.");expect(!std::filesystem::exists(directory/"invalid.png"),"Invalid original algebra data leave no partial PNG file.");invalid=wide;invalid.basisLabels.pop_back();reject([&]{(void)rasterRgb({invalid},options);},"A basis label count cannot silently truncate retained coordinates.");
        const auto result=runtime::execute("algebra S=builtin_algebra(\"sedenion\");element p=S.e10+2*S.e15;figure(1);plot2(\"e10\",\"e15\",p);algebra Cl=builtin_algebra(\"clifford\",options(p=6,q=0));element q=Cl.e5+2*Cl.e6+3*Cl.e123456;figure(2);plot3(\"e5\",\"e6\",\"e123456\",q);");if(!result.ok()){for(const auto& error:result.analysis.diagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';for(const auto& error:result.runtimeDiagnostics)std::cerr<<error.code<<": "<<error.message<<'\n';}expect(result.ok(),"Language produces arbitrary-basis2D/3D vector projections.");if(result.ok()){NativeFigureScene checked;checked.ingest(result.figureActions);const auto first=checked.panels(1),second=checked.panels(2);expect(first[0].axes[0]==10&&first[0].axes[1]==15&&first[0].vectors[0].allCoefficients().size()==16,"Sedenion projection language retains all16 coefficients.");expect(second[0].axes[2]==63&&second[0].vectors[0].coefficient(63)==3,"Clifford projection language addresses coefficient63 without fixed-five fallback.");checked.exportCurrentPng(directory/"language-clifford.png",options,2);expect(std::filesystem::is_regular_file(directory/"language-clifford.png"),"Language-generated dynamic projection exports natively to PNG.");}
    }catch(const std::exception& error){++failures;std::cerr<<"Unexpected exception: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" universal algebra projection checks, "<<failures<<" failures\n";return failures?1:0;
}
