R"PENTA_ADV_M(
function [values,first,second]=targetad(fn,point,order)
point=point(:);n=numel(point);if n>128,error('Target analytic coordinate derivative bound128 exceeded');end
h=[];if order==2,h=zeros(n,n,n);end;seed=penta_dual(point,eye(n),h);result=penta_runtime('pack',{fn(seed)});
if isa(result,'penta_dual'),values=result.value;first=result.tangent;second=result.second;else,values=result;first=zeros(numel(result),n);second=[];end
if order==2,if numel(values)*n*n>1048576,error('Target Hessian coordinate allowance exceeded');end;if isempty(second),second=zeros(numel(values),n,n);end;end
if any(~isfinite(values(:)))||any(~isfinite(first(:)))||any(~isfinite(second(:))),error('Nonfinite target analytic derivative');end
end
function result=unpackgroups(flat,widths)
result=cell(size(widths));offset=0;for k=1:numel(widths),width=widths(k);if width==5,result{k}=struct('c',reshape(flat(offset+(1:5)),1,5));else,result{k}=flat(offset+1);end;offset=offset+width;end;count=numel(flat);if isa(flat,'penta_dual'),count=numel(flat.value);end;if offset~=count,error('Complete target coordinate layout mismatch');end
end
function [value,first,second]=targetsolve(M,b,firstM,firstB,secondM,secondB)
scale=max(abs(M(:)));if isempty(scale)||scale==0,error('Target mass/regularity matrix has no reliable inverse');end
[U,S,V]=svd(M/scale,'econ');s=diag(S);if numel(s)~=numel(b)||isempty(s)||s(end)<=1e-12*s(1),error('Target mass/regularity matrix has no reliable unique inverse');end
solve=@(rhs)V*((U'*(rhs/scale))./s);value=solve(b);if norm(M*value-b,inf)>1e-10*max(realmin,norm(M,inf)*norm(value,inf)+norm(b,inf)),error('Target mass solve residual failed');end
first=[];second=[];if nargin<4,return;end;d=size(firstB,2);load=firstB;for k=1:d,load(:,k)=load(:,k)-firstM(:,:,k)*value;end;first=solve(load);
if nargin>=6&&~isempty(secondB),second=zeros(numel(b),d,d);for i=1:d,for j=1:d,second(:,i,j)=solve(secondB(:,i,j)-secondM(:,:,i,j)*value-firstM(:,:,i)*first(:,j)-firstM(:,:,j)*first(:,i));end;end;end
end
function [values,J,H]=targetfield(fn,t,y,parameters,widths,mass,order)
n=numel(y);parameterWidths=ones(size(parameters));for k=1:numel(parameters),if isstruct(parameters{k}),parameterWidths(k)=5;end;end
point=[y(:);penta_runtime('pack',parameters)];if order,[values,J,H]=targetad(@right,point,order);else,values=penta_runtime('pack',{right(point)});J=[];H=[];end
if isempty(mass),return;end
if order
    [flat,first,second]=targetad(@left,point,order);M=reshape(flat,n,n).';d=numel(point);Mj=zeros(n,n,d);for k=1:d,Mj(:,:,k)=reshape(first(:,k),n,n).';end;Mh=[];if order==2,Mh=zeros(n,n,d,d);for i=1:d,for j=1:d,Mh(:,:,i,j)=reshape(second(:,i,j),n,n).';end;end;end
    [values,J,H]=targetsolve(M,values,Mj,J,Mh,H);
else,flat=penta_runtime('pack',{left(point)});values=targetsolve(reshape(flat,n,n).',values);end
    function out=right(seeded),params=unpackgroups(seeded(n+1:end),parameterWidths);out=fn(t,seeded(1:n),params);end
    function out=left(seeded),states=unpackgroups(seeded(1:n),widths);params=unpackgroups(seeded(n+1:end),parameterWidths);out=mass(t,states{:},params{:});end
end
function out=pentaodetarget(fn,names,widths,initial,parameters,settings,varargin)
mass=[];if ~isempty(varargin),mass=varargin{1};end
t0=option(settings,'t0',0);t1=option(settings,'t1',10);sample=option(settings,'sample_step',.01);y0=penta_runtime('pack',initial);n=numel(y0);np=numel(penta_runtime('pack',parameters));started=tic;
if any(~isfinite([t0,t1,sample]))||sample<=0||any(~isfinite(y0))||n>64,error('ODE target needs finite scalar-state coordinates and positive sample step');end
if ~strcmp(option(settings,'method','bdf'),'bdf')||~strcmp(option(settings,'jacobian','analytic'),'analytic'),error('Advanced target mapping requires BDF with analytic derivatives');end
if isfield(settings,'mass'),if ~isempty(mass),error('Ambiguous constant and state-dependent target mass');end;constant=settings.mass;if ~isequal(size(constant),[n,n]),error('Constant target mass shape mismatch');end;mass=@constantMass;end
selection=option(settings,'sensitivities','none');if ~any(strcmp(selection,{'none','parameters','initial','all'})),error('Unknown target sensitivity selection');end
selected=[];initialCount=0;if any(strcmp(selection,{'parameters','all'})),selected=1:np;end;if any(strcmp(selection,{'initial','all'})),initialCount=n;end
if isfield(settings,'sensitivity_parameters'),if any(strcmp(selection,{'initial','all'})),error('Explicit parameter selection cannot combine with initial/all');end;selected=reshape(settings.sensitivity_parameters,1,[]);if any(selected~=floor(selected))||any(selected<1)||any(selected>np)||numel(unique(selected))~=numel(selected),error('Sensitivity coordinates must be unique1-based parameters');end;end
directions=[selected,zeros(1,initialCount)];nd=numel(directions);S=zeros(n,nd);if initialCount,S(:,numel(selected)+1:end)=eye(n);end
if isfield(settings,'sensitivity_initial'),S=settings.sensitivity_initial;if nd==0||~isequal(size(S),[n,nd])||any(~isfinite(S(:))),error('Sensitivity initial seeds need n rows and selected columns');end;end
if ~strcmp(option(settings,'sensitivity_method','simultaneous'),'simultaneous')||option(settings,'sensitivity_error_control',1)~=1,error('Target uses simultaneous augmented error-controlled sensitivities only');end
if n*(1+nd)>512||n*(n+np)^2>1048576,error('Target augmented/Hessian coordinate bound exceeded');end
calls=0;jacobianCalls=0;eventCalls=0;maximum=option(settings,'max_evaluations',1000000);seconds=option(settings,'max_seconds',120);cachedTime=nan;cachedY=[];cachedOrder=-1;cachedResult={};
eventsCallback=[];eventCount=0;eventDirections=[];eventTerminals=[];if isfield(settings,'events'),eventsCallback=settings.events;groups=unpackgroups(y0,widths);roots=penta_runtime('pack',{eventsCallback(t0,groups{:},parameters{:})});eventCount=numel(roots);if eventCount<1||eventCount>16,error('Target event callback needs1..16 roots');end;eventDirections=flags('event_direction',0,-1,1);eventTerminals=flags('event_terminal',0,0,1);elseif isfield(settings,'event_direction')||isfield(settings,'event_terminal'),error('Event flags need a callback');end
times=t0:sign(t1-t0)*sample:t1;if t0==t1,times=t0;end;if isempty(times),times=t0;end;if numel(times)>1000000,error('ODE target retained sample allowance exceeded');end;if abs(times(end)-t1)>1e-13*max(1,abs(t1)),times(end+1)=t1;else,times(end)=t1;end
initialAug=[y0;S(:)];result=[];eventTimes=zeros(0,1);eventValues=zeros(0,n);eventIndices=zeros(0,1);eventSigns=zeros(0,1);terminated=false;
if t0==t1,values=initialAug.';else
    config=odeset('RelTol',option(settings,'rtol',1e-7),'AbsTol',option(settings,'atol',1e-9),'BDF','on','Jacobian',@jacobian);
    if option(settings,'initial_step',0)>0,config=odeset(config,'InitialStep',settings.initial_step);end;if option(settings,'max_step',0)>0,config=odeset(config,'MaxStep',settings.max_step);end;if ~isempty(eventsCallback),config=odeset(config,'Events',@events);end
    result=ode15s(@rhs,[t0,t1],initialAug,config);actual=result.x(end);terminated=abs(actual-t1)>16*eps*max(1,abs(t1));times=times((times-actual)*sign(t1-t0)<=0);if isempty(times)||abs(times(end)-actual)>16*eps*max(1,abs(actual)),times(end+1)=actual;end;values=deval(result,times).';
    if isfield(result,'xe')&&~isempty(result.xe),eventTimes=result.xe(:);eventValues=result.ye(1:n,:).';eventIndices=result.ie(:);for k=1:numel(eventTimes),[~,J]=targetad(@eventSeed,[eventTimes(k);eventValues(k,:).'],1);f=field(eventTimes(k),eventValues(k,:).',0);eventSigns(k,1)=sign(J(eventIndices(k),1)+J(eventIndices(k),2:end)*f);end;end
end
fields=struct('t',times(:));offset=0;for k=1:numel(names),width=widths(k);if width==1,fields.(names{k})=values(:,offset+1);else,data=cell(numel(times),1);for row=1:numel(times),data{row}=struct('c',values(row,offset+(1:5)));end;fields.(names{k})=data;end;offset=offset+width;end
metadata=struct('n',n,'widths',widths,'names',{names},'values',values(:,1:n),'sensitivities',values(:,n+1:end),'directions',directions,'event_times',eventTimes,'event_values',eventValues,'event_indices',eventIndices,'event_signs',eventSigns,'event_terminal',terminated,'result',result,'settings',settings,'elapsed',toc(started),'rhs_evaluations',calls,'jacobian_evaluations',jacobianCalls,'event_evaluations',eventCalls);
out=struct('penta_solution_format',1,'fields',fields,'metadata',metadata);
    function value=constantMass(varargin),value=constant;end
    function [value,J,H]=field(timeValue,y,order)
        if calls>=maximum||toc(started)>seconds,error('Target ODE callback/wall budget exceeded');end
        if timeValue==cachedTime&&isequal(y,cachedY)&&order==cachedOrder,value=cachedResult{1};J=cachedResult{2};H=cachedResult{3};return;end
        calls=calls+1;[value,J,H]=targetfield(fn,timeValue,y,parameters,widths,mass,order);cachedTime=timeValue;cachedY=y;cachedOrder=order;cachedResult={value,J,H};
    end
    function value=rhs(timeValue,z)
        order=0;if nd,order=1;end;[value,J]=field(timeValue,z(1:n),order);if nd,tangent=reshape(z(n+1:end),n,nd);derivative=J(:,1:n)*tangent;for d=1:nd,if directions(d)>0,derivative(:,d)=derivative(:,d)+J(:,n+directions(d));end;end;value=[value;derivative(:)];end
    end
    function matrix=jacobian(timeValue,z)
        jacobianCalls=jacobianCalls+1;order=1;if nd,order=2;end;[~,J,H]=field(timeValue,z(1:n),order);matrix=zeros(numel(z));matrix(1:n,1:n)=J(:,1:n);if nd,tangent=reshape(z(n+1:end),n,nd);for d=1:nd,block=n+(d-1)*n+(1:n);matrix(block,block)=J(:,1:n);for coordinate=1:n,for row=1:n,matrix(block(row),coordinate)=reshape(H(row,1:n,coordinate),1,n)*tangent(:,d);if directions(d)>0,matrix(block(row),coordinate)=matrix(block(row),coordinate)+H(row,n+directions(d),coordinate);end;end;end;end;end
    end
    function values=flags(key,fallback,low,high),values=option(settings,key,fallback);values=values(:);if isscalar(values),values=repmat(values,eventCount,1);end;if numel(values)~=eventCount||any(values~=floor(values))||any(values<low)||any(values>high),error('Invalid per-event target flags');end;end
    function [values,terminal,direction]=events(timeValue,z),eventCalls=eventCalls+1;groups=unpackgroups(z(1:n),widths);values=penta_runtime('pack',{eventsCallback(timeValue,groups{:},parameters{:})});if numel(values)~=eventCount||any(~isfinite(values)),error('Invalid target event root layout');end;terminal=eventTerminals;direction=eventDirections;end
    function values=eventSeed(seeded),groups=unpackgroups(seeded(2:end),widths);values=eventsCallback(seeded(1),groups{:},parameters{:});end
end
function out=pentaodequery(name,solution,varargin)
if ~isstruct(solution)||~isfield(solution,'penta_solution_format'),error('Expected checked exported ODE solution');end;meta=solution.metadata;n=meta.n;
switch name
case 'odesens'
    if isempty(meta.directions),error('Solution has no integrated sensitivities');end;out=meta.sensitivities;if ~isempty(varargin),index=varargin{1};if index~=floor(index)||index<1||index>numel(meta.directions),error('Sensitivity direction out of range');end;out=out(:,(index-1)*n+(1:n));end
case 'odeevents',out=struct('t',meta.event_times,'y',meta.event_values,'index',meta.event_indices,'direction',meta.event_signs,'terminal',double(meta.event_terminal));
case 'odestats'
    steps=0;last=0;if ~isempty(meta.result),steps=numel(meta.result.x)-1;if steps,last=meta.result.x(end)-meta.result.x(end-1);end;end
    jacobian='analytic_target_ad1';if ~isempty(meta.directions),jacobian='analytic_target_ad2';end
    out=struct('method','matlab_ode15s_bdf','jacobian',jacobian,'steps',steps,'rhs_evaluations',meta.rhs_evaluations,'jacobian_evaluations',meta.jacobian_evaluations,'event_evaluations',meta.event_evaluations,'sensitivity_rhs_evaluations',meta.rhs_evaluations*double(~isempty(meta.directions)),'error_test_failures',-1,'nonlinear_iterations',-1,'last_step',last,'last_order',-1,'elapsed_seconds',meta.elapsed,'cache_hit',0,'cache_key','','terminal_event',double(meta.event_terminal),'dense_segments',steps,'sensitivity_count',numel(meta.directions),'sensitivity_parameters',meta.directions);
case 'deval'
    query=varargin{1};if numel(varargin)>1,state=varargin{2};elseif numel(meta.names)==1,state=meta.names{1};else,error('Select a dense-output state by name');end
    index=find(strcmp(meta.names,state),1);interval=sort([solution.fields.t(1),solution.fields.t(end)]);if isempty(index)||isempty(meta.result)||any(query(:)<interval(1))||any(query(:)>interval(2)),error('Dense state/time selection outside retained integration interval');end
    values=deval(meta.result,query(:));offset=sum(meta.widths(1:index-1));if meta.widths(index)==1,out=reshape(values(offset+1,:),size(query));else,out=cell(size(query));for k=1:numel(query),out{k}=struct('c',values(offset+(1:5),k).');end;end
otherwise,error('Unsupported advanced ODE query');
end
end
function out=pentamasssolve(rhs,mass,initial,initialDerivative,options,metadata)
checkoptions(options,{'parameters','mass_parameters','algebraic','initialization','jacobian','t0','t1','sample_step','rtol','atol','initial_residual_tolerance','index_tolerance','max_wall_seconds','initial_step','maximum_step','max_steps','max_evaluations'});
y=penta_runtime('pack',{initial});yp=penta_runtime('pack',{initialDerivative});n=numel(y);t0=opt(options,'t0',0);tolerance=opt(options,'initial_residual_tolerance',1e-8);
if n<1||n>32||numel(yp)~=n||any(~isfinite([y;yp]))||tolerance<=0,error('Regular mass target needs matching1..32 finite coordinates');end
algebraic=opt(options,'algebraic','auto');if ~(ischar(algebraic)&&strcmp(algebraic,'auto'))&&any(algebraic(:)~=0),error('Target mass adapter supports regular systems, not algebraic constraints');end
initialization=opt(options,'initialization','check');if ~any(strcmp(initialization,{'check','consistent'}))||~strcmp(opt(options,'jacobian','analytic'),'analytic'),error('Regular mass target needs check/consistent derivatives and analytic AD');end
if opt(options,'index_tolerance',1e-12)~=1e-12,error('Regular mass target uses explicit1e-12 conditioning policy');end
parameters=bindings('parameters',metadata.rhs);massParameters=bindings('mass_parameters',metadata.mass);point=num2cell(y.');M=mass(t0,point{:},massParameters{:});if ~isequal(size(M),[n,n]),error('State/time mass matrix shape mismatch');end
load=penta_runtime('pack',{rhs(t0,point{:},parameters{:})});consistent=targetsolve(M,load);residual=norm(M*yp-load,inf);if strcmp(initialization,'check')&&residual>tolerance,error('Initial mass residual exceeds requested consistency tolerance');end
corrected=yp;if strcmp(initialization,'consistent'),corrected=consistent;end;correction=norm(corrected-yp);settings=struct('method','bdf','jacobian','analytic','max_seconds',opt(options,'max_wall_seconds',120));for key={'t0','t1','sample_step','rtol','atol','initial_step','max_evaluations'},name=key{1};if isfield(options,name),settings.(name)=options.(name);end;end;if isfield(options,'maximum_step'),settings.max_step=options.maximum_step;end
names=cell(1,n);for k=1:n,names{k}=['x',num2str(k)];end;solved=pentaodetarget(@right,names,ones(1,n),num2cell(y.'),parameters,settings,@left);meta=solved.metadata;steps=0;if ~isempty(meta.result),steps=numel(meta.result.x)-1;end;if steps>opt(options,'max_steps',1000000),error('Target regular-mass accepted-step allowance exceeded');end
values=meta.values;times=solved.fields.t;derivatives=zeros(size(values));maximumResidual=0;
for k=1:numel(times),value=values(k,:).';derivatives(k,:)=targetfield(@right,times(k),value,parameters,ones(1,n),@left,0).';point=num2cell(value.');matrix=mass(times(k),point{:},massParameters{:});f=penta_runtime('pack',{rhs(times(k),point{:},parameters{:})});maximumResidual=max(maximumResidual,norm(matrix*derivatives(k,:).'-f,inf));end
initialResidual=residual;if strcmp(initialization,'consistent'),initialResidual=norm(M*corrected-load,inf);end
out=struct('t',times,'values',values,'derivatives',derivatives,'consistent_initial',y.','consistent_derivative',corrected.','algebraic',zeros(1,n),'steps',steps,'residual_evaluations',meta.rhs_evaluations,'jacobian_evaluations',meta.jacobian_evaluations,'initial_residual_norm',initialResidual,'initial_correction_norm',correction,'maximum_sample_residual',maximumResidual,'elapsed_seconds',meta.elapsed,'native_operations',0,'cache_hit',0,'cache_key','','mass_cache_key','','classification','regular_mass_verified_each_evaluation','index_contract','explicit_regular_mass_target_no_dae_index_claim','jacobian','analytic_target_mass_derivatives','method','matlab_ode15s_regular_mass','status','completed','threads_used',1);
    function values=bindings(key,names)
        binding=opt(options,key,struct);tail=names(2+n:end);if numel(names)<1+n||~isequal(sort(fieldnames(binding)),sort(tail(:))),error('Bind every trailing mass/RHS callback parameter once');end;values=cell(size(tail));for k=1:numel(tail),values{k}=binding.(tail{k});end
    end
    function result=right(t,flat,params),callargs=cell(1,n);for k=1:n,callargs{k}=flat(k);end;result=rhs(t,callargs{:},params{:});end
    function result=left(t,varargin),result=mass(t,varargin{1:n},massParameters{:});end
end
)PENTA_ADV_M"
