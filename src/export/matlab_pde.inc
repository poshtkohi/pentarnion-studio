R"PENTA_PDE_M(
function out=pentapde(name,varargin)
if strcmp(name,'pdecoeff')
    field=varargin{1};index=varargin{2};data=field.values;components=1;if iscell(data),components=5;end
    if index~=floor(index)||index<0||index>=components,error('PDE coefficient index out of range');end
    if components==1,out=data;else,out=zeros(size(data));for i=1:numel(data),out(i)=data{i}.c(index+1);end;end;return
end
fn=varargin{1};x=varargin{2};initial=varargin{3};options=varargin{4};names=varargin{5};kinds=varargin{6};
checkoptions(options,{'parameters','method','boundary','length','diffusion','t0','t1','time_step','sample_step','left','right','max_steps','max_reaction_evaluations','max_wall_seconds','threads','integrator'});
x=x(:);n=numel(x);components=1;if strcmp(kinds{1},'pentarnion'),components=5;end;state=reshape(penta_runtime('pack',{initial}),components,[]).';
if n<3||n>4096||size(state,1)~=n||any(~isfinite(x))||any(diff(x)<=0)||any(~isfinite(state(:))),error('PDE needs3..4096 increasing nodes and finite matching initial values');end
method=opt(options,'method','fd');boundary=opt(options,'boundary','periodic');integrator=opt(options,'integrator','rk4');if ~any(strcmp(method,{'fd','fem'}))||~any(strcmp(boundary,{'periodic','dirichlet','neumann'}))||~any(strcmp(integrator,{'rk4','imex_euler'})),error('Unsupported target PDE method/boundary/integrator');end
periodic=strcmp(boundary,'periodic');fixed=strcmp(boundary,'dirichlet');width=x(end)-x(1);domainLength=opt(options,'length',width+double(periodic)*(x(2)-x(1)));closing=domainLength-width;diffusion=opt(options,'diffusion',.1);t=opt(options,'t0',0);endTime=opt(options,'t1',1);step=opt(options,'time_step',.001);sample=opt(options,'sample_step',.05);seconds=opt(options,'max_wall_seconds',120);maximum=opt(options,'max_steps',1000000);maxcalls=opt(options,'max_reaction_evaluations',20000000);requested=opt(options,'threads',0);
if any(~isfinite([domainLength,diffusion,t,endTime,step,sample,seconds]))||diffusion<0||endTime<t||step<=0||sample<=0||seconds<=0||seconds>3600||maximum<1||maximum>10000000||maxcalls<1||maxcalls>1000000000||requested<0||requested>64||maximum~=floor(maximum)||maxcalls~=floor(maxcalls)||requested~=floor(requested),error('Invalid PDE target settings/budgets');end
if periodic&&closing<=0,error('Periodic grid omits repeated endpoint; length exceeds stored span');end
if ~periodic&&abs(domainLength-width)>1e-12*max(1,abs(width)),error('Nonperiodic length must match coordinate span');end
if ~fixed&&(isfield(options,'left')||isfield(options,'right')),error('left/right apply only to Dirichlet boundaries');end
h=diff(x);minimum=min(h);if periodic,minimum=min(minimum,closing);end;dx=h(1);
if strcmp(method,'fd')&&(any(abs(h-dx)>1e-9*dx)||(periodic&&abs(closing-dx)>1e-9*dx)),error('FD requires a uniform complete grid');end
stability=.6;if strcmp(method,'fem'),stability=.2;end;if strcmp(integrator,'rk4')&&diffusion*step/minimum^2>stability,error('Explicit PDE diffusion stability bound exceeded');end
if (ceil((endTime-t)/sample)+2)*n>1000000,error('PDE retained field exceeds1million entries');end
bindings=opt(options,'parameters',struct);bound=names(4:end);if ~isequal(sort(fieldnames(bindings)),sort(bound(:))),error('Bind all trailing PDE parameters exactly once');end
parameters=cell(size(bound));for k=1:numel(bound),parameters{k}=penta_runtime('cast',bindings.(bound{k}),kinds{k+3});end
mass=sparse(n,n);stiffness=sparse(n,n);weights=zeros(n,1);
if strcmp(method,'fem')
    count=n-1;if periodic,count=n;end
    for a=1:count,b=mod(a,n)+1;if b==1,delta=closing;else,delta=x(b)-x(a);end;weights(a)=weights(a)+delta/2;weights(b)=weights(b)+delta/2;indices=[a,b];mass(indices,indices)=mass(indices,indices)+delta*[2,1;1,2]/6;stiffness(indices,indices)=stiffness(indices,indices)+diffusion*[1,-1;-1,1]/delta;end
