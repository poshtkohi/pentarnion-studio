clc;
clear;

% Files contain data only; imports never execute script text or spreadsheet cells.
algebra D = import_algebra("dual-numbers.json");
element<D> p = 2 + 3*D.eps;
assert(p*p == 4 + 12*D.eps);
display("Imported dual-number square:", p*p);
display("Checked property evidence:", algebra_info(D).properties);
display("Sparse product rows [left1,right1,output1,coefficient]:", algebra_products(D));

% A unit can be a linear combination of basis elements.
algebra M = import_algebra("matrix-two-by-two.json");
element<M> identity = 1;
assert(identity == M.E11 + M.E22);
element a = 2*M.E11 + M.E12 + 3*M.E22;
element b = M.E11 - 2*M.E21 + M.E22;
display("Imported matrix-algebra product a*b:", a*b);
element recovered = solveleft(a,a*b);
assert(algebra_norm(recovered-b) < 1e-11);
display("Recovered b from a*x=a*b:", recovered);
display("Source/table hashes:", algebra_info(M).source_hash, algebra_info(M).hash);

% Optional export through Studio's existing native CSV/XLSX support:
% writematrix(algebra_products(M),"matrix-products.csv");
% writematrix(algebra_products(M),"matrix-products.xlsx","Products");
