use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
ode left_grouped(t,pentarnion q,pentarnion v;pentarnion A,pentarnion B,real m,real a,real b) {
    q'=v;
    v'=(a*(A*q)+b*(B*(q*q)))/m;
}
ode right_grouped(t,pentarnion q,pentarnion v;pentarnion A,pentarnion B,real m,real a,real b) {
    q'=v;
    v'=(a*(A*q)+b*((B*q)*q))/m;
}
% Explicit full-coordinate coefficient rows, including scalar directions.
matrix Aset=[-1,0,0,0,0;0,1,1,0,0]/sqrt(2);
matrix Bset=[0,0,1,1,0;0,1,0,0,1]/sqrt(2);
matrix starts=[0.1,0.2,-0.1,0.05,0.15, 0.2,-0.1,0.25,0.1,-0.05;
               0.2,0.1,0.05,-0.1,0.1, 0.1,0.2,-0.1,0.05,0.15];
record settings=options(parameters=options(A=searchset(Aset),B=searchset(Bset),
    m=1.7,a=0.2,b=0.1),positive_parameters="m,a,b",gammas=[0.5,1],
    t1=2.0,sample_step=0.01,threads=4,max_runs=32,
    physical_coordinates=10,escape_radius=50.0,max_native_operations=20000000);
record first=discover(left_grouped,starts,settings);
record second=discover(right_grouped,starts,settings);
assert(first.initials==second.initials);
assert(first.parameters==second.parameters);
assert(first.gammas==second.gammas);
display("Matched candidate count for each grouping:",first.runs);
display("First model run 1:",discoverstatus(first,1));
display("Second model run 1:",discoverstatus(second,1));
record a=experimentrun(left_grouped,first,1);
record b=experimentrun(right_grouped,second,1);
figure(1,"Name","Matched force groupings","NumberTitle","off");
subplot(1,2,1);plot(a.t,a.values(:,1));title("$B(qq)$: scalar q(t)");
subplot(1,2,2);plot(b.t,b.values(:,1));title("$(Bq)q$: scalar q(t)");
% All recurrence decisions use all ten physical coordinates, including velocity.
% The two curves are projections; a cutoff is not proof of finite-time blow-up.
