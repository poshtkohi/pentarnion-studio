R"PENTA_ADV_PY(

def p_target_ad(function,point,order=1):
    point=np.asarray(point,dtype=float).ravel();n=len(point)
    if n>128:raise ValueError('Target analytic coordinate derivative bound128 exceeded')
    seeded=[Dual(v,np.eye(n)[k],np.zeros((n,n)) if order==2 else None) for k,v in enumerate(point)];result=p_pack([function(seeded)])
    if len(result)*n*n>1048576 and order==2:raise ValueError('Target Hessian coordinate allowance exceeded')
    values=np.asarray([a.value if isinstance(a,Dual) else a for a in result],dtype=float);first=np.asarray([a.tangent if isinstance(a,Dual) else np.zeros(n) for a in result]);second=np.asarray([a._h() if isinstance(a,Dual) else np.zeros((n,n)) for a in result]) if order==2 else None
    if not np.all(np.isfinite(values)) or not np.all(np.isfinite(first)) or (second is not None and not np.all(np.isfinite(second))):raise ValueError('Nonfinite target analytic derivative')
    return values,first,second

def p_unpack_groups(flat,widths):
    result=[];offset=0
    for width in widths:result.append(Penta(flat[offset:offset+5]) if width==5 else flat[offset]);offset+=width
    if offset!=len(flat):raise ValueError('Complete target coordinate layout mismatch')
    return result

def p_target_solve(M,b,firstM=None,firstB=None,secondM=None,secondB=None):
    M=np.asarray(M,dtype=float);b=np.asarray(b,dtype=float);scale=float(np.max(abs(M)));normalized=M/scale if scale else M;u,s,vh=np.linalg.svd(normalized,full_matrices=False)
    if len(s)!=len(b) or not len(s) or s[-1]<=1e-12*s[0]:raise ValueError('Target mass/regularity matrix has no reliable unique inverse')
    solve=lambda rhs:vh.T@((u.T@(rhs/scale))/s.reshape((-1,)+(1,)*(np.asarray(rhs).ndim-1)))
    value=solve(b)
    if np.linalg.norm(M@value-b,np.inf)>1e-10*max(np.finfo(float).tiny,np.linalg.norm(M,np.inf)*np.linalg.norm(value,np.inf)+np.linalg.norm(b,np.inf)):raise ValueError('Target mass solve residual failed')
    if firstB is None:return value
    first=solve(firstB-np.einsum('ijk,j->ik',firstM,value));second=None
    if secondB is not None:
        d=first.shape[1];second=np.empty((len(b),d,d))
        for i in range(d):
            for j in range(d):second[:,i,j]=solve(secondB[:,i,j]-secondM[:,:,i,j]@value-firstM[:,:,i]@first[:,j]-firstM[:,:,j]@first[:,i])
    return value,first,second

class PentaSolution:
    def __init__(self,fields,metadata):self.fields,self.metadata=fields,metadata

def p_target_field(function,t,y,params,widths,mass=None,order=0):
    n=len(y);parameter_widths=[5 if isinstance(value,Penta) else 1 for value in params];point=np.concatenate((y,p_pack(params)))
    def rhs(seeded):return function(t,list(seeded[:n]),p_unpack_groups(seeded[n:],parameter_widths))
    if order:values,J,H=p_target_ad(rhs,point,order)
    else:values=np.asarray(p_pack([rhs(point)]),dtype=float);J=H=None
    if mass is None:return values,J,H
    def matrix(seeded):return mass(t,*p_unpack_groups(seeded[:n],widths),*p_unpack_groups(seeded[n:],parameter_widths))
    if order:
        m,Mj,Mh=p_target_ad(matrix,point,order);M=m.reshape(n,n);Mj=Mj.reshape(n,n,len(point));Mh=Mh.reshape(n,n,len(point),len(point)) if order==2 else None
        return p_target_solve(M,values,Mj,J,Mh,H)
    return p_target_solve(np.asarray(p_pack([matrix(point)]),dtype=float).reshape(n,n),values),None,None

