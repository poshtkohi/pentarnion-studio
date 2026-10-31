R"PENTA_MECH_M(
function out=pentamechanics(fn,q0,v0,options,names)
checkoptions(options,{'form','method','parameters','mass','force_layout','t0','t1','time_step','sample_step','max_wall_seconds','max_steps','max_evaluations','newton_tolerance','regularity_tolerance','max_newton_iterations'});
form=opt(options,'form','newton');fallback='rk4';if strcmp(form,'hamiltonian'),fallback='midpoint';end;method=opt(options,'method',fallback);layout='state';if strcmp(form,'newton'),layout=opt(options,'force_layout','position');end
if ~any(strcmp(form,{'newton','lagrangian','hamiltonian'}))||(~strcmp(form,'newton')&&(isfield(options,'mass')||isfield(options,'force_layout'))),error('L/H models declare their own mass/coordinate functions');end
q=q0(:);v=v0(:);n=numel(q);if n<1||n>8||numel(v)~=n||any(~isfinite([q;v]))||~any(strcmp(method,{'rk4','verlet','midpoint'}))||~any(strcmp(layout,{'position','state'}))||(strcmp(method,'verlet')&&(~strcmp(form,'newton')||~strcmp(layout,'position')))||(strcmp(method,'midpoint')&&~strcmp(form,'hamiltonian')),error('Mechanics target method and coordinate layout are incompatible');end
count=1+n*(1+double(strcmp(layout,'state')));bindings=opt(options,'parameters',struct);bound=names(count+1:end);if count>numel(names)||~isequal(sort(fieldnames(bindings)),sort(bound(:))),error('Bind every trailing Newton parameter exactly once');end;parameters=cell(size(bound));for k=1:numel(bound),parameters{k}=bindings.(bound{k});end
masses=opt(options,'mass',1);masses=masses(:);if isscalar(masses),masses=repmat(masses,n,1);end;if numel(masses)~=n||any(~isfinite(masses))||any(masses<=0),error('Newton masses need finite positive entries');end
t=opt(options,'t0',0);endTime=opt(options,'t1',1);step=opt(options,'time_step',.001);sample=opt(options,'sample_step',.01);seconds=opt(options,'max_wall_seconds',120);maximum=opt(options,'max_steps',1000000);maxcalls=opt(options,'max_evaluations',1000000);
if any(~isfinite([t,endTime,step,sample,seconds]))||endTime<t||step<=0||sample<=0||seconds<=0||seconds>3600||maximum<1||maximum>10000000||maxcalls<1||maxcalls>10000000||maximum~=floor(maximum)||maxcalls~=floor(maxcalls)||(ceil((endTime-t)/sample)+2)*(3*n+10)>8000000,error('Invalid Newton target time/resource settings');end
newtonTolerance=opt(options,'newton_tolerance',1e-11);regularity=opt(options,'regularity_tolerance',1e-12);maxNewton=opt(options,'max_newton_iterations',20);if regularity<=0||regularity>=.1||~isfinite(newtonTolerance)||newtonTolerance<=0||maxNewton<1||maxNewton>100||maxNewton~=floor(maxNewton),error('Invalid mechanics regularity/Newton tolerance');end
started=tic;calls=0;steps=0;newtonIterations=0;state=[q;v];times=zeros(0,1);positions=zeros(0,n);velocities=positions;startTime=t;sampleIndex=1;saveSample;
while t<endTime
    if steps>=maximum||toc(started)>seconds,error('Newton target step/wall budget exceeded');end
    target=min(endTime,startTime+sampleIndex*sample);if target<=t,sampleIndex=sampleIndex+1;continue;end;dt=min(step,target-t);if t+dt<=t,error('Newton step cannot advance binary64 time');end
    if strcmp(method,'rk4'),a=rhs(t,state);b=rhs(t+dt/2,state+dt*a/2);c=rhs(t+dt/2,state+dt*b/2);d=rhs(t+dt,state+dt*c);state=state+dt*(a+2*b+2*c+d)/6;
    elseif strcmp(method,'verlet'),half=state(n+1:end)+dt*force(t,state)./(2*masses);position=state(1:n)+dt*half;state=[position;half+dt*force(t+dt,[position;half])./(2*masses)];
    else
        proposed=state+dt*rhs(t,state);converged=false;for iteration=1:maxNewton,newtonIterations=newtonIterations+1;[field,J]=rhs(t+dt/2,(state+proposed)/2);residual=proposed-state-dt*field;if norm(residual,inf)<=newtonTolerance*max([1;abs(state);abs(proposed)]),converged=true;break;end;proposed=proposed-targetsolve(eye(2*n)-dt*J/2,residual);end;if ~converged,error('Implicit midpoint target Newton iteration limit exceeded');end;state=proposed;
    end
    if any(~isfinite(state)),error('Newton state escaped finite target range');end
    t=t+dt;steps=steps+1;if t>=target-16*eps*max(1,abs(target)),t=target;saveSample;sampleIndex=sampleIndex+1;end
