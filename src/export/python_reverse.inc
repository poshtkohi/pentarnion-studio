R"PENTA_REVERSE_PY(

def p_target_cost(callback,t,y,parameters,widths):
    parameter_widths=[5 if isinstance(value,Penta) else 1 for value in parameters];n=len(y);point=np.concatenate(([t],y,p_pack(parameters)))
    value,J,_=p_target_ad(lambda seeded:callback(seeded[0],*p_unpack_groups(seeded[1:1+n],widths),*p_unpack_groups(seeded[1+n:],parameter_widths)),point,1)
    if len(value)!=1:raise ValueError('Target objective/cost must be one real scalar')
    return float(value[0]),J[0]

def p_reverse_epoch(function,parameters,widths,forward,first,last,covector,parameter_gradient,running,settings,mass=None):
    from scipy.integrate import solve_ivp
    n=len(covector);np_=len(parameter_gradient);calls=0;started=time.perf_counter();seconds=float(settings.get('max_seconds',120));cache=None
    def coefficients(t):
        nonlocal calls,cache
        if time.perf_counter()-started>seconds:raise ValueError('Target reverse epoch wall allowance exceeded')
        tolerance=64*np.finfo(float).eps*max(1,abs(first),abs(last))
        if t<first-tolerance or t>last+tolerance:raise ValueError('Target reverse stage outside retained forward epoch')
        t=min(max(t,first),last)
        if cache is not None and t==cache[0]:return cache[1:]
        y=np.asarray(forward.sol(t))[:n];_,J,_=p_target_field(function,t,y,parameters,widths,mass,1);L=0.;gradient=np.zeros(1+n+np_)
        if running is not None:L,gradient=p_target_cost(running,t,y,parameters,widths)
        calls+=1;cache=(t,J,L,gradient);return J,L,gradient
    def rhs(t,z):
        J,L,g=coefficients(t);return np.concatenate((-J[:,:n].T@z[:n]-g[1:1+n],-J[:,n:].T@z[:n]-g[1+n:],[-L]))
    def jacobian(t,z):
        J,_,_=coefficients(t);matrix=np.zeros((n+np_+1,n+np_+1));matrix[:n,:n]=-J[:,:n].T;matrix[n:n+np_,:n]=-J[:,n:].T;return matrix
    initial=np.concatenate((covector,parameter_gradient,[0.]));result=solve_ivp(rhs,(last,first),initial,method='BDF',jac=jacobian,rtol=settings.get('rtol',1e-7),atol=settings.get('atol',1e-9))
    if not result.success:raise ValueError(result.message)
    return result.y[:n,-1],result.y[n:n+np_,-1],float(result.y[-1,-1]),len(result.t)-1,calls

def p_ode_adjoint(function,initial,settings,metadata):
    started=time.perf_counter();options=p_options(settings);meta=p_options(metadata);p_check_options(options,{'parameters','objective','running_cost','t0','t1','sample_step','rtol','atol','method','jacobian','mass','initial_step','max_step','max_seconds'})
    if 'objective' not in options:raise ValueError('Target adjoint requires terminal objective')
    bindings=p_options(options.get('parameters'));names=meta['parameters'];pwidths=meta['parameter_widths']
    if set(bindings)!=set(names):raise ValueError('Bind every adjoint model parameter once')
    parameters=[Penta(bindings[name]) if width==5 else float(bindings[name]) for name,width in zip(names,pwidths)];widths=meta['widths'];state=np.asarray(p_pack([initial]),dtype=float);forward_settings={key:value for key,value in options.items() if key not in ('parameters','objective','running_cost')};forward=p_ode_target(function,meta['states'],widths,p_unpack_groups(state,widths),parameters,forward_settings);stored=forward.metadata;last=float(forward.fields['t'][-1,0]);value,gradient=p_target_cost(options['objective'],last,stored.values[-1],parameters,widths);n=len(state);lambda_=gradient[1:1+n];gp=gradient[1+n:];running_cost=0.;steps=0
    if stored.result is not None:
        forward_settings['max_seconds']=float(options.get('max_seconds',120))-(time.perf_counter()-started)
        if forward_settings['max_seconds']<=0:raise ValueError('Target adjoint aggregate wall allowance exceeded')
        mass=None
        if 'mass' in options:mass=lambda *unused:np.asarray(options['mass'],dtype=float)
        lambda_,gp,running_cost,steps,_=p_reverse_epoch(function,parameters,widths,stored.result,float(forward.fields['t'][0,0]),last,lambda_,gp,options.get('running_cost'),forward_settings,mass)
    return SimpleNamespace(objective=value+running_cost,running_cost=running_cost,reverse_table='scipy_BDF_continuous_adjoint',parameter_gradient=np.asarray(gp)[:,None],initial_gradient=np.asarray(lambda_)[:,None],backward_steps=steps,checkpoints=0 if stored.result is None else len(stored.result.t),solution=forward,cache_hit=0,cache_key='')

