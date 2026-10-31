use_algebra("A_gamma", gamma=1);

clc;
clear;
close("all");

% First submitted paper: the bracketing family (eq:bracketing-compact).
% A, B and p0 are exactly the paper's coefficients and initial condition.
% eta=0: Ap+B(pp). eta=1: Ap+(Bp)p. Changing eta reuses the native kernel.
pentarnion A = -1/2 - 2*i + j/2 - k - ell/2;
pentarnion B = 1 + 2*j - ell/2;
pentarnion p0 = (1-i-j+k-ell)/2;
real eta = 0;

ode bracketing(t, pentarnion p; pentarnion A, pentarnion B, real eta) {
    p' = A*p + (1-eta)*(B*(p*p)) + eta*((B*p)*p);
}

% Time execution only; native compilation is excluded automatically.
tic;
solution s = odesolve(bracketing,
    p=p0, A=A, B=B, eta=eta,
    t0=0, t1=200, sample_step=0.01,
    method="bdf", jacobian="analytic", rtol=1e-10, atol=1e-12);
toc;

display("First-paper bracketing family; eta = ", eta);
display(s);

figure(1, "Name", "Paper 1: five component time series", "NumberTitle", "off");
subplot(3,2,1);
plot(s.t, coeff(s.p,0));
title("Scalar component $p_0(t)$");
xlabel("$t$"); ylabel("$p_0$");

subplot(3,2,2);
plot(s.t, coeff(s.p,1));
title("Component $p_1(t)$ along $i$");
xlabel("$t$"); ylabel("$p_1$");

subplot(3,2,3);
plot(s.t, coeff(s.p,2));
title("Component $p_2(t)$ along $j$");
xlabel("$t$"); ylabel("$p_2$");

subplot(3,2,4);
plot(s.t, coeff(s.p,3));
title("Component $p_3(t)$ along $k$");
xlabel("$t$"); ylabel("$p_3$");

subplot(3,2,5);
plot(s.t, coeff(s.p,4));
title("Component $p_4(t)$ along $\\ell$");
xlabel("$t$"); ylabel("$p_4$");

figure(2, "Name", "Paper 1: trajectory projections", "NumberTitle", "off");
subplot(2,2,1);
plot3("1","i","j",s.p);
title("Projection $(1,i,j)$");
xlabel("$p_0$"); ylabel("$p_1$"); zlabel("$p_2$");

subplot(2,2,2);
plot3("i","j","k",s.p);
title("Projection $(i,j,k)$");
xlabel("$p_1$"); ylabel("$p_2$"); zlabel("$p_3$");

subplot(2,2,3);
plot3("i","j","ell",s.p);
title("Projection $(i,j,\\ell)$");
xlabel("$p_1$"); ylabel("$p_2$"); zlabel("$p_4$");

subplot(2,2,4);
plot3("1","k","ell",s.p);
title("Projection $(1,k,\\ell)$");
xlabel("$p_0$"); ylabel("$p_3$"); zlabel("$p_4$");

tilefigures("studio",1,2);
