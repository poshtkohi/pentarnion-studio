R"PENTA_OPT_PY(
def p_scientific_minimize(function,point,settings,metadata):
    from scipy.optimize import minimize,Bounds,OptimizeResult,direct
    options=p_options(settings);meta=p_options(metadata)
    p_check_options(options,{'parameters','algorithm','jacobian','lower','upper','parameter_scale','inequality','equality','inequality_parameters','equality_parameters','constraint_tolerance','ftol','ftol_abs','xtol','xtol_abs','max_evaluations','max_wall_seconds','history_limit','initial_step','threads'})
    x,restore=p_real_point(point);n=len(x);algorithm=options.get('algorithm','lbfgs')
    targets={'lbfgs':'L-BFGS-B','nelder_mead':'Nelder-Mead','slsqp':'SLSQP','cobyla':'COBYLA','bobyqa':'COBYQA','direct':'DIRECT'}
    if algorithm not in targets:raise ValueError('Unmapped explicit optimization algorithm')
    target=targets[algorithm];analytic=options.get('jacobian','analytic') in ('analytic','automatic','ad')
    if not analytic and options.get('jacobian')!='finite_difference':raise ValueError('Unknown target derivative selector')
    def vector(key,fallback):
        value=np.asarray(options.get(key,fallback),dtype=float).ravel(order='C')
        if value.size==1:value=np.repeat(value,n)
        if value.size!=n:raise ValueError(key+' does not match unknown coordinates')
        return value
    scale=vector('parameter_scale',1.);lower=vector('lower',-np.inf);upper=vector('upper',np.inf)
    if np.any(scale<=0) or not np.all(np.isfinite(scale)) or np.any(lower>=upper) or np.any(x<lower) or np.any(x>upper):raise ValueError('Invalid target bounds/scales')
    if algorithm=='direct' and (not np.all(np.isfinite(lower)) or not np.all(np.isfinite(upper))):raise ValueError('DIRECT requires explicit finite bounds')
    bound=p_bound_callback(function,meta['objective'],n,options.get('parameters'));constraints=[]
    for kind in ('inequality','equality'):
        if kind in options:
            if target not in ('SLSQP','COBYLA'):raise ValueError('This target algorithm has no requested nonlinear constraint mapping')
            if target=='COBYLA' and kind=='equality':raise ValueError('COBYLA equality conversion needs a separate tolerance contract')
            constraints.append((kind,p_bound_callback(options[kind],meta[kind],n,options.get(kind+'_parameters'))))
        elif kind+'_parameters' in options:raise ValueError('Constraint parameter bindings require their callback')
    maximum=p_integer_option(options,'max_evaluations',10000,1,10000000);seconds=float(options.get('max_wall_seconds',60));limit=p_integer_option(options,'history_limit',min(33333,maximum),0,33333);tolerance=float(options.get('constraint_tolerance',1e-8));started=time.perf_counter();requested=p_integer_option(options,'threads',0,0,64)
    if maximum<1 or maximum>10000000 or not 0<seconds<=3600 or not 0<=limit<=33333 or not np.isfinite(tolerance) or tolerance<=0:raise ValueError('Invalid target optimization resource/constraint bounds')
    evaluations=0;objective_evaluations=0;constraint_evaluations=0;gradient_evaluations=0;history=[];best=None;cache={}
    class BudgetStop(Exception):pass
    class ToleranceStop(Exception):pass
    previous_accepted=None
    if target not in ('L-BFGS-B','SLSQP'):
        for key in ('ftol','xtol'):
            if float(options.get(key,0))!=0:raise ValueError(key+' has no relative-tolerance mapping for this target algorithm; choose supported absolute controls')
        if target in ('COBYLA','COBYQA','DIRECT') and float(options.get('ftol_abs',0))!=0:raise ValueError('Objective-change ftol_abs is not mapped for this target algorithm')
    def evaluate(callback,index,z,derivative):
        nonlocal evaluations,objective_evaluations,constraint_evaluations,gradient_evaluations
        key=(index,tuple(z));old=cache.get(key)
        if old is not None and (not derivative or old[1] is not None):return old
        if evaluations>=maximum or time.perf_counter()-started>seconds:raise BudgetStop
        evaluations+=1
        if index==0:objective_evaluations+=1
        else:constraint_evaluations+=1
        if derivative:
            values,J=p_first_derivative(callback,z*scale);J=J*scale[None,:];gradient_evaluations+=1
        else:values=np.asarray(p_pack([callback(z*scale)]),dtype=float);J=None
        if not np.all(np.isfinite(values)) or (J is not None and not np.all(np.isfinite(J))):raise ValueError('Nonfinite target objective/constraint')
        if index==0 and len(values)!=1:raise ValueError('Optimization objective must be one real scalar')
        if len(values)>128:raise ValueError('Target constraint count exceeds128')
        # Limit retained callback snapshots independently of the solver history.
        if len(cache)>2*(len(constraints)+1):cache.clear()
        cache[key]=(values,J);return values,J
    def violation(z):
        result=0.
        for index,(kind,callback) in enumerate(constraints,1):
            values,_=evaluate(callback,index,z,False);result=max(result,float(np.max(np.abs(values))) if kind=='equality' else float(np.max(np.maximum(0,values))))
        return result
    def objective(z):
        nonlocal best
        values,J=evaluate(bound,0,z,analytic and target in ('L-BFGS-B','SLSQP'));value=float(values[0]);v=violation(z);candidate=(v>tolerance,v if v>tolerance else 0.,value)
        if best is None or candidate<best[0]:best=(candidate,np.asarray(z).copy(),value,v)
        return (value,J[0]) if analytic and target in ('L-BFGS-B','SLSQP') else value
    def observer(z):
        nonlocal previous_accepted
        result=objective(np.asarray(z));value=result[0] if isinstance(result,tuple) else result;v=violation(z)
        if len(history)<limit:history.append([evaluations,value,v])
        if target in ('L-BFGS-B','SLSQP') and previous_accepted is not None and v<=tolerance:
            old,old_value=previous_accepted;position=np.asarray(z)*scale
            xtol=float(options.get('xtol',1e-10));xabs=float(options.get('xtol_abs',1e-12));ftol=float(options.get('ftol',1e-10));fabs=float(options.get('ftol_abs',1e-12))
            if np.all(np.abs(position-old)<=xabs+xtol*np.abs(position)) or abs(value-old_value)<=fabs+ftol*max(abs(value),abs(old_value)):raise ToleranceStop
        previous_accepted=(np.asarray(z).copy()*scale,value)
    converted=[]
    for index,(kind,callback) in enumerate(constraints,1):
        sign=-1 if kind=='inequality' else 1
        entry={'type':'ineq' if kind=='inequality' else 'eq','fun':lambda z,fn=callback,i=index,s=sign:s*evaluate(fn,i,z,False)[0]}
        if analytic and target=='SLSQP':entry['jac']=lambda z,fn=callback,i=index,s=sign:s*evaluate(fn,i,z,True)[1]
        converted.append(entry)
    solver_options={'maxiter':maximum}
    if target=='L-BFGS-B':solver_options.update(maxfun=maximum,ftol=float(options.get('ftol',1e-10)))
    elif target=='Nelder-Mead':solver_options.update(maxfev=maximum,xatol=float(options.get('xtol_abs',1e-12)),fatol=float(options.get('ftol_abs',1e-12)))
    elif target=='SLSQP':solver_options['ftol']=max(float(options.get('ftol',1e-10)),float(options.get('ftol_abs',1e-12)))
    elif target in ('COBYLA','COBYQA'):solver_options.update(maxfev=maximum,final_tr_radius=float(options.get('xtol_abs',1e-10))) if target=='COBYQA' else solver_options.update(catol=tolerance,tol=float(options.get('xtol_abs',1e-10)))
    if options.get('initial_step',0):
        if target=='COBYLA':solver_options['rhobeg']=float(options['initial_step'])
        elif target=='COBYQA':solver_options['initial_tr_radius']=float(options['initial_step'])
        else:raise ValueError('initial_step is not mapped for this target algorithm')
    stopped=False
    try:
        if target=='DIRECT':solved=direct(objective,list(zip(lower/scale,upper/scale)),maxfun=maximum,maxiter=maximum,callback=observer,len_tol=float(options.get('xtol_abs',1e-6)))
        else:solved=minimize(objective,x/scale,jac=True if analytic and target in ('L-BFGS-B','SLSQP') else ('3-point' if target in ('L-BFGS-B','SLSQP') else None),method=target,bounds=Bounds(lower/scale,upper/scale),constraints=converted,callback=observer,options=solver_options)
        objective(solved.x)
    except BudgetStop:
        stopped=True
        if best is None:raise ValueError('Target optimization budget ended before a valid candidate')
        solved=OptimizeResult(x=best[1],success=False,status=-1,message='Target evaluation/wall budget reached')
    except ToleranceStop:
        solved=OptimizeResult(x=best[1],success=True,status=2,message='Explicit exported step/objective tolerance reached')
    chosen=best if best is not None else (None,solved.x,float(solved.fun),violation(solved.x));position=chosen[1]*scale;feasible=chosen[3]<=tolerance
    return SimpleNamespace(x=restore(position),objective=chosen[2],constraint_violation=chosen[3],feasible=int(feasible),converged=int(solved.success and feasible and not stopped),status='converged' if solved.success and feasible and not stopped else 'target_stopped',message=str(solved.message),flag=int(solved.status),evaluations=evaluations,objective_evaluations=objective_evaluations,constraint_evaluations=constraint_evaluations,gradient_evaluations=gradient_evaluations,native_evaluations=0,history=np.asarray(history,dtype=float).reshape(-1,3),elapsed_seconds=time.perf_counter()-started,algorithm=algorithm,jacobian_method=options.get('jacobian','analytic'),seed=0,backend='scipy.optimize/'+target,cache_hit=0,cache_key='',threads_requested=requested,threads_used=1)
)PENTA_OPT_PY"
