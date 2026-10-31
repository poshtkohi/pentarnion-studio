# Optional QA utility: requires NumPy and SciPy; Studio has no Python dependency.
from pathlib import Path
import sys,json,hashlib
import numpy as np
from scipy.integrate import solve_ivp
import scipy
out=Path(__file__).parent;out.mkdir(parents=True,exist_ok=True)
s2=np.sqrt(2.)
C=np.diag([1.,-1.,-1.,-1.,-1.])
def L(q):
 a,b,c,d,e=q
 return np.array([[a,-b,-c,-d,-e],
  [b,a,(-d+e)/s2,(c+e)/s2,(-c-d)/s2],
  [c,(d+e)/s2,a,(-b+e)/s2,(-b-d)/s2],
  [d,(-c+e)/s2,(b+e)/s2,a,(-b-c)/s2],
  [e,(-c+d)/s2,(b-d)/s2,(-b+c)/s2,a]])
def R(q):return C@L(C@q)@C
A=np.array([-.5,-2.,.5,-1.,-.5]);B=np.array([1.,0.,2.,0.,-.5]);p0=.5*np.array([1.,-1.,-1.,1.,-1.])
def rhs(t,p,eta):return L(A)@p+(1-eta)*(L(B)@(L(p)@p))+eta*(L(L(B)@p)@p)
def jac(p,eta):return L(A)+(1-eta)*(L(B)@(L(p)+R(p)))+eta*(R(p)@L(B)+L(L(B)@p))
assoc=rhs(0,p0,1)-rhs(0,p0,0)
assert np.max(np.abs(assoc-np.array([3*s2,7.,13.,-5.,-15.])/8))<1e-14
for eta,name in [(0.,'eta0'),(1.,'eta1'),(.37,'eta037')]:
 times=np.linspace(0.,5.,51)
 sol=solve_ivp(lambda t,p:rhs(t,p,eta),(0,5),p0,method='DOP853',rtol=2.3e-14,atol=2e-15,t_eval=times)
 assert sol.success,sol.message
 np.savetxt(out/f'paper_bracketing_{name}.csv',np.column_stack([sol.t,sol.y.T]),delimiter=',',fmt='%.17g',header='t,x0,x1,x2,x3,x4',comments='')
pp=np.array([.352211456319145,.280943768469983,.580647718066596,.274403530427830,.442578560477541])
period=5.795118818486838
cycle=solve_ivp(lambda t,p:rhs(t,p,1),(0,period),pp,method='DOP853',rtol=2.3e-14,atol=2e-15)
closure=float(np.linalg.norm(cycle.y[:,-1]-pp))
assert closure<5e-12,closure
metadata={'source':'tests/ode/reference/generate_reference.py; original algebra research and checked numerical fixture','equations':['bracketing-compact','bracketing-A','bracketing-B','bracketing-p0','bracketing-left-matrix','bracketing-jacobian'],
 'method':'independent SciPy DOP853, explicitly implemented coordinate matrices','scipy':scipy.__version__,'rtol':2.3e-14,'atol':2e-15,'initial_associator':assoc.tolist(),'periodic_orbit_closure':closure,
 'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.csv')}}
(out/'provenance.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')
print(json.dumps(metadata,indent=2))
