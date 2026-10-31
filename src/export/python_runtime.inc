R"PENTA_PY(# Generated Pentarnion export runtime. Requires NumPy; figures use Matplotlib.
import math
import time
import copy
import cmath
from types import SimpleNamespace
import numpy as np

GAMMA = 1.0
_timer = None

class Dual:
    def __init__(self, value, tangent,second=None):
        self.value, self.tangent, self.second = float(value), np.asarray(tangent, dtype=float),None if second is None else np.asarray(second,dtype=float)
    def _h(self):return np.zeros((len(self.tangent),len(self.tangent))) if self.second is None else self.second
    def __add__(self, b):
        if isinstance(b,Penta):return NotImplemented
        return Dual(self.value+b.value, self.tangent+b.tangent,None if self.second is None and b.second is None else self._h()+b._h()) if isinstance(b,Dual) else Dual(self.value+b,self.tangent,self.second)
    __radd__ = __add__
    def __neg__(self): return Dual(-self.value,-self.tangent,None if self.second is None else -self.second)
    def __sub__(self,b): return self+-b
    def __rsub__(self,b): return -self+b
    def __mul__(self,b):
        if isinstance(b,Penta):return NotImplemented
        return Dual(self.value*b.value,self.tangent*b.value+b.tangent*self.value,None if self.second is None and b.second is None else self._h()*b.value+b._h()*self.value+np.outer(self.tangent,b.tangent)+np.outer(b.tangent,self.tangent)) if isinstance(b,Dual) else Dual(self.value*b,self.tangent*b,None if self.second is None else self.second*b)
    __rmul__ = __mul__
    def __truediv__(self,b):
        return self*(b**-1) if isinstance(b,Dual) else Dual(self.value/b,self.tangent/b,None if self.second is None else self.second/b)
    def __rtruediv__(self,b): return b*self**-1
    def __pow__(self,b):
        if isinstance(b,Dual): return p_unary('exp',b*p_unary('log',self))
        slope=0 if b==0 else b*self.value**(b-1);curvature=0 if b in (0,1) else b*(b-1)*self.value**(b-2)
        return self.chain(self.value**b,slope,curvature)
    def chain(self,value,slope,curvature):return Dual(value,slope*self.tangent,None if self.second is None else slope*self.second+curvature*np.outer(self.tangent,self.tangent))
    def __bool__(self):return bool(self.value)
    def __eq__(self,b):return self.value==(b.value if isinstance(b,Dual) else b)
    def __lt__(self,b):return self.value<(b.value if isinstance(b,Dual) else b)
    def __le__(self,b):return self.value<=(b.value if isinstance(b,Dual) else b)
    def __gt__(self,b):return self.value>(b.value if isinstance(b,Dual) else b)
    def __ge__(self,b):return self.value>=(b.value if isinstance(b,Dual) else b)

class Penta:
    def __init__(self, value=0):
        if isinstance(value,Penta): self.c=value.c.copy()
        elif isinstance(value,(list,tuple,np.ndarray)):
            if len(value)!=5: raise ValueError('Pentarnions require five coefficients')
            self.c=list(value)
        else: self.c=[value,0,0,0,0]
    def __add__(self,b): return Penta([a+c for a,c in zip(self.c,Penta(b).c)])
    __radd__=__add__
    def __neg__(self): return Penta([-a for a in self.c])
    def __sub__(self,b): return self+-Penta(b)
    def __rsub__(self,b): return Penta(b)+-self
    def __mul__(self,b):
        b=Penta(b); c=[0]*5
        k=1/math.sqrt(1+GAMMA*GAMMA); s=GAMMA*k; h=1/math.sqrt(2)
        table={(1,2):(0,0,0,k,s),(1,3):(0,0,-k,0,-s),(1,4):(0,0,-h,-h,0),
               (2,1):(0,0,0,-k,-s),(2,3):(0,k,0,0,s),(2,4):(0,-h,0,-h,0),
               (3,1):(0,0,k,0,s),(3,2):(0,-k,0,0,-s),(3,4):(0,-h,-h,0,0),
               (4,1):(0,0,h,h,0),(4,2):(0,h,0,h,0),(4,3):(0,h,h,0,0)}
        for i in range(5):
            for j in range(5):
                q=self.c[i]*b.c[j]
                if i==0: c[j]=c[j]+q
                elif j==0: c[i]=c[i]+q
                elif i==j: c[0]=c[0]-q
                else:
                    for z,t in enumerate(table[i,j]): c[z]=c[z]+q*t
        return Penta(c)
    __rmul__=__mul__
    def __truediv__(self,b):
        if isinstance(b,Penta): raise ValueError('Equation division requires a validated adapter')
        return Penta([a/b for a in self.c])
    def __pow__(self,n):
        if int(n)!=n: raise ValueError('Pentarnion powers require integers')
        if n<0: raise ValueError('Negative Pentarnion powers require a validated adapter')
        out=Penta(1)
        for _ in range(int(n)): out=out*self
        return out
    def __eq__(self,b): return all(a==c for a,c in zip(self.c,Penta(b).c))
    def __repr__(self): return 'Penta('+repr(self.c)+')'

