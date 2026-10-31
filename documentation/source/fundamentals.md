# Pentarnion Theory and Programming Language Fundamentals

Pentarnion Studio 0.2
An introduction to the algebra and its scientific programming environment
Language 0.2   Windows release   31 October 2026

This manual explains how to express and check calculations in Pentarnion Studio. It begins with the algebra and the working environment, then develops the typed language, arrays, native numerical callbacks, differential equations, figures and reproducible workflows. It is an introduction to implemented facilities, with selected examples and their limits, rather than an assertion that every method or every mathematical problem is supported.

The current language specification and feature-specific contracts are the detailed authority for signatures, resource limits and optional facilities. Earlier milestones in the specification retain historical exclusions; later named extensions supersede those exclusions where stated. This book describes the combined current interface and identifies important distinctions that prevent misleading calculations.

@authors

@contents

## 1 Purpose and supported scope

### 1 1 A scientific environment for explicit algebra

Pentarnion Studio combines an editor, a MATLAB-like Command Window, project management, native numerical modules and scientific figures. Its central subject is a five-dimensional real nonassociative algebra represented by five real coefficients. A `.p` file records both the algebraic expressions and the choices needed for a numerical experiment. A saved script therefore gives a reader more information than an isolated image or a table of output values.

The language makes multiplication order visible. It does not treat all products as ordinary complex arithmetic, and it does not infer that an element's canonical reciprocal is a general-purpose cancellation operation. These distinctions matter when comparing different bracketings of a differential equation, constructing left and right multiplication operators, or fitting a model whose parameters are algebra elements.

Studio also supports real numerical work and explicit complex values. Additional finite-dimensional algebras are described by immutable definitions, including quaternions, octonions, sedenions, dual numbers and dual quaternions. A user-defined multiplication table can be imported as data under a validated schema. The additional algebra definitions coexist with the original Pentarnion context; they do not change what the unqualified basis names `i`, `j`, `k` and `ell` mean.

### 1 2 Interpreted orchestration and native models

Ordinary checked scripts orchestrate calculations. Designated native functions, ODE equations and supported solver callbacks are translated into shared libraries, using DLLs on Windows and shared objects on Linux. Numerical modules load when needed. The native boundary is deliberate: a restricted, checked callback is not an unrestricted just-in-time compiler for every possible script statement.

Checking a source file does not run its calculations, write its data files or compile its callbacks. Execution requires an explicit Run action or Command Window submission. The distinction lets a user inspect diagnostics and generated suggestions before deciding to execute them. Copilot is optional and uses separately configured provider access; local Help and ordinary scientific calculations do not require it.

The Windows installer contains the runtime tools required for the workflows it packages. The source framework uses Qt and CMake with cross-platform intentions, but a Windows release is not evidence that every Linux distribution or graphics configuration has passed acceptance testing. Platform-specific results belong in the release notes.

@table Facility families and how to approach them
Family | Starting point | Important qualification
Pentarnion arithmetic | calculations and calculations/grouping.p | Explicit order and brackets are part of the expression.
Other algebras | native-algebras | An algebra definition hash identifies the type and conventions.
Arrays and linear algebra | array-shapes and linear-algebra | Real matrix inverse and ordered algebra operators are different objects.
Native differential equations | paper1-bracketing and advanced-odes | Sample spacing is distinct from adaptive internal steps.
PDEs and geometry | pdes-and-geometry and implicit-pdes | Choose a supported discretisation and validate convergence.
Research analysis | chaos-analysis and global-sensitivity | Finite numerical evidence does not establish a mathematical proof.

### 1 3 Reading and reproducing the examples

Begin with the small calculations project, then read the relevant example's README and source before running it. Some examples deliberately display failure reports, compare two numerical methods, create output files or demonstrate a limited resource budget. A diagnostic in such an example can be its intended result. Read `converged`, `status`, residuals and messages together rather than treating any printed object as a successful answer.

The book uses short source excerpts to explain an interface. The bundled complete project supplies its surrounding declarations and plotting steps. When a problem needs a capability beyond the documented subset, make that requirement explicit instead of weakening the algebra or silently replacing one method with another.

## 2 Installation and the Studio workspace

### 2 1 Install without a developer environment

Run `Pentarnion Studio Installer.exe` as the intended Windows user. The default destination is `%LocalAppData%\Programs\Pentarnion Studio`. This is a per-user installation. Its supported workflows do not depend on a preinstalled MATLAB, Excel, GCC or developer environment. The package keeps compiler tools, required runtime libraries and bundled fonts local to the application rather than modifying the global PATH or system font collection.

The installer is unsigned. Compare its SHA-256 checksum with the release manifest before use, and consult the release notes for tests actually performed. A matching checksum confirms that you have the referenced file; it does not establish the identity of a publisher. Do not interpret the Windows 10 VM checklist as a completed compatibility result unless its entries explicitly record that outcome.

Writable examples are placed beneath the Windows Documents known folder in `Pentarnion Studio\Examples`. The Windows known folder may be redirected, including to OneDrive. The Examples shortcut is the reliable entry point. During an update, only missing example files are copied; an existing edited example or result is preserved. Keep a separate project folder for a scientific study so its source, input data and outputs have a clear identity.

### 2 2 Panels and interface modes

The editor, Project panel, Command Window and Figures belong to the working area. Copilot occupies an independent right-hand panel. Classic menus and Ribbon pages share actions, shortcuts and enablement. Changing the interface mode does not execute a model or replace an open source document. Restore Default View resets arrangement while retaining documents and the scientific workspace.

The Command Window is one scrollable surface. Ordinary output appears in black, while warnings and errors appear in red. Source diagnostics retain a line and column; activating a diagnostic moves the editor to the relevant location. While an operation runs, Studio indicates Running instead of presenting the idle prompt. Ctrl+C requests cancellation and Stop provides the corresponding action. A callback or solver observes cancellation at defined checkpoints, so cancellation is a request rather than an instruction to forcibly terminate Windows processes.

The worker indicator reports observed native participation. Requested thread counts are maxima, not promises that all cores will run. BLAS reservations can be shown as bounds rather than as measured simultaneous worker activity. The website link immediately before the worker icon is `pentarnion.com`.

### 2 3 Files and execution

Open a `.pprj` project to collect source files under an explicit project root. Opening a source file by itself also works. Save before Run File; a new untitled buffer must acquire a path before execution. The project or source directory determines where relative data paths and callback caches belong. A saved script can be run from the Command Window with `run("main.p");` in the appropriate directory.

