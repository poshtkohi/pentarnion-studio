#include "plotting/native_export.hpp"
#include "runtime/interpreter.hpp"
#include "runtime/module_inventory.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <QXmlStreamReader>
using namespace penta;
std::string read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};}
bool vectorOnlySvg(const std::string& content){
    QXmlStreamReader xml(QByteArray(content.data(),static_cast<qsizetype>(content.size())));
    bool root=false;std::size_t geometry=0,outlinedPaths=0;
    while(!xml.atEnd()){
        xml.readNext();if(!xml.isStartElement())continue;
        const auto local=xml.name();
        if(!root){root=true;if(local!=QLatin1String("svg")||xml.namespaceUri()!=QLatin1String("http://www.w3.org/2000/svg"))return false;}
        // Any namespace prefix is permitted, but text/image nodes in any
        // namespace violate the outlined, font-independent vector contract.
        if(local==QLatin1String("image")||local==QLatin1String("text"))return false;
        const bool svgNamespace=xml.namespaceUri()==QLatin1String("http://www.w3.org/2000/svg");
        if(local==QLatin1String("title")&&!svgNamespace)return false;
        const bool shape=local==QLatin1String("path")||local==QLatin1String("polyline")||
            local==QLatin1String("polygon")||local==QLatin1String("line")||
            local==QLatin1String("rect")||local==QLatin1String("circle")||
            local==QLatin1String("ellipse");
        if(shape){
            if(!svgNamespace)return false;
            ++geometry;
            if(local==QLatin1String("path")&&!xml.attributes().value(QLatin1String("d")).isEmpty())++outlinedPaths;
        }
    }
    return root&&geometry>0&&outlinedPaths>0&&!xml.hasError();
}
int main(){int checks=0,failures=0;auto expect=[&](bool yes,const char* text){++checks;if(!yes){++failures;std::cerr<<text<<'\n';}};const auto directory=std::filesystem::temp_directory_path()/("penta-vector-export-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);
    try{
        expect(vectorOnlySvg("<n:svg xmlns:n=\"http://www.w3.org/2000/svg\"><n:path d=\"M0 0L1 1\"/></n:svg>"),"Namespace-prefixed SVG with outlined geometry is accepted.");
        expect(!vectorOnlySvg("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:x=\"urn:foreign\"><path d=\"M0 0L1 1\"/><x:image/></svg>")&&!vectorOnlySvg("<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0 0L1 1\"/><text>font dependency</text></svg>"),"Raster images and live text are rejected independently of namespace prefixes.");
        expect(!vectorOnlySvg("<x:svg xmlns:x=\"urn:wrong\" xmlns:s=\"http://www.w3.org/2000/svg\"><s:path d=\"M0 0L1 1\"/></x:svg>")&&!vectorOnlySvg("<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0 0L1 1\"/>"),"Wrong SVG namespaces and malformed documents are rejected.");
        expect(!vectorOnlySvg("<n:svg xmlns:n=\"http://www.w3.org/2000/svg\"><n:polyline points=\"0,0 1,1\"/><path d=\"M0 0L1 1\"/></n:svg>")&&!vectorOnlySvg("<n:svg xmlns:n=\"http://www.w3.org/2000/svg\"><n:path d=\"M0 0L1 1\"><title>lost accessible label</title></n:path></n:svg>"),"Unqualified glyph paths and accessible titles are rejected under namespace-prefixed SVG roots.");
        const auto inventory=runtime::moduleInventory();for(const auto& module:inventory)if(module.name=="vector_plot")expect(module.available&&!module.loaded,"Vector module remains unloaded before first export.");
        plotting::NativePanel panel;panel.dimensions=2;panel.title="$\\alpha(t)=\\sin(t)$";panel.caption="Unicode μ and λ";panel.latex[4]=false;panel.curves.push_back({{{0,0,0},{.5,.25,0},{1,1,0}},"squared"});panel.xLabel="$t$";panel.yLabel="$p_0$";panel.context="All original coordinates retained";plotting::PngOptions options;options.width=900;options.height=600;options.dpi=150;
        plotting::writeVector({panel},directory/"figure.svg",options);const auto svg=read(directory/"figure.svg");expect(vectorOnlySvg(svg),"SVG contains valid outlined geometry and mathematical text without raster images or live text nodes.");expect(svg.find("Unicode")!=std::string::npos&&svg.find("All original coordinates retained")!=std::string::npos,"SVG retains UTF-8 labels and scientific metadata.");
        plotting::writeVector({panel},directory/"figure.pdf",options);const auto pdf=read(directory/"figure.pdf");expect(pdf.starts_with("%PDF-")&&pdf.find("%%EOF")!=std::string::npos,"Native PDF is a complete vector document.");bool rejected=false;try{plotting::writeVector({panel},directory/"figure.svg",options);}catch(const std::exception&){rejected=true;}expect(rejected&&read(directory/"figure.svg")==svg,"Existing vector exports remain unchanged without overwrite.");
        plotting::NativePanel geometry;geometry.style.fontSize=8;geometry.style.colors={{{1,0,1}}};geometry.title="Geometry without a scalar field";
        geometry.mesh=runtime::PlotMesh{{{{0,0,0},{1,0,0},{0,1,0}}},{{{0,1,2}}},{{{0,1},{1,2},{2,0}}},{},"triangle boundary"};
        plotting::writeVector({geometry},directory/"geometry.svg",options);const auto geometrySvg=read(directory/"geometry.svg");
        expect(vectorOnlySvg(geometrySvg)&&geometrySvg.find("#ff00ff")!=std::string::npos,"Uncoloured mesh surfaces and boundary segments export as outlined SVG geometry in the configured mesh colour.");
        plotting::writeVector({geometry},directory/"geometry.pdf",options);const auto geometryPdf=read(directory/"geometry.pdf");
        expect(geometryPdf.starts_with("%PDF-")&&geometryPdf.find("%%EOF")!=std::string::npos,"Uncoloured geometry exports as a complete native vector PDF without scalar-array indexing.");
        expect(geometry.mesh->scalars.empty(),"Vector export preserves absent field samples instead of fabricating a zero-valued field.");
        auto field=geometry;field.mesh->scalars={0,.5,1};plotting::writeVector({field},directory/"mesh-field.svg",options);const auto fieldSvg=read(directory/"mesh-field.svg");
        expect(vectorOnlySvg(fieldSvg)&&fieldSvg.find("#ff00ff")==std::string::npos,"An explicit scalar field continues to use field colours instead of the geometry palette.");
        auto malformed=geometry;malformed.mesh->scalars={1};rejected=false;try{plotting::writeVector({malformed},directory/"bad-mesh.svg",options);}catch(const std::exception&){rejected=true;}
        expect(rejected&&!std::filesystem::exists(directory/"bad-mesh.svg"),"Malformed nonempty mesh fields are rejected before committing a vector export.");
        auto cancellation=std::make_shared<std::atomic_bool>(true);options.cancellation=cancellation;rejected=false;try{plotting::writeVector({panel},directory/"cancel.svg",options);}catch(const std::exception&){rejected=true;}expect(rejected&&!std::filesystem::exists(directory/"cancel.svg"),"Cancelled exports never commit partial files.");options.cancellation.reset();panel.title="$\\unsupportedcommand{x}$";rejected=false;try{plotting::writeVector({panel},directory/"invalid.svg",options);}catch(const std::exception&){rejected=true;}expect(rejected&&!std::filesystem::exists(directory/"invalid.svg"),"Unsupported LaTeX never silently degrades or commits a partial export.");
        runtime::ExecutionOptions execution;execution.dataDirectory=directory;execution.nativeFigureExport=true;const auto result=runtime::execute("figure(1);subplot(1,2,1);plot([0,1],[0,1]);title(\"$p+q$\");subplot(1,2,2);plot3([0,1],[1,0],[0,1]);title(\"$pq$\");exportgraphics(\"subplots.svg\",options(width_cm=20,height_cm=10,dpi=150));exportgraphics(\"one-panel.pdf\",options(subplot=2));",{},execution);if(!result.ok()){for(const auto& diagnostic:result.analysis.diagnostics)std::cerr<<diagnostic.message<<'\n';for(const auto& diagnostic:result.runtimeDiagnostics)std::cerr<<diagnostic.message<<'\n';}expect(result.ok()&&std::filesystem::exists(directory/"subplots.svg")&&std::filesystem::exists(directory/"one-panel.pdf"),"Headless language exports complete figures and selected subplots as SVG/PDF.");
        if(const auto* artifacts=std::getenv("PENTA_QA_ARTIFACT_DIR")){const std::filesystem::path target=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(artifacts)));std::filesystem::create_directories(target);for(const auto* file:{"figure.svg","figure.pdf","subplots.svg","one-panel.pdf"})std::filesystem::copy_file(directory/file,target/file,std::filesystem::copy_options::overwrite_existing);}
    }catch(const std::exception& error){++failures;std::cerr<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" native vector export checks, "<<failures<<" failures\n";return failures?1:0;
}
