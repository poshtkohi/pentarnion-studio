R"PENTA_SCI_PY(
# Explicit scientific target mappings. Source Ceres/NLopt algorithms do not
# share implementations with SciPy; every result identifies the target backend.
def p_scientific(name,*args):
    if name in ('lsqfit','lsqnonlin','curvefit'):return p_scientific_fit(name,*args)
    if name=='minimize':return p_scientific_minimize(*args)
    raise ValueError('Unmapped scientific target adapter '+name)
def p_options(value):
    if value is None:return {}
    if isinstance(value,dict):return dict(value)
    if isinstance(value,SimpleNamespace):return vars(value).copy()
    raise ValueError('Scientific adapter options require a checked record')
def p_check_options(options,allowed):
    unknown=set(options)-set(allowed)
    if unknown:raise ValueError('Unmapped target options: '+','.join(sorted(unknown)))

def p_integer_option(options,key,fallback,low,high):
    value=options.get(key,fallback)
    if not np.isscalar(value) or not np.isfinite(value) or value!=int(value) or not low<=value<=high:raise ValueError(key+' must be an integral scalar within the target resource allowance')
    return int(value)
def p_real_point(point):
    value=np.asarray(point,dtype=float);flat=value.ravel(order='C').copy()
    if not flat.size or flat.size>128 or not np.all(np.isfinite(flat)):raise ValueError('Target point requires1..128 finite real coordinates')
    def restore(x):
        if isinstance(point,tuple):return tuple(float(v) for v in x)
        if value.ndim==0:return float(x[0])
        return np.asarray(x).reshape(value.shape,order='C').copy()
    return flat,restore
def p_bound_callback(function,names,unknowns,parameters,independent=False):
    fixed=p_options(parameters);bound=names[unknowns:len(names)-(1 if independent else 0)]
    if unknowns>len(names)-(1 if independent else 0):raise ValueError('Initial point exceeds the checked callback coordinate layout')
    if set(bound)!=set(fixed):raise ValueError('Bind every trailing callback parameter once; real unknowns precede fixed parameters')
    tail=[float(fixed[name]) for name in bound]
    return lambda point,x=None:function(*list(point),*tail,*([x] if independent else []))
def p_first_derivative(callback,x):
    point=[Dual(value,np.eye(len(x))[i]) for i,value in enumerate(x)]
    values=p_pack([callback(point)])
    return np.asarray([v.value if isinstance(v,Dual) else v for v in values],dtype=float),np.asarray([v.tangent if isinstance(v,Dual) else np.zeros(len(x)) for v in values],dtype=float)
