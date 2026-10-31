use_algebra("A_gamma",gamma=1);
clc;clear;
ode pentfold(t,pentarnion q;pentarnion A) {
    q'=scalar(A)+scalar(q)*scalar(q)-pure(q);
}
pentarnion initial=.2+.1*i-.1*j+.15*k+.05*ell;
pentarnion A=-.05+10*i+20*j+30*k+40*ell;
record point=refinebifurcation(pentfold,initial,
    options(kind="fold",parameter="A",component=0,
        parameters=options(A=A),tolerance=1e-8));
display("Complete five-coordinate state at the fold, expected zeros: ",point.x);
display("Selected scalar A coefficient, expected0: ",point.parameter);
display("All other A coefficients preserved: ",point.parameter_values);
display("Declared state widths / numerical status: ",point.state_widths,point.status);