def p_ode_target(function,names,widths,initial,params,settings,mass=None):
    from scipy.integrate import solve_ivp
    t0=float(settings.get('t0',0));t1=float(settings.get('t1',10));sample=float(settings.get('sample_step',.01));y0=np.asarray(p_pack(initial),dtype=float);n=len(y0);np_=len(p_pack(params));started=time.perf_counter()
    if not np.all(np.isfinite([t0,t1,sample])) or sample<=0 or not np.all(np.isfinite(y0)) or n>64:raise ValueError('ODE target requires finite scalar-state coordinates/times and positive sample step')
    if settings.get('method','bdf')!='bdf' or settings.get('jacobian','analytic')!='analytic':raise ValueError('This explicit advanced target mapping requires BDF with analytic derivatives')
    if 'mass' in settings:
        if mass is not None:raise ValueError('Ambiguous constant and state-dependent target mass')
        constant=np.asarray(settings['mass'],dtype=float)
        if constant.shape!=(n,n):raise ValueError('Constant target mass shape mismatch')
        mass=lambda *unused:constant
    sensitivity=settings.get('sensitivities','none');selected=list(range(np_)) if sensitivity in ('parameters','all') else [];initial_count=n if sensitivity in ('initial','all') else 0
    if sensitivity not in ('none','parameters','initial','all'):raise ValueError('Unknown target sensitivity selection')
    if 'sensitivity_parameters' in settings:
        if sensitivity in ('initial','all'):raise ValueError('Explicit parameter selection cannot combine with initial/all')
        raw=np.asarray(settings['sensitivity_parameters'],dtype=float).ravel();selected=[int(k)-1 for k in raw]
        if any(k!=v+1 or not 0<=v<np_ for k,v in zip(raw,selected)) or len(set(selected))!=len(selected):raise ValueError('Sensitivity parameter coordinates must be unique1-based model coordinates')
    directions=selected+[-1]*initial_count;nd=len(directions);S=np.zeros((n,nd))
    if initial_count:S[:,len(selected):]=np.eye(n)
    if 'sensitivity_initial' in settings:
        S=np.asarray(settings['sensitivity_initial'],dtype=float)
        if not nd or S.shape!=(n,nd) or not np.all(np.isfinite(S)):raise ValueError('Sensitivity initial seeds need n rows and selected direction columns')
    if settings.get('sensitivity_method','simultaneous')!='simultaneous' or settings.get('sensitivity_error_control',1)!=1:raise ValueError('Target sensitivities use simultaneous augmented error-controlled integration only')
    if n*(1+nd)>512 or n*(n+np_)**2>1048576:raise ValueError('Target augmented/analytic Hessian allowance exceeded')
    calls=jac_calls=event_calls=0;maximum=int(settings.get('max_evaluations',1000000));seconds=float(settings.get('max_seconds',120));cached=None
    def field(t,y,order):
        nonlocal calls,cached
        if calls>=maximum or time.perf_counter()-started>seconds:raise ValueError('Target ODE callback/wall budget exceeded')
        key=(t,tuple(y),order)
        if cached is not None and cached[0]==key:return cached[1]
        calls+=1;result=p_target_field(function,t,y,params,widths,mass,order);cached=(key,result);return result
    def rhs(t,z):
        y=z[:n];values,J,_=field(t,y,1 if nd else 0)
        if not nd:return values
        tangent=z[n:].reshape(nd,n).T;derivative=J[:,:n]@tangent
        for d,index in enumerate(directions):
            if index>=0:derivative[:,d]+=J[:,n+index]
        return np.concatenate((values,derivative.T.ravel()))
    def jacobian(t,z):
        nonlocal jac_calls
        jac_calls+=1;_,J,H=field(t,z[:n],2 if nd else 1);matrix=np.zeros((len(z),len(z)));matrix[:n,:n]=J[:,:n]
        if nd:
            tangent=z[n:].reshape(nd,n)
            for d,index in enumerate(directions):
                block=slice(n+d*n,n+(d+1)*n);matrix[block,block]=J[:,:n];matrix[block,:n]=np.einsum('ijk,j->ik',H[:,:n,:n],tangent[d])+(H[:,n+index,:n] if index>=0 else 0)
        return matrix
    roots=[];terminal_flags=[];directions_flags=[];event_count=0
    if 'events' in settings:
        callback=settings['events'];sample_events=np.asarray(p_pack([callback(t0,*p_unpack_groups(y0,widths),*params)]),dtype=float);event_count=len(sample_events)
        if not 1<=event_count<=16:raise ValueError('Target event callback returns1..16 real roots')
        def flags(key,default,low,high):
            raw=np.asarray(settings.get(key,default),dtype=float).ravel();raw=np.repeat(raw,event_count) if len(raw)==1 else raw
            if len(raw)!=event_count or np.any(raw!=np.floor(raw)) or np.any(raw<low) or np.any(raw>high):raise ValueError('Invalid per-event target flags')
            return raw.astype(int)
        directions_flags=flags('event_direction',0,-1,1);terminal_flags=flags('event_terminal',0,0,1)
        for index in range(event_count):
            def event(t,z,i=index):
                nonlocal event_calls
                event_calls+=1;values=np.asarray(p_pack([callback(t,*p_unpack_groups(z[:n],widths),*params)]),dtype=float)
                if len(values)!=event_count or not np.all(np.isfinite(values)):raise ValueError('Invalid target event root layout')
                return values[i]
            event.direction=int(directions_flags[index]);event.terminal=bool(terminal_flags[index]);roots.append(event)
    elif 'event_direction' in settings or 'event_terminal' in settings:raise ValueError('Event flags require callback')
    times=t0+np.arange(int(np.floor(abs(t1-t0)/sample))+1)*math.copysign(sample,t1-t0)
    if len(times)>1000000:raise ValueError('ODE target retained sample allowance exceeded')
    if abs(times[-1]-t1)>1e-13*max(1,abs(t1)):times=np.append(times,t1)
    else:times[-1]=t1
    initial_aug=np.concatenate((y0,S.T.ravel()));result=None;events=[];terminated=False
    if t0==t1:values=initial_aug[:,None]
    else:
        extras={}
        if float(settings.get('initial_step',0))>0:extras['first_step']=float(settings['initial_step'])
        if float(settings.get('max_step',0))>0:extras['max_step']=float(settings['max_step'])
        result=solve_ivp(rhs,(t0,t1),initial_aug,method='BDF',jac=jacobian,dense_output=True,events=roots or None,rtol=settings.get('rtol',1e-7),atol=settings.get('atol',1e-9),**extras)
        if not result.success:raise ValueError(result.message)
        for i,event_times in enumerate(result.t_events or []):
            for time_value in event_times:
                point=np.asarray(result.sol(time_value))[:n];_,gradient,_=p_target_ad(lambda seeded:callback(seeded[0],*p_unpack_groups(seeded[1:],widths),*params),np.concatenate(([time_value],point)),1);flow=field(time_value,point,0)[0];sign=int(np.sign(gradient[i,0]+gradient[i,1:]@flow));events.append((float(time_value),i,point,sign))
        events.sort(key=lambda event:(math.copysign(1,t1-t0)*event[0],event[1]));actual=float(result.t[-1]);terminated=result.status==1;times=times[(times-actual)*math.copysign(1,t1-t0)<=0]
        if not len(times) or abs(times[-1]-actual)>16*np.finfo(float).eps*max(1,abs(actual)):times=np.append(times,actual)
        values=result.sol(times)
    output={'t':times[:,None]};offset=0
    for name,width in zip(names,widths):output[name]=values[offset,:][:,None] if width==1 else np.asarray([Penta(values[offset:offset+5,k]) for k in range(len(times))],dtype=object)[:,None];offset+=width
    metadata=SimpleNamespace(n=n,widths=widths,names=names,values=values[:n].T,sensitivities=values[n:].T.copy(),directions=directions,events=events,event_directions=directions_flags,event_terminal=terminated,result=result,settings=settings,field=field,params=params,elapsed=time.perf_counter()-started,rhs_evaluations=calls,jacobian_evaluations=jac_calls,event_evaluations=event_calls)
    return PentaSolution(output,metadata)