The editor supports syntax highlighting, a nonmodal Find and Replace panel, formatted copying and printing with source colours. Autosaving is a useful recovery measure, but source control and explicit backups remain the way to identify a reviewed scientific revision. Do not use a temporary native compilation cache as the only copy of an equation definition.

### 2 4 Help and optional Copilot

Use F1, Help for selection, Language Reference or the Help search box to read local documentation. Command Window forms such as `help plot2` and `help("plot2");` explain a specific command. Help browsing does not execute an example. Check its argument types and the scope of its notes before copying source.

Copilot starts with a local greeting and example prompts. Shuffle and example selection remain local; selecting an example fills the composer and does not send a message. A provider must be configured and Send must be pressed for a provider request. Source sharing defaults to None, and the context controls let you review the chosen outgoing material. Replies use readable source titles instead of displaying internal reference identifiers. Generated source remains a proposal to review, statically validate and explicitly apply; static validity does not prove scientific correctness.

## 3 Mathematical foundations

### 3 1 Coordinates and a fixed context

The original Studio algebra is the normalized switching family `A_gamma_v1`. Its basis order is $1,i,j,k,\ell$, and the context contains a fixed real parameter $\gamma$ in the closed interval from zero to one. The complete multiplication table is the authority for a product. Changing a displayed projection does not change this context or drop omitted coefficients from the numerical element.

@equation element

Multiplication is bilinear over the reals and has a two-sided identity. Define the two switching coefficients and the normalization used in the pure basis table as follows.

@equation switching

The square of each pure basis element is minus the identity. The remaining products depend on their order. Table @ref table_pure gives the complete pure basis table; each cell multiplies its row basis element on the left by its column basis element on the right. Here the typeset letters refer to algebra elements, not array indices.

@table id=table_pure Pure basis multiplication for the normalized switching family
Left by right | i | j | k | ell
i | -1 | kappa k + sigma ell | -kappa j - sigma ell | -h(j+k)
j | -kappa k - sigma ell | -1 | kappa i + sigma ell | -h(i+k)
k | kappa j + sigma ell | -kappa i - sigma ell | -1 | -h(i+j)
ell | h(j+k) | h(i+k) | h(i+j) | -1

At gamma zero, the subspace spanned by the identity and `i`, `j`, `k` is Hamilton's quaternion algebra. The full five-dimensional algebra still includes `ell`; it does not become an associative quaternion algebra merely because gamma is zero. Generic quaternion elements should instead use the explicit quaternion preset when that is the intended model.

### 3 2 Conjugation and the quadratic identity

Conjugation changes the sign of the four pure coefficients. The function `norm2(p)` returns their sum of squares together with the scalar coefficient's square. The number returned is a squared norm. Its existence does not assert that the norm is multiplicative for arbitrary products.

@equation conjugation
@equation norm
@equation quadratic

The quadratic identity gives an associative one-element subalgebra. Powers of one element are consequently unambiguous for this family. The full algebra is not generally associative, alternative or flexible. The one-element identity does not authorize regrouping products involving independent elements. Studio therefore retains explicit groups in its checked representation and rejects an ambiguous chain of at least three nonassociative operands.

### 3 3 Brackets change a calculation

For gamma one, the following two expressions provide a concrete comparison.

@equation bracketing

The associator measures this difference for general inputs. The commutator measures an order difference in a binary product.

@equation associator

Write `(B*p)*p` or `B*(p*p)` according to the model you intend. Do not write `B*p*p` and rely on an ordinary algebra simplifier to guess the model. Scalar factors do not count as independent algebra operands, but adding a scalar coefficient does not otherwise remove the need for meaningful grouping.

### 3 4 Reciprocals and solving equations

For a nonzero Pentarnion, Studio defines a canonical reciprocal in its one-element subalgebra.

@equation inverse

Equation division has a different meaning. The expression `p/q` asks for the unique solution of a right-multiplication equation; `p\q` asks for the unique solution of a left-multiplication equation. The corresponding real coordinate operators are explicit linear maps.

@equation operators

These numerical solves can fail because an operator is singular, inconsistent or unacceptably ill-conditioned. A two-sided canonical reciprocal does not make every multiplication operator invertible. Multiplying an equation by `inv(q)` and cancelling across a third factor is therefore not a general replacement for the documented solve.

### 3 5 One element calculus

Write an element as its scalar part plus a pure part, and let the latter's Euclidean coefficient length be $r$. The published one-element calculus gives the following exponential and trigonometric functions. These formulas act on the entire element rather than applying the real function independently to each coefficient.

@equation exponential
@equation trigonometric
@equation cosine
@equation hyperbolicsine
@equation hyperboliccosine

The hyperbolic functions use the corresponding slice formulas. At a zero pure part the continuous limit is the ordinary real scalar result. The implemented whole-Pentarnion functions are `sin`, `cos`, `exp`, `sinh` and `cosh`; an additional real overload such as `sqrt` is not automatically a whole-Pentarnion overload. No identity that separates the exponential of a sum of independent noncommuting elements is assumed. The public algebra contract and implementation define the mathematical conventions used here [A1].

## 4 Expressions values and scripts

### 4 1 Names literals and statements

Source files use UTF-8 and the extension `.p`. Identifiers are case-sensitive ASCII names beginning with a letter or underscore, followed by letters, digits or underscores. Unicode is allowed in comments, strings and filesystem names. `%` introduces a line comment; block comments use `%{` and `%}` on otherwise whitespace-only lines. Double-quoted strings recognize the documented escape sequences, including `\n`, `\t`, `\"` and `\\`.

End executable statements with a semicolon. A line break normally counts as whitespace, not a statement terminator. Braced control and function definitions do not need a trailing statement semicolon. There is no implicit multiplication: write `2*i`, not `2i`. Single quotes are not strings; they serve derivative notation on an ODE equation's left side or conjugate transpose in an expression.

@example A complete arithmetic script
```p
use_algebra("A_gamma", gamma=1);
integer count = 5;
real half = 1/2;
pentarnion p = 1 + i - j + ell;
pentarnion q = pent([1,0,-1,0,1]);
display("same value:", p == q);
display("scaled value:", half*p);
```

The optional `use_algebra` directive must be the first nonempty, noncomment statement. It fixes gamma for the file; gamma is a numeric literal, not a time-varying variable. Without a file directive, the project default applies, and without either the default is gamma one. Generic named algebras introduced later are separate immutable definitions.

### 4 2 Types and conversions

