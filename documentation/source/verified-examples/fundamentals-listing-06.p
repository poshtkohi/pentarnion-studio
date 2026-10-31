tensor<2,2,2> T = tensor([1,2,3,4,5,6,7,8],[2,2,2]);
display(T(2,1,2));        % 6
display(size(T));
algebra H = builtin_algebra("quaternion");
tensor<H,2,2,2> Q = tensor(H.i,[2,2,2]);
Q(2,1,2) = H.j;