end
secondsValue=velocities;momenta=velocities.*masses.';energy=zeros(size(times));modelValues=energy;power=energy;forcePower=energy;timePower=energy;diagnosticStart=calls;
for index=1:numel(times)
    order=1;if strcmp(form,'newton'),order=0;end;[value,J]=model(times(index),[positions(index,:).';secondsValue(index,:).'],order);
    if strcmp(form,'newton'),forcePower(index)=dot(value,velocities(index,:));elseif strcmp(form,'lagrangian'),momenta(index,:)=J(1,2+n:end);energy(index)=dot(velocities(index,:),momenta(index,:))-value;modelValues(index)=value;timePower(index)=-J(1,1);else,momenta(index,:)=secondsValue(index,:);velocities(index,:)=J(1,2+n:end);energy(index)=value;modelValues(index)=value;timePower(index)=J(1,1);end;power(index)=forcePower(index)+timePower(index);
end
kinetic=.5*sum(masses.'.*velocities.^2,2);balanceEnergy=energy;energyKind='lagrangian_or_hamiltonian_energy';if strcmp(form,'newton'),balanceEnergy=kinetic;energyKind='kinetic_energy';end
work=zeros(size(times));negative=work;for index=2:numel(times),dt=times(index)-times(index-1);work(index)=work(index-1)+dt*(power(index)+power(index-1))/2;negative(index)=negative(index-1)+dt*(max(0,-forcePower(index))+max(0,-forcePower(index-1)))/2;end
secondInput='velocity';if strcmp(form,'hamiltonian'),secondInput='momentum';end;jacobian='not_requested';if strcmp(method,'midpoint'),jacobian='analytic_target_hamiltonian_hessian';end
out=struct('t',times,'q',positions,'v',velocities,'p',momenta,'power',power,'force_power',forcePower,'explicit_time_power',timePower,'work',work,'negative_force_work',negative,'energy_balance',balanceEnergy-balanceEnergy(1)-work,'work_quadrature','trapezoidal_retained_samples','balance_energy',energyKind,'diagnostic_evaluations',calls-diagnosticStart,'form',form,'method',method,'second_input',secondInput,'coordinate_dimension',n,'steps',steps,'evaluations',diagnosticStart,'force_evaluations',0,'newton_iterations',newtonIterations,'energy_defined',double(~strcmp(form,'newton')),'native_jacobian',jacobian,'elapsed_seconds',toc(started),'native_operations',0,'cache_hit',0,'cache_key','','force_cache_key','','threads_used',1,'status','completed');
if strcmp(form,'newton'),out.kinetic_energy=kinetic;else,out.energy=energy;out.(form)=modelValues;out.max_relative_energy_drift=max(abs(energy-energy(1)))/max(1,abs(energy(1)));end
    function [value,J,H]=model(timeValue,y,order)
        if calls>=maxcalls||toc(started)>seconds,error('Mechanics target callback/wall allowance exceeded');end;calls=calls+1;point=[timeValue;y(1:n)];if strcmp(layout,'state'),point=[point;y(n+1:end)];end
        if order,[value,J,H]=targetad(@invoke,point,order);else,value=invoke(point);value=penta_runtime('pack',{value});J=[];H=[];end
        expected=1;if strcmp(form,'newton'),expected=n;end;if numel(value)~=expected||any(~isfinite(value)),error('Mechanics model returned wrong finite coordinate layout');end
        function result=invoke(seeded),callargs=cell(1,numel(point));for k=1:numel(point),callargs{k}=seeded(k);end;result=fn(callargs{:},parameters{:});end
    end
    function value=force(timeValue,y),value=model(timeValue,y,0);end
    function [value,jacobian]=rhs(timeValue,y)
        jacobian=[];if strcmp(form,'newton'),value=[y(n+1:end);force(timeValue,y)./masses];return;end
        [~,J,H]=model(timeValue,y,2);gradient=J(1,:).';hessian=reshape(H(1,:,:),1+2*n,1+2*n);
        if strcmp(form,'hamiltonian'),value=[gradient(2+n:end);-gradient(2:1+n)];jacobian=[hessian(2+n:end,2:end);-hessian(2:1+n,2:end)];return;end
        mass=hessian(2+n:end,2+n:end);singular=svd(mass);if singular(end)<=regularity*singular(1),error('Singular Lagrangian velocity Hessian needs a DAE target adapter');end
        load=gradient(2:1+n)-hessian(2+n:end,1)-hessian(2+n:end,2:1+n)*y(n+1:end);value=[y(n+1:end);targetsolve(mass,load)];
    end
    function saveSample,index=numel(times)+1;times(index,1)=t;positions(index,:)=state(1:n).';velocities(index,:)=state(n+1:end).';end
end
)PENTA_MECH_M"
