#include "runtime/interpreter.hpp"
#include "plotting/native_export.hpp"
#include <iostream>
int main(){using namespace penta;int failures=0,checks=0;auto verify=[&](bool okay,const char* text){++checks;if(!okay){++failures;std::cerr<<text<<'\n';}};
    auto result=runtime::execute("figure(1);histplot([0,0.25,0.5,1.0,1.5],[0,0.5,1,2]);");if(!result.ok()){for(const auto& d:result.analysis.diagnostics)std::cerr<<d.message<<'\n';for(const auto& d:result.runtimeDiagnostics)std::cerr<<d.message<<'\n';}verify(result.ok(),"Histogram display accepts native histogram data.");
    if(result.ok()){plotting::NativeFigureScene scene;scene.ingest(result.figureActions);const auto& mesh=*scene.panels()[0].mesh;verify(mesh.points.size()==12&&mesh.triangles.size()==6,"Every unequal-width histogram bin has a filled native rectangle.");verify(mesh.points[2][1]==2&&mesh.points[6][1]==1&&mesh.points[10][1]==2,"Independent histogram counts determine bar heights.");}
    result=runtime::execute("record bins=histogram([0,0.25,0.5,1.0,1.5],[0,0.5,1,2]);bar(bins,options(density=1,width=1));");verify(result.ok(),"An existing histogram can display normalized density.");
    result=runtime::execute("bar([1,3,7],[2,-1,4],options(baseline=1,width=0.5));");verify(result.ok(),"Signed numeric bars preserve explicit positions and baseline.");if(result.ok()){const auto& mesh=*result.figureActions[0].mesh;verify(mesh.points[4][1]==1&&mesh.points[6][1]==-1,"Negative bars retain their exact baseline/endpoint.");}
    verify(!runtime::execute("bar([2,1],[1,2]);").ok(),"Unordered bar positions reject.");verify(!runtime::execute("bar([1,2],options(width=0));").ok(),"Invalid widths reject.");verify(!runtime::execute("histplot([1,2],[0,1,1]);").ok(),"Invalid bin edges reject.");
    std::cout<<checks<<" histogram/bar checks, "<<failures<<" failures\n";return failures?1:0;}
