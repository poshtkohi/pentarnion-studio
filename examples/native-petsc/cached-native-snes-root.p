native function [real first,real second] residual(real x,real y,real target) {
    return x*x+y-target,x+y*y-target;
}
record first=petscroot(residual,[1,1],options(parameters=options(target=3.0),atol=1e-12));
record second=petscroot(residual,[1,1],options(parameters=options(target=4.0),atol=1e-12));
display("First / changed runtime target: ",first.x,second.x);
display("Same compiled native residual and analytic Jacobian: ",first.cache_key==second.cache_key);
display("Second run native cache hit: ",second.cache_hit);
