use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Native maps: the compiler emits a cached DLL/SO and its real Jacobian.
% Runtime parameters and iteration/plot options reuse that compiled structure.
native function real logistic(real x, real r) {
    return r*x*(1-x);
}

record orbit=mapiterate(logistic,0.2,
    options(parameters=options(r=3.7),iterations=400,transient=200));
record pairs=returnmap(orbit);
assert(orbit.samples==201);
assert(pairs.samples==200);
display("Logistic map final state: ",orbit.x);
display("Native map Jacobian at the final state: ",orbit.jacobian);

% Independent initial state for each r, not branch continuation.
vector rates=linspace(2.8,4.0,45);
record diagram=mapdiagram(logistic,0.2,"r",rates,
    options(parameters=options(r=3.7),iterations=220,transient=180,
            jacobian="none"));
assert(diagram.cache_hit);
assert(diagram.samples==1845);
display("Sampled parameter orbit diagram: ",diagram.samples," points.");

native function pentarnion affine_map(pentarnion p,pentarnion a,pentarnion b) {
    return a*p+b;
}
pentarnion a=0.8+0.1*i+0.05*j;
pentarnion b=0.05*k+0.1*ell;
record pent_orbit=mapiterate(affine_map,1+i+j+k+ell,
    options(parameters=options(a=a,b=b),iterations=180,escape_radius=100));
assert(pent_orbit.escaped==0);
assert(abs(coeff(pent_orbit.x,0)-0.0630691771570533)<1e-10);
assert(abs(coeff(pent_orbit.x,1)+0.0577008840756757)<1e-10);
assert(abs(coeff(pent_orbit.x,2)+0.136874940478021)<1e-10);
assert(abs(coeff(pent_orbit.x,3)+0.0365273127470211)<1e-10);
assert(abs(coeff(pent_orbit.x,4)-0.468264749944103)<1e-10);
display("Pentarnion affine map final state: ",pent_orbit.x);
display("Five-real-coordinate native Jacobian: ",pent_orbit.jacobian);

figure(1,"Name","Native real and Pentarnion maps","NumberTitle","off");
subplot(2,2,1);
plot(orbit.n,orbit.states);
title("Logistic orbit after the transient");
xlabel("$n$"); ylabel("$x_n$");

subplot(2,2,2);
plot(pairs.x,pairs.y);
title("Successive sampled values");
xlabel("$x_n$"); ylabel("$x_{n+1}$");

subplot(2,2,3);
plot(diagram.parameter,diagram.states(:,1));
title("Sampled parameter orbit diagram");
xlabel("$r$"); ylabel("$x_n$");
caption("Samples are joined for display; this is not bifurcation continuation.");

subplot(2,2,4);
plot3(coeff(pent_orbit.orbit,1),coeff(pent_orbit.orbit,2),
      coeff(pent_orbit.orbit,3));
title("Pentarnion affine map: $(i,j,k)$ projection");
xlabel("$p_1$"); ylabel("$p_2$"); zlabel("$p_3$");
caption("All five coefficients are retained in pent_orbit.states.");