Variables must be declared with an initializer before use. Reassignment preserves their declared type. Integer values are signed 64-bit values; real values use finite binary64 approximations. Integers can widen to reals, and real scalars can embed into a Pentarnion's scalar coordinate. There is no automatic narrowing from a Pentarnion to a real or from a real to an integer. Large integers may cease to be exact when represented as real values.

The `pent([a,b,c,d,e])` constructor creates one element with five coefficients. A vector declaration creates an array, so a five-entry real vector is not automatically one Pentarnion. Explicit complex values are also separate from Pentarnion basis `i`: use `complex(re,im)` and the corresponding functions rather than assuming every object using imaginary notation has the same type.

### 4 3 Precedence and explicit output

Calls, groups, indexing and transpose bind tightly. Exponentiation binds above unary sign, so `-p^2` means the negative of the square. Exponentiation associates to the right; multiplication and division parse to the left subject to the explicit-group rule. A Pentarnion power needs an integer exponent, while real scalar powers check that their result lies in the real domain.

Use `display` for intentional printed output. A script expression can be evaluated and discarded; Studio does not create MATLAB's implicit `ans` binding. In the Command Window a submitted bare expression can be displayed for inspection. The command environment shares workspace bindings across submissions, while a saved file has its checked top-level scope and documented run behavior.

### 4 4 Errors domains and clearing

`sqrt` requires a nonnegative real input, logarithms require a positive real input, and inverse trigonometric functions enforce their real domains. A nonfinite or unrepresentable result is an explicit failure. Do not remove a domain check merely to make a numerical model continue.

`clc;` clears visible output; `clear;` removes user bindings; `clear p q;` removes the named bindings. Protected names and algebra bases cannot be cleared or reassigned. `who` lists names and `whos` also describes type and shape. `assert(condition,"message");`, `warning("message");` and `error("message");` make assumptions and diagnostics visible in reproducible source.

## 5 Control flow functions and callbacks

### 5 1 Conditions and bounded iteration

Studio uses braces for blocks and explicit types for function signatures. Real scalar conditions accept comparisons and logical operators. Comparison results are integer zero or one, and `true` and `false` are constants with that interpretation. The logical operators `&&` and `||` short-circuit. A Pentarnion or an entire array does not acquire an implicit ordering or truth value.

@example A loop and a typed function
```p
function real square(real x) { return x*x; }
integer total = 0;
for(n=1:5) { total = total+n; }
if(total == 15) {
    display("sum:", total, "square:", square(3));
} else {
    error("Unexpected sum");
}
```

A loop iterator is local to the loop, and an earlier binding with the same name is restored afterward. Declarations in a block are local; assignment to an existing outer variable persists. `break` exits the nearest loop and `continue` begins its next iteration. A `while` condition is re-evaluated at each iteration. Resource budgets and cancellation protect against unbounded work; an infinite loop is not an authorized way to exhaust all machine resources.

### 5 2 Function scopes and multiple outputs

Define a function before calling it. Functions have their own parameter and local scopes and no implicit global numerical captures. They can call earlier functions, and self-recursion is supported within a bounded call depth. Return statements must match the declared result type; reaching the end of a non-void function without a value is a runtime error.

@example Explicit multiple outputs and records
```p
function [real lo,real hi] bounds(real x) {
    return x-1, x+1;
}
[real lower,real upper] = bounds(5);
options cfg = options(scale=2.0);
display(lower, upper, cfg.scale);
```

Multiple-output assignment evaluates once and commits compatible targets together. A typed single assignment selects output one. Records and options have immutable fields with a checked schema; they are not dictionaries that silently accept misspelled option names. Named arguments follow positional arguments and are checked for missing, unknown or duplicate names.

### 5 3 Callback values and native declarations

`callback f=@named_function;` creates a named callback and `callback g=@(real x)=>x*x;` creates an explicitly typed anonymous callback. Anonymous captures are immutable snapshots. A solver may require the designated native subset rather than an arbitrary interpreted callback.

@example A native scalar model
```p
native function real cubic(real x,real a) {
    return x*x*x-a;
}
```

Native callbacks compile on first explicit execution to a structural cache. Their checked body, argument layout, algebra identity and code-generation ABI matter to cache identity. Runtime parameter values are supplied separately. Editing only those values does not require regenerating an unchanged body. Unsupported statements fail at the native boundary instead of silently turning a requested native solve into repeated interpreted execution.

### 5 4 Imports and safe module use

An import such as `import "helpers.p" as science;` gives access through an explicit namespace, for example `science.square(3.0)`. Imports are checked recursively without execution; running the source evaluates the imported module in its own scope. Cycles, incompatible contexts and bounded source budgets are checked. A workspace data file is not an importable executable module. Keep source dependencies with a project, and preserve their identities when archiving an experiment.

## 6 Arrays matrices and tensors

### 6 1 Shape is part of the computation

Dense array literals use commas or suitable spaces between columns and semicolons or newlines between rows. Scalars and arrays remain distinct even for a one-by-one array. A `vector` is a row or column array, and matrix multiplication follows conforming row and column dimensions.

@example Matrix multiplication and indexing
```p
matrix A = [1,2;3,4];
vector x = [1;2];
display(A*x);             % [5; 11]
display(A(2));            % 3 in column-major indexing
matrix entries = A.*A;    % entrywise products
matrix P = [1+i,j; k,ell];
display(coeff(P,1));      % i coefficients, same array shape
```

Ordinary array indices start at one. Linear indexing traverses columns, so the second entry of the shown matrix is three. By contrast, `coeff(p,k)` uses a zero-based coefficient index. This is an intentional distinction between selecting a mathematical array element and selecting a coordinate of that element.

`A*B` is matrix multiplication when both operands are arrays; `A.*B` is an entrywise ordered product. A scalar on one side of an algebra-valued array acts on that same side of each entry. Equal-shape arithmetic and the documented scalar broadcasting rules do not authorize every form of MATLAB implicit expansion. Shape errors should be fixed explicitly, rather than by flattening an entire field into a curve.

### 6 2 Slices constructors and immutable values

`zeros`, `ones`, `eye`, `linspace`, ranges, `reshape`, `diag`, `meshgrid` and `ndgrid` supply common array constructions. Slices use one-based indices, colon ranges and `end` under the documented shape rules. Logical masks are distinct from numeric lists of indices. `find` reports one-based locations in column-major order.