def p_basis(n):
    c=[0]*5; c[n]=1; return Penta(c)
def p_cast(a, kind):
    if kind=='pentarnion': return Penta(a)
    if kind=='pentarnionmatrix': return np.vectorize(Penta,otypes=[object])(np.asarray(a))
    if kind=='realmatrix': return np.asarray(a).copy() if np.asarray(a).dtype==object else np.asarray(a,dtype=float).copy()
    return a.copy() if isinstance(a,np.ndarray) else a
def p_truth(a):
    if isinstance(a,np.ndarray):
        if a.size!=1: raise ValueError('Condition requires one scalar')
        a=a.item()
    return bool(a)
def p_unary(name,a):
    if isinstance(a,np.ndarray): return np.vectorize(lambda x:p_unary(name,x),otypes=[object] if a.dtype==object else [float])(a)
    if isinstance(a,Penta):
        x=a.c[0]; r2=sum(v*v for v in a.c[1:]); r=p_unary('sqrt',r2) if not isinstance(r2,Dual) or r2.value else 0
        # sinc and sinhc are analytic in r^2 at zero, including their tangent.
        if (r.value if isinstance(r,Dual) else r)==0: sinc=1-r2/6+r2*r2/120; sinhc=1+r2/6+r2*r2/120;circular=1-r2/2+r2*r2/24;hyperbolic=1+r2/2+r2*r2/24
        else: sinc=p_unary('sin',r)/r;sinhc=p_unary('sinh',r)/r;circular=p_unary('cos',r);hyperbolic=p_unary('cosh',r)
        if name=='exp': real=p_unary('exp',x)*circular; factor=p_unary('exp',x)*sinc
        elif name=='sin': real=p_unary('sin',x)*hyperbolic; factor=p_unary('cos',x)*sinhc
        elif name=='cos': real=p_unary('cos',x)*hyperbolic; factor=-p_unary('sin',x)*sinhc
        elif name=='sinh': real=p_unary('sinh',x)*circular; factor=p_unary('cosh',x)*sinc
        elif name=='cosh': real=p_unary('cosh',x)*circular; factor=p_unary('sinh',x)*sinc
        else: raise ValueError('Unsupported Pentarnion function '+name)
        return Penta([real]+[factor*v for v in a.c[1:]])
    if isinstance(a,Dual):
        x=a.value
        if name in ('abs','floor','ceil'):
            if (name=='abs' and x==0) or (name!='abs' and x==math.floor(x)):raise ValueError('Nondifferentiable exported elementary point')
            return a.chain(abs(x) if name=='abs' else getattr(math,name)(x),(1 if x>0 else -1) if name=='abs' else 0,0)
        slopes={'sin':lambda:math.cos(x),'cos':lambda:-math.sin(x),'exp':lambda:math.exp(x),'sinh':lambda:math.cosh(x),'cosh':lambda:math.sinh(x),'log':lambda:1/x,'sqrt':lambda:.5/math.sqrt(x),'tan':lambda:1/math.cos(x)**2,'tanh':lambda:1/math.cosh(x)**2}
        curvature={'sin':lambda:-math.sin(x),'cos':lambda:-math.cos(x),'exp':lambda:math.exp(x),'sinh':lambda:math.sinh(x),'cosh':lambda:math.cosh(x),'log':lambda:-1/x**2,'sqrt':lambda:-.25/x**1.5,'tan':lambda:2*math.tan(x)/math.cos(x)**2,'tanh':lambda:-2*math.tanh(x)/math.cosh(x)**2}
        return a.chain(getattr(math,name)(x),slopes[name](),curvature[name]() if a.second is not None else 0)
    if name=='abs': return abs(a)
    return getattr(cmath if isinstance(a,complex) else math,name)(a)
