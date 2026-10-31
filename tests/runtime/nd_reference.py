"""Independent NumPy oracle for artifacts emitted by penta_nd_tests DIR."""
from pathlib import Path
import sys
try:
    import numpy as np
except ImportError as error:
    print("NumPy unavailable:", error)
    raise SystemExit(77)
root = Path(sys.argv[1])
samples = np.arange(30)
field = (np.cos(.2*samples)+1j*np.sin(.3*samples)).reshape((3,5,2),order="F")
expected = np.fft.fft(field,axis=1).ravel(order="F")
actual = np.loadtxt(root/"axis-fft.csv",delimiter=",")
assert np.max(np.abs(actual[:,0]+1j*actual[:,1]-expected)) < 1e-12
a = np.arange(1,7).reshape((2,1,3),order="F")
b = np.asarray([10,20,30,40]).reshape((1,4,1))
assert np.array_equal(np.loadtxt(root/"broadcast.csv"),(a+b).ravel(order="F"))
original = np.arange(1,25).reshape((2,3,4),order="F")
assert np.array_equal(np.loadtxt(root/"permutation.csv"),original.transpose(2,0,1).ravel(order="F"))
print("NUMPY_ND_REFERENCE_PASS",np.__version__)
