R"PENTA_OPT_M(
function out=minimizeadapter(fn,point,options,metadata)
checkoptions(options,{'parameters','algorithm','jacobian','lower','upper','parameter_scale','inequality','equality','inequality_parameters','equality_parameters','constraint_tolerance','ftol','ftol_abs','xtol','xtol_abs','max_evaluations','max_wall_seconds','history_limit','initial_step','threads'});
x=penta_runtime('pack',{point});n=numel(x);if n<1||n>128||any(~isfinite(x)),error('Target optimization point requires1..128 real coordinates');end
algorithm=opt(options,'algorithm','lbfgs');if ~any(strcmp(algorithm,{'lbfgs','nelder_mead','slsqp'})),error('Unmapped MATLAB optimization algorithm');end
jacobian=opt(options,'jacobian','analytic');analytic=any(strcmp(jacobian,{'analytic','automatic','ad'}));if ~analytic&&~strcmp(jacobian,'finite_difference'),error('Unknown derivative selector');end
scale=vector('parameter_scale',1);lower=vector('lower',-inf);upper=vector('upper',inf);if any(~isfinite(scale))||any(scale<=0)||any(lower>=upper)||any(x<lower)||any(x>upper),error('Invalid target parameter bounds/scales');end
constraintNames={};for key={'inequality','equality'},name=key{1};if isfield(options,name),constraintNames{end+1}=name;if ~strcmp(algorithm,'slsqp'),error('This target algorithm has no requested nonlinear constraint mapping');end;elseif isfield(options,[name,'_parameters']),error('Constraint bindings require their callback');end;end
if isfield(options,'initial_step')&&options.initial_step~=0,error('MATLAB target initial_step mapping is unsupported');end
hasBounds=any(isfinite(lower))||any(isfinite(upper));if strcmp(algorithm,'nelder_mead')&&hasBounds,error('MATLAB fminsearch has no implicit bounded-variable transform');end
maximum=opt(options,'max_evaluations',10000);seconds=opt(options,'max_wall_seconds',60);historyLimit=opt(options,'history_limit',min(33333,maximum));tolerance=opt(options,'constraint_tolerance',1e-8);if maximum<1||maximum>10000000||seconds<=0||seconds>3600||historyLimit<0||historyLimit>33333||~isfinite(tolerance)||tolerance<=0,error('Invalid target optimizer resource/constraint bounds');end
evaluations=0;objectiveEvaluations=0;constraintEvaluations=0;gradientEvaluations=0;history=zeros(0,3);started=tic;best=[];bestObjective=inf;bestViolation=inf;bestFeasible=false;
if strcmp(algorithm,'nelder_mead'),backend='matlab.fminsearch/Nelder-Mead';configuration=optimset('Display','off','MaxFunEvals',maximum,'MaxIter',maximum,'TolX',opt(options,'xtol_abs',1e-12),'TolFun',opt(options,'ftol_abs',1e-12),'OutputFcn',@observe);
elseif strcmp(algorithm,'slsqp')||hasBounds
    if exist('fmincon','file')~=2,error('Explicit target constrained optimization requires Optimization Toolbox');end
    if strcmp(algorithm,'slsqp'),method='sqp';else,method='interior-point';end;backend=['matlab.fmincon/',method];configuration=optimoptions('fmincon','Display','off','Algorithm',method,'SpecifyObjectiveGradient',analytic,'SpecifyConstraintGradient',analytic,'FiniteDifferenceType','central','MaxFunctionEvaluations',maximum,'MaxIterations',maximum,'ConstraintTolerance',tolerance,'FunctionTolerance',max(opt(options,'ftol',1e-10),opt(options,'ftol_abs',1e-12)),'StepTolerance',max(opt(options,'xtol',1e-10),opt(options,'xtol_abs',1e-12)),'OutputFcn',@observe);
else
    if exist('fminunc','file')~=2,error('Explicit target optimization requires Optimization Toolbox');end
    backend='matlab.fminunc/quasi-newton';configuration=optimoptions('fminunc','Display','off','Algorithm','quasi-newton','SpecifyObjectiveGradient',analytic,'FiniteDifferenceType','central','MaxFunctionEvaluations',maximum,'MaxIterations',maximum,'FunctionTolerance',max(opt(options,'ftol',1e-10),opt(options,'ftol_abs',1e-12)),'StepTolerance',max(opt(options,'xtol',1e-10),opt(options,'xtol_abs',1e-12)),'OutputFcn',@observe);