def p_member(value,name): return value.fields[name] if isinstance(value,PentaSolution) else value[name] if isinstance(value,dict) else getattr(value,name)
def p_record(names,values):return SimpleNamespace(**dict(zip(names,values)))
def p_closure(function,kinds,values):
    captured=copy.deepcopy(values)
    return lambda *args:function(*[p_cast(value,kind) for value,kind in zip(args,kinds)],*captured)
def p_invoke(function,parameters,kinds,names,values,defaults):
    args=list(defaults); used=set(); position=0
    for name,value in zip(names,values):
        index=parameters.index(name) if name else position
        if index in used or index>=len(args): raise ValueError('Duplicate/outside exported parameter')
        args[index]=value;used.add(index)
        if not name: position+=1
    if any(value is None for value in args): raise ValueError('Missing required exported parameter')
    return function(*[p_cast(value,kind) for value,kind in zip(args,kinds)])
def p_dense_solve(a,b):
    a=np.asarray(a);b=np.asarray(b)
    if a.ndim!=2 or b.ndim!=2 or a.shape[0]!=b.shape[0]: raise ValueError('Equation dimensions do not agree')
    if not np.all(np.isfinite(a)) or not np.all(np.isfinite(b)):raise ValueError('Equation inputs must be finite')
    scale=np.max(np.abs(a)) if a.size else 0
    if not scale:raise ValueError('Linear equation has no accepted unique solution')
    normalized=a/scale;rhs=b/scale
    if not np.all(np.isfinite(rhs)):raise ValueError('Scaled equation exceeds finite target range')
    u,s,vh=np.linalg.svd(normalized,full_matrices=False)
    if len(s)<a.shape[1] or not len(s) or s[-1]<=1e-12*s[0]:raise ValueError('Linear equation has no accepted reliable unique solution')
    x=vh.conj().T@((u.conj().T@rhs)/s.reshape(-1,1))
    if not np.all(np.isfinite(x)):raise ValueError('Nonfinite equation solution')
    common=max(1.,np.max(np.abs(x)),np.max(np.abs(rhs)))
    denominator=np.linalg.norm(normalized,np.inf)*np.linalg.norm(x/common,np.inf)+np.linalg.norm(rhs/common,np.inf)
    if np.linalg.norm(normalized@(x/common)-rhs/common,np.inf)>1e-10*max(denominator,np.finfo(float).tiny): raise ValueError('Linear equation residual is not satisfied')
    return x
def p_ordered_operator(a,right=False):
    a=np.asarray(a,dtype=object)
    if a.ndim==0:a=a.reshape(1,1)
    rows,columns=a.shape;out=np.zeros((5*(columns if right else rows),5*(rows if right else columns)))
    for row in range(rows):
        for column in range(columns):
            value=Penta(a[row,column])
            for basis in range(5):
                product=p_basis(basis)*value if right else value*p_basis(basis)
                if right:out[column*5:column*5+5,row*5+basis]=product.c
                else:out[row*5:row*5+5,column*5+basis]=product.c
    return out
