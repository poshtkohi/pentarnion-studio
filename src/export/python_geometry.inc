R"PENTA_GEOM_PY(

def p_geometry(name,*args):
    if name=='curvegeometry':
        points=np.asarray(args[0],dtype=float)
        if points.ndim!=2 or len(points)<3 or points.shape[1] not in (2,3) or not np.all(np.isfinite(points)):raise ValueError('Curve needs at least3 finite2D/3D points')
        delta=np.linalg.norm(np.diff(points,axis=0),axis=1)
        if np.any(delta==0):raise ValueError('Consecutive duplicate curve points')
        arc=np.concatenate(([0.],np.cumsum(delta)));first=np.maximum(np.arange(len(points))-1,0);last=np.minimum(np.arange(len(points))+1,len(points)-1);tangent=(points[last]-points[first])/(arc[last]-arc[first])[:,None];speed=np.linalg.norm(tangent,axis=1)
        if np.any(speed==0):raise ValueError('Curve tangent undefined at reversing cusp')
        tangent/=speed[:,None];normal=(tangent[last]-tangent[first])/(arc[last]-arc[first])[:,None];curvature=np.linalg.norm(normal,axis=1);normal=np.divide(normal,curvature[:,None],out=np.zeros_like(normal),where=curvature[:,None]>0)
        return SimpleNamespace(arc=arc[:,None],length=float(arc[-1]),tangent=tangent,normal=normal,curvature=curvature[:,None],status='sampled_estimate')
    import matplotlib.pyplot as plt
    x=np.asarray(args[0],dtype=float).ravel();y=np.asarray(args[1],dtype=float).ravel();volume=name=='isosurface';z=np.asarray(args[2],dtype=float).ravel() if volume else None;values=np.asarray(args[3] if volume else args[2],dtype=float)
    for axis in [x,y]+([z] if volume else []):
        if len(axis)<2 or not np.all(np.isfinite(axis)) or not np.all(np.diff(axis)>0):raise ValueError('Geometry axes must be finite and strictly increasing')
    total=len(x)*len(y)*(len(z) if volume else 1)
    if total>1000000 or not np.all(np.isfinite(values)) or values.size!=total or (not volume and values.shape!=(len(y),len(x))):raise ValueError('Geometry shape or1million sample bound exceeded')
    dimensions=3 if name in ('surf','isosurface') else 2;axes=plt.gca()
    if dimensions==3 and not hasattr(axes,'get_zlim'):axes.remove();axes=plt.gcf().add_subplot(111,projection='3d')
    X,Y=np.meshgrid(x,y)
    if name=='surf':axes.plot_surface(X,Y,values,cmap='viridis')
    elif name=='imagesc':axes.pcolormesh(x,y,values,shading='nearest',cmap='viridis')
    elif name=='contour':
        levels=np.asarray(args[3],dtype=float).ravel() if len(args)>3 else np.linspace(float(values.min()),float(values.max()),10)[1:-1]
        if not 1<=len(levels)<=32 or not np.all(np.isfinite(levels)):raise ValueError('Contour needs1..32 finite levels')
        axes.contour(X,Y,values,levels=np.unique(levels))
    elif volume:
        from mpl_toolkits.mplot3d.art3d import Poly3DCollection
        level=float(args[4]);data=values.ravel(order='C').reshape(len(z),len(y),len(x));triangles=[];owned=set();offsets=[(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)];tets=[(0,1,2,6),(0,2,3,6),(0,3,7,6),(0,7,4,6),(0,4,5,6),(0,5,1,6)]
        if not np.isfinite(level):raise ValueError('Isosurface level must be finite')
        for k in range(len(z)-1):
            for j in range(len(y)-1):
                for i in range(len(x)-1):
                    points=[np.asarray([x[i+a],y[j+b],z[k+c]]) for a,b,c in offsets];scalar=[data[k+c,j+b,i+a] for a,b,c in offsets];ids=[((k+c)*len(y)+j+b)*len(x)+i+a for a,b,c in offsets]
                    for tet in tets:
                        zero=[a for a in tet if scalar[a]==level]
                        if len(zero)==4:continue
                        if len(zero)==3:
                            key=tuple(sorted(ids[a] for a in zero))
                            if key in owned:continue
                            owned.add(key)
                        hits=[points[a] for a in zero]
                        for a in range(4):
                            for b in range(a+1,4):
                                u,v=tet[a],tet[b]
                                if (scalar[u]<level<scalar[v]) or (scalar[v]<level<scalar[u]):
                                    scale=max(abs(scalar[u]),abs(scalar[v]),abs(level));fraction=(level/scale-scalar[u]/scale)/(scalar[v]/scale-scalar[u]/scale);point=(1-fraction)*points[u]+fraction*points[v]
                                    if not any(np.array_equal(point,old) for old in hits):hits.append(point)
                        if len(hits)<3:continue
                        hits=np.asarray(hits);offset=hits-hits.mean(axis=0);normal=np.cross(offset[0],offset[1]);best=np.linalg.norm(normal)
                        for a in range(len(hits)):
                            for b in range(a+1,len(hits)):
                                candidate=np.cross(offset[a],offset[b]);length=np.linalg.norm(candidate)
                                if length>best:normal=candidate;best=length
                        if best==0:continue
                        normal/=best;u=offset[np.argmax(np.linalg.norm(offset,axis=1))];u=u/np.linalg.norm(u);v=np.cross(normal,u);order=np.argsort(np.arctan2(offset@v,offset@u));hits=hits[order]
                        for a in range(1,len(hits)-1):
                            if np.linalg.norm(np.cross(hits[a]-hits[0],hits[a+1]-hits[0]))>0:triangles.append(hits[[0,a,a+1]])
                        if len(triangles)>333333:raise ValueError('Isosurface portable triangle allowance exceeded')
        if triangles:axes.add_collection3d(Poly3DCollection(triangles,facecolor='C0',alpha=.8));axes.set_xlim(x[0],x[-1]);axes.set_ylim(y[0],y[-1]);axes.set_zlim(z[0],z[-1])
    else:raise ValueError('Unsupported geometry target')
    return 0
)PENTA_GEOM_PY"
