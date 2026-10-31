algebra H = builtin_algebra("quaternion");
element<H> a = H.i;
element<H> b = H.j;
assert(a*b == H.k);
assert(b*a == -H.k);
display("Hamilton i*j:", a*b);
