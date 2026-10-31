use_algebra("A_gamma", gamma=1);
clc; clear; close("all");

% The first-paper A, B and initial state; eta selects the published bracketing.
pentarnion A=-1/2-2*i+j/2-k-ell/2;
pentarnion B=1+2*j-ell/2;
pentarnion p0=(1-i-j+k-ell)/2;
real eta=0;
ode bracketing(t,pentarnion p;pentarnion A,pentarnion B,real eta) {
    p'=A*p+(1-eta)*(B*(p*p))+eta*((B*p)*p);
}
record l=lyapunov(bracketing,p0,options(parameters=options(A=A,B=B,eta=eta),
    t1=100,transient=20,qr_step=0.5,rtol=1e-10,atol=1e-12));
display("First-paper model: finite-time five-dimensional spectrum:",l.exponents);
display(l.status,l.message);
figure(1,"Name","Paper 1: five tangent growth rates","NumberTitle","off");
subplot(3,2,1);plot(l.t,l.history(:,1));title("First tangent growth estimate");ylabel("$\\lambda_1$");
subplot(3,2,2);plot(l.t,l.history(:,2));title("Second tangent growth estimate");ylabel("$\\lambda_2$");
subplot(3,2,3);plot(l.t,l.history(:,3));title("Third tangent growth estimate");ylabel("$\\lambda_3$");
subplot(3,2,4);plot(l.t,l.history(:,4));title("Fourth tangent growth estimate");ylabel("$\\lambda_4$");
subplot(3,2,5);plot(l.t,l.history(:,5));title("Fifth tangent growth estimate");xlabel("$t$");ylabel("$\\lambda_5$");
caption("Repeat with longer observation, different QR intervals and tighter tolerances.");
