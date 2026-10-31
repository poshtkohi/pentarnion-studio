clc;
clear;
% Recreate the example product data with Studio's native CSV/XLSX writer.
matrix products = [1,1,1,1; 1,2,2,1; 2,1,2,1];
writematrix(products,"dual-products.csv");
writematrix(products,"dual-products.xlsx","Products");
display("Prepared dual-products.csv and dual-products.xlsx.");
