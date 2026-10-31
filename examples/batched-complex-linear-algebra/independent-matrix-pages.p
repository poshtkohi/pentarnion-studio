clc;
clear;
threads(4);
% Column-major pages: [1,2;3,4] and [2,0;0,3].
record A = ndarray([1,3,2,4,2,0,0,3],[2,2,2]);
record diagonal = ndarray([2,0,0,5],[2,2,1]);
record products = ndmatmul(A,diagonal);
display("Two pages times one broadcast diagonal matrix:",ndvalues(products));

record inverses = ndinv(A);
record identities = ndmatmul(A,inverses);
display("Independent checked inverses, one page per experiment:",ndvalues(identities));
display("Shared CPU budget:",parallel_info());
