% One native evaluation computes D^3f(x)[u,v,w], without a dense tensor.
use_algebra("A_gamma", gamma=1);
clc;
clear;

native function real model(real x, real y, real gain) {
    return gain*x*x*x*y*y + sin(x) + exp(y);
}

record d = thirdderivative(@model, [0.6,-0.4],
    [0.7,-0.2], [-0.3,0.5], [1.1,0.9],
    options(parameters=options(gain=2)));
display("Function value: ", d.values);
display("Analytic D3f[u,v,w]: ", d.third);
display("Method: ", d.method);
display("Native evaluations: ", d.native_evaluations);

% Changing a fixed parameter or direction reuses the same compiled body.
record changed = thirdderivative(@model, [0.6,-0.4],
    [1,0], [1,0], [0,1], options(parameters=options(gain=3)));
assert(changed.cache_key == d.cache_key);
display("D3f[ex,ex,ey] after changing gain: ", changed.third);
