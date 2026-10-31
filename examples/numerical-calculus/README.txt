NUMERICAL CALCULUS

Open project.pprj in Studio and Run main.p. The example prints analytical checks
and opens four calculus plots. The first ODE run may compile its cached equation.

Expected principal results:
  integral(x^3,0,1) = 0.25
  integral(1+x*i+x^2*j,0,1) = 1 + 0.5*i + (1/3)*j
  trapz(x,1+2*x) = 2
  interp1([0,0.5,1],[0,1,0],0.25) = 0.5
  diff([1,4,9,16]) = [3,5,7]
  gradient of x^2+3y^2 at (2,3): gx=4, gy=18
  laplacian(x^2+3y^2) = 8; divergence(2x,3y) = 5
  deval at 0.0625 is approximately exp(-0.0625)*(1+i-j+ell)

integral uses adaptive Simpson quadrature, with a finite subdivision/evaluation
budget and explicit convergence failure. Scalar real and Pentarnion callbacks
are supported. Pentarnion integration is linear integration of its five real
coefficients; the integrand's algebraic products retain their original grouping.

interp1 is piecewise linear sampled interpolation. Coordinates must be strictly
increasing/decreasing. Queries outside the sampled interval fail unless the
explicit "linear","extrap" options are supplied.

deval is a cubic Hermite interpolant of stored ODE samples and their retained
compiled-RHS endpoint slopes. It is not the integrator's internal dense-output
polynomial. Its interpolation accuracy depends on sample_step; compare refined
sample spacing when accuracy matters. Stored sample values are preserved exactly.

gradient/laplacian use local three-point coordinate-polynomial derivatives,
including one-sided endpoint stencils. Nonuniform monotone coordinate vectors
or nonzero scalar spacings are accepted. For a 2D matrix, x runs across columns
and y down rows. Laplacian is the unscaled sum of second spatial derivatives.
These spatial/sample operators are distinct from objective-function gradients.