Assignments preserve earlier immutable values. If `matrix B=A;` is followed by an indexed write to `A`, the earlier `B` remains the earlier value. This is useful when comparing a transformed array with its input. Retained-coordinate and operation budgets still apply; aliases do not turn a small resource limit into unlimited storage.

`A.'` and `transpose(A)` transpose without conjugation; `A'` conjugate-transposes. Whole-Pentarnion elementary functions act on each complete entry, preserving all five coefficients. They are not five independent real maps.

### 6 3 Random values

`rng(seed);` sets a reproducible Studio stream. `rand` generates uniform values in the half-open unit interval, `randn` generates standard normal values, and `randi` generates bounded integers or integer-valued arrays. Reset a seed before an experiment when you need repeatability, and retain the seed with the analysis settings. The implementation does not promise MATLAB-identical random sequences or bit-for-bit normal samples across every platform's math library.

### 6 4 Fixed shapes and native tensors

Current extensions provide fixed-shape native matrices and true tensors. A tensor has rank one through eight, positive checked extents and an explicit entry kind. Its packing is column-major over complete entries; each entry retains all real coordinates of its algebra value. A tensor's multiplication by another tensor is not an implied contraction.

@example A rank three field
```p
tensor<2,2,2> T = tensor([1,2,3,4,5,6,7,8],[2,2,2]);
display(T(2,1,2));        % 6
display(size(T));
algebra H = builtin_algebra("quaternion");
tensor<H,2,2,2> Q = tensor(H.i,[2,2,2]);
Q(2,1,2) = H.j;
```

Use `tensorvalues`, `tensorcoords`, `tensorinfo` and `coeff` to inspect data and its complete shape. Elementwise tensor products use `.*`; real scaling preserves side order. Native callback and ODE signatures require fixed type and shape so generated argument layouts are unambiguous. The separate ND record APIs serve multidimensional fields, physical axes, complex arrays and batched matrix pages; they are not interchangeable with every tensor signature. The `native-tensors` and `batched-complex-linear-algebra` examples demonstrate these distinct interfaces.

## 7 Native algebra definitions and imports

### 7 1 Choose a family explicitly

`builtin_algebra` returns an immutable algebra definition. An element annotation binds a value to that definition, not merely to a coordinate count or a familiar display name. The normalized definition hash identifies type compatibility. Two imported tables with the same dimension do not automatically define interchangeable element types.

@example Hamilton products with an explicit definition
```p
algebra H = builtin_algebra("quaternion");
element<H> a = H.i;
element<H> b = H.j;
assert(a*b == H.k);
assert(b*a == -H.k);
display("Hamilton i*j:", a*b);
```

Presets include real, complex, quaternion, octonion, sedenion, dual, dual quaternion, split complex, split quaternion, matrix2, lie_so3, jordan_sym2 and bounded Clifford signatures. The mathematical families do not all have the same properties. A Lie product need not have a multiplicative unit, and a Jordan product is not ordinary associative matrix multiplication. Scalar embedding requires the verified unit; a matrix2 unit need not be the first basis vector.

### 7 2 Conventions and capability evidence

Quaternion basis labels are `one`, `i`, `j`, `k`. Octonion and sedenion labels use `one`, `e1` and so on, with the ordered Hamilton-based Cayley-Dickson convention reported by `algebra_info`. A different published Fano diagram can require a coordinate change. Do not compare coefficient arrays from different conventions as if their indices were universally meaningful [R1].

`algebra_info(A)` reports basis labels, unit, conjugation, metric, provenance and property evidence. Evidence can be exact proof, numerical evidence, failure, declaration or unknown. A declared property is not upgraded to an exact proof merely because it appears in metadata. Whole-element calculus and associative native shortcuts require the specific capability evidence stated in the contract.

### 7 3 Sedenion zero divisors

The bundled `sedenion-zero-divisors.p` uses the product pair shown below in Studio's stated convention.

@equation sedenion

Both factors are nonzero, but their product is zero. The example also checks a canonical reciprocal and reports a singular left-multiplication equation. This illustrates why the presence of a reciprocal does not prove cancellation or uniqueness. Use a solve report to inspect rank and conditioning, and distinguish `has_solution` meaning an accepted unique solution from a merely consistent singular equation.

### 7 4 Data only multiplication tables

`import_algebra("definition.json")` accepts a validated definition, not executable code. JSON declares the format and version, dimension, basis, coefficient domain and treatment of omitted products. Sparse products use zero-based left, right and output indices. A dense tensor uses left, right, output axis order. Duplicate products, malformed units and invalid shapes are rejected.

CSV and XLSX tables use a JSON metadata companion and four numeric columns: one-based left index, right index, output index and coefficient. These table indices intentionally differ from JSON indices. Use the supplied `dual-products-metadata.json`, CSV and XLSX examples instead of guessing a schema. Numeric cached formula values may be read, but spreadsheet formulas are not executed. Raw snapshots and definition hashes provide provenance; they do not prove every desired identity.

Imported dimensions and proof work are bounded. Exhausting optional proof work can leave a property unknown, while a malformed table or invalid claimed unit is an error. Keep the imported files with the source that uses them. A family name alone is not a substitute for the captured table and convention.

## 8 Numerical calculus linear algebra and roots

### 8 1 Derivatives of a checked native computation

`jacobian`, `gradient`, `hessian`, `jvp`, `vjp` and `hvp` operate on the real coordinates of supported typed native callbacks. A Pentarnion input contributes five real coordinates. A Jacobian has one row per output coordinate and one column per selected input coordinate. A gradient or Hessian requires a single real scalar objective. These are derivatives of a specified coordinate computation, not an invented universal derivative over every nonassociative algebra.

Analytic modes use compiler-generated automatic differentiation. The Hessian-vector product can use directional second-order propagation without allocating a full dense Hessian. Finite differences explicitly evaluate the compiled value function with the documented probes. `derivative_check` compares these routes numerically under its chosen step and tolerance. Agreement is useful evidence at tested points, rather than a proof that an expression is smooth throughout an entire domain.

Sampled `diff`, `gradient`, integration and interpolation APIs are separate numerical operations. They depend on sample spacing, the supplied coordinates and available data. In particular, two samples do not determine a meaningful three-point second derivative: insufficient data must be reported rather than silently returned as a successful zero.

### 8 2 Solve real systems before forming inverses

For real matrices, `linsolve` and `solve_report` expose checked numerical solves. Decompositions include LU, pivoted QR, Cholesky, SVD, eigenvalue and real Schur interfaces. Choose Cholesky only for a suitable symmetric positive definite matrix. LU and QR have their documented permutation conventions; inspect the returned record before writing a reconstruction formula.

