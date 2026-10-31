clc;
clear;
close("all");

algebra H = builtin_algebra("quaternion");
element p = 1 + H.i + 2*H.j + 0.5*H.k;
element q = -0.5*H.i + H.j + 2*H.k;
display("Quaternion p+q:", p+q);
display("Quaternion p*q:", p*q);

figure(1,"Name","Native algebra vector projections","NumberTitle","off");
subplot(2,2,1);plot2("i","j",p,q,p+q,p*q);
title("Quaternion projection onto $(i,j)$");
subplot(2,2,2);plot3("i","j","k",p,q,p+q,p*q);
title("Quaternion projection onto $(i,j,k)$");

algebra S = builtin_algebra("sedenion");
element a = 1 + S.e1 + 2*S.e10 - 0.5*S.e15;
element b = 2 - S.e4 + S.e15;
subplot(2,2,3);plot2("e10","e15",a,b,a+b,a*b);
title("Sedenions: coordinates $e_{10},e_{15}$");
display("All sixteen sedenion coefficients:", algebra_coefficients(a*b));

algebra Cl = builtin_algebra("clifford",options(p=6,q=0));
element u = Cl.e5 + 2*Cl.e6 + Cl.e123456;
element v = -Cl.e5 + Cl.e6 - 0.5*Cl.e123456;
subplot(2,2,4);plot3("e5","e6","e123456",u,v,u+v,u*v);
title("Clifford algebra: a projection of all64 coordinates");
display("Clifford coefficient count:", algebra_info(Cl).dimension);
% Every arrow ends in an arrowhead. Hover it to inspect the original complete
% coefficient vector. Use subplot Save or Export Image for PNG/SVG/PDF images.
