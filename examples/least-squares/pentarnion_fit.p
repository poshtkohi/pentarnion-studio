use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% All five coefficients of p are fitted. Ordered left multiplication remains
% the original Pentarnion product; no associative matrix identity is assumed.
native function pentarnion ordered_residual(pentarnion p,pentarnion a,pentarnion target) {
    return a*p-target;
}
pentarnion expected = 1+i-j+0.25*ell;
pentarnion a = 2+0.2*j;
pentarnion target = a*expected;
record fitted = lsqfit(ordered_residual,0.0,
    options(parameters=options(a=a,target=target),weights=[1,2,2,2,2]));
assert(fitted.converged);
assert(abs(coeff(fitted.x-expected,0))<1e-7);
assert(abs(coeff(fitted.x-expected,1))<1e-7);
assert(abs(coeff(fitted.x-expected,2))<1e-7);
display("Expected Pentarnion:",expected);
display("Fitted Pentarnion:",fitted.x);
display("Five real residual coordinates:",fitted.residuals);

figure(11,"Name","Pentarnion native least squares","NumberTitle","off");
plot3("i","j","ell",expected,fitted.x);
title("Expected and fitted Pentarnion projections");