@example A small real linear system
```p
matrix A = [2,0;0,4];
vector b = [2;8];
matrix x = linsolve(A,b,options(threads=1));
display(x);               % [1; 2]
assert(norm(A*x-b) < 1e-12);
```

`det` and `inv` support real matrices, with numerical safeguards. A determinant near zero is not a reliable rank test by itself. Prefer solving for the required right-hand side to forming an entire inverse. A least-squares candidate from an explicitly selected method can still fail the equation's residual criterion. Read convergence, rank, conditioning availability and the residual together [R2].

For an algebra-valued matrix, use explicit left or right real coordinate operators. `solveleft(A,B)` and `solveright(A,B)` solve different ordered equations. Determinants or inverses of those real operators do not define a general associative Pentarnion matrix determinant or inverse.

### 8 3 Roots fixed points and equilibria

`root` uses a bracketing scalar method for a continuous real callback with a valid sign-changing bracket or an exact endpoint root. `rootsolve` uses a square real-coordinate system and generated Jacobian by default. A Pentarnion equation contributes five real residuals. `fixedpoint` solves the residual of a map minus its argument; `equilibrium` sets an autonomous ODE right-hand side to zero rather than integrating an orbit.

@example A parameterized scalar root
```p
native function real equation(real x,real a) { return x*x-a; }
record answer = root(equation,[0,2],
    options(parameters=options(a=2.0),xtol=1e-12,ftol=1e-10));
assert(answer.converged);
display(answer.x,answer.residual_norm,answer.status);
```

A narrow bracket, small step or iteration limit alone is not a certified root. An example with no real root may deliberately report `solver_failed`, a nonzero residual and a backend message. That report is evidence of the attempted solve's failure, not proof that a root cannot exist anywhere. Scale variables and residuals meaningfully and preserve the report with the result.

### 8 4 Thread limits and shared resources

`threads(0)` selects automatic runtime policy and `threads(n)` sets the session maximum. A supported call can override it through a final `options(threads=n)` record. Independent work shares a CPU reservation; nesting does not imply that every nested solve may start a complete new machine-sized pool. Some algorithms are serial and small operations may remain serial. Parallelism preserves the algebra's ordered binary products; it does not grant reassociation of three factors or guarantee a speedup on every matrix size.

## 9 Differential equations and native compilation

### 9 1 Declare the equation and its parameters

An `ode` block declares time, typed state variables and explicit parameters. Every state has one derivative assignment. Parameters are supplied to `odesolve`; the equation does not implicitly capture arbitrary changing workspace variables. Pentarnion states retain all five real coordinates, while fixed-shape array and tensor extensions retain their complete checked coordinate layouts.

@example A short real decay equation
```p
ode decay(t,real x;real rate) { x' = -rate*x; }
solution s = odesolve(decay,x=1.0,rate=0.5,
    t0=0,t1=2,sample_step=0.05,
    method="bdf",jacobian="analytic",rtol=1e-9,atol=1e-11);
display(s);
```

The analytic reference solution is an exponential. Compare sampled values with it over a short interval, then refine tolerances or sampling as appropriate. The current universal ODE interface bounds the total number of real state coordinates; a tensor shape does not evade that bound. The full contract lists state, parameter, retained sample and execution limits.

### 9 2 Choose a supported method

@table Native ODE methods
method value | Backend and method | Intended use
bdf | CVODE variable-order backward differentiation formulas of orders 1 to 5 | Stiff systems where this multistep method is suitable
adams | CVODE variable-order Adams Moulton formulas of orders 1 to 12 | Nonstiff systems
erk | ARKODE Dormand-Prince embedded 5(4) explicit Runge-Kutta pair | Nonstiff systems with adaptive one-step integration
esdirk | ARKODE ESDIRK4(3)6L[2]SA implicit pair | Stiff systems with the documented implicit table

`sample_step` controls returned samples, not the adaptive solver's internal step sizes. Smaller sample spacing can make an exported time series easier to inspect without necessarily changing its integration accuracy. Relative and absolute tolerances control local numerical error measures; they do not by themselves bound every scientific observable or long-time instability. CVODE and ARKODE method definitions are documented by SUNDIALS [R3, R4].

`jacobian="analytic"` requests the compiler-generated real-coordinate Jacobian. `finite_difference` selects the documented numerical alternative. The explicit ERK method does not require a Jacobian during integration and reports that fact. The compiler still preserves a common model identity; selecting a method or changing numerical tolerances does not alter the equation's mathematical brackets.

### 9 3 Cache structure separately from runtime data

The first execution of a native model can compile a shared library. Its cache identity depends on checked structure, state and parameter layouts, algebra conventions, generated support and target ABI. Changing equation literals or its body changes that identity. Changing supplied parameter values, initial conditions or integrator settings can reuse an unchanged kernel. Corrupted or incompatible entries must be regenerated rather than reused silently.

`tic;` starts a monotonic execution timer and `toc;` prints elapsed seconds. `real elapsed=toc();` obtains the elapsed value. Time spent in the native compiler process is excluded, including a first cache miss. Cache lookup, preparation and solver work remain included; figure drawing that occurs after worker evaluation is not included in a timer stopped inside the script. Timing across separate Command Window submissions includes the time spent at the prompt.

### 9 4 Reproduce the first manuscript bracketing family

The `paper1-bracketing` educational example fixes its coefficients and initial value and exposes eta as a runtime parameter. Its two endpoints select different bracketings of the quadratic term. The equation is not made associative by using a numerical integrator.

@equation paperode

@example The bundled bracketing model
```p
use_algebra("A_gamma",gamma=1);
pentarnion A = -1/2 - 2*i + j/2 - k - ell/2;
pentarnion B = 1 + 2*j - ell/2;
pentarnion p0 = (1-i-j+k-ell)/2;
real eta = 0;
ode bracketing(t,pentarnion p;pentarnion A,pentarnion B,real eta) {
    p' = A*p + (1-eta)*(B*(p*p)) + eta*((B*p)*p);
}
solution s = odesolve(bracketing,p=p0,A=A,B=B,eta=eta,
    t0=0,t1=200,sample_step=0.01,
    method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
```

The complete bundled source adds five time-series panels and several three-coordinate projections. Retain the chosen eta, time interval and tolerances with each exported figure. The long example is not the first installation smoke test. For a new model, start with a short interval and a known special case before treating an unfamiliar long-time trajectory as a scientific discovery.

### 9 5 Advanced trajectories and sensitivities

