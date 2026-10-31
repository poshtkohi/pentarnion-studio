use_algebra("A_gamma", gamma=1);
clc;
clear;
matrix A=[2+0.1*i,0.05*j;0.03*ell,3+0.1*k];
matrix known=[1+i+j,1-j+ell;1+ell,2+i];
matrix left_rhs=A*known;
matrix right_rhs=known*A;
matrix left_solution=solveleft(A,left_rhs,options(threads=2));
matrix right_solution=solveright(A,right_rhs,options(threads=2));
for(c=0:4){
    assert(norm(coeff(left_solution-known,c),"fro")<1e-9);
    assert(norm(coeff(right_solution-known,c),"fro")<1e-9);
}
display("Solution of A*X=B:",left_solution);
display("Solution of X*A=C:",right_solution);

% Explicit real coefficient operators are10x10 for this2x2 Pentarnion matrix.
matrix left_operator=leftop(A);
matrix right_operator=rightop(A);
display("Left/right real operator determinants:",det(left_operator),det(right_operator));
matrix inverse_left_operator=inv(left_operator);
assert(norm(left_operator*inverse_left_operator-eye(10),"fro")<1e-9);
display("Left-operator inverse residual:",norm(left_operator*inverse_left_operator-eye(10),"fro"));
display("This is a real coordinate-operator inverse, not a Pentarnion matrix inverse.");
