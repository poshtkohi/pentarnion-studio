function [real lo,real hi] bounds(real x) {
    return x-1, x+1;
}
[real lower,real upper] = bounds(5);
options cfg = options(scale=2.0);
display(lower, upper, cfg.scale);