end
stopped=false;
try
    if strcmp(algorithm,'nelder_mead'),[z,~,flag,details]=fminsearch(@objective,x./scale,configuration);
    elseif strcmp(algorithm,'slsqp')||hasBounds,[z,~,flag,details]=fmincon(@objective,x./scale,[],[],[],[],lower./scale,upper./scale,@constraints,configuration);
    else,[z,~,flag,details]=fminunc(@objective,x./scale,configuration);end
    objective(z);
catch failure
    if ~strcmp(failure.identifier,'PentaExport:Budget'),rethrow(failure);end
    if isempty(best),error('Target budget ended before a complete valid candidate');end
    stopped=true;flag=-1;details=struct('message','Target evaluation/wall budget reached');
end
if isempty(best),error('Target optimizer returned no fully validated candidate');end
position=best.*scale;if iscell(point),resultPoint=num2cell(position.');else,resultPoint=reshape(position,size(point));end
out=struct('x',0,'objective',bestObjective,'constraint_violation',bestViolation,'feasible',double(bestFeasible),'converged',double(flag>0&&bestFeasible&&~stopped),'status',status(double(flag>0&&bestFeasible&&~stopped)),'message',details.message,'flag',flag,'evaluations',evaluations,'objective_evaluations',objectiveEvaluations,'constraint_evaluations',constraintEvaluations,'gradient_evaluations',gradientEvaluations,'native_evaluations',0,'history',history,'elapsed_seconds',toc(started),'algorithm',algorithm,'jacobian_method',jacobian,'seed',0,'backend',backend,'cache_hit',0,'cache_key','','threads_requested',opt(options,'threads',0),'threads_used',1);out.x=resultPoint;
    function value=vector(key,fallback),value=opt(options,key,fallback);value=value(:);if isscalar(value),value=repmat(value,n,1);end;if numel(value)~=n,error([key,' has wrong coordinate count']);end;end
    function [value,J]=evaluate(callback,names,z,parameters,derivatives,isObjective)
        if evaluations>=maximum||toc(started)>seconds,error('PentaExport:Budget','Target optimization evaluation/wall allowance exceeded');end
        evaluations=evaluations+1;if isObjective,objectiveEvaluations=objectiveEvaluations+1;else,constraintEvaluations=constraintEvaluations+1;end
        fixed=opt(options,parameters,struct);bound=names(n+1:end);if ~isequal(sort(fieldnames(fixed)),sort(bound(:))),error('Bind every trailing callback parameter once');end
        if derivatives,seed=penta_dual(z.*scale,eye(n));args=cell(1,n);for j=1:n,args{j}=seed(j);end;gradientEvaluations=gradientEvaluations+1;else,args=num2cell((z.*scale).');end
        tail=cell(size(bound));for j=1:numel(bound),tail{j}=fixed.(bound{j});end
        value=penta_runtime('pack',{callback(args{:},tail{:})});if derivatives&&isa(value,'penta_dual'),J=value.tangent.*scale.';value=value.value;else,J=zeros(numel(value),n);end
        if any(~isfinite(value(:)))||any(~isfinite(J(:))),error('Nonfinite exported objective/constraint');end
        if isObjective&&numel(value)~=1,error('Optimization objective must be one real scalar');end
        if ~isObjective&&numel(value)>128,error('Target constraint count exceeds128');end
    end
    function [value,gradient]=objective(z)
        [value,J]=evaluate(fn,metadata.objective,z,'parameters',analytic,true);gradient=J.';[inequality,equality]=constraints(z);violation=max([0;inequality(:);abs(equality(:))]);feasible=violation<=tolerance;
        if isempty(best)||(feasible&&~bestFeasible)||(feasible==bestFeasible&&((~feasible&&violation<bestViolation)||(feasible&&value<bestObjective)||(violation==bestViolation&&value<bestObjective))),best=z;bestObjective=value;bestViolation=violation;bestFeasible=feasible;end
    end
    function [inequality,equality,inequalityGradient,equalityGradient]=constraints(z)
        inequality=zeros(0,1);equality=zeros(0,1);inequalityGradient=zeros(n,0);equalityGradient=zeros(n,0);
        if isfield(options,'inequality'),[inequality,J]=evaluate(options.inequality,metadata.inequality,z,'inequality_parameters',analytic,false);inequalityGradient=J.';end
        if isfield(options,'equality'),[equality,J]=evaluate(options.equality,metadata.equality,z,'equality_parameters',analytic,false);equalityGradient=J.';end
    end
    function stop=observe(z,~,state)
        stop=false;if strcmp(state,'done'),return;end
        value=objective(z);[inequality,equality]=constraints(z);violation=max([0;inequality(:);abs(equality(:))]);if size(history,1)<historyLimit,history(end+1,:)=[evaluations,value,violation];end
    end
end
)PENTA_OPT_M"
