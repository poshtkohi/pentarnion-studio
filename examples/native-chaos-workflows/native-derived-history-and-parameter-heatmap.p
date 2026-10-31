use_algebra("A_gamma",gamma=1);
ode driven(t,pentarnion p;real rate,real drive){p'=-rate*p+drive*i;}
native function pentarnion force(real t,pentarnion p,real rate,real drive){
    return -rate*p+drive*i;
}
native function pentarnion associator_history(real t,pentarnion p,real rate,real drive){
    return (p*i)*j-p*(i*j);
}
record e=discover(driven,[1,0,0,0,0],options(
    parameters=options(rate=searchset([0.5,1]),drive=searchset([0.1,0.2])),
    t1=2,sample_step=0.02,threads=4));
record output=exportexperiment(driven,e,options(
    directory="derived-discovery-plots",ids=[1,2,3,4],
    kinds="heatmap,force,associator,return_error",
    derived=options(force=force,associator=associator_history),
    heatmap_parameters=[1,2],heatmap_value="maximum_norm",
    units=options(time="s",force="state/s",associator="state"),
    filename_template="{prefix}_{kind}_{run6}.png",overwrite=1,
    width=800,height=600));
display(output);
% Histories show Euclidean norms of all native callback output coefficients.
% The compiler caches each callback body independently of runtime parameters.