The `advanced-odes` family demonstrates documented events, dense evaluation, backward-time cases, sensitivities and terminal adjoints. Event and mass-operator callbacks obey native and cache contracts. Parameter sensitivity trajectories are different from an instantaneous right-hand-side Jacobian, and a terminal adjoint is different from an arbitrary derivative through every script side effect. Use the feature-specific example and option schema for the requested quantity.

## 10 Figures subplots and publication output

### 10 1 Projections and curves

`plot2("i","j",p,q);` projects algebra elements onto two coefficients and draws vectors with arrowheads. `plot3("1","i","j",s.p);` projects a retained trajectory. The literal axis `"1"` denotes the scalar coefficient, and `"ell"` denotes the fifth basis coefficient. Retained values preserve all coordinates, including those absent from the view.

Real coordinate curves use `plot(x,y)` or `plot3(x,y,z)` with matching sample counts. `coeff(s.p,0)` extracts the scalar coefficient series of a Pentarnion state. For an array or tensor state, use the documented state-coordinate accessor so a packing convention is not guessed from an unrelated array shape.

@example Two views of one calculation
```p
pentarnion p = 1+i+j+ell;
pentarnion q = 1-i+j-ell;
figure(1,"Name","Addition and product","NumberTitle","off");
subplot(1,2,1);
plot2("i","j",p,q,p+q,p*q);
title("Projection onto $(i,j)$");
subplot(1,2,2);
plot3("i","j","k",p,q,p+q,p*q);
title("Projection onto $(i,j,k)$");
```

### 10 2 Figure identity and layout

`figure(n)` selects or creates a numbered figure. Name and NumberTitle control its visible title. Figure commands do not return general MATLAB graphics handles. `subplot(rows,columns,index)` selects a one-based row-major cell within the current figure. In contrast, `tilefigures("studio",rows,columns)` arranges whole figures. A layout of two figures can therefore contain independent subplot layouts inside each figure.

Use the visual grid picker and split targets to arrange views interactively. Dragging toward a target edge previews a half split; the centre groups views as tabs. Floating windows retain their figure data and expose normal window controls. Selected figures and active subplots receive coloured selection borders without changing exported mathematical data.

`clf;` clears the current figure's panels while retaining its identity and name. `close(n);` closes that figure and `close("all");` closes all figures. `clear` and `clc` serve numerical workspace and text-output functions instead. Close All Documents is a separate command and preflights unsaved source or workbook changes.

### 10 3 Labels tools and exports

`title`, `xlabel`, `ylabel`, `zlabel` and `caption` target the active panel. Labels support a bounded mathematical subset with `$...$` spans. Escape each source backslash, for example `caption("$p\\star q$");`. The renderer is not a TeX document processor and does not execute packages, macros or filesystem commands.

Zoom, pan, reset, data tips and sample-point controls operate on retained data. Several data tips can be selected and removed. A data cursor reports a sample rather than proving that the sampled point is an exact extremum or that a curve between sparse samples has been resolved. Change the solver sampling or the underlying model when more numerical information is required.

The figure Export Image action saves the entire figure, while the subplot Save icon saves only its panel. PNG and JPEG are raster outputs; supported SVG routes preserve vector curves, arrow geometry and typography. Publication and selected headless routes also support vector PDF. Dense curves can create large vector files, and projected 3D vector export uses its stated ordering rather than promising an identical depth-buffer raster. Check the chosen plot type and output format before submitting a figure to a journal.

### 10 4 Geometry and fields

`surf`, `imagesc`, `contour`, level-set and mesh plotting facilities display sampled real geometry or explicitly selected algebra coefficients. They do not implicitly turn a five-dimensional value into a three-dimensional object. A scalar field associated with a mesh is optional for plain geometry; a colour field must have the compatible shape when supplied. Labels, visible ranges and camera settings belong with the exported figure, while the full numerical field belongs in the saved data.

## 11 Persistence data and reproducible workflows

### 11 1 Preserve source and numerical data separately

A `.p` file records an executable scientific description. A `.pws` workspace stores supported numerical values and strings under a versioned data schema. `save("workspace.pws");` saves supported data, and quoted names select particular variables. Default persistence excludes executable function and ODE definitions. Save those definitions in source, not as an assumption about a workspace snapshot.

`load` validates versions, shapes, algebra context, finite values and resource limits before committing a set of restored bindings. A malformed or over-budget load leaves the existing workspace intact. A checked script must declare compatible typed targets before using names loaded later in that same file; subsequent Command Window submissions can use the successfully loaded names. A data file is not evaluated as code.

### 11 2 CSV XLSX and workbook editing

@example Numeric table exchange
```p
matrix measurements = [0,1;1,2;2,4];
writematrix(measurements,"measurements.csv");
writematrix(measurements,"measurements.xlsx","Results");
matrix restored = readmatrix("measurements.xlsx","Results");
assert(restored == measurements);
```

Numeric CSV uses a bounded native parser. XLSX uses native XLSXIO with its supporting libraries and does not require Excel, COM or Python at runtime. Binary legacy `.xls` is not an alias for `.xlsx`. Numeric tables must be rectangular and finite; the numeric API does not interpret arbitrary text headers, merged cells or spreadsheet formula semantics. A stored cached numeric formula result may be read, but Studio does not execute Excel formulas.

Writing a numeric XLSX creates a new single-sheet workbook rather than preserving unrelated sheets in an existing file. Save to a new path when you need to keep an original workbook. Export a Pentarnion array through explicitly chosen coefficient arrays, or another documented structured format, so no coefficient is lost. Studio's workbook viewing and editing panel is a separate user interface; it does not change the numeric import contract into complete Excel compatibility.

### 11 3 Paths side effects and atomic writes

Relative data paths use the active project directory in Studio, or the source directory when no project is open. The CLI uses its script directory. Save input and output names explicitly, especially when sharing an example with someone who uses a different working directory.

Supported file writers validate data and use adjacent temporary files before an atomic commit. A failure or cancellation before commit preserves an existing destination. An already completed explicit write is a real side effect and is not undone by a later script error. Keep important input files read-only or copy them to an experiment directory rather than depending on a later cancellation to reverse earlier output.

### 11 4 Provenance and research archives

Research record and archive facilities retain typed data, settings, source identity, cache metadata and canonical data hashes under their specific schemas. A hash detects a changed byte sequence; it is neither a digital signature nor a mathematical validation certificate. Checkpoint/resume and replay are explicit workflows and require matching model and configuration identities. Do not rename a stale cache or checkpoint to force a model with changed equations to accept it.

