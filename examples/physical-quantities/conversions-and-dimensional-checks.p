% Units remain explicit. unitvalues is the deliberate stripping operation.
clc;clear;
record distances=quantity([100,250,500],"cm");
record metres=unitconvert(distances,"m");
display("Original tagged distances: ",distances);
display("Converted metre magnitudes: ",unitvalues(metres));
record duration=quantity(2,"h");
record distance=quantity(100,"km");
record speed=unitdiv(distance,duration);
display("Canonical SI speed: ",speed);
display("Speed in km/h: ",unitvalues(unitconvert(speed,"km/h")));
assert(unitcompatible(speed,"m/s"));
assert(unitcompatible("N","kg*m/s^2"));
assert(unitcompatible("m","s")==0);
display("Seven SI dimension exponents for force: ",unitinfo("N").dimensions);
% These intentionally diagnose a dimensional mistake if uncommented:
% record invalid=unitadd(distance,duration);
% record invalid=unitconvert(distance,"s");
