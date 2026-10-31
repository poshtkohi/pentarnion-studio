#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

int main(){using namespace penta::runtime;int checks=0,failures=0;auto expect=[&](bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}};
    const auto directory=std::filesystem::temp_directory_path()/("penta-publication-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(directory);ExecutionOptions options;options.dataDirectory=directory;options.nativeFigureExport=true;
    auto run=[&](const std::string& source,bool good=true){const auto result=execute(source,{},options);if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"publication language program has expected success/failure");return result;};
    run("matrix x=linspace(0,6.28,100);record style=options(colors=[1,0,0;0,0,1],line_width=2.0,line_style=\"dash\",font_size=8.0,legend_location=\"northwest\");figure(1);plotstyle(style);plot(x,sin(x));hold(\"on\");plot(x,cos(x));hold(\"off\");legend(\"sine\",\"cosine\",options(location=\"northwest\",box=1));axis([0,6.3,-1.2,1.2]);xlabel(\"time\");ylabel(\"value\");title(\"Native headless publication curves\");exportgraphics(\"curves.png\",options(width_cm=8.0,height_cm=6.0,dpi=100));");
    expect(std::filesystem::is_regular_file(directory/"curves.png"),"headless exportgraphics creates an image without a GUI");
    run("figure(1);matrix x=[0,1];plot(x,x);exportgraphics(\"curves.png\");",false);
    run("figure(1);matrix x=[0,1];plot(x,x);exportgraphics(\"curves.png\",options(overwrite=1,width_cm=8.0,height_cm=6.0,dpi=100));");
    run("figure(1);matrix x=[0,1];subplot(1,2,1);plot(x,x);subplot(1,2,2);plot(x,-x);linkaxes([1,2],\"x\");exportgraphics(\"subplot.png\",options(subplot=2,width_cm=8.0,height_cm=6.0,dpi=100));");
    expect(std::filesystem::is_regular_file(directory/"subplot.png"),"headless export can select one subplot");
    run("plotstyle(options(colors=[1.1,0,0]));",false);
    run("plotstyle(options(line_width=0));",false);
    run("xlim([1,0]);",false);
    run("hold(\"invalid\");",false);
    run("legend(1.0);",false);
    run("figure(1);matrix x=[0,1];plot(x,x);exportgraphics(\"bad.bmp\");",false);
    run("figure(1);matrix x=[0,1];plot(x,x);exportgraphics(\"large.png\",options(width_cm=100.0,height_cm=100.0,dpi=1200));",false);
    expect(!std::filesystem::exists(directory/"large.png"),"oversized export leaves no partial image");
    options.nativeFigureExport=false;const auto gui=run("figure(1);matrix x=[0,1];plot(x,x);exportgraphics(\"native-vector.pdf\",options(width_cm=16.0,height_cm=10.0,dpi=150));");
    expect(gui.figureActions.back().kind==FigureActionKind::ExportPlot&&gui.figureActions.back().exportOptions.has_value(),"GUI export is a typed deferred action with physical dimensions");
    expect(!std::filesystem::exists(directory/"native-vector.pdf"),"numerical worker does not instantiate Qt or pretend to render a PDF");
    std::error_code ignored;std::filesystem::remove_all(directory,ignored);std::cout<<checks<<" publication language checks, "<<failures<<" failures\n";return failures?1:0;
}