def p_ode_query(name,solution,*args):
    if not isinstance(solution,PentaSolution):raise ValueError('Expected exported checked ODE solution')
    meta=solution.metadata;n=meta.n
    if name=='odesens':
        if not meta.directions:raise ValueError('Solution has no integrated sensitivities')
        if not args:return meta.sensitivities.copy()
        index=int(args[0])-1
        if index+1!=args[0] or not 0<=index<len(meta.directions):raise ValueError('Sensitivity direction out of range')
        return meta.sensitivities[:,index*n:(index+1)*n].copy()
    if name=='odeevents':return SimpleNamespace(t=np.asarray([e[0] for e in meta.events])[:,None],y=np.asarray([e[2] for e in meta.events]).reshape(-1,n),index=np.asarray([e[1]+1 for e in meta.events])[:,None],direction=np.asarray([e[3] for e in meta.events])[:,None],terminal=int(meta.event_terminal))
    if name=='odestats':return SimpleNamespace(method='scipy_bdf',jacobian='analytic_target_ad2' if meta.directions else 'analytic_target_ad1',steps=0 if meta.result is None else len(meta.result.t)-1,rhs_evaluations=meta.rhs_evaluations,jacobian_evaluations=meta.jacobian_evaluations,event_evaluations=meta.event_evaluations,sensitivity_rhs_evaluations=meta.rhs_evaluations if meta.directions else 0,error_test_failures=-1,nonlinear_iterations=-1,last_step=0 if meta.result is None or len(meta.result.t)<2 else meta.result.t[-1]-meta.result.t[-2],last_order=-1,elapsed_seconds=meta.elapsed,cache_hit=0,cache_key='',terminal_event=int(meta.event_terminal),dense_segments=0 if meta.result is None else len(meta.result.t)-1,sensitivity_count=len(meta.directions),sensitivity_parameters=np.asarray([d+1 if d>=0 else 0 for d in meta.directions])[None,:])
    if name=='deval':
        query=np.asarray(args[0],dtype=float);state=args[1] if len(args)>1 else meta.names[0] if len(meta.names)==1 else None
        if state not in meta.names or meta.result is None:raise ValueError('Dense query requires a valid state and nonzero integrated interval')
        low,high=sorted([solution.fields['t'][0,0],solution.fields['t'][-1,0]])
        if np.any(query<low) or np.any(query>high):raise ValueError('Dense query outside retained interval')
        index=meta.names.index(state);offset=sum(meta.widths[:index]);values=meta.result.sol(query.ravel())
        return values[offset].reshape(query.shape) if meta.widths[index]==1 else np.asarray([Penta(values[offset:offset+5,k]) for k in range(query.size)],dtype=object).reshape(query.shape)
    raise ValueError('Unsupported advanced ODE query')

