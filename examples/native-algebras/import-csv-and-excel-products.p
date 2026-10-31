clc;
clear;

% Import numeric rows [left1,right1,output1,coefficient] without executing cells.
algebra FromCSV = import_algebra("dual-products-metadata.json","dual-products.csv");
algebra FromExcel = import_algebra("dual-products-metadata.json","dual-products.xlsx","Products");
element p = 2 + 3*FromCSV.eps;
element q = 2 + 3*FromExcel.eps;
assert(p*p == 4 + 12*FromCSV.eps);
assert(q*q == 4 + 12*FromExcel.eps);
display("CSV table square:", p*p);
display("Excel table square:", q*q);
display("Floating identity evidence:", algebra_info(FromExcel).properties);

% Numerical identity checks do not establish exact power-associativity.
% Whole-element exp/sin/cos require a builtin or a table proved with exact data.
% Use dual-numbers.json when exact rational verification is required.
