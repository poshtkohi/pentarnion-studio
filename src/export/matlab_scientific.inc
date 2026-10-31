R"PENTA_SCI_M(
function out=penta_scientific(name,varargin)
% Explicit scientific target mappings, independent of Ceres/NLopt.
if any(strcmp(name,{'lsqfit','lsqnonlin','curvefit'})),out=fitadapter(name,varargin{:});elseif strcmp(name,'minimize'),out=minimizeadapter(varargin{:});else,error(['Unmapped scientific adapter ',name]);end
end
function out=fitadapter(name,fn,point,varargin)
if exist('lsqnonlin','file')~=2,error('Explicit MATLAB least-squares adapter requires Optimization Toolbox');end
curve=strcmp(name,'curvefit');names=varargin{end};options=varargin{end-1};
allowed={'parameters','method','loss','loss_scale','jacobian','weights','lower','upper','parameter_scale','max_iterations','max_evaluations','max_wall_seconds','ftol','gtol','xtol','covariance','noise_variance','threads','history_limit'};if curve,allowed=[allowed,{'coefficient_weights','observation_weights'}];end
checkoptions(options,allowed);x=penta_runtime('pack',{point});n=numel(x);if n<1||n>128||any(~isfinite(x)),error('Target point requires1..128 finite real coordinates');end
fixed=opt(options,'parameters',struct);bound=names(n+1:end-curve);if ~isequal(sort(fieldnames(fixed)),sort(bound(:))),error('Bind every trailing callback parameter once');end;tail=cell(size(bound));for k=1:numel(bound),tail{k}=fixed.(bound{k});end
method=opt(options,'method','lm');if ~any(strcmp(method,{'lm','levenberg_marquardt','dogleg'})),error('Unmapped least-squares source method');end
jacobian=opt(options,'jacobian','analytic');analytic=any(strcmp(jacobian,{'analytic','ad','automatic'}));if ~analytic&&~strcmp(jacobian,'finite_difference'),error('Unknown Jacobian selector');end
loss=opt(options,'loss','none');if strcmp(loss,'none'),loss='linear';end;if ~any(strcmp(loss,{'linear','huber','cauchy','soft_l1'})),error('Unmapped robust loss');end
if curve,datax=varargin{1}(:);data=varargin{2};observations=reshape(data.',[],1);if isempty(datax)||rem(numel(observations),numel(datax)),error('Incomplete curve observation coordinates');end;width=numel(observations)/numel(datax);end
initial=raw(x,false);m=numel(initial);if m<1||m*n>1048576,error('Residual/Jacobian exceeds target bound');end
weights=vector('weights',m,1);if curve,weights=weights.*repelem(vector('observation_weights',numel(datax),1),width).*repmat(vector('coefficient_weights',width,1),numel(datax),1);end
if any(~isfinite(weights))||any(weights<0)||~any(weights>0),error('Invalid nonnegative residual weights');end
rootweights=sqrt(weights);scale=vector('parameter_scale',n,1);lower=vector('lower',n,-inf);upper=vector('upper',n,inf);if any(~isfinite(scale))||any(scale<=0)||any(lower>=upper)||any(x<lower)||any(x>upper),error('Invalid parameter scales/bounds');end
maximumIterations=opt(options,'max_iterations',200);maximumEvaluations=opt(options,'max_evaluations',10000);maximumSeconds=opt(options,'max_wall_seconds',60);historyLimit=opt(options,'history_limit',min(20000,maximumIterations+1));fscale=opt(options,'loss_scale',1);if maximumIterations<1||maximumIterations>1000000||historyLimit<0||historyLimit>20000||maximumSeconds<=0||maximumSeconds>3600||~isfinite(fscale)||fscale<=0,error('Invalid target resource/loss bounds');end
algorithm='trust-region-reflective';if any(strcmp(method,{'lm','levenberg_marquardt'}))&&all(isinf(lower))&&all(isinf(upper)),algorithm='levenberg-marquardt';end
started=tic;evaluations=0;jacobians=0;history=zeros(0,5);previous=x;
configuration=optimoptions('lsqnonlin','Algorithm',algorithm,'Display','off','SpecifyObjectiveGradient',analytic,'FiniteDifferenceType','central','FunctionTolerance',opt(options,'ftol',1e-10),'OptimalityTolerance',opt(options,'gtol',1e-10),'StepTolerance',opt(options,'xtol',1e-10),'MaxIterations',maximumIterations,'MaxFunctionEvaluations',maximumEvaluations,'OutputFcn',@observe);
[z,resnorm,~,flag,details]=lsqnonlin(@objective,x./scale,lower./scale,upper./scale,configuration);solution=z.*scale;residual=raw(solution,false);
if analytic,[~,J]=raw(solution,true);else,J=zeros(m,n);for k=1:n,h=eps^(1/3)*(1+abs(solution(k)));a=solution;b=solution;a(k)=min(upper(k),a(k)+h);b(k)=max(lower(k),b(k)-h);J(:,k)=(raw(a,false)-raw(b,false))/(a(k)-b(k));end;end
weighted=J.*rootweights;[~,S,V]=svd(weighted,'econ');s=diag(S);rankvalue=sum(s>1e-12*max([s;0]));condition=0;if numel(s)>=n&&~isempty(s)&&s(1)>0,condition=s(end)/s(1);end
covariance=zeros(0,0);covarianceValid=false;noise=opt(options,'noise_variance',-1);assumption='Unavailable: target covariance contract not satisfied';dof=sum(weights>0)-rankvalue;inactive=all(solution>lower+1e-8*(1+abs(solution)))&&all(solution<upper-1e-8*(1+abs(solution)));
if opt(options,'covariance',0)&&flag>0&&strcmp(loss,'linear')&&inactive&&rankvalue==n&&condition>1e-12&&(noise>=0||dof>0),if noise<0,noise=sum(weights.*residual.^2)/dof;end;covariance=(V./(s.'.^2))*V.'*noise;covarianceValid=true;assumption='Local linearization; independent Gaussian residuals after sqrt(weights) scaling; full rank and inactive bounds. Local rank is not parameter uniqueness.';end
if iscell(point),resultPoint=num2cell(solution.');else,resultPoint=reshape(solution,size(point));end
[~,influence]=robust(rootweights.*residual);out=struct('x',0,'residuals',residual,'jacobian',J,'jacobian_available',1,'covariance',covariance,'covariance_valid',double(covarianceValid),'covariance_assumption',assumption,'noise_variance',noise,'cost',resnorm/2,'initial_cost',robustcost(rootweights.*initial),'residual_norm',norm(residual),'weighted_residual_norm',norm(rootweights.*residual),'gradient_norm',norm(J.'*(weights.*residual.*influence),inf),'rcond',condition,'rank',rankvalue,'iterations',details.iterations,'evaluations',evaluations,'jacobian_evaluations',jacobians,'native_evaluations',0,'starts_completed',1,'best_start',1,'converged',double(flag>0),'status',status(flag),'message',details.message,'flag',flag,'history',history,'elapsed_seconds',toc(started),'method',method,'loss',loss,'jacobian_method',jacobian,'backend',['matlab.lsqnonlin/',algorithm],'cache_hit',0,'cache_key','','threads_requested',opt(options,'threads',0),'threads_used',1,'backend_threads',1,'seed',0);
out.x=resultPoint;
if curve,out.xdata=datax;out.fitted=reshape(residual+observations,width,numel(datax)).';out.observations=reshape(observations,width,numel(datax)).';end
    function value=vector(key,count,fallback),value=opt(options,key,fallback);value=value(:);if isscalar(value),value=repmat(value,count,1);end;if numel(value)~=count,error([key,' has wrong coordinate count']);end;end
    function [value,J]=raw(position,derivatives)
        if derivatives,seed=penta_dual(position,eye(n));args=cell(1,n);for j=1:n,args{j}=seed(j);end;else,args=num2cell(position.');end
        if curve,parts=cell(numel(datax),1);for sample=1:numel(datax),parts{sample}=penta_runtime('pack',{fn(args{:},tail{:},datax(sample))});end;value=vertcat(parts{:});if derivatives,value=value-observations;else,value=value-observations;end
        else,value=penta_runtime('pack',{fn(args{:},tail{:})});end
        if derivatives,if isa(value,'penta_dual'),J=value.tangent;value=value.value;else,J=zeros(numel(value),n);end;else,J=[];end
        if any(~isfinite(value(:))),error('Nonfinite exported residual');end
    end
    function [value,J]=objective(z)
        if toc(started)>maximumSeconds,error('Target least-squares wall allowance exceeded');end;evaluations=evaluations+1;
        if analytic,[r,J]=raw(z.*scale,true);jacobians=jacobians+1;else,r=raw(z.*scale,false);J=[];end
        weightedResidual=rootweights.*r;[rho,slope]=robust(weightedResidual);value=sign(weightedResidual).*fscale.*sqrt(rho);
        if analytic,factor=rootweights;nonzero=value~=0;factor(nonzero)=weights(nonzero).*r(nonzero).*slope(nonzero)./value(nonzero);J=J.*factor.*scale.';end
    end
    function stop=observe(z,values,state)
        stop=false;if strcmp(state,'done'),return;end
        position=z.*scale;step=norm(position-previous);previous=position;
        if size(history,1)<historyLimit,gradient=0;if isfield(values,'firstorderopt'),gradient=values.firstorderopt;end;r=raw(position,false);history(end+1,:)=[1,values.iteration,robustcost(rootweights.*r),gradient,step];end
        if toc(started)>maximumSeconds,error('Target least-squares wall allowance exceeded');end
    end
    function [rho,slope]=robust(r)
        q=(r/fscale).^2;
        switch loss,case 'linear',rho=q;slope=ones(size(q));case 'huber',rho=q;slope=ones(size(q));outside=q>1;rho(outside)=2*sqrt(q(outside))-1;slope(outside)=1./sqrt(q(outside));case 'cauchy',rho=log1p(q);slope=1./(1+q);otherwise,rho=2*(sqrt(1+q)-1);slope=1./sqrt(1+q);end
    end
    function value=robustcost(r),rho=robust(r);value=.5*fscale*fscale*sum(rho);end
end
function text=status(flag),if flag>0,text='converged';else,text='target_stopped';end;end
function value=opt(options,name,fallback),if isfield(options,name),value=options.(name);else,value=fallback;end;end
function checkoptions(options,allowed),names=fieldnames(options);if any(~ismember(names,allowed)),error(['Unmapped target option: ',strjoin(names(~ismember(names,allowed)),',')]);end;end
)PENTA_SCI_M"