def p_masssolve(rhs,mass,initial,initial_derivative,settings,metadata):
    options=p_options(settings);meta=p_options(metadata);p_check_options(options,{'parameters','mass_parameters','algebraic','initialization','jacobian','t0','t1','sample_step','rtol','atol','initial_residual_tolerance','index_tolerance','max_wall_seconds','initial_step','maximum_step','max_steps','max_evaluations'})
    y=np.asarray(p_pack([initial]),dtype=float);yp=np.asarray(p_pack([initial_derivative]),dtype=float);n=len(y);t0=float(options.get('t0',0));tolerance=float(options.get('initial_residual_tolerance',1e-8))
    if not 1<=n<=32 or len(yp)!=n or not np.all(np.isfinite(y)) or not np.all(np.isfinite(yp)) or not tolerance>0:raise ValueError('Regular mass target needs matching1..32 finite coordinates')
    algebraic=options.get('algebraic','auto')
    if not (isinstance(algebraic,str) and algebraic=='auto') and np.any(np.asarray(algebraic)!=0):raise ValueError('This target mass mapping supports regular nonsingular systems, not algebraic constraints')
    initialization=options.get('initialization','check')
    if initialization not in ('check','consistent') or options.get('jacobian','analytic')!='analytic':raise ValueError('Regular mass target uses check/consistent initial derivatives and analytic AD')
    if float(options.get('index_tolerance',1e-12))!=1e-12:raise ValueError('Regular mass target currently uses its explicit1e-12 conditioning policy')
    def bindings(key,names):
        values=p_options(options.get(key));tail=names[1+n:]
        if len(names)<1+n or set(values)!=set(tail):raise ValueError('Mass/RHS callback must bind every trailing parameter once')
        return [float(values[name]) for name in tail]
    parameters=bindings('parameters',meta['rhs']);mass_parameters=bindings('mass_parameters',meta['mass']);M=np.asarray(mass(t0,*y,*mass_parameters),dtype=float)
    if M.shape!=(n,n):raise ValueError('State/time mass matrix shape mismatch')
    f=np.asarray(p_pack([rhs(t0,*y,*parameters)]),dtype=float);consistent=p_target_solve(M,f);residual=float(np.max(abs(M@yp-f)))
    if initialization=='check' and residual>tolerance:raise ValueError('Initial mass residual exceeds requested consistency tolerance')
    corrected=consistent if initialization=='consistent' else yp;correction=float(np.linalg.norm(corrected-yp));settings={key:options[key] for key in ('t0','t1','sample_step','rtol','atol','initial_step','max_evaluations') if key in options};settings.update(method='bdf',jacobian='analytic',max_seconds=options.get('max_wall_seconds',120))
    if 'maximum_step' in options:settings['max_step']=options['maximum_step']
    rhs_wrapper=lambda t,values,params:p_pack([rhs(t,*values,*params)]);mass_wrapper=lambda t,*values:mass(t,*values[:n],*mass_parameters)
    solved=p_ode_target(rhs_wrapper,['x'+str(k) for k in range(n)],[1]*n,list(y),parameters,settings,mass_wrapper);meta_solution=solved.metadata
    steps=0 if meta_solution.result is None else len(meta_solution.result.t)-1
    if steps>int(options.get('max_steps',1000000)):raise ValueError('Target regular-mass accepted-step allowance exceeded')
    values=meta_solution.values;times=solved.fields['t'][:,0];derivatives=[];maximum_residual=0.
    for time_value,value in zip(times,values):
        derivative=p_target_field(rhs_wrapper,time_value,value,parameters,[1]*n,mass_wrapper,0)[0];matrix=np.asarray(mass(time_value,*value,*mass_parameters),dtype=float);load=np.asarray(p_pack([rhs(time_value,*value,*parameters)]),dtype=float);maximum_residual=max(maximum_residual,float(np.max(abs(matrix@derivative-load))));derivatives.append(derivative)
    return SimpleNamespace(t=times[:,None],values=values,derivatives=np.asarray(derivatives),consistent_initial=y[None,:],consistent_derivative=corrected[None,:],algebraic=np.zeros((1,n)),steps=steps,residual_evaluations=meta_solution.rhs_evaluations,jacobian_evaluations=meta_solution.jacobian_evaluations,initial_residual_norm=residual if initialization=='check' else float(np.max(abs(M@corrected-f))),initial_correction_norm=correction,maximum_sample_residual=maximum_residual,elapsed_seconds=meta_solution.elapsed,native_operations=0,cache_hit=0,cache_key='',mass_cache_key='',classification='regular_mass_verified_each_evaluation',index_contract='explicit_regular_mass_target_no_dae_index_claim',jacobian='analytic_target_mass_derivatives',method='scipy_bdf_regular_mass',status='completed',threads_used=1)
)PENTA_ADV_PY"
