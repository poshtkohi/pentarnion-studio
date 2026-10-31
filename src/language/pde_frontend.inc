#include "pde_stiff_frontend.inc"
#include "pde_spatial_frontend.inc"
#include "petsc_frontend.inc"
    std::optional<Type> checkPdeCall(const ExprPtr& value){
        if(auto type=checkPetscCall(value))return type;
        if(auto type=checkPdeSpatialCall(value))return type;
        if(auto type=checkPdeStiffCall(value))return type;
        const auto& name=value->text;if(name!="pdesolve"&&name!="pdecoeff"&&name!="pdesolve2"&&name!="pdeslice")return {};
        bool valid=true;auto require=[&](bool okay,const char* m){if(!okay){add("E2100",m,value->span);valid=false;}};
        require(m_options.languageVersion=="0.2","PDEs require language 0.2.");require(!hasNamedArguments(value),"Use positional PDE arguments and an options record.");
        const auto count=value->children.size();
        if(name=="pdecoeff"){require(count==2,"Use pdecoeff(field,zero_based_coefficient).");if(count==2){require(value->children[0]->type==Type::Record,"pdecoeff requires a PDE result.");require(scalarType(value->children[1]->type),"Coefficient index must be an integer.");}return valid?Type::RealMatrix:Type::Invalid;}
        if(name=="pdeslice"){
            require(count==2||count==3,"Use pdeslice(field,one_based_time_index[,zero_based_coefficient]).");
            if(count<2)return Type::Invalid;
            require(value->children[0]->type==Type::Record,"pdeslice requires a two-dimensional PDE result.");require(scalarType(value->children[1]->type),"Time index must be an integer.");
            if(count==3){require(scalarType(value->children[2]->type),"Coefficient must be an integer.");return valid?Type::RealMatrix:Type::Invalid;}
            const auto schema=value->children[0]->schema;
            if(schema)for(const auto& field:schema->fields)if(field.name=="values")return valid?field.type:Type::Invalid;
            return valid?Type::RealMatrix:Type::Invalid;
        }
        const bool grid=name=="pdesolve2";
        if(grid){
            require(count==4||count==5,"Use pdesolve2(reaction,x,y,initial[,options]).");if(count<4)return Type::Invalid;
            require(value->children[0]->type==Type::Function,"Reaction must be a typed native-compatible function.");
            for(std::size_t i=1;i<=3;++i)require(matrixType(value->children[i]->type)||rangeType(value->children[i]->type),"PDE2 axes/initial data require numeric vectors/matrices.");
            if(count==5)require(value->children[4]->type==Type::Record,"PDE options must be a record.");
        }else{
        require(count==3||count==4,"Use pdesolve(reaction,x,initial[,options]).");if(count<3)return Type::Invalid;
        require(value->children[0]->type==Type::Function,"Reaction must be a typed native-compatible function.");require(matrixType(value->children[1]->type)||rangeType(value->children[1]->type),"PDE coordinates require a real vector.");require(matrixType(value->children[2]->type)||rangeType(value->children[2]->type),"PDE initial field requires a real/Pentarnion vector.");if(count==4)require(value->children[3]->type==Type::Record,"PDE options must be a record.");
        }
        const auto f=value->children[0]->function?value->children[0]->function:value->children[0]->schema?value->children[0]->schema->function:nullptr;const bool pent=f?f->result==Type::Pentarnion:value->children[grid?3:2]->type==Type::PentarnionMatrix;
        if(f){require(f->parameters.size()>=(grid?4u:3u),"Reaction starts with (state,time,x[,y]).");require(f->result==Type::Real||f->result==Type::Pentarnion,"Reaction returns a real/Pentarnion scalar.");}
        auto s=std::make_shared<ValueSchema>();for(const auto field:{"x","t","spatial_rms"})s->fields.push_back({field,Type::RealMatrix,{}});for(const auto field:{"values","mean"})s->fields.push_back({field,pent?Type::PentarnionMatrix:Type::RealMatrix,{}});for(const auto field:{"steps","reaction_evaluations","native_operations","cache_hit","threads_requested","threads_granted","threads_used","peak_active_threads","parallel_calls","linear_solves","factorizations","dimension","nx","ny"})s->fields.push_back({field,Type::Integer,{}});for(const auto field:{"status","message","method","boundary","cache_key","integrator"})s->fields.push_back({field,Type::String,{}});for(const auto field:{"elapsed_seconds"})s->fields.push_back({field,Type::Real,{}});if(grid){for(const auto field:{"y"})s->fields.push_back({field,Type::RealMatrix,{}});for(const auto field:{"diffusion_x","diffusion_y"})s->fields.push_back({field,Type::Real,{}});}else s->fields.push_back({"diffusion",Type::Real,{}});value->schema=std::move(s);return valid?Type::Record:Type::Invalid;
    }
