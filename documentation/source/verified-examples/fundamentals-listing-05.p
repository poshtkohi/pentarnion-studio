matrix A = [1,2;3,4];
vector x = [1;2];
display(A*x);             % [5; 11]
display(A(2));            % 3 in column-major indexing
matrix entries = A.*A;    % entrywise products
matrix P = [1+i,j; k,ell];
display(coeff(P,1));      % i coefficients, same array shape
