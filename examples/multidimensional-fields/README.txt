NAMED FIELDS, NATIVE MAPS AND COMPLEX FFTS

The three scripts demonstrate arbitrary-length complex FFTs, column-major
shape/index rules, immutable updates, singleton expansion, named coordinates,
native cached scalar maps, interpolation and Pentarnion component plots.

Constructors:
  ndarray(real_values,shape[,options])
  complexarray(real_values,imag_values,shape[,options])
  pentarray(Pentarnion_values,shape[,options])
Values are read from source matrices in one-based column-major order. Shape
has1..8 nonnegative dimensions, at most one-million elements and eight-million
real coordinates. Complex imaginary input may be one broadcast scalar.
options(axes=options(axis1="x",axis2="time"),coords=options(axis1=x,axis2=t))
attaches distinct axis names and strictly increasing coordinate vectors.
Missing names default axis1,axis2,...; coordinates default1..size.

Each immutable PentarnionND-v1 record stores shape, explicit kind, data, gamma,
order, axes, coords, transformed_axes and the producing operation's worker count. Its data is an
elements-by-components real matrix: widths1/2/5 distinguish real, complex and
Pentarnion values. ndvalues(A) returns that coordinate matrix; ndinfo(A)
summarizes metadata. These records do not change existing DenseMatrix or A*B.
The fixed axis1..axis8 metadata records leave inactive axes empty.

ndget(A,k) selects a one-based column-major linear entry. ndget(A,[i,j,...])
selects one coordinate per axis. It returns a one-element N-D record retaining
the domain; ndvalues exposes its coordinate row. ndset(A,index,value) returns
a changed copy, accepting a matching scalar record or real/complex/Pentarnion
scalar. ndslice(A,starts,stops[,positive_steps]) retains every axis, using
inclusive one-based bounds. ndpermute(A,order) permutes axes and coordinates.
ndreshape(A,shape) preserves element order and resets metadata when shape
changes, since old coordinate axes no longer describe the same dimensions.

ndadd/ndsub/ndmul/nddiv use explicit elementwise operations with singleton
broadcasting at matching axes, padding trailing singleton dimensions as MATLAB
does. Non-singleton field axes must have matching names and coordinates.
Singleton values are broadcast constants; coordinates from the non-singleton
axis win. Named singleton axes cannot cross a different named physical domain.
ndmul preserves ordered Pentarnion binary products. nddiv requires real
Pentarnion divisors; complex division uses conventional complex arithmetic.
ndmatmul is an explicit rank2 ordered matrix product; batch products are
rejected. Contracted named physical axes require matching names, coordinates
and transform domains. Unannotated default axes are positional matrix axes.
Duplicate output row/column names receive a _columns suffix on the column.
Complex/Pentarnion domains are never mixed implicitly.
ndconj,ndabs,ndreal,ndimag and ndcoeff(A,0..4) select explicit meanings.

ndfft/ndifft(A,axis_index_or_name) transform along one axis, with normalization
on inverse. Native radix2/Bluestein kernels support arbitrary axis lengths
1..262144, including primes. Fourier outputs have the complex domain. Whole
Pentarnion FFT is rejected: use ndcoeff(A,c), then ndfft on that real field.
The algebra's i remains a Pentarnion basis vector. FFT keeps source sampling
coordinates as provenance and marks transformed_axes; they are not relabeled
as physical frequencies. Use ndvalues with explicitly chosen spectral bins
for spectral plots; ndplot/interpolation reject transformed coordinate axes.
The inverse clears the selected marker. ndinterp(A,axis,queries)
does native componentwise linear interpolation on increasing coordinates;
queries must increase and stay within the closed sampled interval.

ndmap(A,@callback[,options(parameters=options(...),threads=0)]) compiles a pure
scalar callback through Studio's existing structural cache. Bodies are reused
when shape, values or fixed runtime parameters change. Input/output domains
are preserved: real->real, Pentarnion->Pentarnion, or two real arguments and
two real return values for split-complex elements. This is a pointwise map,
not a general array-state ODE callback or an automatic higher tensor derivative
interface. Module work and compiled callback work share a bounded allowance.

ndplot(A[,options(xaxis="x",yaxis="y",component=0,value="abs")]) uses native
geometry for a scalar surface. Unshown axes must be singleton: first use
ndslice to select one time/component frame. A complex plot defaults magnitude;
a Pentarnion plot defaults scalar component0. All five source coefficients
remain stored. Current plots are surfaces, without implicit extrapolation or
averaging of hidden dimensions.

The lazy penta_nd DLL/SO uses Studio's shared CPU budget and no private pool.
Threads0 means automatic. FFT worker count is also bounded by aggregate scratch
memory. Cancellation, output sizes, callback failures and finite values are
checked before result publication. Independent NumPy/MATLAB comparisons and
direct-DFT prime-length tests accompany these examples.
