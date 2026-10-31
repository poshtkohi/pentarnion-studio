NATIVE ARRAY CALLBACKS AND MATRIX ODE STATES

Open this folder as a project and run the descriptive .p examples.

matrix-product-derivatives.p shows a true 2x2 matrix function compiled once
to a cached shared DLL/SO, and checks its complete real-coordinate Jacobian.
quaternion-array-ode.p evolves two quaternion entries together and plots
selected coordinates without dropping any of the four coefficients per entry.

Use matrix<R,C>, pentarnionmatrix<R,C>, or matrix<A,R,C> for explicit native
shape contracts. Ordinary matrix declarations remain resizable. Native arrays
cross the C ABI in row-major cell order then algebra coefficient order; source
linear indexing remains MATLAB column-major and one-based. Derivatives are
ordinary real-coordinate derivatives of all retained coefficients.

See specs/native_arrays_v0_2.txt for supported operations and resource bounds.