def p_ordered_solve(a,b,right=False):
    scalar=not isinstance(a,np.ndarray) and not isinstance(b,np.ndarray)
    aa=np.asarray(a,dtype=object);bb=np.asarray(b,dtype=object)
    if aa.ndim==0:aa=aa.reshape(1,1)
    if bb.ndim==0:bb=bb.reshape(1,1)
    pent=any(isinstance(x,Penta) for x in list(aa.flat)+list(bb.flat))
    if not pent:
        value=p_dense_solve(aa.T.astype(float),bb.T.astype(float)).T if right else p_dense_solve(aa.astype(float),bb.astype(float))
        return value.item() if scalar else value
    if right:packed=np.asarray([[Penta(bb[r,c]).c[k] for r in range(bb.shape[0])] for c in range(bb.shape[1]) for k in range(5)])
    else:packed=np.asarray([[Penta(bb[r,c]).c[k] for c in range(bb.shape[1])] for r in range(bb.shape[0]) for k in range(5)])
    solved=p_dense_solve(p_ordered_operator(aa,right),packed)
    rows,columns=(bb.shape[0],aa.shape[0]) if right else (aa.shape[1],bb.shape[1]);out=np.empty((rows,columns),dtype=object)
    for r in range(rows):
        for c in range(columns):out[r,c]=Penta(solved[c*5:c*5+5,r] if right else solved[r*5:r*5+5,c])
    return out.item() if scalar else out
def p_bin(op,a,b,am=False,bm=False):
    if op in ('==','!='):
        equal=bool(np.array_equal(a,b)) if isinstance(a,np.ndarray) or isinstance(b,np.ndarray) else a==b
        return int(equal if op=='==' else not equal)
    if op=='*' and am and bm:
        aa=np.asarray(a);bb=np.asarray(b)
        return aa.astype(float)@bb.astype(float) if aa.dtype!=object and bb.dtype!=object else aa@bb
    if (am and bm) and np.shape(a)!=np.shape(b): raise ValueError('Array dimensions must match')
    if isinstance(a,np.ndarray) or isinstance(b,np.ndarray):
        aa,bb=np.broadcast_arrays(a,b)
        out=np.empty(aa.shape,dtype=object if aa.dtype==object or bb.dtype==object else (bool if op in ('<','<=','>','>=','&','|') else float))
        for i in np.ndindex(aa.shape): out[i]=p_bin(op,aa[i],bb[i])
        return out
    if op=='+': return a+b
    if op=='-': return a-b
    if op in ('*','.*'): return a*b
    if op in ('/','./'): return a/b
    if op in ('^','.^'): return a**b
    if op=='<': return a<b
    if op=='<=': return a<=b
    if op=='>': return a>b
    if op=='>=': return a>=b
    if op=='&': return bool(a) and bool(b)
    if op=='|': return bool(a) or bool(b)
    raise ValueError('Unsupported operator '+op)
def p_mat(rows):
    if not rows: return np.empty((0,0))
    out=np.block([[np.asarray(v).reshape(1,1) if np.asarray(v).ndim==0 else np.asarray(v) for v in row] for row in rows])
    return out if out.dtype==object else out.astype(float)
def p_range(a,h,b):
    if h==0: raise ValueError('Range increment cannot be zero')
    count=max(0,int(math.floor((b-a)/h+4*np.finfo(float).eps*max(1,abs((b-a)/h))))+1)
    return (a+np.arange(count)*h).reshape(1,-1)
def p_end(a,axis,total): return np.asarray(a).size if total==1 else np.asarray(a).shape[axis]
def _selector(s,n):
    if s is None: return np.arange(n),False,(n,1)
    v=np.asarray(s)
    if v.dtype==bool:
        if v.size!=n: raise ValueError('Mask has incorrect length')
        return np.flatnonzero(v.ravel(order='F')),False,(int(v.sum()),1)
    flat=v.ravel(order='F')
    if np.any(flat!=np.floor(flat)) or np.any(flat<1) or np.any(flat>n): raise IndexError('Index outside one-based bounds')
    return flat.astype(int)-1,v.ndim==0,v.shape if v.ndim==2 else (1,v.size)
def p_get(a,selectors):
    a=np.asarray(a); first,scalar,shape=_selector(selectors[0],a.size if len(selectors)==1 else a.shape[0])
    if len(selectors)==1:
        values=a.ravel(order='F')[first]
        if scalar: return values[0]
        if selectors[0] is None: shape=(len(first),1)
        elif 1 in a.shape: shape=(1,len(first)) if a.shape[0]==1 else (len(first),1)
        return values.reshape(shape,order='F')
    second,scalar2,_=_selector(selectors[1],a.shape[1]); out=a[np.ix_(first,second)]
    return out[0,0] if scalar and scalar2 else out
