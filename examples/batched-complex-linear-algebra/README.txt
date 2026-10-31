BATCHED REAL/COMPLEX LINEAR ALGEBRA

complex-solves-and-pivoted-factors.p checks a complex matrix solve against a
known answer, verifies a checked inverse and reconstructs P*A*Q=L*U.

independent-matrix-pages.p uses rank3 records for separate experiments, including
singleton batch expansion, shared CPU thread budgeting and per-page inverses.

The existing penta_nd DLL/SO provides all operations. A scaled FullPivLU per
page supplies factors, rank and conditioning; an unnecessary SVD is avoided.
Solve/inverse failures are explicit for singular/unreliable matrices. Physical
axis metadata and complete complex coordinates are retained. Inner Eigen
parallelism is disabled so independent pages respect Studio's CPU pool.

See specs/batched_nd_products_v0_2.txt and nd_complex_linalg_v0_2.txt for scope,
order<=256, rank/scratch/work limits and conditioning/residual thresholds.
