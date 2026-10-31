% This module exports its typed functions and variables under a namespace.
function real square(real x) {
    return x*x;
}
function [real low, real high] bounds(real center, real radius=1.0) {
    return center-radius, center+radius;
}
function real scale(real x, options cfg=options(gain=2.0, offset=0.0)) {
    return x*cfg.gain+cfg.offset;
}
