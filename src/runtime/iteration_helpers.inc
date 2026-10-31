    bool inIterationObserver_=false;
    void requireOutsideIterationObserver(SourceSpan span) {
        if(inIterationObserver_)fail("R2050","An OutputFcn cannot recursively start a fitting,optimization,evolutionary or root solver. Plot or inspect the supplied iterate instead.",span);
    }
    Value iterationInformation(const penta_iteration_snapshot_v1* snapshot,SourceSpan span,const std::vector<double>& history={},std::size_t dropped=0) {
        const auto flags=snapshot?snapshot->flags:0u;
        const bool objective=(flags&PENTA_ITERATION_HAS_OBJECTIVE)!=0,residual=(flags&PENTA_ITERATION_HAS_RESIDUAL_NORM)!=0,step=(flags&PENTA_ITERATION_HAS_STEP_NORM)!=0;
        const std::string event=!snapshot?"none":snapshot->event==PENTA_ITERATION_EVENT_ITERATION?"iteration":snapshot->event==PENTA_ITERATION_EVENT_GENERATION?"generation":"evaluation";
        const std::string state=!snapshot||snapshot->phase==PENTA_ITERATION_INIT?"init":snapshot->phase==PENTA_ITERATION_DONE?"done":"iter";
        auto boundedInteger=[&](uint64_t value){if(value>static_cast<uint64_t>(std::numeric_limits<I>::max()))fail("R2050","Iteration counter exceeds the signed language range.",span);return static_cast<I>(value);};
        return makeRecord({{"iteration",snapshot?boundedInteger(snapshot->iteration):I(0)},{"funccount",snapshot?boundedInteger(snapshot->evaluations):I(0)},{"start_index",snapshot?boundedInteger(snapshot->start_index):I(0)},{"dimension",snapshot?boundedInteger(snapshot->dimension):I(0)},
            {"iteration_available",I(snapshot&&snapshot->event!=PENTA_ITERATION_EVENT_EVALUATION)},{"objective_available",I(objective)},{"residual_available",I(residual)},{"step_available",I(step)},{"accepted",I((flags&PENTA_ITERATION_ACCEPTED)!=0)},
            {"objective",objective?finite(snapshot->objective,span):0.0},{"residual_norm",residual?finite(snapshot->residual_norm,span):0.0},{"step_norm",step?finite(snapshot->step_norm,span):0.0},{"elapsed_seconds",snapshot?finite(snapshot->elapsed_seconds,span):0.0},
            {"event",event},{"state",state},{"history",numericResult(history,history.size()/6,6,1,span)},{"history_dropped",I(dropped)},{"history_columns",std::string("iteration,funccount,objective,residual_norm,step_norm,elapsed_seconds")}},span);
    }
    std::optional<Value> iterationCall(const Expr& expression,const std::vector<Value>& arguments) {
        if(expression.text!="iterationinfo")return {};
        if(!arguments.empty())fail("R2050","Use iterationinfo() without arguments.",expression.span);
        return iterationInformation(nullptr,expression.span);
    }
    std::function<bool(const penta_iteration_snapshot_v1&)> makeIterationObserver(const std::map<std::string,Value>* fields,SourceSpan span) {
        requireOutsideIterationObserver(span);
        if(!fields)return {};
        const auto found=fields->find("OutputFcn"),frequency=fields->find("OutputEvery");
        I every=1;if(frequency!=fields->end()){every=integer(frequency->second,span);if(every<1||every>1000000)fail("R2050","OutputEvery must be an Integer in1..1000000.",span);}
        if(found==fields->end()){if(frequency!=fields->end())fail("R2050","OutputEvery requires OutputFcn.",span);return {};}
        const auto* callback=std::get_if<FunctionValue>(&found->second);if(!callback||!callback->definition)fail("R2050","OutputFcn must be a typed monitor function.",span);
        const auto& definition=*callback->definition;
        if(definition.parameters.size()!=3||definition.parameters[0].type!=Type::RealMatrix||definition.parameters[1].type!=Type::Record||definition.parameters[2].type!=Type::String||(definition.result!=Type::Integer&&definition.result!=Type::Real))fail("R2050","OutputFcn accepts(matrix x,record optimValues,string state) and returns scalar0/1.",span);
        struct ObserverState {Value callback;uint64_t every=1,events=0,calls=0;std::vector<double> history;std::size_t dropped=0,frameStart=0,frameCount=0,framePoints=0;};
        auto state=std::make_shared<ObserverState>();state->callback=found->second;state->every=static_cast<uint64_t>(every);
        return [this,state,span](const penta_iteration_snapshot_v1& snapshot){
            if(snapshot.struct_size<sizeof(snapshot)||snapshot.abi_version!=PENTA_ITERATION_ABI_V1||snapshot.dimension<1||snapshot.dimension>128||!snapshot.x||snapshot.phase>PENTA_ITERATION_DONE)fail("R2050","The numerical module returned an incompatible OutputFcn snapshot.",span);
            tick(span);
            if(snapshot.phase==PENTA_ITERATION_ITER&&state->events++%state->every)return false;
            if(++state->calls>10000)fail("R2050","OutputFcn exceeded10000 invocations; increase OutputEvery or lower solver budgets.",span);
            const auto historyLimit=std::min<std::size_t>(1000,options_.maxMatrixElements/6);
            if(historyLimit){if(state->history.size()/6>=historyLimit){state->history.erase(state->history.begin(),state->history.begin()+6);++state->dropped;}state->history.insert(state->history.end(),{static_cast<double>(snapshot.iteration),static_cast<double>(snapshot.evaluations),(snapshot.flags&PENTA_ITERATION_HAS_OBJECTIVE)?finite(snapshot.objective,span):0.0,(snapshot.flags&PENTA_ITERATION_HAS_RESIDUAL_NORM)?finite(snapshot.residual_norm,span):0.0,(snapshot.flags&PENTA_ITERATION_HAS_STEP_NORM)?finite(snapshot.step_norm,span):0.0,finite(snapshot.elapsed_seconds,span)});}
            std::vector<FigureAction> previousFrame;
            if(!options_.figureProgress&&snapshot.phase!=PENTA_ITERATION_INIT&&state->frameCount){if(state->frameStart+state->frameCount!=result_.figureActions.size())fail("R2050","Unexpected nested OutputFcn figure frame.",span);previousFrame.assign(std::make_move_iterator(result_.figureActions.begin()+static_cast<std::ptrdiff_t>(state->frameStart)),std::make_move_iterator(result_.figureActions.end()));result_.figureActions.resize(state->frameStart);plotPoints_-=std::min(plotPoints_,state->framePoints);}
            const auto firstAction=result_.figureActions.size(),firstPoint=plotPoints_;
            std::vector<double> coordinates(snapshot.x,snapshot.x+snapshot.dimension);auto point=numericResult(coordinates,1,coordinates.size(),1,span);auto info=iterationInformation(&snapshot,span,state->history,state->dropped);const Value phase=std::string(snapshot.phase==PENTA_ITERATION_INIT?"init":snapshot.phase==PENTA_ITERATION_DONE?"done":"iter");
            struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(inIterationObserver_);
            const auto returned=invokeCallable(state->callback,{std::move(point),std::move(info),phase},span);const double stop=real(returned,span);if(stop!=0&&stop!=1)fail("R2050","OutputFcn must return0(continue) or1(clean user stop).",span);
            if(options_.figureProgress){if(!result_.figureActions.empty()){options_.figureProgress(result_.figureActions);result_.figureActions.clear();plotPoints_=0;}state->frameCount=state->framePoints=0;}
            // INIT establishes persistent figure/subplot/title context. Keep it
            // before the replaceable tail, including any initial plot, so a
            // subsequent plot-only frame replays into the intended figure.
            else if(snapshot.phase==PENTA_ITERATION_INIT){state->frameStart=result_.figureActions.size();state->frameCount=state->framePoints=0;}
            else if(result_.figureActions.size()==firstAction&&!previousFrame.empty()){result_.figureActions.insert(result_.figureActions.end(),std::make_move_iterator(previousFrame.begin()),std::make_move_iterator(previousFrame.end()));plotPoints_+=state->framePoints;}
            else {state->frameStart=firstAction;state->frameCount=result_.figureActions.size()-firstAction;state->framePoints=plotPoints_-firstPoint;}
            return stop!=0;
        };
    }
