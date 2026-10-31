% Category 2: column-major reshape, diagonals, grids and logical indexing.
use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");

matrix A = [1,2,3;4,5,6];
matrix reshaped = reshape(A,3,2);
matrix restored = reshape(reshaped,2,[]);
assert(restored==A);
display("A =",A);
display("reshape(A,3,2) =",reshaped);

matrix diagonal = diag([2,4,6]);
display("diag([2,4,6]) =",diagonal);
display("Extracted diagonal =",diag(diagonal));

[matrix X,matrix Y] = meshgrid([-1,0,1],[0,2]);
[matrix U,matrix V] = ndgrid([-1,0,1],[0,2]);
display("meshgrid X =",X);
display("meshgrid Y =",Y);
display("ndgrid U =",U);
display("ndgrid V =",V);

matrix samples = [2,5,1,8,3,9];
matrix selected = samples(samples>4);
matrix locations = find(samples>4);
display("Samples above 4 =",selected);
display("Their one-based indices =",locations);
matrix unchanged = samples;
samples(samples>4)=4;
display("After a masked write =",samples);
display("Previous immutable value =",unchanged);

display("dot([1,2,3],[4;5;6]) =",dot([1,2,3],[4;5;6]));
display("Euclidean norm of [3,4] =",norm([3,4]));
display("Matrix Frobenius norm =",norm(A,"fro"));

matrix pent_diagonal = diag([1+i,1+j,1+ell]);
display("Pentarnion diagonal =",pent_diagonal);

figure(1,"Name","Category 2: masked samples","NumberTitle","off");
vector sample_indices = 1:6;
subplot(2,1,1);
plot(sample_indices,unchanged);
title("Original samples");xlabel("Index");ylabel("Value");
subplot(2,1,2);
plot(sample_indices,samples);
title("After a logical-mask write");xlabel("Index");ylabel("Value");
