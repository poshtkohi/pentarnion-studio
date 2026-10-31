#include "plotting/field_animation.hpp"
#include "runtime/interpreter.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int checks=0,failures=0;
void expect(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});}
}
int main(){using namespace penta;using namespace penta::plotting;const auto folder=std::filesystem::temp_directory_path()/("penta-animation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
    try{
        FieldAnimationData data;data.dimension=2;data.nx=data.ny=3;data.components=5;data.coefficient=2;data.x=data.y={0,.5,1};data.t={0,.1,.2};data.selectedFrames={0,1,2};data.context="A_gamma_v1 | gamma=1";
        for(int t=0;t<3;++t)for(int y=0;y<3;++y)for(int x=0;x<3;++x)for(int c=0;c<5;++c)data.values.push_back(100*t+10*y+x+1000*c);
        const auto original=data.values;validateFieldAnimation(data);
        expect(data.values==original&&data.components==5,"animation retains all original Pentarnion coefficients without mutating samples");
        const auto first=fieldAnimationFrame(data,0),second=fieldAnimationFrame(data,1);expect(first.mesh&&first.mesh->points.size()==9&&first.mesh->triangles.size()==8,"native surface frame has the complete regular-grid topology");
        expect(second.mesh->scalars[4]==2111&&second.mesh->points[4]==std::array<double,3>{.5,.5,2111},"frame extraction preserves x-fastest layout and selected coefficient");
        expect(first.style.colorLimits==second.style.colorLimits&&first.style.colorLimits->lower==2000&&first.style.colorLimits->upper==2222,"fixed color/height range prevents per-frame auto-rescaling artifacts");
        const auto other=fieldAnimationFrame(data,1,4);expect(other.mesh->scalars[4]==4111&&other.style.colorLimits->lower==4000,"component inspection uses retained original coefficients and precomputed ranges");
        auto image=data;image.surface=false;const auto flat=fieldAnimationFrame(image,2);expect(flat.dimensions==2&&flat.mesh->points[4][2]==0&&flat.mesh->scalars[4]==2211,"image animation preserves scalar data on a flat domain");
        auto line=data;line.dimension=1;line.ny=1;line.y.clear();line.values.clear();for(int t=0;t<3;++t)for(int x=0;x<3;++x)for(int c=0;c<5;++c)line.values.push_back(100*t+x+1000*c);validateFieldAnimation(line);const auto curve=fieldAnimationFrame(line,2);expect(curve.curves.size()==1&&curve.curves[0].points[1]==std::array<double,3>{.5,2201,0},"one-dimensional fields animate native saved-sample curves");
        auto reject=[&](auto action,const char* message){bool caught=false;try{action();}catch(const std::exception&){caught=true;}expect(caught,message);};
        reject([&]{auto bad=data;bad.selectedFrames={0,0};validateFieldAnimation(bad);},"duplicate animation indices rejected");
        reject([&]{auto bad=data;bad.values.pop_back();validateFieldAnimation(bad);},"incomplete full-state field data rejected");
        reject([&]{(void)fieldAnimationFrame(data,3);},"frame lookup is bounded to saved selection");
        FrameExportOptions output;output.image.width=480;output.image.height=320;output.createDirectory=true;output.prefix="field";const auto destination=folder/"frames";const auto report=exportFieldFrames(data,destination,output);
        expect(report.frames==3&&report.bytes>0&&std::filesystem::is_regular_file(report.manifest),"native frame export writes exactly selected images and a reproducibility manifest");
        const auto catalogue=read(report.manifest);expect(catalogue.find("1,1,0,2,field-000001.png,complete")!=std::string::npos&&catalogue.find("3,3,0.20000000000000001,2,field-000003.png,complete")!=std::string::npos,"manifest records original frame indices, exact times and coefficient");
        const auto old=read(destination/"field-000001.png");reject([&]{(void)exportFieldFrames(data,destination,output);},"frame overwrite is opt-in");expect(read(destination/"field-000001.png")==old,"rejected sequence preserves existing image bytes");
        auto disk=output;disk.maximumBytes=1024;reject([&]{(void)exportFieldFrames(data,folder/"too-large",disk);},"disk requirement rejected before frame output");expect(!std::filesystem::exists(folder/"too-large"),"preflight disk rejection creates no output folder");
        auto stop=output;stop.image.cancellation=std::make_shared<std::atomic_bool>(true);reject([&]{(void)exportFieldFrames(data,folder/"cancelled",stop);},"native frame export obeys cancellation");expect(!std::filesystem::exists(folder/"cancelled"/"field-000001.png"),"cancelled sequence leaves no partial frame");
        auto prefix=output;prefix.prefix="../escape";reject([&]{(void)exportFieldFrames(data,folder/"prefix",prefix);},"frame prefix cannot escape chosen directory");

        runtime::ExecutionOptions options;options.dataDirectory=folder;options.nativeFigureExport=true;
        auto run=[&](const std::string& source,bool good=true){const auto result=runtime::execute(source,{},options);if(result.ok()!=good){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.code<<": "<<d.message<<'\n';}expect(result.ok()==good,"field animation language has expected result");return result;};
        const std::string field="record f=options(dimension=1,x=[0,0.5,1],t=[0;0.1;0.2],values=[1,0,1;0.5,0,0.5;0.25,0,0.25]);";
        const auto result=run(field+"animatefield(f,options(fps=60.0));animationframe(2);playanimation(\"off\");exportgraphics(\"selected-frame.png\",options(width_cm=8.0,height_cm=6.0,dpi=100));record out=exportframes(f,\"language-frames\",options(width=480,height=320,prefix=\"heat\",create_directory=1,max_bytes=10000000));assert(out.frames==3);assert(out.status==\"exported\");");
        expect(result.figureActions.size()==3&&result.figureActions.front().animation&&result.figureActions.front().animation->values.size()==9,"language actions share an immutable original frame payload");expect(std::filesystem::is_regular_file(folder/"selected-frame.png"),"selected animation frame exports through the headless scene");
        const auto raw=run("record f=options(dimension=1,x=[0,0.5,1],t=[0;0.1],gamma=0.25,value_layout=\"node_coefficients\",components=5,values=[0,1,2,3,4,10,11,12,13,14,20,21,22,23,24;100,101,102,103,104,110,111,112,113,114,120,121,122,123,124]);animatefield(f,options(component=4));");
        expect(raw.ok()&&raw.figureActions.front().animation->components==5&&raw.figureActions.front().animation->values.size()==30&&raw.figureActions.front().gamma==.25&&raw.figureActions.front().animation->gamma==.25,"raw PDE ensemble snapshots preserve full coefficients and recorded run gamma instead of the current workspace context");
        run(field+"animatefield(f,options(component=1));",false);
        run(field+"animatefield(f,options(max_frames=2));",false);
        run(field+"record out=exportframes(f,\"no-disk\",options(create_directory=1,max_bytes=1024,width=480,height=320));",false);
    }catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::error_code ignored;std::filesystem::remove_all(folder,ignored);std::cout<<checks<<" field animation checks, "<<failures<<" failures\n";return failures?1:0;
}
