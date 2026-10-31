clc;
clear;

algebra D = builtin_algebra("dual");
assert(D.eps*D.eps == 0*D.one);
element input = 2 + D.eps;
element cubic = input^3;
display("(2+eps)^3, expected 8+12*eps:", cubic);
display("Cubic value / derivative at 2:", coeff(cubic,0), coeff(cubic,1));

algebra DQ = builtin_algebra("dual_quaternion");
real angle = 1.0471975511965976;
element rotor = cos(angle/2) + sin(angle/2)*DQ.k;
element translation = DQ.i + 2*DQ.j + 0.5*DQ.k;
element pose = rotor + 0.5*(DQ.eps*translation)*rotor;
display("Dual quaternion pose r + eps*t*r/2:", pose);

% Extract the real quaternion coefficients of the dual part, then recover t.
element dual_part = element(DQ,coeff(pose,4),coeff(pose,5),coeff(pose,6),coeff(pose,7),0,0,0,0);
element recovered_translation = 2*dual_part*conj(rotor);
assert(algebra_norm(recovered_translation-translation) < 1e-12);
display("Recovered translation:", recovered_translation);
assert(DQ.i*DQ.eps == DQ.eps_i);
assert(DQ.eps*DQ.i == DQ.eps_i);
