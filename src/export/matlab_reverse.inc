R"PENTA_REVERSE_M(
function [value,gradient]=targetcost(callback,t,y,parameters,widths)
parameterWidths=ones(size(parameters));for k=1:numel(parameters),if isstruct(parameters{k}),parameterWidths(k)=5;end;end;n=numel(y);[values,J]=targetad(@invoke,[t;y(:);penta_runtime('pack',parameters)],1);if numel(values)~=1,error('Target objective/cost must return one real scalar');end;value=values(1);gradient=J(1,:);
    function result=invoke(point),states=unpackgroups(point(2:1+n),widths);params=unpackgroups(point(2+n:end),parameterWidths);result=callback(point(1),states{:},params{:});end
end
function [covector,parameterGradient,integral,steps,calls]=reverseepoch(fn,parameters,widths,forward,first,last,covector,parameterGradient,running,settings,mass)
n=numel(covector);np=numel(parameterGradient);calls=0;started=tic;seconds=opt(settings,'max_seconds',120);cachedTime=nan;cachedResult={};
configuration=odeset('RelTol',opt(settings,'rtol',1e-7),'AbsTol',opt(settings,'atol',1e-9),'BDF','on','Jacobian',@jacobian);solved=ode15s(@rhs,[last,first],[covector(:);parameterGradient(:);0],configuration);final=solved.y(:,end);covector=final(1:n);parameterGradient=final(n+(1:np));integral=final(end);steps=numel(solved.x)-1;
    function [J,L,g]=coefficients(t)
        if toc(started)>seconds,error('Target reverse epoch wall allowance exceeded');end
        tolerance=64*eps*max([1,abs(first),abs(last)]);if t<first-tolerance||t>last+tolerance,error('Target reverse stage lies outside retained forward epoch');end;t=min(max(t,first),last);
        if t==cachedTime,J=cachedResult{1};L=cachedResult{2};g=cachedResult{3};return;end
        values=deval(forward,t);y=values(1:n);[~,J]=targetfield(fn,t,y,parameters,widths,mass,1);L=0;g=zeros(1,1+n+np);if ~isempty(running),[L,g]=targetcost(running,t,y,parameters,widths);end;calls=calls+1;cachedTime=t;cachedResult={J,L,g};
    end
    function value=rhs(t,z),[J,L,g]=coefficients(t);value=[-J(:,1:n).'*z(1:n)-g(2:1+n).';-J(:,1+n:end).'*z(1:n)-g(2+n:end).';-L];end
    function matrix=jacobian(t,z),[J,~,~]=coefficients(t);matrix=zeros(n+np+1);matrix(1:n,1:n)=-J(:,1:n).';matrix(n+(1:np),1:n)=-J(:,1+n:end).';end
