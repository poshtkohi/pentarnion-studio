    std::optional<Value> discoveryWorkflow(const Expr& expression,const std::vector<Value>& args){
        const auto& name=expression.text;if(name!="basins"&&name!="lyapunovscan")return {};
        const auto span=expression.span;auto allowed=discoveryOptionNames();
        if(name=="basins")for(const auto* key:{"tail_samples","tail_fraction","cluster_tolerance","separation_tolerance","stationarity_tolerance","maximum_attractors"})allowed.insert(key);
        else for(const auto* key:{"horizons","tightening","minimum_observation","qr_step","max_intervals","lyapunov_tolerance","minimum_positive","compare_method","verification_checkpoint","verification_resume","verification_overwrite"})allowed.insert(key);
        const auto* fields=discoveryFields(args,2,allowed,span);DiscoveryFields screenOptions;
        if(fields)for(const auto& [key,value]:*fields)if(discoveryOptionNames().contains(key))screenOptions.emplace(key,value);
        const auto screenSettings=makeRecord(std::move(screenOptions),span);
        const auto forecast=discoveryInput({args[0],args[1],screenSettings},recordFields(screenSettings),false,span);
        if(forecast.options.runs>256)fail("R2053","Native discovery workflows accept at most256 catalogue rows.",span);
        if(name=="lyapunovscan"&&forecast.options.solver.method!=ode::Method::Bdf&&forecast.options.solver.method!=ode::Method::Adams)fail("R2053","Native Lyapunov workflows require bdf/adams variational integration.",span);
        if(name=="basins")for(std::size_t row=1;row<forecast.options.runs;++row){if(forecast.options.gamma[row]!=forecast.options.gamma[0])fail("R2053","Basin labels require fixed gamma.",span);for(std::size_t k=0;k<forecast.parameterCount;++k)if(forecast.options.parameters[row*forecast.parameterCount+k]!=forecast.options.parameters[k])fail("R2053","Basin labels require fixed model parameters.",span);}
        Expr screen=expression;screen.text="discover";const auto experiment=*discoveryCall(screen,{args[0],args[1],screenSettings});
        const auto* source=recordFields(experiment);const auto runs=static_cast<std::size_t>(integer(source->at("runs"),span)),n=static_cast<std::size_t>(integer(source->at("coordinates"),span)),physical=static_cast<std::size_t>(integer(source->at("physical_coordinates"),span));
        if(name=="lyapunovscan"){
            if(runs>256)fail("R2053","lyapunovscan accepts at most256 catalogue rows.",span);
            DiscoveryFields verifyOptions{{"lyapunov",I(1)}};if(fields)for(const auto* key:{"horizons","tightening","minimum_observation","qr_step","max_intervals","lyapunov_tolerance","minimum_positive","compare_method","threads","max_wall_seconds","max_native_operations","maximum_bytes"})if(const auto found=fields->find(key);found!=fields->end())verifyOptions.emplace(*found);
            if(fields)for(const auto& [from,to]:std::initializer_list<std::pair<const char*,const char*>>{{"verification_checkpoint","checkpoint"},{"verification_resume","resume"},{"verification_overwrite","overwrite_checkpoint"}})if(const auto found=fields->find(from);found!=fields->end())verifyOptions.emplace(to,found->second);
            std::vector<double> ids;const auto codes=numericVector(source->at("status_codes"),span,false);for(std::size_t row=0;row<runs;++row)if(codes[row]>=1&&codes[row]<=8)ids.push_back(static_cast<double>(row+1));
            if(ids.empty())fail("R2053","No completed discovery rows are available for Lyapunov verification.",span);
            Expr verify=expression;verify.text="verifyexperiment";const auto value=*verificationCall(verify,{args[0],experiment,numericResult(ids,1,ids.size(),1,span),makeRecord(std::move(verifyOptions),span)});
            DiscoveryFields output{{"verification",value},{"parameters",source->at("parameters")},{"initials",source->at("initials")},{"catalogue_runs",I(runs)},{"workflow",std::string("native_parameter_lyapunov_scan")}};
            return makeRecord(std::move(output),span);
        }
        const auto count=static_cast<std::size_t>(chaosInteger(fields,"tail_samples",64,8,256,span)),maximum=static_cast<std::size_t>(chaosInteger(fields,"maximum_attractors",16,1,256,span));
        const double fraction=chaosNumber(fields,"tail_fraction",.5,span),tolerance=chaosNumber(fields,"cluster_tolerance",.1,span),separation=chaosNumber(fields,"separation_tolerance",3*tolerance,span),stationarity=chaosNumber(fields,"stationarity_tolerance",tolerance,span);
        if(runs>256||physical>32||!std::isfinite(fraction)||fraction<=0||fraction>1||!std::isfinite(stationarity)||stationarity<=0||!std::isfinite(tolerance)||tolerance<=0||!std::isfinite(separation)||separation<=tolerance||static_cast<long double>(runs)*maximum*count*count*physical>100000000)fail("R2053","Basin tail/work limits exceeded or invalid clustering thresholds.",span);
        // A basin catalogue compares one fixed dynamical system. Sweeps belong
        // to lyapunovscan/discover, never to a mixed-parameter basin fraction.
        const auto parameters=numericArray(source->at("parameters"),span);const auto gammas=numericVector(source->at("gammas"),span,false);const auto np=static_cast<std::size_t>(integer(source->at("parameter_count"),span));
        for(std::size_t row=1;row<runs;++row){if(gammas[row]!=gammas[0])fail("R2053","Basin labels require a single fixed gamma.",span);for(std::size_t k=0;k<np;++k)if(parameters.values[row*np+k]!=parameters.values[k])fail("R2053","Basin labels require fixed model parameters; use initial-condition rows only.",span);}
        discovery::basin::Catalogue catalogue;catalogue.tolerance=tolerance;catalogue.separation=separation;catalogue.maximum=maximum;
        const auto codes=numericVector(source->at("status_codes"),span,false);std::vector<double> labels(runs,0),resolved(runs,0);std::vector<std::size_t> representatives;std::size_t escaped=0,failed=0,pending=0;std::string messages;
        for(std::size_t row=0;row<runs;++row){
            if(options_.cancellation&&options_.cancellation->load())fail("R1006","Basin labelling cancelled; discovery checkpoints were retained.",span);
            if(codes[row]==5){labels[row]=-1;++escaped;continue;}if(codes[row]==6||codes[row]==7){labels[row]=-2;++failed;continue;}if(codes[row]==0||codes[row]==9){labels[row]=-3;++pending;continue;}
            try{Expr replay=expression;replay.text="experimentrun";const auto value=discoveryReplay(replay,{args[0],experiment,I(row+1)});const auto* trajectory=recordFields(value);const auto values=numericArray(trajectory->at("values"),span);const bool equilibrium=codes[row]==2;const auto begin=equilibrium?values.rows-1:static_cast<std::size_t>(std::floor((1-fraction)*values.rows));auto tail=discovery::basin::sampledTail(values.values,n,physical,std::min(begin,values.rows-1),count,equilibrium);
                if(!equilibrium&&!discovery::basin::stationary(tail,stationarity))continue;
                const auto before=catalogue.representatives.size();const int label=catalogue.classify(tail);labels[row]=label;resolved[row]=label>0;if(catalogue.representatives.size()>before)representatives.push_back(row+1);
                chargeNumericWork(std::max<std::size_t>(1,catalogue.representatives.size()*tail.samples.size()*count),span);
            }catch(const Failure& error){if(error.code=="R1006"||error.code=="R1005")throw;labels[row]=-2;++failed;messages+="Run "+std::to_string(row+1)+": "+error.what()+"\n";}
        }
        std::vector<double> ids,fractions,conditional,counts,referenceIds;std::size_t totalResolved=0;for(double flag:resolved)totalResolved+=flag>0;
        for(std::size_t k=0;k<catalogue.representatives.size();++k){const auto occurrences=static_cast<std::size_t>(std::count(labels.begin(),labels.end(),static_cast<double>(k+1)));ids.push_back(double(k+1));counts.push_back(double(occurrences));fractions.push_back(double(occurrences)/runs);conditional.push_back(totalResolved?double(occurrences)/totalResolved:0);referenceIds.push_back(double(representatives[k]));}
        auto column=[&](const std::vector<double>& values){return numericResult(values,values.size(),1,1,span);};
        return makeRecord({{"format",std::string("PentarnionBasinCandidates")},{"source_experiment",experiment},{"initials",source->at("initials")},{"labels",column(labels)},{"resolved",column(resolved)},{"attractor_ids",column(ids)},{"representative_run_ids",column(referenceIds)},{"counts",column(counts)},{"fractions",column(fractions)},{"conditional_fractions",column(conditional)},{"runs",I(runs)},{"attractors",I(ids.size())},{"unresolved",I(runs-totalResolved-escaped-failed-pending)},{"escaped",I(escaped)},{"failures",I(failed)},{"pending",I(pending)},{"resolved_count",I(totalResolved)},{"unresolved_fraction",double(runs-totalResolved-escaped-failed-pending)/runs},{"escape_fraction",double(escaped)/runs},{"failure_fraction",double(failed)/runs},{"pending_fraction",double(pending)/runs},{"messages",messages},{"notice",std::string("Finite-tail candidate labels;0=unresolved,-1=escape,-2=failure,-3=pending. Fractions use every initial row; conditional_fractions explicitly condition on resolved candidate labels. No attractor existence/global basin measure is certified.")}},span);
    }
