% Differentiating independent argument blocks preserves both bracketings.
use_algebra("A_gamma", gamma=1);
clc;
clear;

native function [pentarnion left, pentarnion right] products(
    pentarnion a, pentarnion b, pentarnion c) {
    return (a*b)*c, a*(b*c);
}

% Flat coordinates are a0..a4,b0..b4,c0..c4.
record d = thirdderivative(@products, zeros(1,15),
    [0,1,0,0,0, 0,0,0,0,0, 0,0,0,0,0],
    [0,0,0,0,0, 0,0,1,0,0, 0,0,0,0,0],
    [0,0,0,0,0, 0,0,0,0,0, 0,0,0,0,1]);
display("D3((a*b)*c)[i in a,j in b,ell in c]: ", d.third(1:5));
display("D3(a*(b*c))[i in a,j in b,ell in c]: ", d.third(6:10));
display("The difference is the ordered associator: ", d.third(1:5)-d.third(6:10));
assert(norm(d.third(1:5)-d.third(6:10)) > 0.5);
