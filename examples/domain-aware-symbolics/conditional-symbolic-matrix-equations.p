clc;
clear;
record A = casmatrix("a,1;1,1",options(symbols="a"));
record b = casmatrix("1;0",options(symbols="a"));
record x = caslinsolve(A,b);
display("Conditional symbolic solution:",x.expression);
display("Nonzero pivot conditions:",x.nonzero);
display("At a=3:",casnumeric(x,options(a=3)).real);
display("Determinant:",casdet(A).expression);
display("Checked native matrix function:",cascode(x,"conditional_matrix_solution"));

record product = casmatmul(A,x);
display("Independent A*x check at a=3:",casnumeric(product,options(a=3)).real);