For a paper, retain the complete source revision, imported multiplication definitions, input data, seeds, solver/discretisation settings, version and platform details. Keep the numerical data behind figures and the analysis that generated any summary metric. Record failures and exclusions under a defined rule rather than dropping inconvenient trajectories without explanation.

## 12 Fitting PDEs geometry and research extensions

### 12 1 Native optimization and fitting

`minimize` handles a real scalar objective through documented NLopt algorithms. `lsqfit` and `lsqnonlin` minimize native residual coordinates through Ceres, and `curvefit` supplies a sampled model interface. `odefit` couples an ODE model to supported trajectory sensitivities or explicitly selected differences. Unknowns, fixed parameters, bounds, scales, weights and loss functions are explicit options. A Pentarnion parameter expands to its full coordinate representation rather than becoming one undocumented scalar.

Iterative numerical routines support the documented MATLAB-like output-observer interface. An observer can update a fitted-model display or request stopping under its contract. Numerical objectives and supported event or derivative callbacks use cached native code. An output observer does not authorize hidden changes to the objective, data or solver state. Read the complete `iteration_output` contract before combining plotting, callbacks and a long-running fit.

General optimization includes local and selected global methods; evolutionary facilities add documented differential evolution, CMA-ES, a real genetic algorithm and NSGA-II workflows. A finite search has a stopping rule and a candidate, not a proof of a global optimum. Covariance and bootstrap summaries require their statistical assumptions. A low residual does not by itself prove parameter uniqueness, correct mechanism or agreement within unmeasured experimental uncertainty.

### 12 2 PDE discretisation and convergence

The PDE facilities cover real and supported algebra-valued states with explicit local reactions and discretisation choices. The early one-dimensional interface provides centred finite differences and P1 finite elements; later extensions add documented grid, implicit, surface and high-order finite-element routes. Read the relevant example rather than assuming that selecting one library exposes all of that library's possible solvers.

For a diffusion-reaction equation, the real diffusion coefficient acts on each retained state coordinate. In the consistent-mass finite-element formulation, the mass matrix must remain in the semidiscrete equation.

@equation pde
@equation fem

The early P1 reaction term is a nodal interpolant, not exact quadrature of every nonlinear reaction. Explicit stepping has stability restrictions that depend on spatial resolution and reaction stiffness. The `implicit-pdes` and `stiff-pdes` examples provide separate interfaces for their implicit cases. Compare mesh and time refinements, boundary conditions, conserved quantities and a known solution where available. Parallel execution can reduce some costs, but cannot repair a wrong boundary condition or an unstable discretisation.

The MFEM extension exposes bounded supported spaces and forms, with separate contracts for mixed order, adaptation, weak fluxes and transient consistent mass. Optional PETSc routes require the packaged backend to be available. The presence of a backend library is not a claim of arbitrary distributed-memory, GPU or unbounded model support [R5].

### 12 3 Geometry and mechanics

Geometry examples distinguish sampled curves, parametric charts, intrinsic metrics, differential forms and tagged triangle meshes. A Pentarnion-valued parametrisation retains all coordinates and requires a defined metric before a geometric quantity is given a physical interpretation. Euclidean coordinate length is not automatically a composition norm or the intended metric of a manifold.

Mechanics facilities provide documented regular and constrained models, selected geometric integration and explicit diagnostics. Position, velocity and momentum are distinct concepts. Check constraints, admissible initial conditions, energy behavior and the documented integration assumptions. An algebra-valued state does not give an automatic physical interpretation to every coefficient or a universal mechanics theory.

### 12 4 Chaos and continuation as numerical evidence

Lyapunov spectra, localized Poincare sections, recurrence summaries, periodic shooting, Floquet multipliers and continuation/refinement facilities are available under their bounded native contracts. A finite positive exponent can be affected by transients, sampling, tolerances and the tested time interval. Recheck promising cases with longer intervals, refined tolerances and independent diagnostics. A bounded computed trajectory is not a proof of an invariant bounded set for every initial condition.

Checkpointed ODE/PDE experiments and map ensembles preserve task identity and isolate failed or escaped runs. Shared CPU reservations coordinate native workers. Select a reproducible seed and retain unsuccessful cases when the protocol requires them. Research Explorer and Map Explorer offer checked interfaces for particular workflows and can generate scripts; they do not remove the need to define the algebra, model and inference question.

### 12 5 A practical route through bundled examples

@table Suggested reading sequence for research examples
Folder | What to inspect first | Main question
calculations | main.p and grouping.p | What does the algebra calculate and why do brackets matter
native-algebras | quaternion-rotations-and-products.p | Which algebra definition and coordinate convention are being used
array-shapes | main.p | What are the shape and indexing rules
linear-algebra | main.p and pentarnion_operators.p | Is this a real solve or an ordered algebra equation
root-finding | main.p | Does a solver report a residual-accepted root
paper1-bracketing | main.p | How does eta choose the two explicit bracketings
advanced-odes | main.p and adjoint.p | Which derivative of a trajectory is being requested
native-tensors | column_major_fields.p and typed_tensor_decay.p | How are full entries packed and integrated
optimization | main.p | What are the objective constraints and termination conditions
pdes-and-geometry | README.txt | Which PDE grid method and boundary conditions are demonstrated
implicit-pdes | adaptive-stiff-reaction.p | How is a stiff reaction integrated and checked
chaos-analysis | lorenz.p and paper1_lyapunov.p | What finite-time evidence supports a dynamical interpretation

## 13 Troubleshooting limitations and reliable practice

### 13 1 Diagnose the layer that failed

A source error, native compiler error, solver failure and plot failure are different events. Activate a source diagnostic to inspect its location. For a native compiler error, retain the message, model identity and release version. For a solver failure, retain status, residuals, settings and initial values. A plotting error can occur after a numerical calculation has completed; do not assume that a missing figure proves the solver never ran.

The installed launcher supplies the local runtime environment. Copying only `pentarnion-studio.exe` or deleting its adjacent dependencies can break startup or native compilation. Use the complete installer and verify the checksum manifest. Keep a failing user script and data outside the application directory before testing a clean installation. Never delete a long-running experiment's valid checkpoint merely because a later optional report or image export failed.

### 13 2 Common source and mathematical mistakes

