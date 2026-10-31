R"PENTA_PDE_PY(

def p_pde(name,*args):
    if name=='pdecoeff':
        field,index=args;index=int(index);values=np.asarray(field.values);pent=values.size and isinstance(values.flat[0],Penta)
        if index<0 or index>=(5 if pent else 1):raise ValueError('PDE coefficient index out of range')
        return np.asarray([value.c[index] if pent else value for value in values.flat],dtype=float).reshape(values.shape)
    from scipy.sparse import lil_matrix,eye
    from scipy.sparse.linalg import splu
    function,axis,initial,options,names,kinds=args;options=p_options(options)
    p_check_options(options,{'parameters','method','boundary','length','diffusion','t0','t1','time_step','sample_step','left','right','max_steps','max_reaction_evaluations','max_wall_seconds','threads','integrator'})
    x=np.asarray(axis,dtype=float).ravel(order='F');n=len(x);components=5 if kinds[0]=='pentarnion' else 1;state=np.asarray(p_pack([initial]),dtype=float).reshape(-1,components)
    if not 3<=n<=4096 or len(state)!=n or not np.all(np.isfinite(x)) or not np.all(np.diff(x)>0) or not np.all(np.isfinite(state)):raise ValueError('PDE needs3..4096 increasing nodes and finite matching initial values')
    method=options.get('method','fd');boundary=options.get('boundary','periodic');integrator=options.get('integrator','rk4')
    if method not in ('fd','fem') or boundary not in ('periodic','dirichlet','neumann') or integrator not in ('rk4','imex_euler'):raise ValueError('Unsupported explicit PDE target method/boundary/integrator')
    periodic=boundary=='periodic';fixed=boundary=='dirichlet';width=x[-1]-x[0];length=float(options.get('length',width+(x[1]-x[0] if periodic else 0)));closing=length-width;diffusion=float(options.get('diffusion',.1));t=float(options.get('t0',0));end=float(options.get('t1',1));step=float(options.get('time_step',.001));sample=float(options.get('sample_step',.05));seconds=float(options.get('max_wall_seconds',120));maximum=p_integer_option(options,'max_steps',1000000,1,10000000);maxcalls=p_integer_option(options,'max_reaction_evaluations',20000000,1,1000000000);requested=p_integer_option(options,'threads',0,0,64)
    if not np.all(np.isfinite([length,diffusion,t,end,step,sample,seconds])) or diffusion<0 or end<t or step<=0 or sample<=0 or not 0<seconds<=3600 or not 1<=maximum<=10000000 or not 1<=maxcalls<=1000000000 or not 0<=requested<=64:raise ValueError('Invalid PDE target settings/budgets')
    if periodic and closing<=0:raise ValueError('Periodic grid omits repeated endpoint; length exceeds stored span')
    if not periodic and abs(length-width)>1e-12*max(1,abs(width)):raise ValueError('Nonperiodic length must match stored coordinate span')
    if not fixed and ('left' in options or 'right' in options):raise ValueError('left/right apply only to Dirichlet data')
    h=np.diff(x);minimum=min(float(h.min()),closing) if periodic else float(h.min());dx=x[1]-x[0]
    if method=='fd' and (np.any(abs(h-dx)>1e-9*dx) or (periodic and abs(closing-dx)>1e-9*dx)):raise ValueError('FD requires a uniform complete grid')
    if integrator=='rk4' and diffusion*step/minimum**2>(.2 if method=='fem' else .6):raise ValueError('Explicit PDE diffusion stability bound exceeded')
    if (np.ceil((end-t)/sample)+2)*n>1000000:raise ValueError('PDE retained field exceeds1million values')
    bound=p_options(options.get('parameters'));tail=names[3:]
    if set(bound)!=set(tail):raise ValueError('Bind all trailing PDE parameters exactly once')
    parameters=[p_cast(bound[key],kind) for key,kind in zip(tail,kinds[3:])];mass=lil_matrix((n,n));stiffness=lil_matrix((n,n));weights=np.zeros(n)
    if method=='fem':
        for a in range(n if periodic else n-1):
            b=(a+1)%n;delta=x[b]-x[a] if b else closing;weights[a]+=delta/2;weights[b]+=delta/2
            for i,row in enumerate((a,b)):
                for j,col in enumerate((a,b)):mass[row,col]+=delta*(2 if i==j else 1)/6;stiffness[row,col]+=diffusion*(1 if i==j else -1)/delta
    else:
        mass=eye(n,format='lil');weights[:]=dx
        if not periodic:weights[[0,-1]]/=2
        for a in range(n):
            left=a-1 if a else n-1 if periodic else 1;right=a+1 if a+1<n else 0 if periodic else n-2;stiffness[a,a]+=2*diffusion/dx**2;stiffness[a,left]-=diffusion/dx**2;stiffness[a,right]-=diffusion/dx**2
    mass=mass.tocsc();stiffness=stiffness.tocsc();free=np.arange(1,n-1) if fixed else np.arange(n);boundary_values=np.zeros((n,components))
    if fixed:
        for index,key in [(0,'left'),(n-1,'right')]:boundary_values[index]=p_pack([p_cast(options.get(key,0),kinds[0])])
    def enforce(value):
        value=value.copy()
        if fixed:value[[0,-1]]=boundary_values[[0,-1]]
        return value
    state=enforce(state);started=time.perf_counter();calls=steps=linear_solves=factorizations=operations=0;factor_key=None;implicit_factor=None;mass_factor=splu(mass[free,:][:,free]) if integrator=='rk4' and method=='fem' else None
    def reaction(time_value,value):
        nonlocal calls,operations
        if time.perf_counter()-started>seconds or calls+n>maxcalls:raise ValueError('PDE target reaction/wall budget exceeded')
        result=np.empty_like(value)
        for index in range(n):
            calls+=1;u=Penta(value[index]) if components==5 else float(value[index,0]);result[index]=p_pack([function(u,time_value,float(x[index]),*parameters)])
        operations+=n*components
        if not np.all(np.isfinite(result)):raise ValueError('Nonfinite PDE reaction')
        return result
    def rhs(time_value,value):
        force=reaction(time_value,value);load=mass@force-stiffness@value
        if method=='fem':result=np.zeros_like(value);result[free]=mass_factor.solve(load[free])
        else:result=load
        if fixed:result[[0,-1]]=0
        return result
    values=[];means=[];rms=[];times=[];start=t;sample_index=1
    def save():
        average=np.sum(weights[:,None]*state,axis=0)/weights.sum();times.append(t);values.append(state.copy());means.append(average);rms.append(np.sqrt(np.sum(weights[:,None]*(state-average)**2)/weights.sum()))
    save()
    while t<end:
        if steps>=maximum or time.perf_counter()-started>seconds:raise ValueError('PDE target step/wall budget exceeded')
        target=min(end,start+sample_index*sample)
        if target<=t:sample_index+=1;continue
        dt=min(step,target-t)
        if t+dt<=t:raise ValueError('PDE time step cannot advance binary64 time')
        if integrator=='rk4':
            a=rhs(t,state);b=rhs(t+dt/2,enforce(state+dt*a/2));c=rhs(t+dt/2,enforce(state+dt*b/2));d=rhs(t+dt,enforce(state+dt*c));state=enforce(state+dt*(a+2*b+2*c+d)/6)
        else:
            force=reaction(t,state);key=dt if stiffness.nnz else 0.
            if key!=factor_key:combined=mass+dt*stiffness;implicit_factor=splu(combined[free,:][:,free]);correction=combined@boundary_values;factor_key=key;factorizations+=1
            load=mass@(state+dt*force)-correction;state[free]=implicit_factor.solve(load[free]);state=enforce(state);linear_solves+=components
        if not np.all(np.isfinite(state)):raise ValueError('PDE field escaped finite target range')
        t+=dt;steps+=1
        if t>=target-16*np.finfo(float).eps*max(1,abs(target)):t=target;save();sample_index+=1
    def unpack(data):
        data=np.asarray(data)
        if components==1:return data[...,0]
        result=np.empty(data.shape[:-1],dtype=object)
        for index in np.ndindex(result.shape):result[index]=Penta(data[index])
        return result
    return SimpleNamespace(x=x[None,:],t=np.asarray(times)[:,None],values=unpack(values),mean=unpack(means).reshape(-1,1),spatial_rms=np.asarray(rms)[:,None],steps=steps,reaction_evaluations=calls,native_operations=0,target_operations=operations,status='completed',message='Explicit portable binary64 PDE target',method=method,boundary=boundary,diffusion=diffusion,elapsed_seconds=time.perf_counter()-started,cache_hit=0,cache_key='',threads_requested=requested,threads_granted=1,threads_used=1,peak_active_threads=1,parallel_calls=0,integrator=integrator,linear_solves=linear_solves,factorizations=factorizations,dimension=1,nx=n,ny=1)
)PENTA_PDE_PY"
