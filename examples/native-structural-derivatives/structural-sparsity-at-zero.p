% No derivative pattern is inferred from this numerical zero.
native function [real quadratic,real oscillation] model(real x,real y,real weight){
    return weight*x*x,sin(y);
}
record incidence=jacobianpattern(model);
display("All three declared input coordinates: ",incidence);
record at_zero=jacobiansparse(model,[0,0],options(parameters=options(weight=2.0)));
display("The stored zero derivative remains in the structural CSR: ",at_zero);
record changed=jacobiansparse(model,[2,1],options(parameters=options(weight=3.0)));
display("Same native body, changed numerical input and parameter: ",changed);
assert(changed.cache_key==at_zero.cache_key);
assert(changed.pattern_key==at_zero.pattern_key);
assert(at_zero.nnz==2&&at_zero.values(1)==0);
