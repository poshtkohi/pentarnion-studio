R"PENTA_HYBRID_PY(

def p_hybrid(function,event,reset,initial,settings,metadata):
    options=p_options(settings);meta=p_options(metadata);p_check_options(options,{'parameters','t0','t1','sample_step','method','jacobian','rtol','atol','max_steps','max_samples','max_seconds','max_bytes','event_direction','event_terminal','minimum_event_interval','grazing_tolerance','max_events','event_priority','simultaneous','event_time_tolerance','event_gradient_tolerance','sensitivities','sensitivity_parameters','sensitivity_initial'})
    names=meta['states'];widths=meta['widths'];parameter_names=meta['parameters'];parameter_widths=meta['parameter_widths'];bindings=p_options(options.get('parameters'))
    if set(bindings)!=set(parameter_names):raise ValueError('Bind every hybrid model parameter exactly once')
    parameters=[Penta(bindings[key]) if width==5 else float(bindings[key]) for key,width in zip(parameter_names,parameter_widths)];flat_parameters=np.asarray(p_pack(parameters),dtype=float);state=np.asarray(p_pack([initial]),dtype=float);n=len(state);np_=len(flat_parameters);started=time.perf_counter()
    if n!=sum(widths) or not 1<=n<=32 or np_>64:raise ValueError('Hybrid target requires complete1..32 state and<=64 parameter coordinates')
    time_value=float(options.get('t0',0));end=float(options.get('t1',1));sample=float(options.get('sample_step',.01));minimum=float(options.get('minimum_event_interval',1e-8));grazing=float(options.get('grazing_tolerance',1e-10));time_tolerance=float(options.get('event_time_tolerance',1e-8));gradient_tolerance=float(options.get('event_gradient_tolerance',1e-8));maximum=p_integer_option(options,'max_events',100,1,10000);max_steps=p_integer_option(options,'max_steps',1000000,1,10000000);max_samples=p_integer_option(options,'max_samples',1000000,1,1000000);max_bytes=p_integer_option(options,'max_bytes',128*1024*1024,1024,1024**3);seconds=float(options.get('max_seconds',120));policy=options.get('simultaneous','priority')
    if not end>time_value or minimum<=0 or grazing<=0 or time_tolerance<=0 or not 0<gradient_tolerance<.01 or policy not in ('priority','all') or options.get('method','bdf')!='bdf' or options.get('jacobian','analytic')!='analytic':raise ValueError('Invalid hybrid target interval/root policy or unsupported solver mapping')
    def root_ad(t,y):return p_target_ad(lambda point:event(point[0],*p_unpack_groups(point[1:1+n],widths),*p_unpack_groups(point[1+n:],parameter_widths)),np.concatenate(([t],y,flat_parameters)),1)
    roots,J,_=root_ad(time_value,state);components=len(roots)
    if not 1<=components<=16:raise ValueError('Hybrid event callback requires1..16 real roots')
    if any(value==0 or (np.sum(abs(row)) and abs(value)/np.sum(abs(row))<=grazing) for value,row in zip(roots,J)):raise ValueError('Hybrid initial state lies on/too near an event surface')
    def flags(key,default,low,high):
        values=np.asarray(options.get(key,default),dtype=float).ravel();values=np.repeat(values,components) if len(values)==1 else values
        if len(values)!=components or np.any(values!=np.floor(values)) or np.any(values<low) or np.any(values>high):raise ValueError('Invalid hybrid per-event flags')
        return values.astype(int)
    event_directions=flags('event_direction',0,-1,1);terminals=flags('event_terminal',0,0,1);priorities=flags('event_priority',0,-1000000,1000000);selection=options.get('sensitivities','all');parameter_directions=list(range(np_)) if selection in ('parameters','all') else []
    if selection not in ('none','parameters','initial','all'):raise ValueError('Invalid hybrid sensitivity selection')
    if 'sensitivity_parameters' in options:
        if selection in ('initial','all'):raise ValueError('Explicit parameter directions cannot combine with initial/all')
        raw=np.asarray(options['sensitivity_parameters'],dtype=float).ravel();parameter_directions=[int(v)-1 for v in raw]
        if len(set(parameter_directions))!=len(raw) or any(v!=k+1 or not 0<=k<np_ for v,k in zip(raw,parameter_directions)):raise ValueError('Invalid one-based hybrid parameter directions')
    initial_directions=list(range(n)) if selection in ('initial','all') else [];directions=parameter_directions+[-1]*len(initial_directions);d=len(directions);S=np.zeros((n,d));S[:,len(parameter_directions):]=np.eye(n) if initial_directions else np.zeros((n,0))
    if 'sensitivity_initial' in options:
        S=np.asarray(options['sensitivity_initial'],dtype=float)
        if S.shape!=(n,d) or not d:raise ValueError('Hybrid sensitivity seeds need n rows and selected columns')
    original_seeds=S.copy();initial_state=state.copy();times=[];values=[];slopes=[];sensitivities=[];segments=[];sides=[];events=[];before=[];after=[];taus=[];fixed_before=[];fixed_after=[];total_before=[];total_after=[];saltations=[];jumps=[];denominators=[];transverse=[];indices=[];group_indices=[];group_offsets=[0];segment=1;steps=calls=event_calls=0;derivatives_available=True;terminal=False;last_event=-np.inf;epoch_solutions=[]
    def append(t,y,s,side,flow):
        times.append(t);values.append(y.copy());slopes.append(flow.copy());sensitivities.append(s.T.ravel().copy());segments.append(segment);sides.append(side)
        if len(times)>max_samples or (len(times)*(2*n+n*d+3)+len(events)*(4*n*d+n*n+n*np_+d+2*n+3))*8>max_bytes:raise ValueError('Hybrid target retained-coordinate allowance exceeded')
    append(time_value,state,S,0,p_target_field(function,time_value,state,parameters,widths,None,0)[0])
    while time_value<end:
        epoch_start=time_value
        def callback(t,*groups):
            output=p_pack([event(t,*groups)])
            if t==epoch_start:
                flow=p_target_field(function,t,state,parameters,widths,None,0)[0];_,root_gradient,_=root_ad(t,state);signs=root_gradient[:,0]+root_gradient[:,1:1+n]@flow
                output=[np.nextafter(0.,1. if sign>=0 else -1.) if value==0 else value for value,sign in zip(output,signs)]
            return tuple(output)
        epoch_settings={key:options[key] for key in ('sample_step','method','jacobian','rtol','atol','sensitivity_parameters') if key in options};epoch_settings.update(t0=time_value,t1=end,events=callback,event_direction=event_directions,event_terminal=np.ones(components),sensitivities=selection,max_seconds=seconds-(time.perf_counter()-started))
        if d:epoch_settings['sensitivity_initial']=S
        solved=p_ode_target(function,names,widths,p_unpack_groups(state,widths),parameters,epoch_settings);epoch_solutions.append(solved);stored=solved.metadata;steps+=0 if stored.result is None else len(stored.result.t)-1;calls+=stored.rhs_evaluations;event_calls+=stored.event_evaluations
        if steps>max_steps or time.perf_counter()-started>seconds:raise ValueError('Hybrid target aggregate step/wall allowance exceeded')
        for row in range(1,len(solved.fields['t'])):
            t=float(solved.fields['t'][row,0]);y=stored.values[row];seed=stored.sensitivities[row].reshape(d,n).T if d else S;append(t,y,seed,-1 if stored.event_terminal and row+1==len(stored.values) else 0,p_target_field(function,t,y,parameters,widths,None,0)[0])
        time_value=float(solved.fields['t'][-1,0]);state=stored.values[-1].copy();S=stored.sensitivities[-1].reshape(d,n).T if d else S
        if not stored.event_terminal:break
        if len(events)>=maximum or time_value-last_event<minimum:raise ValueError('Hybrid target event/chattering allowance exceeded')
        last_event=time_value;roots,J,_=root_ad(time_value,state);fminus=p_target_field(function,time_value,state,parameters,widths,None,0)[0];den=J[:,0]+J[:,1:1+n]@fminus;scale=abs(J[:,0])+abs(J[:,1:1+n]*fminus).sum(axis=1);active=[i for i in range(components) if (event_directions[i]==0 or np.sign(den[i])==event_directions[i]) and (roots[i]==0 or (den[i]!=0 and abs(roots[i]/den[i])<=time_tolerance))]
        if not active:raise ValueError('Localized target event has no identifiable original surface')
        active.sort(key=lambda i:(priorities[i],i));winner=active[0];relative=abs(den[winner])/scale[winner] if scale[winner] else 0.;valid=relative>grazing
        if valid:
            for i in active:
                if not scale[i] or abs(den[i])/scale[i]<=grazing or np.any(abs(J[i]/den[i]-J[winner]/den[winner])>gradient_tolerance*(1+abs(J[i]/den[i])+abs(J[winner]/den[winner]))):valid=False;break
        if valid:
            last_step=abs(stored.result.t[-1]-stored.result.t[-2]);delta=min(np.sqrt(np.finfo(float).eps)*max(1,abs(time_value)),.001*(time_value-epoch_start),.1*last_step)
            if delta>0:
                _,probe,_=root_ad(time_value-delta,state-delta*fminus);probe_den=probe[:,0]+probe[:,1:1+n]@fminus
                if any(abs(den[i])<=grazing*abs(probe_den[i]) for i in active):valid=False
        if d and not valid:raise ValueError('Simultaneous/grazing hybrid event branch has undefined or unequal event-time derivatives')
        derivatives_available=derivatives_available and valid;applied=active if policy=='all' else [winner];terminal=any(terminals[i] for i in applied)
        if d and not terminal and end-time_value<minimum:raise ValueError('Differentiated hybrid event lies at fixed observation endpoint')
        pre=state.copy();R=np.column_stack((np.zeros(n),np.eye(n),np.zeros((n,np_))));post=pre.copy()
        for index in applied:
            def reset_ad(point):return reset(*([float(index+1)] if meta.get('indexed_reset',False) else []),point[0],*p_unpack_groups(point[1:1+n],widths),*p_unpack_groups(point[1+n:],parameter_widths))
            post,local,_=p_target_ad(reset_ad,np.concatenate(([time_value],post,flat_parameters)),1);composed=local[:,1:1+n]@R;composed[:,0]+=local[:,0];composed[:,1+n:]+=local[:,1+n:];R=composed
        if len(post)!=n:raise ValueError('Reset target callback lost state coefficients')
        fplus=p_target_field(function,time_value,post,parameters,widths,None,0)[0];correction=R[:,0]+R[:,1:1+n]@fminus-fplus;denominator=den[winner] if den[winner] else 1.;Xi=R[:,1:1+n]-np.outer(correction,J[winner,1:1+n])/denominator;Bp=R[:,1+n:]-np.outer(correction,J[winner,1+n:])/denominator;tau=np.zeros(d)
        for direction,index in enumerate(directions):tau[direction]=-(J[winner,1:1+n]@S[:,direction]+(J[winner,1+n+index] if index>=0 else 0))/denominator
        postS=R[:,1:1+n]@S+np.outer(correction,tau)
        for direction,index in enumerate(directions):
            if index>=0:postS[:,direction]+=R[:,1+n+index]
        events.append(time_value);before.append(pre);after.append(post);taus.append(tau);fixed_before.append(S.T.ravel());fixed_after.append(postS.T.ravel());total_before.append((S+np.outer(fminus,tau)).T.ravel());total_after.append((postS+np.outer(fplus,tau)).T.ravel());saltations.append(Xi.ravel());jumps.append(Bp.ravel());denominators.append(den[winner]);transverse.append(relative);indices.append(winner+1);group_indices.extend(i+1 for i in active);group_offsets.append(len(group_indices));state=post;S=postS;segment+=1;append(time_value,state,S,1,fplus)
        if terminal:break
    matrix=lambda data,columns:np.asarray(data,dtype=float).reshape(-1,columns) if columns else np.empty((0,0));event_info=SimpleNamespace(components=components,indices=np.asarray(indices)[:,None],group_offsets=np.asarray(group_offsets)[None,:],group_indices=np.asarray(group_indices)[None,:],directions=event_directions[None,:],terminal=terminals[None,:],priorities=priorities[None,:],policy=policy,derivatives_available=int(derivatives_available),time_tolerance=time_tolerance,gradient_tolerance=gradient_tolerance)
    result=SimpleNamespace(format='PentarnionHybridODE',version=1,t=np.asarray(times)[:,None],values=matrix(values,n),derivatives=matrix(slopes,n),sensitivities=matrix(sensitivities,d*n),segment_ids=np.asarray(segments)[:,None],sides=np.asarray(sides)[:,None],event_times=np.asarray(events)[:,None],event_info=event_info,event_before=matrix(before,n),event_after=matrix(after,n),event_time_derivatives=matrix(taus,d),event_fixed_before=matrix(fixed_before,d*n),event_fixed_after=matrix(fixed_after,d*n),event_total_before=matrix(total_before,d*n),event_total_after=matrix(total_after,d*n),saltation=matrix(saltations,n*n),parameter_jumps=matrix(jumps,n*np_),event_denominators=np.asarray(denominators)[:,None],transversality=np.asarray(transverse)[:,None],terminal_time_derivatives=(taus[-1][None,:] if terminal and d else np.empty((0,0))),terminal_value_derivatives=(np.asarray(total_after[-1])[None,:] if terminal and d else np.empty((0,0))),dimension=n,parameter_count=np_,directions=d,segments=segment,events=len(events),steps=steps,evaluations=calls,sensitivity_evaluations=calls if d else 0,event_evaluations=event_calls,terminal_event=int(terminal),cache_hit=0,elapsed_seconds=time.perf_counter()-started,cache_key='',event_cache_key='',reset_cache_key='',rhs_derivative_key='',request_hash='',gamma=GAMMA,initial=initial_state[None,:],parameter_values=flat_parameters[None,:],direction_parameters=np.asarray([i+1 if i>=0 else 0 for i in directions])[None,:],direction_initials=np.asarray([0]*len(parameter_directions)+[i+1 for i in initial_directions])[None,:],initial_seeds=original_seeds,state_names=','.join(names),state_widths=np.asarray(widths)[None,:],parameter_layout=SimpleNamespace(**{name:SimpleNamespace(offset=1+sum(parameter_widths[:i]),components=parameter_widths[i]) for i,name in enumerate(parameter_names)}),coordinate_layout='flat_declared_state_coefficients',sensitivity_layout='direction_major_then_flat_state',derivative_semantics='fixed-time samples; moving-event total derivatives; explicit target saltation',method='scipy_bdf',jacobian='analytic_target_ad2',native_operations=0,source_hash='',threads_used=1,status='terminal_event' if terminal else 'completed',settings=SimpleNamespace(**options))
    if not derivatives_available:
        for key in ('saltation','parameter_jumps','event_time_derivatives','event_fixed_before','event_fixed_after','event_total_before','event_total_after','terminal_time_derivatives','terminal_value_derivatives'):setattr(result,key,np.empty((0,0)))
    result._target_epochs=epoch_solutions
    return result

def p_hybridval(result,query,side='post'):
    if side not in ('pre','post'):raise ValueError('Hybrid interpolation side is pre/post')
    output=[];times=result.t[:,0]
    for t in np.asarray(query,dtype=float).ravel(order='F'):
        if not times[0]<=t<=times[-1]:raise ValueError('Hybrid query outside retained interval')
        exact=np.flatnonzero(times==t)
        if len(exact):output.append(result.values[exact[0] if side=='pre' else exact[-1]]);continue
        right=int(np.searchsorted(times,t));left=right-1
        if result.segment_ids[left]!=result.segment_ids[right]:raise ValueError('Hybrid interpolation cannot bridge a reset')
        h=times[right]-times[left];u=(t-times[left])/h;output.append((2*u**3-3*u**2+1)*result.values[left]+(u**3-2*u**2+u)*h*result.derivatives[left]+(-2*u**3+3*u**2)*result.values[right]+(u**3-u**2)*h*result.derivatives[right])
    return np.asarray(output)
)PENTA_HYBRID_PY"
