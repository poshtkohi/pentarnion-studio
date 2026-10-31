LINEAR ALGEBRA AND SPARSE OPERATORS

Open project.pprj in Studio. Run each script separately (all also run in CLI):
 main.p: real solves, diagnostics, determinant/inverse, decompositions and spectra.
 pentarnion_operators.p: separate A*X=B and X*A=C equations and real operators.
 sparse_operators.p: canonical CSC assembly/products and sparse solves.

Expected principal results:
 main.p: solution [2/9;1/9;13/9], det(A)=18, reconstruction errors below1e-10.
 Rotation eigenvalues have real parts0 and imaginary parts+1/-1 in unspecified
 order. The rank-deficient inconsistent example has converged=0 and a positive
 residual even though it has a valid SVD least-squares candidate.
 Pentarnion: both explicitly ordered equation solves recover the known matrix
 within1e-9 per coefficient. The left and right real coefficient operators are
10x10. Their determinants/inverses are ordinary real-operator quantities.
 Sparse:898 nonzeros; direct and ILUT-preconditioned BiCGSTAB residual assertions
 pass. Duplicate triplets [2,3] at one location sum to5. Squaring the2x2 sparse
 diagonal gives [25,0;0,49]. Worker participation and timings vary by machine.

CONVENTIONS
det/inv/decompositions operate on real matrices. Do not infer an associative
Pentarnion matrix determinant/inverse. leftop(A) maps coefficient columns of x
to A*x; rightop(A) maps coefficient rows of x to x*A. Each Pentarnion unknown
has five consecutive real coordinates. solveleft/solveright solve those real
operator equations with rank/condition/residual safeguards; they never multiply
by scalar reciprocals. inv(leftop(A)) is a real5n-by5n operator inverse.
Real rcond is reciprocal2-norm condition, based on singular values. LU has
P*A=L*U, pivoted thin QR has A*P=Q*R, thin SVD has A=U*S*V-transpose, real Schur
has A=Q*T*Q-transpose. Eigenvalues/vectors use separate real/imaginary arrays.

Every new heavy numerical operation uses the lazy penta_linalg DLL/SO. Dense
factorizations use OpenBLAS/LAPACKE with a per-call CPU limit; Eigen sparse
factorization is serial within each independent RHS, while multiple RHS are
dispatched through the shared bounded host pool. Concurrent RHS lanes are also
capped by a conservative512MiB aggregate factor workspace estimate:
64*n*n+128*nnz bytes per factor. This limits possible fill/concurrency; it is not
a hard bound on Eigen allocation or total process RAM. SparseLU uses COLAMD ordering;
BiCGSTAB accepts ilut, diagonal or identity preconditioning. Sparse reports mark
unavailable condition/rank estimates explicitly; zeros are not scientific
condition estimates. Use solve_report/sparse_solve_report to inspect candidates.

Sparse records have format, rows, columns, row_indices, column_pointers, values.
Public CSC indices/pointers are one-based, sorted, unique per column, omit exact
zeros and are revalidated at every operation. Sparse values can be Pentarnions
for assembly and multiplication; sparse solves currently require real entries.
No PDE solver, determinant identity across nonassociative products, arbitrary
tensor storage or universal bit-for-bit BLAS reproducibility is claimed.
