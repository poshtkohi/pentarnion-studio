    std::string inferenceObjective(const std::map<std::string,Value>* fields,penta_fit_request_v1& request,SourceSpan span){
        const auto objective=chaosText(fields,"objective","sse",span);request.reserved=0;
        if(objective=="sse"){if(chaosOption(fields,"noise_variance"))fail("R2600","Descriptive SSE does not infer an observation noise model.",span);request.noise_variance=-1;}
        else if(objective=="gaussian_known_variance"||objective=="lognormal_known_variance"||objective=="student_t_known_scale"){
            if(!chaosOption(fields,"noise_variance")||!std::isfinite(request.noise_variance)||request.noise_variance<=0)fail("R2600","Known noise models require explicit positive noise_variance (Student-t scale squared; lognormal log-space variance).",span);
            request.reserved=objective=="lognormal_known_variance"?1:objective=="student_t_known_scale"?2:0;
        }else if(objective=="poisson_deviance"){if(chaosOption(fields,"noise_variance"))fail("R2600","Poisson variance equals its positive model mean; do not supply noise_variance.",span);request.reserved=3;request.noise_variance=-1;}
        else fail("R2600","Objective is sse,gaussian_known_variance,lognormal_known_variance,student_t_known_scale or poisson_deviance.",span);
        if(request.reserved==2){request.loss_scale=chaosNumber(fields,"noise_degrees",4,span);if(request.loss_scale<=0||request.loss_scale>1e6)fail("R2600","Student-t noise_degrees must be in(0,1e6].",span);}
        else if(chaosOption(fields,"noise_degrees"))fail("R2600","noise_degrees applies only to student_t_known_scale.",span);
        return objective;
    }
