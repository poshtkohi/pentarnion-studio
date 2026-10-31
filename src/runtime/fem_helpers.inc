    struct FemCallback {
        std::shared_ptr<ExtraPreparedCallback> prepared;
        static int value(void* raw,double x,double y,double* output,char* error,uint64_t capacity)noexcept{
            auto& self=*static_cast<FemCallback*>(raw);try{const double point[]{x,y};self.prepared->values(point,2,output,1);return 0;}
            catch(const std::exception& failure){self.prepared->remember();ExtraPreparedCallback::errorText(error,capacity,failure.what());return 1;}
            catch(...){self.prepared->remember();return 1;}
        }
    };
    std::optional<Value> femCall(const Expr& expression,const std::vector<Value>& arguments){
        if(auto value=femMixedCall(expression,arguments))return value;
        if(auto value=femTimeDataCall(expression,arguments))return value;
        if(auto value=femMeshAdaptCall(expression,arguments))return value;
        if(auto value=femAdaptiveCall(expression,arguments))return value;
        if(auto value=femSystemCall(expression,arguments))return value;
        const auto& name=expression.text;const auto span=expression.span;if(name!="femsolve"&&name!="femplot")return {};
        if(name=="femplot"){
            if(arguments.size()!=1)fail("R2215","Use femplot(solution).",span);
            const auto* fields=recordFields(arguments[0]);if(!fields||!fields->contains("format")||extraText(fields->at("format"),span)!="PentarnionFEM-v1")fail("R2215","femplot requires a versioned native FEM solution.",span);
            for(const auto* key:{"visualization_vertices","visualization_triangles","visualization_values"})if(!fields->contains(key))fail("R2215","Incomplete FEM visualization snapshot.",span);
            const auto points=numericArray(fields->at("visualization_vertices"),span),faces=numericArray(fields->at("visualization_triangles"),span),values=numericArray(fields->at("visualization_values"),span);
            if(points.components!=1||points.columns!=2||faces.components!=1||faces.columns!=3||values.components!=1||values.values.size()!=points.rows)fail("R2215","Invalid FEM visualization arrays.",span);
            PlotMesh plot;plot.label="MFEM sampled scalar field (full high-order DOFs retained)";
            for(std::size_t v=0;v<points.rows;++v)plot.points.push_back({points.values[2*v],points.values[2*v+1],values.values[v]});
            for(std::size_t f=0;f<faces.rows;++f){std::array<uint32_t,3> triangle{};for(unsigned j=0;j<3;++j){const auto id=faces.values[3*f+j];if(id<1||id>points.rows||id!=std::floor(id))fail("R2215","Invalid FEM visualization face index.",span);triangle[j]=static_cast<uint32_t>(id-1);}plot.triangles.push_back(triangle);}
            plot.scalars=values.values;reservePlotPoints(plot.points.size(),span);auto action=figureAction(FigureActionKind::Plot,span);action.dimensions=3;action.axes={-1,-1,-1};action.mesh=std::move(plot);result_.figureActions.push_back(std::move(action));return I(0);
        }
        const auto* fields=extraOptions(arguments,2,{"order","visualization_refinement","diffusion","reaction","dirichlet","dirichlet_tags","exact","parameters","rtol","atol","max_iterations","max_work","max_bytes","max_seconds","mean","compatibility_tolerance","threads"},span);
        const auto mesh=extraMeshInput(arguments[0],span);if(mesh.dimension!=2||mesh.coordinateKind)fail("R2215","Initial MFEM support requires a real R2 triangle mesh; Pentarnion-valued/embedded-surface elliptic FEM is not implemented.",span);
        if(options_.maxNativeOperations==0)fail("R1005","FEM is disabled by the execution's native operation allowance.",span);
        const auto requestedWork=static_cast<uint64_t>(extraInt(fields,"max_work",static_cast<I>(std::min<std::size_t>(options_.maxNativeOperations,100000000)),1,1000000000,span));
        if(requestedWork>options_.maxNativeOperations)fail("R1005","FEM max_work exceeds the execution's native operation allowance.",span);
        auto budget=std::make_shared<ExtraGeometryBudget>(requestedWork);
        penta_fem_request_v1 request{};request.order=static_cast<uint32_t>(extraInt(fields,"order",1,1,4,span));request.visualization_refinement=static_cast<uint32_t>(extraInt(fields,"visualization_refinement",std::max<I>(4,request.order),1,16,span));request.vertices=mesh.points.size()/2;request.triangles=mesh.faces.size()/3;request.boundaries=mesh.boundaries.size()/3;request.points=mesh.points.data();request.faces=mesh.faces.data();request.boundary_tags=mesh.boundaries.data();request.dirichlet_count=UINT64_MAX;
        request.relative_tolerance=extraReal(fields,"rtol",1e-10,span);request.absolute_tolerance=extraReal(fields,"atol",1e-14,span);request.max_seconds=extraReal(fields,"max_seconds",30,span);request.max_iterations=static_cast<uint64_t>(extraInt(fields,"max_iterations",10000,1,100000,span));request.max_work=budget->limit;request.max_bytes=static_cast<uint64_t>(extraInt(fields,"max_bytes",256*1024*1024,65536,1024ll*1024*1024,span));request.diffusion_constant=1;
        std::vector<uint64_t> tags;if(const auto* selected=extraOption(fields,"dirichlet_tags")){for(const auto tag:numericVector(*selected,span,true)){if(tag<1||tag>1000000||tag!=std::floor(tag))fail("R2215","Dirichlet tag selection requires positive integer tag IDs.",span);tags.push_back(static_cast<uint64_t>(tag));}request.dirichlet_count=tags.size();request.dirichlet_tags=tags.data();}
        if(extraOption(fields,"mean")&&!extraOption(fields,"dirichlet_tags"))request.dirichlet_count=0;
        if(extraOption(fields,"compatibility_tolerance")&&!extraOption(fields,"mean"))fail("R2215","compatibility_tolerance applies to an explicit mean constraint.",span);
        std::array<FemCallback,5> callbacks;std::set<std::string> used;const auto* parameters=extraOption(fields,"parameters");const auto* fixed=parameters?recordFields(*parameters):nullptr;if(parameters&&!fixed)fail("R2215","FEM runtime parameters require a typed record.",span);
        auto prepare=[&](const Value& value,unsigned index){
            const auto* function=std::get_if<FunctionValue>(&value);if(!function||!function->definition)fail("R2215","FEM field requires a declared pure native real function.",span);
            ExtraGeometryFields filtered;for(std::size_t n=2;n<function->definition->parameters.size();++n){const auto& parameter=function->definition->parameters[n];if(fixed){const auto found=fixed->find(parameter.name);if(found!=fixed->end()){filtered.emplace(*found);used.insert(parameter.name);}}}
            Value filteredValue=makeRecord(std::move(filtered),span);callbacks[index].prepared=extraPrepare(value,{Type::Real,Type::Real},&filteredValue,budget,span);const auto& cb=*callbacks[index].prepared->callback;if(cb.outputCount()!=1||cb.outputTypes().size()!=1||cb.outputTypes().front()!=Type::Real)fail("R2215","FEM callbacks must return one real scalar.",span);
        };
        prepare(arguments[1],0);request.forcing=FemCallback::value;request.forcing_context=&callbacks[0];
        auto optional=[&](const char* key,unsigned index,penta_fem_field_v1& callback,void*& context,double& constant){if(const auto* value=extraOption(fields,key)){if(std::holds_alternative<FunctionValue>(*value)){prepare(*value,index);callback=FemCallback::value;context=&callbacks[index];}else constant=finite(real(*value,span),span);}};
        optional("diffusion",1,request.diffusion,request.diffusion_context,request.diffusion_constant);optional("reaction",2,request.reaction,request.reaction_context,request.reaction_constant);optional("dirichlet",3,request.dirichlet,request.dirichlet_context,request.dirichlet_constant);
        if(const auto* exact=extraOption(fields,"exact")){prepare(*exact,4);request.exact=FemCallback::value;request.exact_context=&callbacks[4];}
        if(fixed&&used.size()!=fixed->size())fail("R2215","Unknown FEM runtime parameter; it is not a trailing argument of any supplied native field.",span);
        const uint64_t n=request.visualization_refinement,p=request.order,dofs=request.vertices+3*request.triangles*(p-1)+(p>=3?request.triangles*(p-1)*(p-2)/2:0),vertices=request.triangles*(n+1)*(n+2)/2,faces=request.triangles*n*n;
        if(2*dofs>options_.maxMatrixCoordinates||2*vertices>options_.maxMatrixCoordinates||3*faces>options_.maxMatrixCoordinates)fail("R2215","FEM output exceeds Studio's matrix coordinate allowance; reduce mesh/order/visualization refinement.",span);
        fem::Result output;
        try{fem::SolverOptions settings;settings.threads=static_cast<std::size_t>(extraInt(fields,"threads",static_cast<I>(parallel_.settings.threads),0,64,span));settings.meanConstraint=extraOption(fields,"mean")!=nullptr;settings.targetMean=extraReal(fields,"mean",0,span);settings.compatibilityTolerance=extraReal(fields,"compatibility_tolerance",1e-10,span);output=fem::Module(options_.numericModuleDirectory).solveConfigured(request,settings,options_.cancellation,extraCharge(budget,span));extraParallelStats(output.statistics);}
        catch(...){for(const auto& callback:callbacks)if(callback.prepared)callback.prepared->rethrow();try{throw;}catch(const fem::Error& failure){fail(failure.code==3?"R1006":failure.code==7?"R1005":"R2215",failure.what(),span);}}
        const auto& r=output.report;std::vector<double> triangles(output.visualizationFaces.begin(),output.visualizationFaces.end());std::array<std::string,5> keys{};bool cacheHit=true;for(std::size_t n=0;n<callbacks.size();++n)if(callbacks[n].prepared){keys[n]=callbacks[n].prepared->callback->key();cacheHit=cacheHit&&callbacks[n].prepared->callback->cacheHit();}
        return makeRecord({{"format",std::string("PentarnionFEM-v1")},{"mesh",arguments[0]},{"dof_coordinates",numericResult(output.coordinates,r.dofs,2,1,span)},{"dof_values",numericResult(output.values,r.dofs,1,1,span)},{"visualization_vertices",numericResult(output.visualizationCoordinates,r.visualization_vertices,2,1,span)},{"visualization_triangles",numericResult(triangles,r.visualization_triangles,3,1,span)},{"visualization_values",numericResult(output.visualizationValues,r.visualization_vertices,1,1,span)},{"order",I(p)},{"dofs",I(r.dofs)},{"converged",I(r.converged)},{"iterations",I(r.iterations)},{"evaluations",I(r.evaluations)},{"native_work",I(budget->used.load())},{"estimated_bytes",I(r.estimated_bytes)},{"residual_norm",r.residual_norm},{"relative_residual",r.relative_residual},{"l2_error",r.l2_error},{"has_exact",I(r.has_exact)},{"min_sampled_diffusion",r.min_diffusion},{"min_sampled_reaction",r.min_reaction},{"mean_constraint",I(output.meanConstraint)},{"nullspace_components",I(output.nullspaceComponents)},{"compatibility_residual",output.compatibilityResidual},{"mean_error",output.meanError},{"target_mean",extraReal(fields,"mean",0,span)},{"backend",std::string(output.threadsGranted>1?"MFEM4.10_H1_shared_parallel_Jacobi_CG":"MFEM4.10_serial_H1_CG")},{"threads_requested",I(output.statistics.requestedThreads)},{"threads_granted",I(output.threadsGranted)},{"threads_used",I(output.statistics.threadsUsed)},{"parallel_calls",I(output.statistics.parallelCalls)},{"thread_policy",std::string(output.threadsGranted>1?"serialized_MFEM_assembly_shared_parallel_CSR_CG":"shared_cpu_reservation_serialized_MFEM")},{"status",std::string(r.converged?"converged":"iteration_limit_or_residual_not_satisfied")},{"forcing_cache_key",keys[0]},{"diffusion_cache_key",keys[1]},{"reaction_cache_key",keys[2]},{"dirichlet_cache_key",keys[3]},{"exact_cache_key",keys[4]},{"cache_hit",I(cacheHit)},{"source_hash",result_.sourceHash},{"visualization_semantics",std::string("independent_sampled_triangles_full_H1_dofs_retained")}},span);
    }

#include "fem_system_helpers.inc"

#include "fem_adaptive_helpers.inc"

#include "fem_mesh_refinement_helpers.inc"

#include "fem_time_helpers.inc"
#include "fem_mixed_helpers.inc"
