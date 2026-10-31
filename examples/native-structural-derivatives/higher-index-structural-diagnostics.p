% Structural offsets are candidates, not a general nonlinear index proof.
native function [real first,real second,real third] chain(
    real t,real x,real y,real z,real xp,real yp,real zp){
    return yp-x,zp-y,z-sin(t);
}
record diagnostic=daestructure(chain,0.0,[0,0,0],[0,0,0]);
display("Index-three chain: compiler signature, matching, canonical offsets: ",diagnostic);
assert(diagnostic.index_candidate==3);
assert(!diagnostic.local_initialization_supported);
% This constant pencil has an independently validated native reduction:
native function [real first,real second,real third] forcing(real t){
    return 0.0,0.0,-sin(t);
}
record solved=lineardaesolve([0,1,0;0,0,1;0,0,0],[1,0,0;0,1,0;0,0,1],
    forcing,[0,0,0],options(t1=2,sample_step=0.01));
display("Validated constant-pencil index and consistent initial derivatives: ",solved.index,solved.consistent_derivative);
