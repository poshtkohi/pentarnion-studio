ARRAYS, MASKS AND GRIDS

Open project.pprj in Studio and Run main.p.
The Command Window prints each result and Figure 1 compares original and masked
samples. The example creates no data files; normal native graphics are used.

Expected principal results:
  reshape([1,2,3;4,5,6],3,2) = [1,5;4,3;2,6]
  diag([2,4,6]) = [2,0,0;0,4,0;0,0,6]
  meshgrid X = [-1,0,1;-1,0,1], Y = [0,0,0;2,2,2]
  ndgrid U = [-1,-1;0,0;1,1], V = [0,2;0,2;0,2]
  selected = [5,8,9], locations = [2;4;6]
  masked samples = [2,4,1,4,3,4]
  dot product = 32, norm([3,4]) = 5

Indices/reshape traversal remain one based and column major. Logical masks are
explicit values, distinct from ordinary numeric index vectors. A full-array
mask must match numel(A); row/column masks must match their selected extent.
Scalar masked assignment broadcasts; matching arrays traverse column major.
Old aliases remain unchanged. Arrays remain bounded and two dimensional.

dot is an explicitly real vector inner product. norm defaults to the Euclidean
real-vector norm; p=1, "inf" and real-matrix "fro" are available. Whole-Pentarnion
inner-product conventions are not inferred; inspect coefficients explicitly.
