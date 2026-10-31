#include "fem_system_frontend.inc"
#include "fem_adaptive_frontend.inc"
#include "fem_mesh_refinement_frontend.inc"
#include "fem_time_frontend.inc"
#include "fem_mixed_frontend.inc"
    std::optional<Type> checkFemCall(const ExprPtr& value){
        if(auto type=checkFemMixedCall(value))return type;
        if(auto type=checkFemTimeDataCall(value))return type;
        if(auto type=checkFemMeshAdaptCall(value))return type;
        if(auto type=checkFemAdaptiveCall(value))return type;
        if(auto type=checkFemSystemCall(value))return type;
        const auto& name=value->text;if(name!="femsolve"&&name!="femplot")return {};
        bool valid=true;auto require=[&](bool condition,const char* message){if(!condition){add("E2215",message,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","FEM requires language0.2.");require(!hasNamedArguments(value),"Use positional FEM arguments and final typed options.");const auto count=value->children.size();require(name=="femplot"?count==1:count==2||count==3,"Use femsolve(mesh,@forcing[,options]) or femplot(solution).");if(!count)return Type::Invalid;
        require(value->children[0]->type==Type::Record,"FEM requires a versioned mesh/solution record.");if(name=="femplot")return valid?Type::Void:Type::Invalid;
        if(count<2)return Type::Invalid;
        require(value->children[1]->type==Type::Function,"FEM forcing requires a declared function compiled to native code.");if(count>2)require(value->children[2]->type==Type::Record,"FEM settings require a typed options record.");
        auto schema=std::make_shared<ValueSchema>();auto fields=[&](std::initializer_list<const char*> names,Type type){for(const auto* field:names)schema->fields.push_back({field,type,{}});};
        fields({"format","backend","thread_policy","status","forcing_cache_key","diffusion_cache_key","reaction_cache_key","dirichlet_cache_key","exact_cache_key","source_hash","visualization_semantics"},Type::String);
        fields({"dof_coordinates","dof_values","visualization_vertices","visualization_triangles","visualization_values"},Type::RealMatrix);
        fields({"order","dofs","converged","iterations","evaluations","native_work","estimated_bytes","has_exact","threads_used","cache_hit","mean_constraint","nullspace_components","threads_requested","threads_granted","parallel_calls"},Type::Integer);
        fields({"residual_norm","relative_residual","l2_error","min_sampled_diffusion","min_sampled_reaction","compatibility_residual","mean_error","target_mean"},Type::Real);schema->fields.push_back({"mesh",Type::Record,value->children[0]->schema});value->schema=std::move(schema);return valid?Type::Record:Type::Invalid;
    }
