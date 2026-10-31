native function real identifiable(real slope,real intercept,real x){return slope*x+intercept;}
record proof=structuralidentifiability(identifiable);
display("Exact affine polynomial structural status: ",proof.status);
display("Parameter coefficient matrix: ",proof.coefficient_matrix);
display("Exact rational certificate: ",proof.coefficient_matrix_exact);
native function real confounded(real a,real b,real x){return (a+b)*x;}
record ambiguity=structuralidentifiability(confounded);
display(ambiguity.status);
display("Exact parameter-null directions: ",ambiguity.nullspace_exact);
native function real nonlinear(real amplitude,real x){return amplitude*amplitude*x;}
record unsupported=structuralidentifiability(nonlinear);
display("No theorem was substituted for this nonlinear model: ",unsupported.reason);
% The theorem concerns the ideal real curve observed on an open x interval.
% It does not assert practical identifiability from finite/noisy measurements.