def p_set(a,selectors,b):
    out=np.asarray(a).copy(); first,_,_=_selector(selectors[0],out.size if len(selectors)==1 else out.shape[0])
    if len(selectors)==1:
        b=np.asarray(b); values=b.ravel(order='F') if b.ndim else np.repeat(b.item(),len(first))
        if values.size!=len(first): raise ValueError('Slice assignment size mismatch')
        for idx,value in zip(first,values): out[idx%out.shape[0],idx//out.shape[0]]=value
    else:
        second,_,_=_selector(selectors[1],out.shape[1]); b=np.asarray(b)
        if b.ndim and b.shape!=(len(first),len(second)): raise ValueError('Slice assignment shape mismatch')
        out[np.ix_(first,second)]=b
    return out
def p_transpose(a,conjugate):
    def conj(x): return Penta([x.c[0]]+[-v for v in x.c[1:]]) if isinstance(x,Penta) else x if isinstance(x,Dual) else np.conjugate(x)
    if isinstance(a,np.ndarray): return np.vectorize(conj,otypes=[object] if a.dtype==object else [float])(a).T if conjugate else a.T.copy()
    return conj(a) if conjugate else a
def p_call(name,*args):
    a=args[0] if args else None
    if name in ('sin','cos','exp','sinh','cosh','sqrt','log','tan','tanh','abs','floor','ceil'): return p_unary(name,a)
    if name=='assert':
        if not p_truth(a): raise AssertionError(args[1] if len(args)>1 else 'Studio assertion')
        return 0
    if name=='display': print(*args); return 0
    if name=='complex':return complex(*args)
    if name=='real':return np.real(a)
    if name=='imag':return np.imag(a)
    if name in ('linsolve','solveleft','solveright'):
        if len(args)!=2:raise ValueError('Portable equation solve currently uses default numerical policy')
        return p_ordered_solve(args[0],args[1],name=='solveright')
    if name in ('leftop','rightop'):return p_ordered_operator(a,name=='rightop')
    if name=='det':return np.linalg.det(np.asarray(a,dtype=float))
    if name=='inv':return p_dense_solve(np.asarray(a,dtype=float),np.eye(np.shape(a)[0]))
    if name=='coeff':
        index=int(args[1]); return np.vectorize(lambda p:Penta(p).c[index],otypes=[object] if a.dtype==object else [float])(a) if isinstance(a,np.ndarray) else Penta(a).c[index]
    if name=='scalarpart': return p_call('coeff',a,0)
    if name=='purepart':
        if isinstance(a,np.ndarray): return np.vectorize(lambda p:Penta([0]+Penta(p).c[1:]),otypes=[object])(a)
        return a-Penta(Penta(a).c[0])
    if name=='conj': return p_transpose(a,True).T if isinstance(a,np.ndarray) else p_transpose(a,True)
    if name=='norm2':
        if isinstance(a,complex):return abs(a)**2
        if isinstance(a,np.ndarray): return np.vectorize(lambda p:sum(v*v for v in Penta(p).c),otypes=[object] if a.dtype==object else [float])(a)
        return sum(v*v for v in Penta(a).c)
    if name=='commutator': return args[0]*args[1]-args[1]*args[0]
    if name=='associator': return (args[0]*args[1])*args[2]-args[0]*(args[1]*args[2])
    if name=='norm':
        if isinstance(a,Penta): return p_unary('sqrt',sum(v*v for v in a.c))
        mode=args[1] if len(args)>1 else 2
        if isinstance(a,np.ndarray) and a.dtype==object:
            if mode not in (2,'fro'):raise ValueError('Analytic object-array norm supports Euclidean/fro only')
            if mode!='fro' and min(a.shape)>1:raise ValueError('Analytic matrix norm requires fro')
            return p_unary('sqrt',sum(value*value for value in p_pack([a])))
        if mode=='fro': return float(np.linalg.norm(a,ord='fro'))
        if np.asarray(a).ndim==2 and min(a.shape)>1: raise ValueError('Matrix norm requires fro')
        return float(np.linalg.norm(np.asarray(a).ravel(),ord=np.inf if mode=='inf' else mode))
    if name=='reshape':
        dims=list(np.asarray(args[1]).ravel()) if len(args)==2 else list(args[1:]); dims=[-1 if isinstance(d,np.ndarray) and d.size==0 else int(d) for d in dims]
        return np.reshape(a,dims,order='F').copy()
    if name in ('zeros','ones','eye'):
        if not args: return 1.0 if name=='ones' else 0.0
        dims=(int(a),int(a)) if len(args)==1 else (int(a),int(args[1])); return getattr(np,name)(*dims) if name=='eye' else getattr(np,name)(dims)
    if name=='linspace':
        n=int(args[2]) if len(args)>2 else 100
        return np.asarray([[args[1]]]) if n==1 else np.linspace(args[0],args[1],n).reshape(1,-1)
    if name=='size':
        shape=np.shape(a) if isinstance(a,np.ndarray) else (1,1)
        return np.asarray(shape).reshape(1,2) if len(args)==1 else int(shape[int(args[1])-1] if int(args[1])<=2 else 1)
    if name=='numel': return np.asarray(a).size
    if name=='length': return max(np.shape(a)) if isinstance(a,np.ndarray) and a.size else (0 if isinstance(a,np.ndarray) else 1)
    if name=='transpose': return p_transpose(a,False)
    if name=='find': return (np.flatnonzero(np.asarray(a).ravel(order='F'))+1).reshape(-1,1)
    if name=='logical': return np.asarray(a,dtype=bool).reshape(np.shape(a) if isinstance(a,np.ndarray) else (1,1))
    if name in ('sum','mean'):
        if not isinstance(a,np.ndarray): return a
        axis=int(args[1])-1 if len(args)>1 else (1 if a.shape[0]==1 else 0)
        return getattr(np,name)(a,axis=axis,keepdims=True) if axis<2 else a.copy()
    if name=='diag':
        offset=int(args[1]) if len(args)>1 else 0
        return np.diag(np.asarray(a).ravel(),offset) if not isinstance(a,np.ndarray) or min(a.shape)<=1 else np.diag(a,offset).reshape(-1,1)
    if name=='tic':
        global _timer; _timer=time.perf_counter(); return 0
    if name=='toc':
        if _timer is None: raise RuntimeError('toc requires tic')
        return time.perf_counter()-_timer
    raise ValueError('Unsupported exported call '+name)
def p_pack(values):
    out=[]
    for v in values:
        if isinstance(v,Penta):out.extend(v.c)
        elif isinstance(v,np.ndarray):out.extend(p_pack(list(v.ravel(order='C'))))
        elif isinstance(v,(tuple,list)):out.extend(p_pack(v))
        else:out.append(v)
    return out
def p_ode(function,names,widths,initial,params,settings):
    return p_ode_target(function,names,widths,initial,params,settings)
def p_figure(number=None,name=None):
    import matplotlib.pyplot as plt
    fig=plt.figure(number); fig.canvas.manager.set_window_title(name or 'Pentarnion export'); return fig
def p_plot(dim,axes,args):
    import matplotlib.pyplot as plt
    ax=plt.gca()
    if dim==3 and not hasattr(ax,'zaxis'):
        spec=ax.get_subplotspec(); ax.remove(); ax=plt.gcf().add_subplot(spec,projection='3d')
    if axes[0]<0:
        ax.plot(*[np.asarray(a).ravel(order='F') for a in args]); return
    for a in args:
        if isinstance(a,np.ndarray):
            points=np.asarray([[Penta(p).c[d] for d in axes[:dim]] for p in a.ravel(order='F')]); ax.plot(*points.T)
        else:
            coords=[Penta(a).c[d] for d in axes[:dim]]
            if dim==2: ax.quiver(0,0,*coords,angles='xy',scale_units='xy',scale=1)
            else: ax.quiver(0,0,0,*coords)
)PENTA_PY"
