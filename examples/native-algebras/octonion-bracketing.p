clc;
clear;

algebra O = builtin_algebra("octonion");
element a = O.e1;
element b = O.e3;
element c = O.e4;
element left_grouping = (a*b)*c;
element right_grouping = a*(b*c);
display("(e1*e3)*e4:", left_grouping);
display("e1*(e3*e4):", right_grouping);
display("Associator, expected 2*e6:", associator(a,b,c));
assert(associator(a,b,c) == 2*O.e6);
assert(left_grouping != right_grouping);

% Alternativity holds, while arbitrary regrouping does not.
assert(associator(a,a,c) == 0*O.one);
record info = algebra_info(O);
assert(info.properties.alternative.value);
assert(!info.properties.associative.value);
display("Basis convention:", info.convention);
display("Associativity evidence:", info.properties.associative);
display("Alternativity evidence:", info.properties.alternative);
