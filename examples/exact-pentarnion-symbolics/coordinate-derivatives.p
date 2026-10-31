use_algebra("A_gamma",gamma=1);
clc;
clear;

% Scalars are explicitly declared central real variables. A declared
% Pentarnion p introduces independent real coordinates p0,p1,p2,p3,p4.
record model=symexpand("p*p+a*p",options(pentarnions="p",reals="a",gamma="1"));
record derivative=sympentdiff(model,"p0");
display("Expanded coordinate form:",sympenttext(model));
display("Partial derivative with respect to p0:",sympenttext(derivative));
pentarnion value=sympentnumeric(derivative,
    options(a=0.5,p0=1,p1=2,p2=0,p3=0,p4=0));
assert(norm2(value-(2.5+4*i))<1e-28);
display("Derivative at p=1+2i and a=1/2:",value);

record substituted=sympentsubs(model,"a","1/2");
display("Exact coefficient substitution:",sympenttext(substituted));

% This returns inspectable checked Studio native-function source. Parameters
% are sorted scalar coordinates prefixed v_ to keep target names safe. Copy
% the source into a .p file, then ordinary native compilation/cache rules apply.
string generated=sympentcode(substituted,"quadratic_pentarnion");
display("Generated native function:",generated);
