% P1 triangular surface FEM, consistent mass, implicit diffusion.
clc; clear; close("all");
native function real initial_mode(real x,real y,real z) { return x; }
record sphere_mesh = meshsphere(1,options(refinements=3));
tic;
record heat = surfaceheat(sphere_mesh,@initial_mode,
    options(t1=0.1,time_step=0.001,sample_step=0.01,diffusion=1,threads=0));
toc;
matrix first = surfacecoeff(heat,0,1);
matrix final_values = surfacecoeff(heat,0,length(heat.t));
display("Surface heat method: ",heat.method);
display("Shared-budget parallel workers used: ",heat.threads_used);
display("Mass drift: ",heat.max_mass_drift);
display("Maximum energy increase: ",heat.max_energy_increase);
display("RMS error against exp(-2t)x: ",norm(final_values-exp(-0.2)*first)/sqrt(length(first)));

figure(1,"Name","Heat on the sphere","NumberTitle","off");
subplot(1,2,1);meshplot(sphere_mesh,options(values=first));title("Initial mode $u=x$");axis("equal");
subplot(1,2,2);meshplot(sphere_mesh,options(values=final_values));title("Surface heat at $t=0.1$");axis("equal");
figure(2,"Name","Surface heat diagnostics","NumberTitle","off");
plot(heat.t,heat.energy);title("Consistent-mass heat energy");xlabel("$t$");ylabel("$\\frac{1}{2}u^TMu$");
