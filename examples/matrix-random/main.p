use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

matrix A = [1, 2; 3, 4];
matrix B = [2, 0; 1, 2];
vector v = [1; 2];
matrix AB = A*B;
vector Av = A*v;
matrix element_products = A.*B;
display("A*B =", AB);
display("A*v =", Av);
display("A.*B =", element_products);
display("Transpose of A =", A.');
display("A(2,1) =", A(2,1));

pentarnionmatrix P = [i, j; k, ell];
pentarnionmatrix Q = [j; 1+i];
pentarnionmatrix PQ = P*Q;
display("P*Q =", PQ);

rng(42);
matrix uniform_samples = rand(1,201);
matrix normal_samples = randn(2,3);
matrix dice = randi(6,2,3);
display("Normal samples =", normal_samples);
display("Integer samples in 1..6 =", dice);

vector t = linspace(0,6.283185307179586,201);
vector y = sin(t);
vector z = cos(t);
figure(1,"Name","Matrices and random numbers","NumberTitle","off");
subplot(1,2,1);
plot(t,uniform_samples);
title("Seeded uniform samples");
xlabel("$t$"); ylabel("$u$");
subplot(1,2,2);
plot3(t,y,z);
title("Vector coordinates");
xlabel("$t$"); ylabel("$\\sin(t)$"); zlabel("$\\cos(t)$");
% Use each subplot's Save icon to export only that panel as PNG, JPEG or SVG.
