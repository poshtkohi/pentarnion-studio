R"PENTA_MECH_PY(

def p_mechanics(function,q0,v0,settings,names):
    options=p_options(settings);p_check_options(options,{'form','method','parameters','mass','force_layout','t0','t1','time_step','sample_step','max_wall_seconds','max_steps','max_evaluations','newton_tolerance','regularity_tolerance','max_newton_iterations'})
    form=options.get('form','newton');method=options.get('method','midpoint' if form=='hamiltonian' else 'rk4');layout=options.get('force_layout','position') if form=='newton' else 'state';q=np.asarray(q0,dtype=float).ravel(order='F');v=np.asarray(v0,dtype=float).ravel(order='F');n=len(q)
    if form not in ('newton','lagrangian','hamiltonian') or (form!='newton' and ('mass' in options or 'force_layout' in options)):raise ValueError('L/H models declare their own mass/coordinate functions')
    if not 1<=n<=8 or len(v)!=n or not np.all(np.isfinite(q)) or not np.all(np.isfinite(v)) or method not in ('rk4','verlet','midpoint') or layout not in ('position','state') or (method=='verlet' and (form!='newton' or layout!='position')) or (method=='midpoint' and form!='hamiltonian'):raise ValueError('Mechanics target method and coordinate layout are incompatible')
    count=1+n*(2 if layout=='state' else 1);bound=p_options(options.get('parameters'));tail=names[count:]
    if count>len(names) or set(bound)!=set(tail):raise ValueError('Bind every trailing Newton callback parameter exactly once')
    parameters=[float(bound[key]) for key in tail];masses=np.asarray(options.get('mass',1.),dtype=float).ravel();masses=np.repeat(masses,n) if masses.size==1 else masses
    if len(masses)!=n or not np.all(np.isfinite(masses)) or np.any(masses<=0):raise ValueError('Newton masses must be finite and positive')
    t=float(options.get('t0',0));end=float(options.get('t1',1));step=float(options.get('time_step',.001));sample=float(options.get('sample_step',.01));seconds=float(options.get('max_wall_seconds',120));maximum=p_integer_option(options,'max_steps',1000000,1,10000000);maxcalls=p_integer_option(options,'max_evaluations',1000000,1,10000000)
    if not np.all(np.isfinite([t,end,step,sample,seconds])) or end<t or step<=0 or sample<=0 or not 0<seconds<=3600 or not 1<=maximum<=10000000 or not 1<=maxcalls<=10000000 or (np.ceil((end-t)/sample)+2)*(3*n+10)>8000000:raise ValueError('Invalid Newton target time/resource settings')
    started=time.perf_counter();calls=0;steps=0;state=np.concatenate((q,v));times=[];positions=[];velocities=[];start=t;sample_index=1
    newton_tolerance=float(options.get('newton_tolerance',1e-11));regularity=float(options.get('regularity_tolerance',1e-12));max_newton=p_integer_option(options,'max_newton_iterations',20,1,100);newton_iterations=0
    if not 0<regularity<.1 or not np.isfinite(newton_tolerance) or newton_tolerance<=0:raise ValueError('Invalid mechanics regularity/Newton tolerance')
    def model(time_value,value,order=0):
        nonlocal calls
        if calls>=maxcalls or time.perf_counter()-started>seconds:raise ValueError('Newton target callback/wall allowance exceeded')
        calls+=1;point=[time_value,*value[:n],*(value[n:] if layout=='state' else [])]
        if order:result,J,H=p_target_ad(lambda seeded:function(*seeded,*parameters),point,order)
        else:result=np.asarray(p_pack([function(*point,*parameters)]),dtype=float);J=H=None
        if len(result)!=(n if form=='newton' else 1) or not np.all(np.isfinite(result)):raise ValueError('Mechanics model returned the wrong finite coordinate layout')
        return result,J,H
    def force(time_value,value):return model(time_value,value)[0]
    def rhs(time_value,value,want_jacobian=False):
        if form=='newton':return np.concatenate((value[n:],force(time_value,value)/masses))
        _,J,H=model(time_value,value,2);gradient=J[0];hessian=H[0]
        if form=='hamiltonian':
            output=np.concatenate((gradient[1+n:],-gradient[1:1+n]));jacobian=np.vstack((hessian[1+n:,1:],-hessian[1:1+n,1:]));return (output,jacobian) if want_jacobian else output
        mass=hessian[1+n:,1+n:];singular=np.linalg.svd(mass,compute_uv=False)
        if singular[-1]<=regularity*singular[0]:raise ValueError('Singular Lagrangian velocity Hessian needs a DAE target adapter')
        load=gradient[1:1+n]-hessian[1+n:,0]-hessian[1+n:,1:1+n]@value[n:];return np.concatenate((value[n:],p_target_solve(mass,load)))
    def save():times.append(t);positions.append(state[:n].copy());velocities.append(state[n:].copy())
    save()
    while t<end:
        if steps>=maximum or time.perf_counter()-started>seconds:raise ValueError('Newton target step/wall allowance exceeded')
        target=min(end,start+sample_index*sample)
        if target<=t:sample_index+=1;continue
        dt=min(step,target-t)
        if t+dt<=t:raise ValueError('Newton step cannot advance binary64 time')
        if method=='rk4':
            a=rhs(t,state);b=rhs(t+dt/2,state+dt*a/2);c=rhs(t+dt/2,state+dt*b/2);d=rhs(t+dt,state+dt*c);state=state+dt*(a+2*b+2*c+d)/6
        elif method=='verlet':
            half=state[n:]+dt*force(t,state)/(2*masses);position=state[:n]+dt*half;trial=np.concatenate((position,half));state=np.concatenate((position,half+dt*force(t+dt,trial)/(2*masses)))
        else:
            proposed=state+dt*rhs(t,state);converged=False
            for _ in range(max_newton):
                newton_iterations+=1;field,J=rhs(t+dt/2,(state+proposed)/2,True);residual=proposed-state-dt*field
                if np.max(abs(residual))<=newton_tolerance*max(1,np.max(abs(state)),np.max(abs(proposed))):converged=True;break
                proposed-=p_target_solve(np.eye(2*n)-dt*J/2,residual)
            if not converged:raise ValueError('Implicit midpoint target Newton iteration limit exceeded')
            state=proposed
        if not np.all(np.isfinite(state)):raise ValueError('Newton state escaped finite target range')
        t+=dt;steps+=1
        if t>=target-16*np.finfo(float).eps*max(1,abs(target)):t=target;save();sample_index+=1
    positions=np.asarray(positions);seconds_value=np.asarray(velocities);velocities=seconds_value.copy();momenta=velocities*masses;energy=np.zeros(len(times));model_values=np.zeros(len(times));power=np.empty(len(times));force_power=np.zeros(len(times));time_power=np.zeros(len(times));diagnostic_start=calls
    for index,time_value in enumerate(times):
        value,J,_=model(time_value,np.concatenate((positions[index],seconds_value[index])),0 if form=='newton' else 1);model_values[index]=0 if form=='newton' else value[0]
        if form=='newton':force_power[index]=np.dot(value,velocities[index])
        elif form=='lagrangian':momenta[index]=J[0,1+n:];energy[index]=velocities[index]@momenta[index]-value[0];time_power[index]=-J[0,0]
        else:momenta[index]=seconds_value[index];velocities[index]=J[0,1+n:];energy[index]=value[0];time_power[index]=J[0,0]
        power[index]=force_power[index]+time_power[index]
    kinetic=.5*np.sum(masses*velocities**2,axis=1);balance_energy=kinetic if form=='newton' else energy
    work=np.zeros(len(times));negative=work.copy()
    for index in range(1,len(times)):dt=times[index]-times[index-1];work[index]=work[index-1]+dt*(power[index]+power[index-1])/2;negative[index]=negative[index-1]+dt*(max(0,-force_power[index])+max(0,-force_power[index-1]))/2
    column=lambda value:np.asarray(value)[:,None]
    result=SimpleNamespace(t=column(times),q=positions,v=velocities,p=momenta,power=column(power),force_power=column(force_power),explicit_time_power=column(time_power),work=column(work),negative_force_work=column(negative),energy_balance=column(balance_energy-balance_energy[0]-work),work_quadrature='trapezoidal_retained_samples',balance_energy='kinetic_energy' if form=='newton' else 'lagrangian_or_hamiltonian_energy',diagnostic_evaluations=calls-diagnostic_start,form=form,method=method,second_input='momentum' if form=='hamiltonian' else 'velocity',coordinate_dimension=n,steps=steps,evaluations=diagnostic_start,force_evaluations=0,newton_iterations=newton_iterations,energy_defined=int(form!='newton'),native_jacobian='analytic_target_hamiltonian_hessian' if method=='midpoint' else 'not_requested',elapsed_seconds=time.perf_counter()-started,native_operations=0,cache_hit=0,cache_key='',force_cache_key='',threads_used=1,status='completed')
    if form=='newton':result.kinetic_energy=column(kinetic)
    else:result.energy=column(energy);result.max_relative_energy_drift=float(np.max(abs(energy-energy[0]))/max(1,abs(energy[0])));setattr(result,form,column(model_values))
    return result
)PENTA_MECH_PY"
