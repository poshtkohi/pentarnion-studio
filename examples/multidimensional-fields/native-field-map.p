use_algebra("A_gamma",gamma=1);
clc;
clear;
close("all");
native function real saturating(real value,real scale){return tanh(scale*value);}
vector samples=linspace(-2,2,20);
record field=ndarray(samples,[5,4],
    options(axes=options(axis1="x",axis2="y"),
        coords=options(axis1=linspace(0,1,5),axis2=linspace(0,1,4))));
record transformed=ndmap(field,@saturating,
    options(parameters=options(scale=2.0),threads=0));
record changed_parameter=ndmap(field,@saturating,
    options(parameters=options(scale=0.5),threads=0));
display("Native field-map report:",ndinfo(transformed));
display("Changed runtime parameter; same cached scalar body:",ndvalues(changed_parameter));
display("Compiler/cache statistics:",compile_info());

% Coordinate interpolation is native and componentwise. Extrapolation is an
% error; queries define a strictly increasing new coordinate axis.
record interpolated=ndinterp(transformed,"x",linspace(0,1,9));
figure(1,"Name","Native maps and coordinate interpolation","NumberTitle","off");
subplot(1,2,1);ndplot(field);title("Named source field");
subplot(1,2,2);ndplot(interpolated);title("Native scalar callback and linear interpolation");