def p_hybrid_adjoint(function,event,reset,objective,initial,settings,metadata):
    started=time.perf_counter();options=p_options(settings);meta=p_options(metadata);running=options.pop('running_cost',None);event_cost=options.pop('event_cost',None);seconds=float(options.get('max_seconds',120))
    if options.get('sensitivities','none')!='none' or 'sensitivity_parameters' in options or 'sensitivity_initial' in options:raise ValueError('Hybrid target reverse adjoint does not silently replace forward seed requests')
    options['sensitivities']='none';trajectory=p_hybrid(function,event,reset,initial,options,meta)
    if not trajectory.event_info.derivatives_available:raise ValueError('Target hybrid adjoint requires differentiable transverse event groups')
    if trajectory.events and not trajectory.terminal_event and float(options.get('t1',1))-float(trajectory.event_times[-1,0])<float(options.get('minimum_event_interval',1e-8)):raise ValueError('Differentiated target hybrid event lies at fixed observation endpoint')
    n=trajectory.dimension;np_=trajectory.parameter_count;widths=meta['widths'];pwidths=meta['parameter_widths'];parameters=p_unpack_groups(trajectory.parameter_values.ravel(),pwidths);final=trajectory.values[-1];last=float(trajectory.t[-1,0]);value,terminal_gradient=p_target_cost(objective,last,final,parameters,widths);lambda_=terminal_gradient[1:1+n].copy();gp=terminal_gradient[1+n:].copy();running_total=event_total=0.;backward_steps=checkpoints=0;before=[];after=[]
    for epoch in range(trajectory.events,-1,-1):
        first=float(trajectory.event_times[epoch-1,0]) if epoch else float(trajectory.t[0,0]);last_epoch=float(trajectory.event_times[epoch,0]) if epoch<trajectory.events else last
        if last_epoch>first:
            options['max_seconds']=seconds-(time.perf_counter()-started)
            if options['max_seconds']<=0:raise ValueError('Target hybrid adjoint aggregate wall allowance exceeded')
            forward=trajectory._target_epochs[epoch].metadata.result;lambda_,gp,integral,steps,_=p_reverse_epoch(function,parameters,widths,forward,first,last_epoch,lambda_,gp,running,options);running_total+=integral;backward_steps+=steps;checkpoints+=len(forward.t)
            if trajectory.steps+backward_steps>int(options.get('max_steps',1000000)):raise ValueError('Target hybrid adjoint aggregate step allowance exceeded')
        if epoch:
            index=epoch-1;t=float(trajectory.event_times[index,0]);pre=trajectory.event_before[index];post=trajectory.event_after[index];winner=int(trajectory.event_info.indices[index,0])-1;lambda_post=lambda_.copy();after.append(lambda_post.copy());Xi=trajectory.saltation[index].reshape(n,n);Bp=trajectory.parameter_jumps[index].reshape(n,np_);lambda_=Xi.T@lambda_post;gp+=Bp.T@lambda_post
            _,roots,_=p_target_ad(lambda point:event(point[0],*p_unpack_groups(point[1:1+n],widths),*p_unpack_groups(point[1+n:],pwidths)),np.concatenate(([t],pre,p_pack(parameters))),1);g=roots[winner];den=float(trajectory.event_denominators[index,0]);fminus=p_target_field(function,t,pre,parameters,widths,None,0)[0];fplus=p_target_field(function,t,post,parameters,widths,None,0)[0];Lminus=Lplus=0.;direct=np.zeros(1+n+np_)
            if running is not None:Lminus=p_target_cost(running,t,pre,parameters,widths)[0];Lplus=p_target_cost(running,t,post,parameters,widths)[0]
            boundary=Lminus-Lplus
            if event_cost is not None:
                callback=(lambda *args:event_cost(float(winner+1),*args)) if meta.get('indexed_event_cost',False) else event_cost;cost,direct=p_target_cost(callback,t,pre,parameters,widths);event_total+=cost;boundary+=direct[0]+direct[1:1+n]@fminus
            if trajectory.terminal_event and epoch==trajectory.events:boundary+=terminal_gradient[0]+terminal_gradient[1:1+n]@fplus+Lplus
            lambda_+=direct[1:1+n]-boundary*g[1:1+n]/den;gp+=direct[1+n:]-boundary*g[1+n:]/den;before.append(lambda_.copy())
    trajectory.adjoint=SimpleNamespace(objective=value+running_total+event_total,reverse_table='scipy_BDF_epoch_continuous_adjoint',running_cost=running_total,event_cost=event_total,terminal_time=last,running_cache_key='',event_cost_cache_key='',initial_gradient=lambda_[None,:],parameter_gradient=gp[None,:],event_before=np.asarray(before[::-1]).reshape(-1,n),event_after=np.asarray(after[::-1]).reshape(-1,n),backward_steps=backward_steps,replay_steps=0,checkpoints=checkpoints,elapsed_seconds=time.perf_counter()-started,objective_cache_key='',request_hash='',status='moving_terminal_target_adjoint' if trajectory.terminal_event else 'fixed_terminal_target_adjoint')
    return trajectory
)PENTA_REVERSE_PY"