@table Diagnostics and likely causes
Symptom | Likely cause | Correct response
Expected semicolon | A statement ended only with a newline | Add the semicolon at the actual statement boundary.
Unknown variable or function | A name is undeclared out of scope or misspelled | Check case declaration order and explicit imports.
Nonassociative grouping diagnostic | Three algebra operands have no explicit grouping | Choose the mathematically intended parentheses.
Shape mismatch | Array dimensions or entry kinds are incompatible | Inspect size and the declared algebra rather than discarding coordinates.
Domain or nonfinite failure | A real function or callback left its valid domain | Check the equation initial values bounds and method assumptions.
Solver did not converge | Residual step or backend criteria were not met | Read the full report and test scales tolerances and a suitable initial point.
No general matrix inverse | The input is a nonassociative array | Select an ordered real coordinate operator or the documented equation solve.
Loaded variable unavailable in same file | Checking finished before the load executes | Predeclare a compatible typed target or use a later console submission.

### 13 3 Resource and platform limits

Source size, nesting, callback coordinate counts, retained data, operation budgets and wall-time limits are part of the runtime contract. Their purpose is to fail explicitly rather than publish a truncated or nonfinite result as a complete success. A resource error is not permission to increase every bound without examining the model and memory cost.

The framework is not an unrestricted MATLAB clone, Excel implementation, TeX engine or general symbolic theorem prover. A checked exporter supports documented MATLAB/Python subsets and explicit target adapters; it does not translate every program or reproduce every external toolbox. Symbolic operations preserve order and declared domains within their stated representation. They do not justify commuting or reassociating products that the algebra does not permit.

For a supported Python export, use the installed `penta-python.exe` launcher with the generated program path. The package includes its scientific Python runtime, so a separate Python or pip installation is not required for those packaged routes. For example, quote both paths when running `"%LocalAppData%\Programs\Pentarnion Studio\penta-python.exe" "C:\path\program.py"`. Keep any generated companion runtime files with the exported program. A MATLAB export requires an appropriate external MATLAB installation; MATLAB itself is not included with Studio.

Some optional numerical libraries or routes may be unavailable in a particular release build. Consult installed Help, dependency notices and the release notes rather than inferring availability from a roadmap title. Cross-platform source support and a successful Windows test do not prove every Linux or Windows 10 configuration. Unperformed compatibility tests remain pending.

### 13 4 Updates uninstall and scientific records

Save source and close Studio before updating it; preserve active calculations and unsaved work. The installer is intended to remove only its owned application tree, shortcuts, registration and settings at uninstall. External workspaces, source, datasets, results, checkpoints and credentials must remain intact. The user-workspace preservation rule is why the installer seeds missing examples rather than overwriting an existing edited file.

Keep versioned scientific records in a workspace outside the installed tree, and back them up independently. Save meaningful source revisions, not only the most recent screen image. When reporting a problem, supply the smallest source and input that reproduce it, expected versus observed behavior, version, platform and exact diagnostic. Do not include API credentials or unrelated private data.

## 14 References and project authority

### 14 1 Project specific definitions

[A1] Pentarnion Studio. Algebra contract `specs/algebras/A_gamma_v1.txt`, implementation `src/algebra`, language specification `language.txt`, and canonical feature contracts under `specs`, source profile 0.2. These public project sources specify the normalized switching table, one-element calculus and nonassociative multiplication order used by the implementation.

The public implementation and its tests are the executable source of the examples in this edition. General algebra references do not substitute a different multiplication table or an associative bracketing convention.

[A2] Pentarnion Studio bundled examples. Selected source folders are listed in Chapter 12. Source excerpts in this book are educational examples; the `paper1-bracketing` excerpt retains the bundled coefficients and brackets. The introductory arithmetic and representative language, real linear-solve, quaternion and tensor snippets were executed with the staged release CLI using one-thread limits. This is a bounded documentation check, not a full validation of every research example.

### 14 2 Established mathematical and numerical methods

[R1] John C. Baez. The Octonions. Bulletin of the American Mathematical Society, 39(2), 145-205, 2002. DOI `10.1090/S0273-0979-01-00934-X`. Author's text: `https://math.ucr.edu/home/baez/octonions/`. Errata appear in volume 42, page 213, 2005. This reference explains the classical construction and context; Studio's reported convention still governs a coordinate computation.

[R2] E. Anderson, Z. Bai, C. Bischof, S. Blackford, J. Demmel, J. Dongarra, J. Du Croz, A. Greenbaum, S. Hammarling, A. McKenney and D. Sorensen. LAPACK Users' Guide, third edition. Society for Industrial and Applied Mathematics, 1999. Authoritative online edition: `https://netlib.org/lapack/lug/index.html`. This is background for standard factorisations and numerical linear algebra, not a claim that every Studio routine calls LAPACK.

[R3] Alan C. Hindmarsh, Peter N. Brown, Keith E. Grant, Steven L. Lee, Radu Serban, Dan E. Shumaker and Carol S. Woodward. SUNDIALS: Suite of Nonlinear and Differential/Algebraic Equation Solvers. ACM Transactions on Mathematical Software, 31(3), 363-396, 2005. DOI `10.1145/1089014.1089020`. Verified through the SUNDIALS publications page at `https://computing.llnl.gov/projects/sundials/publications`.

[R4] SUNDIALS developers. CVODE Mathematical Considerations, version 7.8.0 documentation. `https://sundials.readthedocs.io/en/v7.8.0/cvode/Mathematics_link.html`. Accessed 31 October 2026. The documentation states the Adams Moulton and BDF families, error-control framework and supported solver structures. The exact packaged dependency versions are recorded in the release notices.

[R5] R. Anderson, J. Andrej, A. Barker, J. Bramwell, J.-S. Camier, J. Cerveny, V. Dobrev, Y. Dudouit, A. Fisher, Tz. Kolev, W. Pazner, M. Stowell, V. Tomov, I. Akkerman, J. Dahm, D. Medina and S. Zampini. MFEM: A Modular Finite Element Methods Library. Computers & Mathematics with Applications, 81, 42-74, 2021. DOI `10.1016/j.camwa.2020.06.009`. Verified against `https://mfem.org/about/`. Library capability is broader than any particular Studio adapter.

### 14 3 Versioned documentation and correspondence

References were verified against primary sources. Chapter organisation follows standard technical-book practice; no MathWorks text or branding is reproduced.

This edition describes Studio and language 0.2 for 31 October 2026. Correspondence: Dr Alireza Poshtkohi, `a.poshtkohi@herts.ac.uk`. Website: `https://pentarnion.com`. Archive this edition with its source revision, since later extensions may change signatures or limits.
