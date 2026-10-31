clc;
clear;

native function matrix<2,2> matrix_square(matrix<2,2> A) {
    return A*A;
}

matrix<2,2> A = [1,2;3,4];
matrix<2,2> B = matrix_square(A);
matrix J = jacobian(matrix_square,A);
matrix expected = [2,3,2,0;2,5,0,2;3,0,5,3;0,3,2,8];
assert(norm(J-expected,"fro") < 1e-12);
record verification = derivative_check(matrix_square,A);
assert(verification.passed);

display("A:",A);
display("A squared:",B);
display("Full row-major coordinate Jacobian:",J);
display("Independent finite-difference verification:",verification);

% Literal/end assignment and source column-major indexing are supported.
native function matrix<2,2> edited_transpose(matrix<2,2> input) {
    matrix<2,2> local = input;
    local(end) = input(1);
    return local.';
}
display("Edited transpose:",edited_transpose(A));
