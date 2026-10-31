    struct PdeNativeWorkQuota {
        std::atomic_uint64_t used{0};std::atomic_bool exhausted{false};uint64_t maximum=0;
        void reserve(std::size_t work,SourceSpan span){
            auto current=used.load(std::memory_order_relaxed);
            for(;;){
                if(current>maximum||work>maximum-current){exhausted.store(true,std::memory_order_relaxed);fail("R1005","PDE native callback/assembly work budget exceeded.",span);}
                if(used.compare_exchange_weak(current,current+work,std::memory_order_relaxed))return;
            }
        }
    };
#include "pde_stiff_helpers.inc"
#include "pde_spatial_helpers.inc"
#include "petsc_helpers.inc"
#include "pde_grid_helpers.inc"
    std::optional<Value> pdeCall(const Expr& expression,const std::vector<Value>& arguments){
        const auto span=expression.span;
        if(auto value=petscCall(expression,arguments))return value;
        if(auto value=pdeSpatialCall(expression,arguments))return value;
        if(auto value=pdeStiffCall(expression,arguments))return value;
        if(auto value=pdeGridCall(expression,arguments))return value;
        if(expression.text=="pdecoeff"){
            if(arguments.size()!=2)fail("R2100","Use pdecoeff(field_record,coefficient_index).",span);
            const auto* f=recordFields(arguments[0]);if(!f||!f->contains("values"))fail("R2100","pdecoeff requires a PDE result.",span);
            const auto a=numericArray(f->at("values"),span);const auto j=integer(arguments[1],span);std::size_t components=a.components,columns=a.columns;
            if(const auto layout=f->find("value_layout");layout!=f->end()){const auto* text=std::get_if<std::string>(&layout->second);if(!text||*text!="node_coefficients"||!f->contains("components"))fail("R2100","Unsupported raw PDE coefficient layout.",span);const auto c=integer(f->at("components"),span);if((c!=1&&c!=5)||a.components!=1||a.columns%static_cast<std::size_t>(c))fail("R2100","Invalid raw PDE coefficient shape.",span);components=static_cast<std::size_t>(c);columns/=components;}
            if(j<0||j>=static_cast<I>(components))fail("R2100","Coefficient index is zero based (0..4 for Pentarnions, 0 for reals).",span);
            std::vector<double> out(a.rows*columns);for(std::size_t i=0;i<out.size();++i)out[i]=a.values[i*components+j];return numericResult(out,a.rows,columns,1,span);
        }
        if(expression.text!="pdesolve")return {};
        if(arguments.size()<3||arguments.size()>4)fail("R2100","Use pdesolve(reaction,x,initial[,options]).",span);
        const auto* fields=arguments.size()==4?recordFields(arguments[3]):nullptr;if(arguments.size()==4&&!fields)fail("R2100","PDE options require a record.",span);
        const std::set<std::string> allowed{"parameters","method","boundary","length","diffusion","t0","t1","time_step","sample_step","left","right","max_steps","max_reaction_evaluations","max_native_operations","max_wall_seconds","threads","integrator"};
        if(fields)for(const auto& [key,value]:*fields){(void)value;if(!allowed.contains(key))fail("R2100","Unknown PDE option: "+key,span);}
        auto option=[&](const char* key)->const Value*{if(!fields)return nullptr;const auto it=fields->find(key);return it==fields->end()?nullptr:&it->second;};
        auto number=[&](const char* key,double fallback){return option(key)?real(*option(key),span):fallback;};
        auto text=[&](const char* key,std::string fallback){if(!option(key))return fallback;const auto* s=std::get_if<std::string>(option(key));if(!s)fail("R2100",std::string(key)+" must be a string.",span);return *s;};
        auto count=[&](const char* key,I fallback,I maximum){const auto n=option(key)?integer(*option(key),span):fallback;if(n<1||n>maximum)fail("R2100",std::string(key)+" exceeds its positive resource bound.",span);return static_cast<uint64_t>(n);};
        const auto threads=option("threads")?integer(*option("threads"),span):static_cast<I>(parallel_.settings.threads);
        if(threads<0||threads>64)fail("R2100","PDE threads must be 0 (automatic) or an Integer in1..64.",span);
        const auto x=numericVector(arguments[1],span,false);const auto initial=numericArray(arguments[2],span);const auto n=x.size(),c=initial.components;
        if(n<3||n>4096||initial.rows*initial.columns!=n||(initial.rows!=1&&initial.columns!=1))fail("R2100","PDE initial field must be a real/Pentarnion vector matching 3..4096 coordinates.",span);
        const auto* function=std::get_if<FunctionValue>(&arguments[0]);if(!function||!function->definition||function->definition->parameters.size()<3)fail("R2100","Reaction requires a native-compatible function (state,time,space[,parameters]).",span);
        const auto& definition=*function->definition;const auto stateType=c==5?Type::Pentarnion:Type::Real;
        if(definition.parameters[0].type!=stateType||definition.parameters[1].type!=Type::Real||definition.parameters[2].type!=Type::Real||definition.result!=stateType)fail("R2100","Reaction must return the state type and begin (real/Pentarnion state,real time,real space).",span);
        const Value initialPoint=numericResult(std::vector<double>(initial.values.begin(),initial.values.begin()+static_cast<std::ptrdiff_t>(c)),1,1,c,span,true);
        const auto input=derivativeInput(arguments[0],makeTuple({initialPoint,0.0,0.0},span),fields,span);
        penta_pde_time_request_v1 timed{};auto& r=timed.base;r.struct_size=sizeof(timed);r.nodes=n;r.components=c;
        const auto integrator=text("integrator","rk4");if(integrator!="rk4"&&integrator!="imex_euler")fail("R2100","PDE integrator must be rk4 or imex_euler (explicit reaction, implicit diffusion).",span);timed.integrator=integrator=="imex_euler"?PENTA_PDE_IMEX_EULER_V1:PENTA_PDE_RK4_V1;
        const auto method=text("method","fd"),boundary=text("boundary","periodic");if(method!="fd"&&method!="fem")fail("R2100","PDE method must be fd or fem (linear P1 consistent mass).",span);r.method=method=="fem";
        if(boundary!="periodic"&&boundary!="dirichlet"&&boundary!="neumann")fail("R2100","Boundary must be periodic, constant Dirichlet or homogeneous Neumann.",span);
        r.boundary=boundary=="periodic"?0:boundary=="dirichlet"?1:2;
        r.length=number("length",x.back()-x.front()+(boundary=="periodic"?x[1]-x[0]:0));r.diffusion=number("diffusion",0.1);r.t0=number("t0",0);r.t1=number("t1",1);r.time_step=number("time_step",0.001);r.sample_step=number("sample_step",0.05);r.max_wall_seconds=number("max_wall_seconds",120);r.max_steps=count("max_steps",1000000,10000000);r.max_reaction_evaluations=count("max_reaction_evaluations",20000000,1000000000);r.x=x.data();r.initial=initial.values.data();
        const double samples=std::ceil((r.t1-r.t0)/r.sample_step)+2;if(!std::isfinite(samples)||samples<2||samples*n>std::min<std::size_t>(1000000,options_.maxMatrixElements)||samples*n*c>std::min<std::size_t>(8000000,options_.maxMatrixCoordinates))fail("R2100","PDE sample/field output exceeds the configured array budget.",span);
        if(boundary!="dirichlet"&&(option("left")||option("right")))fail("R2100","left/right apply only to constant Dirichlet boundaries; Neumann currently means zero flux.",span);
        if(boundary!="periodic"&&option("length")){const long double width=static_cast<long double>(x.back())-x.front();if(std::abs(static_cast<long double>(r.length)-width)>1e-12L*std::max(1.0L,std::abs(width)))fail("R2100","For nonperiodic fields, length must match the explicit coordinate span.",span);}
        std::vector<double> left(c,0),right(c,0);auto boundaryValue=[&](const char* key,std::vector<double>& out){if(option(key)){const Value v=convert(*option(key),stateType,span);const auto a=numericArray(v,span);out=a.values;}};boundaryValue("left",left);boundaryValue("right",right);r.left=left.data();r.right=right.data();
        try{
            const auto maximumNative=option("max_native_operations")?count("max_native_operations",1000000000,static_cast<I>(std::min<uint64_t>(options_.maxNativeOperations,1000000000000ULL))):options_.maxNativeOperations;
            auto quota=std::make_shared<PdeNativeWorkQuota>();quota->maximum=maximumNative;
            // Native workers reserve only this independent atomic quota. They
            // never mutate the interpreter operation counter; RHS charges below
            // are performed by the module's owner thread before dispatch/join.
            auto chargeNative=[quota,span](std::size_t work){quota->reserve(work,span);};
            auto nativeOptions=derivativeNativeOptions(span);nativeOptions.chargeOperations=chargeNative;
            native_functions::Callback callback(definition,nativeOptions);
            struct Bridge{const native_functions::Callback* callback;double gamma;std::size_t c;std::vector<double> input;std::exception_ptr failure;std::mutex failureMutex{};
                static int evaluate(void* raw,double t,double x,const double* u,double* out,char* error,uint64_t cap)noexcept{auto& b=*static_cast<Bridge*>(raw);try{
                    // One reusable buffer per worker, never the shared bindings.
                    static thread_local std::vector<double> local;local=b.input;
                    std::copy_n(u,b.c,local.begin());local[b.c]=t;local[b.c+1]=x;
                    b.callback->evaluateValues(b.gamma,local,std::span<double>(out,b.c));return 0;
                }catch(const std::exception& e){std::lock_guard lock(b.failureMutex);if(!b.failure)b.failure=std::current_exception();if(error&&cap){const auto s=std::string(e.what()).substr(0,cap-1);std::copy(s.begin(),s.end(),error);error[s.size()]=0;}return 1;}catch(...){std::lock_guard lock(b.failureMutex);if(!b.failure)b.failure=std::current_exception();return 1;}}
            } bridge{&callback,context_.gamma(),c,input.flat,{}};r.reaction_context=&bridge;r.reaction=Bridge::evaluate;
            pde::Result result;try{result=pde::Module(options_.numericModuleDirectory).solve(r,options_.cancellation,[&](std::size_t work){chargeNative(work);chargeNumericWork(1,span);},static_cast<std::size_t>(threads));}catch(...){if(quota->exhausted.load(std::memory_order_relaxed))fail("R1005","PDE native callback/assembly work budget exceeded.",span);if(bridge.failure)std::rethrow_exception(bridge.failure);throw;}
            const auto nativeWork=quota->used.load(std::memory_order_relaxed);
            const auto& o=result.report;return makeRecord({{"x",numericResult(x,1,n,1,span)},{"t",numericResult(result.times,o.samples,1,1,span)},{"values",numericResult(result.values,o.samples,n,c,span)},{"mean",numericResult(result.means,o.samples,1,c,span)},{"spatial_rms",numericResult(result.rms,o.samples,1,1,span)},{"steps",I(o.steps)},{"reaction_evaluations",I(o.reaction_evaluations)},{"native_operations",I(nativeWork)},{"status",std::string(o.status)},{"message",std::string(o.message)},{"method",method},{"boundary",boundary},{"diffusion",r.diffusion},{"elapsed_seconds",o.elapsed_seconds},{"cache_hit",I(callback.cacheHit())},{"cache_key",callback.key()},{"threads_requested",I(threads)},{"threads_granted",I(result.threadsGranted)},{"threads_used",I(result.statistics.threadsUsed)},{"peak_active_threads",I(result.statistics.peakActiveThreads)},{"parallel_calls",I(result.statistics.parallelCalls)},{"integrator",integrator},{"linear_solves",I(result.linearSolves)},{"factorizations",I(result.factorizations)},{"dimension",I(1)},{"nx",I(n)},{"ny",I(1)}},span);
        }catch(const pde::Error& e){fail(e.status==3?"R1006":"R2100",e.what(),span);}catch(const ode::OdeError& e){fail(options_.cancellation&&options_.cancellation->load(std::memory_order_relaxed)?"R1006":"R2100",e.what(),span);}
    }
