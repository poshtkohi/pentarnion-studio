% All five coefficients participate, including smooth pure-part limits at zero.
use_algebra("A_gamma", gamma=1);
clc;
clear;

native function [pentarnion exponential, pentarnion sine] functions(pentarnion p) {
    return exp(p), sin(p);
}

pentarnion p = 0*i;
record d = thirdderivative(@functions, p, i, i, i);
display("Output widths (five coefficients each): ", d.output_widths);
display("D3exp(0)[i,i,i], expected [0;-1;0;0;0]: ", d.third(1:5));
display("D3sin(0)[i,i,i], expected [0;1;0;0;0]: ", d.third(6:10));
assert(abs(d.third(2)+1) < 1e-12);
assert(abs(d.third(7)-1) < 1e-12);

% A real direction with one active Pentarnion means its scalar coefficient.
record real_axis = thirdderivative(@functions, p, 1, 1, 1);
display("D3exp(0)[1,1,1], expected [1;0;0;0;0]: ", real_axis.third(1:5));
assert(abs(real_axis.third(1)-1) < 1e-12);
