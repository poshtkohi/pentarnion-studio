% Real covariant forms in explicit coordinates; coefficients are lexicographic.
clc; clear;
record dx = dform(2,1,[1,0],options(metric=[4,0;0,9],orientation=1));
record star_dx = hodge(dx);
display("For metric diag(4,9), star(dx) = (3/2)dy: ", star_dx.coefficients);
display("star(star(dx)) = -dx: ", hodge(star_dx).coefficients);
display("dx wedge dx is zero: ", wedge(dx,dx).coefficients);

native function real potential(real x,real y,real z,real gain) {
    return gain*x*x*y + sin(z);
}
record f = formfield(@potential,[2,0.5,0.3],0,options(parameters=options(gain=2)));
record df = exteriord(f);
record ddf = exteriord(df);
display("Analytic differential df at the selected point: ", df.coefficients);
display("Exterior derivative squared vanishes: ", ddf.coefficients);
assert(norm(ddf.coefficients)<1e-13);
