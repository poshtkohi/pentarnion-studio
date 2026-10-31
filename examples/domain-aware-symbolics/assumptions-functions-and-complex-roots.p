clc;
clear;
record f = cas("log(x)+sqrt(x)+1/x",options(symbols="x",positive="x"));
record derivative = casdiff(f,"x");
display("Expression:",f.expression);
display("Derivative:",derivative.expression);
display("Derivative at x=4:",casreal(derivative,options(x=4)));

record formal = cas("f(x)^2",options(symbols="x",functions="f"));
display("Formal chain rule:",casdiff(formal,"x").expression);

record radical = cas("sqrt(-2)",options(domain="complex"));
display("Principal complex radical:",cascomplex(radical));
display("The positive/nonzero domains survive save/load and substitution.");
