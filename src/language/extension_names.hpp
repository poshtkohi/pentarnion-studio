#pragma once
#include <algorithm>
#include <array>
#include <string_view>
namespace penta::language {
inline bool extensionBuiltin(std::string_view name){
    constexpr std::array names{
        std::string_view("module_info"),std::string_view("module_load"),
        std::string_view("bar"),std::string_view("histplot"),
        std::string_view("femtime"),std::string_view("femtimecoeff"),std::string_view("femtimeslice"),
        std::string_view("formpullback"),
        std::string_view("femmixed"),std::string_view("femmixedplot"),
        std::string_view("daeanalyze"),std::string_view("lineardaesolve"),
        std::string_view("jacobianpattern"),std::string_view("jacobiansparse"),std::string_view("daestructure"),std::string_view("daeinitialize"),
        std::string_view("tensor"),std::string_view("tensorinfo"),std::string_view("tensorvalues"),std::string_view("tensorcoords"),std::string_view("ndims"),
        std::string_view("filter"),std::string_view("filterstate"),std::string_view("filtfilt"),std::string_view("butter"),std::string_view("spectrogram"),std::string_view("istft"),std::string_view("cpsd"),std::string_view("mscohere"),std::string_view("peakprops"),std::string_view("distpdf"),std::string_view("distcdf"),std::string_view("distinv"),std::string_view("distrand"),std::string_view("meshintersect"),
        std::string_view("savearchive"),std::string_view("loadarchive"),std::string_view("archiveinfo"),std::string_view("archiveartifact"),std::string_view("migrateexperiment"),std::string_view("experimentcatalog"),std::string_view("catalogueentry"),std::string_view("replayexperiment"),
        std::string_view("femsystem"),std::string_view("femsystemplot"),std::string_view("femevaluate"),std::string_view("femadapt"),std::string_view("meshadapt"),
        std::string_view("odemultistartfit"),std::string_view("odeprofilefit"),std::string_view("odefitbootstrap"),
        std::string_view("pdemultistartfit"),std::string_view("pdeprofilefit"),std::string_view("pdefitbootstrap"),
        std::string_view("pdeimplicit"),std::string_view("pdeimplicitcoeff"),std::string_view("pdeimplicitslice"),std::string_view("pdeconverge"),
        std::string_view("basins"),std::string_view("lyapunovscan"),std::string_view("masssolve"),std::string_view("unitcheck"),std::string_view("mechanicsdae"),std::string_view("branchplot"),std::string_view("codifferential"),std::string_view("formlaplacian"),std::string_view("fieldprobe"),std::string_view("fieldslice"),std::string_view("structuralidentifiability"),std::string_view("intrinsicgeometry"),std::string_view("tensorcovariant"),std::string_view("charttransition"),std::string_view("chartatlas"),std::string_view("atlasgeometry"),std::string_view("atlastransition"),std::string_view("ndsolve"),std::string_view("ndinv"),std::string_view("ndlu"),std::string_view("meshclosest"),std::string_view("meshprobe"),std::string_view("meshcontains"),std::string_view("meshray"),std::string_view("meshslice"),std::string_view("saveplottemplate"),std::string_view("loadplottemplate"),std::string_view("linkcamera"),std::string_view("casnumeric"),
        std::string_view("petscsolve"),std::string_view("petscroot"),std::string_view("predictionbands"),std::string_view("propagateprediction"),std::string_view("profileinterval"),std::string_view("identifiability"),std::string_view("coveragefit"),
        std::string_view("cas"),std::string_view("caspent"),std::string_view("casdiff"),std::string_view("cassubs"),std::string_view("casreal"),std::string_view("cascomplex"),std::string_view("caspentnumeric"),std::string_view("casmatrix"),std::string_view("casmatmul"),std::string_view("caslinsolve"),std::string_view("casdet"),std::string_view("cascode")};
    return std::find(names.begin(),names.end(),name)!=names.end();
}
}
