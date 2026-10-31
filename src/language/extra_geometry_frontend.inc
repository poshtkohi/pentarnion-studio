#include "mesh_intersection_frontend.inc"
#include "form_pullback_frontend.inc"
#include "mesh_query_frontend.inc"
#include "intrinsic_geometry_frontend.inc"
#include "intrinsic_atlas_frontend.inc"
    std::optional<Type> checkExtraGeometryCall(const ExprPtr& value){
        if(auto type=checkMeshIntersectionCall(value))return type;
        if(auto type=checkFormPullbackCall(value))return type;
        if(auto type=checkIntrinsicAtlasCall(value))return type;
        if(auto type=checkIntrinsicGeometryCall(value))return type;
        if(auto type=checkMeshQueryCall(value))return type;
        const auto& name=value->text;static const std::set<std::string> names{"meshplane","meshsphere","meshcylinder","meshcircle","paramesh","trimesh","meshinfo","meshtransform","meshrefine","meshplot","meshread","meshwrite","lineplane","raysphere","dform","formfield","wedge","exteriord","hodge","codifferential","formlaplacian","laplacebeltrami","surfaceheat","surfacecoeff"};if(!names.contains(name))return {};
        bool valid=true;auto require=[&](bool okay,const char* text){if(!okay){add("E2210",text,value->span);valid=false;}};require(m_options.languageVersion=="0.2","Geometry/forms/surface APIs require language0.2.");require(!hasNamedArguments(value),"Use positional geometry arguments and typed options records.");const auto count=value->children.size();const std::size_t required=(name=="meshplane"||name=="meshcylinder"||name=="trimesh"||name=="meshtransform"||name=="meshwrite"||name=="wedge"||name=="surfaceheat"||name=="surfacecoeff")?2:(name=="paramesh"||name=="dform"||name=="formfield"||name=="laplacebeltrami")?3:(name=="lineplane"||name=="raysphere")?4:1;const bool optional=name!="meshinfo"&&name!="meshread"&&name!="meshwrite"&&name!="wedge"&&name!="hodge"&&name!="codifferential"&&name!="formlaplacian"&&name!="exteriord";require(count==required||(optional&&count==required+1),"Incorrect geometry argument count; see help.");if(count<required)return Type::Invalid;
        if(optional&&count>required&&name!="meshtransform"&&name!="surfacecoeff")require(value->children.back()->type==Type::Record,"Geometry options require a typed record.");
        if(name=="paramesh"||name=="formfield")require(value->children[0]->type==Type::Function,"Geometry field requires a declared pure function compiled to the native cache.");
        if(name=="laplacebeltrami"){require(value->children[0]->type==Type::Function&&value->children[1]->type==Type::Function,"Laplace-Beltrami requires native embedding and scalar field functions.");require(matrixType(value->children[2]->type),"Chart point requires real coordinates[u,v].");}
        if(name=="meshinfo"||name=="meshtransform"||name=="meshrefine"||name=="meshplot"||name=="meshwrite"||name=="surfaceheat"||name=="surfacecoeff"||name=="wedge"||name=="hodge"||name=="codifferential"||name=="formlaplacian"||name=="exteriord")require(value->children[0]->type==Type::Record,"This geometry operation requires a versioned typed record.");
        if(name=="wedge")require(value->children[1]->type==Type::Record,"Wedge requires two real differential-form records.");
        if(name=="dform")require(value->children[0]->type==Type::Integer&&value->children[1]->type==Type::Integer,"dform begins with integer dimension and degree.");
        if(name=="formfield")require(matrixType(value->children[1]->type)&&value->children[2]->type==Type::Integer,"formfield uses a real coordinate point and integer degree.");
        if(name=="meshread")require(value->children[0]->type==Type::String,"Mesh path requires a string.");
        if(name=="meshwrite")require(value->children[1]->type==Type::String,"Mesh path requires a string.");
        if(name=="meshplot"||name=="meshwrite")return valid?Type::Void:Type::Invalid;
        if(name=="surfacecoeff")return valid?Type::RealMatrix:Type::Invalid;
        if(name=="laplacebeltrami")return valid?Type::Real:Type::Invalid;
        auto meshSchema=[](){auto s=std::make_shared<ValueSchema>();for(const auto* key:{"format","coordinate_kind","metric_kind","source","cache_key"})s->fields.push_back({key,Type::String,{}});for(const auto* key:{"vertices","triangles","boundary_tags"})s->fields.push_back({key,Type::RealMatrix,{}});s->fields.push_back({"embedding_dimension",Type::Integer,{}});s->fields.push_back({"gamma",Type::Real,{}});return s;};auto schema=std::make_shared<ValueSchema>();
        if(name=="meshinfo"){for(const auto* key:{"vertices","triangles","boundary_edges","embedding_dimension","closed","orientation_consistent","volume_available","native_operations","threads_used"})schema->fields.push_back({key,Type::Integer,{}});for(const auto* key:{"area","length","signed_volume","min_angle","max_aspect"})schema->fields.push_back({key,Type::Real,{}});for(const auto* key:{"face_areas","normals","normal_projectors"})schema->fields.push_back({key,Type::RealMatrix,{}});}
        else if(name=="lineplane"||name=="raysphere"){schema->fields={{"points",Type::RealMatrix,{}},{"parameters",Type::RealMatrix,{}},{"count",Type::Integer,{}},{"status",Type::String,{}}};}
        else if(name=="dform"||name=="formfield"||name=="wedge"||name=="hodge"||name=="codifferential"||name=="formlaplacian"||name=="exteriord"){for(const auto* key:{"format","source","cache_key","metric_cache_key"})schema->fields.push_back({key,Type::String,{}});for(const auto* key:{"dimension","degree","derivative_order","constant","orientation"})schema->fields.push_back({key,Type::Integer,{}});for(const auto* key:{"coefficients","first","second","metric","metric_first","metric_second","point"})schema->fields.push_back({key,Type::RealMatrix,{}});}
        else if(name=="surfaceheat"){for(const auto* key:{"format","method","initial_cache_key","diffusion_cache_key","reaction_cache_key","sample_interpolation","status"})schema->fields.push_back({key,Type::String,{}});schema->fields.push_back({"mesh",Type::Record,meshSchema()});for(const auto* key:{"t","values","means","energy","max_mass_drift"})schema->fields.push_back({key,Type::RealMatrix,{}});for(const auto* key:{"width","steps","factorizations","evaluations","native_operations","threads_used"})schema->fields.push_back({key,Type::Integer,{}});for(const auto* key:{"gamma","max_energy_increase"})schema->fields.push_back({key,Type::Real,{}});}
        else schema=meshSchema();
        value->schema=schema;return valid?Type::Record:Type::Invalid;
    }