end
function out=pentaodeadjoint(fn,initial,options,metadata)
started=tic;
checkoptions(options,{'parameters','objective','running_cost','t0','t1','sample_step','rtol','atol','method','jacobian','mass','initial_step','max_step','max_seconds'});if ~isfield(options,'objective'),error('Target adjoint requires terminal objective');end
bindings=opt(options,'parameters',struct);names=metadata.parameters;parameterWidths=metadata.parameter_widths;if ~isequal(sort(fieldnames(bindings)),sort(names(:))),error('Bind every adjoint model parameter once');end
parameters=cell(size(names));for k=1:numel(names),parameters{k}=bindings.(names{k});if parameterWidths(k)==5,parameters{k}=penta_runtime('pent',parameters{k});end;end
widths=metadata.widths;state=penta_runtime('pack',{initial});settings=options;for key={'parameters','objective','running_cost'},if isfield(settings,key{1}),settings=rmfield(settings,key{1});end;end
forward=pentaodetarget(fn,metadata.states,widths,unpackgroups(state,widths),parameters,settings);stored=forward.metadata;last=forward.fields.t(end);[value,gradient]=targetcost(options.objective,last,stored.values(end,:).',parameters,widths);n=numel(state);lambda=gradient(2:1+n).';gp=gradient(2+n:end).';runningCost=0;steps=0;
if ~isempty(stored.result),settings.max_seconds=opt(options,'max_seconds',120)-toc(started);if settings.max_seconds<=0,error('Target adjoint aggregate wall allowance exceeded');end;mass=[];if isfield(options,'mass'),constant=options.mass;mass=@constantMass;end;[lambda,gp,runningCost,steps]=reverseepoch(fn,parameters,widths,stored.result,forward.fields.t(1),last,lambda,gp,opt(options,'running_cost',[]),settings,mass);end
checkpoints=0;if ~isempty(stored.result),checkpoints=numel(stored.result.x);end
out=struct('objective',value+runningCost,'running_cost',runningCost,'reverse_table','matlab_ode15s_BDF_continuous_adjoint','parameter_gradient',gp,'initial_gradient',lambda,'backward_steps',steps,'checkpoints',checkpoints,'solution',forward,'cache_hit',0,'cache_key','');
    function value=constantMass(varargin),value=constant;end
end
function out=pentahybridadjoint(fn,event,reset,objective,initial,options,metadata)
started=tic;seconds=opt(options,'max_seconds',120);
running=opt(options,'running_cost',[]);eventCost=opt(options,'event_cost',[]);if isfield(options,'running_cost'),options=rmfield(options,'running_cost');end;if isfield(options,'event_cost'),options=rmfield(options,'event_cost');end
if ~strcmp(opt(options,'sensitivities','none'),'none')||isfield(options,'sensitivity_parameters')||isfield(options,'sensitivity_initial'),error('Hybrid target adjoint does not replace forward seed requests');end
options.sensitivities='none';out=pentahybrid(fn,event,reset,initial,options,metadata);if ~out.event_info.derivatives_available,error('Target adjoint requires differentiable transverse event groups');end
if out.events&&~out.terminal_event&&opt(options,'t1',1)-out.event_times(end)<opt(options,'minimum_event_interval',1e-8),error('Differentiated target event lies at fixed observation endpoint');end
n=out.dimension;np=out.parameter_count;widths=metadata.widths;parameterWidths=metadata.parameter_widths;parameters=unpackgroups(out.parameter_values,parameterWidths);final=out.values(end,:).';last=out.t(end);[value,terminalGradient]=targetcost(objective,last,final,parameters,widths);lambda=terminalGradient(2:1+n).';gp=terminalGradient(2+n:end).';runningTotal=0;eventTotal=0;backwardSteps=0;checkpoints=0;before=zeros(out.events,n);after=before;
for epoch=out.events:-1:0
    first=out.t(1);if epoch>0,first=out.event_times(epoch);end;lastEpoch=last;if epoch<out.events,lastEpoch=out.event_times(epoch+1);end
    if lastEpoch>first,options.max_seconds=seconds-toc(started);if options.max_seconds<=0,error('Target hybrid adjoint aggregate wall allowance exceeded');end;forward=out.penta_target_epochs{epoch+1}.metadata.result;[lambda,gp,integral,steps]=reverseepoch(fn,parameters,widths,forward,first,lastEpoch,lambda,gp,running,options,[]);runningTotal=runningTotal+integral;backwardSteps=backwardSteps+steps;checkpoints=checkpoints+numel(forward.x);if out.steps+backwardSteps>opt(options,'max_steps',1000000),error('Target hybrid adjoint aggregate step allowance exceeded');end;end
    if epoch>0
        index=epoch;t=out.event_times(index);pre=out.event_before(index,:).';post=out.event_after(index,:).';winner=out.event_info.indices(index);lambdaPost=lambda;after(index,:)=lambdaPost.';Xi=reshape(out.saltation(index,:),n,n).';Bp=reshape(out.parameter_jumps(index,:),np,n).';lambda=Xi.'*lambdaPost;gp=gp+Bp.'*lambdaPost;
        [~,roots]=targetad(@rootSeed,[t;pre;penta_runtime('pack',parameters)],1);g=roots(winner,:);den=out.event_denominators(index);fminus=targetfield(fn,t,pre,parameters,widths,[],0);fplus=targetfield(fn,t,post,parameters,widths,[],0);Lminus=0;Lplus=0;direct=zeros(1,1+n+np);
        if ~isempty(running),Lminus=targetcost(running,t,pre,parameters,widths);Lplus=targetcost(running,t,post,parameters,widths);end;boundary=Lminus-Lplus;
        if ~isempty(eventCost),callback=eventCost;if isfield(metadata,'indexed_event_cost')&&metadata.indexed_event_cost,callback=@indexedCost;end;[cost,direct]=targetcost(callback,t,pre,parameters,widths);eventTotal=eventTotal+cost;boundary=boundary+direct(1)+direct(2:1+n)*fminus;end
        if out.terminal_event&&epoch==out.events,boundary=boundary+terminalGradient(1)+terminalGradient(2:1+n)*fplus+Lplus;end
        lambda=lambda+direct(2:1+n).'-boundary*g(2:1+n).'/den;gp=gp+direct(2+n:end).'-boundary*g(2+n:end).'/den;before(index,:)=lambda.';
    end
end
label='fixed_terminal_target_adjoint';if out.terminal_event,label='moving_terminal_target_adjoint';end
out.adjoint=struct('objective',value+runningTotal+eventTotal,'reverse_table','matlab_ode15s_BDF_epoch_continuous_adjoint','running_cost',runningTotal,'event_cost',eventTotal,'terminal_time',last,'running_cache_key','','event_cost_cache_key','','initial_gradient',lambda.','parameter_gradient',gp.','event_before',before,'event_after',after,'backward_steps',backwardSteps,'replay_steps',0,'checkpoints',checkpoints,'elapsed_seconds',out.elapsed_seconds,'objective_cache_key','','request_hash','','status',label);
    function result=rootSeed(point),states=unpackgroups(point(2:1+n),widths);params=unpackgroups(point(2+n:end),parameterWidths);result=event(point(1),states{:},params{:});end
    function result=indexedCost(varargin),result=eventCost(double(winner),varargin{:});end
end
)PENTA_REVERSE_M"
