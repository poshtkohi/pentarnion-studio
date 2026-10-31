"""Execute bundles emitted by penta_export_tests; optional target validation.

Usage: python validate_python_exports.py GENERATED_BUNDLE_ROOT
NumPy, SciPy and Matplotlib must be provided by the caller, never downloaded
from a test. This exercises exported artifacts rather than a second emitter.
"""
import os
from pathlib import Path
import runpy
import sys

os.environ.setdefault("MPLBACKEND", "Agg")
try:
    import numpy as np
    import scipy
    import matplotlib
except ImportError as error:
    print("Target dependency unavailable:", error)
    raise SystemExit(77)

root = Path(sys.argv[1]).resolve()
names = ["python-basic", "python-import", "python-ode", "python-figures", "python-structures", "python-fitting", "python-optimization", "python-graphs", "python-pdes", "python-mechanics-geometry", "python-advanced-ode", "python-hybrid", "python-reverse-costs"]
for name in names:
    sys.path.insert(0, str(root / name))
    runpy.run_path(str(root / name / "exported.py"), run_name="penta_validation")
    sys.path.pop(0)

import penta_export_runtime as runtime
# Independent centered-difference oracle for the automatically generated
# analytic coefficient calculus, including products and whole-element exp.
state = np.asarray([0.2, 0.3, -0.1, 0.05, 0.2])
def equation(values):
    p = runtime.Penta(values)
    return (p*p + runtime.p_unary("exp", p)).c
dual = [runtime.Dual(v, np.eye(5)[n]) for n, v in enumerate(state)]
analytic = np.asarray([value.tangent for value in equation(dual)])
reference = np.empty((5, 5))
for n in range(5):
    perturbation = np.zeros(5)
    perturbation[n] = 1e-6
    reference[:, n] = (np.asarray(equation(state+perturbation))-
                       np.asarray(equation(state-perturbation)))/(2e-6)
assert np.max(np.abs(analytic-reference)) < 1e-8

# Mixed second derivatives and whole-element radial-zero limits have independent
# formulas; augmented BDF sensitivities and mechanics rely on these Hessians.
left = runtime.Dual(.3, [1., 0.], np.zeros((2, 2)))
right = runtime.Dual(.7, [0., 1.], np.zeros((2, 2)))
mixed = left*left*runtime.p_unary("sin", right)+runtime.p_unary("exp", right)
expected_hessian = np.asarray([[2*np.sin(.7), .6*np.cos(.7)],
                              [.6*np.cos(.7), -.09*np.sin(.7)+np.exp(.7)]])
assert np.max(np.abs(mixed.second-expected_hessian)) < 1e-13
zero_radial = runtime.Penta([runtime.Dual(value, np.eye(5)[k], np.zeros((5, 5)))
                            for k, value in enumerate([.2, 0., 0., 0., 0.])])
assert np.max(np.abs(runtime.p_unary("exp", zero_radial).c[0].second-
                     np.diag([np.exp(.2)]+[-np.exp(.2)]*4))) < 1e-13

import matplotlib.pyplot as plt
figure = plt.figure(1)
assert len(figure.axes) == 2
assert len(figure.axes[0].collections) == 1
assert len(figure.axes[1].lines) == 1
figure.savefig(root / "python-export-figure.svg")
print("PYTHON_EXPORT_EXECUTION_PASS",
      "NumPy", np.__version__, "SciPy", scipy.__version__,
      "Matplotlib", matplotlib.__version__)
