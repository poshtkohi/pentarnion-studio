use_algebra("A_gamma", gamma=1);
threads(1);
function real square(real x) { return x*x; }
integer total = 0;
for(n=1:5) { total = total+n; }
assert(total == 15);
display("sum 1 to 5 =", total, "square(3) =", square(3));
matrix A = [1,2;3,4];
vector x = [1;2];
display("A*x =", A*x, "A(2) =", A(2));
matrix y = linsolve([2,0;0,4],[2;8],options(threads=1));
display("linear solution =",y);
algebra H = builtin_algebra("quaternion");
element<H> a = H.i;
element<H> b = H.j;
assert(a*b == H.k);
display("Hamilton i*j =",a*b);
tensor<2,2,2> T = tensor([1,2,3,4,5,6,7,8],[2,2,2]);
display("Tensor entry (2,1,2) =",T(2,1,2));
