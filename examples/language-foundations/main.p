use_algebra("A_gamma", gamma=1);
clc;
clear;

import "helpers.p" as science;

% Typed named arguments and multiple returned values.
[real low, real high] = science.bounds(radius=2.0, center=10.0);
display("bounds:", low, high);
options cfg = options(gain=3.0, offset=1.0);
display("scaled:", science.scale(4.0, cfg));
options changed = with_options(cfg, gain=4.0);
display("changed gain:", changed.gain);

% A typed anonymous function captures an immutable snapshot of gain.
real gain = 2.0;
callback double_value = @(real x) => gain*x;
gain = 99.0;
display("captured callback:", double_value(5.0));
callback square = science.square;
display("module callback:", square(3.0));

% Complex results remain distinct from the Pentarnion basis element i.
complex z = complex(0.0, 1.0);
display("complex unit:", z);
display("complex square:", z*z);
pentarnion p = 1+i+j;
display("Pentarnion:", p);
assert(abs(exp(z)-cos(1.0)-z*sin(1.0))<1e-12);

% Pure native functions compile once and reuse a structural cache.
% Runtime arguments can change without recompilation; tic/toc excludes builds.
native function real cube(real x) {
    return x*x*x;
}
tic;
real cubed = cube(3.0);
real elapsed = toc();
display("native cube:", cubed);
display("execution seconds (compilation excluded):", elapsed);