else
    mass=speye(n);weights(:)=dx;if ~periodic,weights([1,end])=weights([1,end])/2;end
    for a=1:n,left=a-1;right=a+1;if a==1,if periodic,left=n;else,left=2;end;end;if a==n,if periodic,right=1;else,right=n-1;end;end;stiffness(a,a)=stiffness(a,a)+2*diffusion/dx^2;stiffness(a,left)=stiffness(a,left)-diffusion/dx^2;stiffness(a,right)=stiffness(a,right)-diffusion/dx^2;end
end
free=1:n;boundaryValues=zeros(n,components);if fixed,free=2:n-1;boundaryValues(1,:)=penta_runtime('pack',{penta_runtime('cast',opt(options,'left',0),kinds{1})}).';boundaryValues(end,:)=penta_runtime('pack',{penta_runtime('cast',opt(options,'right',0),kinds{1})}).';end
state=enforce(state);started=tic;calls=0;steps=0;linearSolves=0;factorizations=0;operations=0;factorKey=nan;factor=[];massFactor=[];
if strcmp(integrator,'rk4')&&strcmp(method,'fem'),massFactor=decomposition(mass(free,free),'lu');end
values=zeros(0,n,components);means=zeros(0,components);rms=zeros(0,1);times=zeros(0,1);startTime=t;sampleIndex=1;saveSample;
while t<endTime
    if steps>=maximum||toc(started)>seconds,error('PDE target step/wall budget exceeded');end
    target=min(endTime,startTime+sampleIndex*sample);if target<=t,sampleIndex=sampleIndex+1;continue;end;dt=min(step,target-t);if t+dt<=t,error('PDE step cannot advance binary64 time');end
    if strcmp(integrator,'rk4')
        a=rhs(t,state);b=rhs(t+dt/2,enforce(state+dt*a/2));c=rhs(t+dt/2,enforce(state+dt*b/2));d=rhs(t+dt,enforce(state+dt*c));state=enforce(state+dt*(a+2*b+2*c+d)/6);
    else
        force=reaction(t,state);key=dt;if nnz(stiffness)==0,key=0;end
        if key~=factorKey,combined=mass+dt*stiffness;factor=decomposition(combined(free,free),'lu');correction=combined*boundaryValues;factorKey=key;factorizations=factorizations+1;end
        load=mass*(state+dt*force)-correction;state(free,:)=factor\load(free,:);state=enforce(state);linearSolves=linearSolves+components;
    end
    if any(~isfinite(state(:))),error('PDE field escaped finite target range');end
    t=t+dt;steps=steps+1;if t>=target-16*eps*max(1,abs(target)),t=target;saveSample;sampleIndex=sampleIndex+1;end
end
out=struct('x',x.','t',times,'values',0,'mean',0,'spatial_rms',rms,'steps',steps,'reaction_evaluations',calls,'native_operations',0,'target_operations',operations,'status','completed','message','Explicit portable binary64 PDE target','method',method,'boundary',boundary,'diffusion',diffusion,'elapsed_seconds',toc(started),'cache_hit',0,'cache_key','','threads_requested',requested,'threads_granted',1,'threads_used',1,'peak_active_threads',1,'parallel_calls',0,'integrator',integrator,'linear_solves',linearSolves,'factorizations',factorizations,'dimension',1,'nx',n,'ny',1);
if components==1,out.values=values(:,:,1);out.mean=means;else,out.values=cell(size(values,1),n);out.mean=cell(size(values,1),1);for row=1:size(values,1),out.mean{row}=struct('c',means(row,:));for col=1:n,out.values{row,col}=struct('c',reshape(values(row,col,:),1,components));end;end;end
    function value=enforce(value),if fixed,value([1,end],:)=boundaryValues([1,end],:);end;end
    function result=reaction(timeValue,value)
        if toc(started)>seconds||calls+n>maxcalls,error('PDE reaction/wall budget exceeded');end;result=zeros(size(value));
        for index=1:n,calls=calls+1;u=value(index,1);if components==5,u=struct('c',value(index,:));end;result(index,:)=penta_runtime('pack',{fn(u,timeValue,x(index),parameters{:})}).';end;operations=operations+n*components;if any(~isfinite(result(:))),error('Nonfinite PDE reaction');end
    end
    function result=rhs(timeValue,value)
        force=reaction(timeValue,value);load=mass*force-stiffness*value;if strcmp(method,'fem'),result=zeros(size(value));result(free,:)=massFactor\load(free,:);else,result=load;end;if fixed,result([1,end],:)=0;end
    end
    function saveSample
        average=sum(weights.*state,1)/sum(weights);index=numel(times)+1;times(index,1)=t;values(index,:,:)=reshape(state,1,n,components);means(index,:)=average;rms(index,1)=sqrt(sum(sum(weights.*(state-average).^2))/sum(weights));
    end
end
)PENTA_PDE_M"