def p_scientific_fit(name,function,point,*arguments):
    from scipy.optimize import least_squares
    curve=name=='curvefit';names=arguments[-1];options=p_options(arguments[-2]);x,restore=p_real_point(point);n=len(x)
    allowed={'parameters','method','loss','loss_scale','jacobian','weights','lower','upper','parameter_scale','max_iterations','max_evaluations','max_wall_seconds','ftol','gtol','xtol','covariance','noise_variance','threads','history_limit'}
    if curve:allowed|={'observation_weights','coefficient_weights'}
    p_check_options(options,allowed)
    method=options.get('method','lm');target={'lm':'trf','levenberg_marquardt':'trf','dogleg':'dogbox'}.get(method)
    if target is None:raise ValueError('The explicit least-squares target supports LM->TRF and dogleg->dogbox mappings')
    jacobian=options.get('jacobian','analytic');analytic=jacobian in ('analytic','ad','automatic')
    if not analytic and jacobian!='finite_difference':raise ValueError('Unknown Jacobian selector')
    loss=options.get('loss','none');loss='linear' if loss in ('none','linear') else loss
    if loss not in ('linear','huber','cauchy','soft_l1'):raise ValueError('Unmapped robust loss')
    bound=p_bound_callback(function,names,n,options.get('parameters'),curve)
    if curve:
        datax=np.asarray(arguments[0],dtype=float).ravel(order='C');datay=np.asarray(arguments[1],dtype=float);observations=datay.ravel(order='C')
        if not datax.size or observations.size%datax.size:raise ValueError('Curve observations require complete output coordinates per independent value')
        width=observations.size//datax.size
        def residual(point):return [value for independent in datax for value in p_pack([bound(point,independent)])]
        def unweighted(point):
            result=residual(point)
            if len(result)!=len(observations):raise ValueError('Curve model output width disagrees with observations')
            return [value-observed for value,observed in zip(result,observations)]
    else:
        def unweighted(point):return p_pack([bound(point)])
    initial=np.asarray(unweighted(x),dtype=float)
    if not initial.size or initial.size*n>1048576:raise ValueError('Target residual/Jacobian exceeds bounded layout')
    def vector(name,size,fallback):
        a=np.asarray(options.get(name,fallback),dtype=float).ravel(order='C')
        if a.size==1:a=np.repeat(a,size)
        if a.size!=size:raise ValueError(name+' has the wrong coordinate count')
        return a
    weights=vector('weights',len(initial),1.)
    if curve:
        weights*=np.repeat(vector('observation_weights',len(datax),1.),width)*np.tile(vector('coefficient_weights',width,1.),len(datax))
    if not np.all(np.isfinite(weights)) or np.any(weights<0) or not np.any(weights>0):raise ValueError('Residual weights must be finite nonnegative with a positive entry')
    root_weights=np.sqrt(weights);scale=vector('parameter_scale',n,1.);lower=vector('lower',n,-np.inf);upper=vector('upper',n,np.inf)
    if np.any(scale<=0) or not np.all(np.isfinite(scale)) or np.any(lower>=upper) or np.any(x<lower) or np.any(x>upper):raise ValueError('Invalid target parameter scales/bounds')
    started=time.perf_counter();evaluations=0;iterations=0;history=[];previous=x.copy();maximum_iterations=int(options.get('max_iterations',200));maximum_seconds=float(options.get('max_wall_seconds',60));history_limit=int(options.get('history_limit',min(20000,maximum_iterations+1)));fscale=float(options.get('loss_scale',1))
    if maximum_iterations<1 or maximum_iterations>1000000 or not 0<=history_limit<=20000 or not 0<maximum_seconds<=3600 or not np.isfinite(fscale) or fscale<=0:raise ValueError('Invalid target least-squares resource/loss bounds')
    def robust_cost(values):
        z=(values/fscale)**2
        rho=z if loss=='linear' else np.where(z<=1,z,2*np.sqrt(z)-1) if loss=='huber' else np.log1p(z) if loss=='cauchy' else 2*(np.sqrt(1+z)-1)
        return float(.5*fscale*fscale*np.sum(rho))
    def check():
        if time.perf_counter()-started>maximum_seconds:raise TimeoutError('Target least-squares wall allowance exceeded')
    def fun(z):
        nonlocal evaluations
        check();evaluations+=1;values=np.asarray(unweighted(z*scale),dtype=float)
        if not np.all(np.isfinite(values)):raise ValueError('Nonfinite exported residual')
        return root_weights*values
    def jac(z):
        check();values,J=p_first_derivative(unweighted,z*scale);return root_weights[:,None]*J*scale[None,:]
    def observer(intermediate_result):
        nonlocal iterations,previous
        iterations+=1;z=np.asarray(intermediate_result.x);position=z*scale;step=float(np.linalg.norm(position-previous));previous=position.copy();residual=fun(z);J=jac(z) if analytic else np.empty((0,n))
        if len(history)<history_limit:history.append([1,iterations,robust_cost(residual),float(np.max(np.abs(J.T@residual))) if J.size else 0.,step])
        if iterations>=maximum_iterations:raise StopIteration
    solved=least_squares(fun,x/scale,jac=jac if analytic else '3-point',bounds=(lower/scale,upper/scale),method=target,loss=loss,f_scale=float(options.get('loss_scale',1)),ftol=float(options.get('ftol',1e-10)),gtol=float(options.get('gtol',1e-10)),xtol=float(options.get('xtol',1e-10)),max_nfev=int(options.get('max_evaluations',10000)),callback=observer)
    result_x=solved.x*scale;raw=np.asarray(unweighted(result_x),dtype=float)
    if analytic:_,raw_jacobian=p_first_derivative(unweighted,result_x)
    else:
        raw_jacobian=np.empty((len(raw),n))
        for column in range(n):
            h=np.cbrt(np.finfo(float).eps)*(1+abs(result_x[column]));a=result_x.copy();b=result_x.copy();a[column]=min(upper[column],a[column]+h);b[column]=max(lower[column],b[column]-h);raw_jacobian[:,column]=(np.asarray(unweighted(a),dtype=float)-np.asarray(unweighted(b),dtype=float))/(a[column]-b[column])
    weighted=raw_jacobian*root_weights[:,None];u,s,vh=np.linalg.svd(weighted,full_matrices=False);rank=int(np.sum(s>(s[0]*1e-12 if len(s) else 0)));rcond=float(s[-1]/s[0]) if len(s)>=n and s[0] else 0.;covariance=np.empty((0,0));valid=False;noise=float(options.get('noise_variance',-1));assumption='Unavailable: target covariance contract not satisfied'
    inactive=np.all(result_x>lower+1e-8*(1+abs(result_x))) and np.all(result_x<upper-1e-8*(1+abs(result_x)));dof=int(np.count_nonzero(weights))-rank
    if options.get('covariance',0) and solved.success and loss=='linear' and inactive and rank==n and rcond>1e-12 and (noise>=0 or dof>0):
        if noise<0:noise=float(np.dot(root_weights*raw,root_weights*raw)/dof)
        covariance=(vh.T/(s*s))@vh*noise;valid=True;assumption='Local linearization; independent Gaussian residuals after sqrt(weights) scaling; full column rank and inactive bounds. Local rank is not parameter uniqueness.'
    report=dict(x=restore(result_x),residuals=raw.reshape(-1,1),jacobian=raw_jacobian,jacobian_available=1,covariance=covariance,covariance_valid=int(valid),covariance_assumption=assumption,noise_variance=noise,cost=float(solved.cost),initial_cost=robust_cost(root_weights*initial),residual_norm=float(np.linalg.norm(raw)),weighted_residual_norm=float(np.linalg.norm(root_weights*raw)),gradient_norm=float(np.max(np.abs(solved.grad/scale))),rcond=rcond,rank=rank,iterations=iterations,evaluations=evaluations,jacobian_evaluations=int(solved.njev or 0),native_evaluations=0,starts_completed=1,best_start=1,converged=int(solved.success),status='converged' if solved.success else 'target_stopped',message=solved.message,flag=int(solved.status),history=np.asarray(history,dtype=float).reshape(-1,5),elapsed_seconds=time.perf_counter()-started,method=method,loss='none' if loss=='linear' else loss,jacobian_method=jacobian,backend='scipy.least_squares/'+target,cache_hit=0,cache_key='',threads_requested=int(options.get('threads',0)),threads_used=1,backend_threads=1,seed=0)
    if curve:report.update(xdata=datax.reshape(-1,1),fitted=(raw+observations).reshape(len(datax),width),observations=observations.reshape(len(datax),width))
    return SimpleNamespace(**report)
)PENTA_SCI_PY"
