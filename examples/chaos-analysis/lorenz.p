use_algebra("A_gamma", gamma=1);
clc; clear; close("all");

ode lorenz(t,real x,real y,real z;real sigma,real rho,real beta) {
    x'=sigma*(y-x);
    y'=x*(rho-z)-y;
    z'=x*y-beta*z;
}
% Increase the duration and repeat with other QR intervals/tolerances before
% interpreting a finite-time spectrum. A positive estimate alone is not proof.
record l=lyapunov(lorenz,[1,1,1],options(
    parameters=options(sigma=10.0,rho=28.0,beta=8/3),
    t1=40,transient=10,qr_step=0.25,rtol=1e-9,atol=1e-11));
% The divergence is exactly -(sigma+1+beta), independently of the orbit.
real divergence_sum=l.exponents(1)+l.exponents(2)+l.exponents(3);
assert(abs(divergence_sum+41/3)<2e-3);
display("Finite-time Lorenz spectrum:",l.exponents);
display("Early-window variation; this is a diagnostic, not a certificate:",
    l.window_difference);
figure(1,"Name","Lorenz tangent-spectrum convergence","NumberTitle","off");
subplot(3,1,1);plot(l.t,l.history(:,1));title("Largest finite-time estimate");ylabel("$\\lambda_1$");
subplot(3,1,2);plot(l.t,l.history(:,2));title("Second finite-time estimate");ylabel("$\\lambda_2$");
subplot(3,1,3);plot(l.t,l.history(:,3));title("Third finite-time estimate");xlabel("$t$");ylabel("$\\lambda_3$");
