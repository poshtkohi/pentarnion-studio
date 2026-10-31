clc;
clear;
close("all");

algebra H = builtin_algebra("quaternion");
element<H> p = 1 + H.i + 2*H.j;
element<H> q = 2 - H.i + H.k;
display("Hamilton product p*q:", p*q);
display("Hamilton product q*p:", q*p);
display("Commutator:", commutator(p,q));
assert(H.i*H.j == H.k);
assert(H.j*H.i == -H.k);

% Scalar promotion uses the declared algebra unit; H.i is not Pentarnion i.
element<H> identity = 1;
assert(identity == H.one);
element recovered = solveleft(p,p*q);
assert(algebra_norm(recovered-q) < 1e-11);
display("Solution of p*x=p*q:", recovered);

% Rotate the i direction about k. Every point comes from a quaternion product.
matrix path = zeros(121,3);
real full_turn = 6.283185307179586;
for(n=1:121) {
    real angle = full_turn*(n-1)/120;
    element rotor = cos(angle/2) + sin(angle/2)*H.k;
    element direction = (rotor*H.i)*conj(rotor);
    path(n,1) = coeff(direction,1);
    path(n,2) = coeff(direction,2);
    path(n,3) = coeff(direction,3);
}
figure(1,"Name","Quaternion rotation","NumberTitle","off");
plot3(path(:,1),path(:,2),path(:,3));
title("$r\\,i\\,\\overline{r}$: rotation about $k$");
xlabel("$i$"); ylabel("$j$"); zlabel("$k$");
axis("equal");
