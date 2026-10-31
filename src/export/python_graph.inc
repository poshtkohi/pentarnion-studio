R"PENTA_GRAPH_PY(

def p_graph(name,*args):
    import heapq
    from scipy.sparse import coo_matrix
    from scipy.sparse.csgraph import connected_components
    if name in ('graph','digraph'):
        options=p_options(args[1]) if len(args)>1 else {};p_check_options(options,{'n','weights','multigraph','labels'})
        edges=np.asarray(args[0] if args else [],dtype=float);edges=edges.reshape(0,2) if edges.size==0 else edges
        if edges.ndim!=2 or edges.shape[1]!=2 or not np.all(np.isfinite(edges)) or np.any(edges!=np.floor(edges)) or np.any(edges<1):raise ValueError('Graph edges require two columns of positive integral node IDs')
        n=options.get('n',int(edges.max()) if edges.size else 0);m=len(edges);multiple=options.get('multigraph',0)
        if int(n)!=n or not 0<=n<=100000 or m>500000 or (edges.size and edges.max()>n) or multiple not in (0,1):raise ValueError('Graph size/multigraph allowance exceeded')
        weights=np.asarray(options.get('weights',1.),dtype=float).ravel();weights=np.repeat(weights,m) if weights.size==1 else weights
        if weights.size!=m or not np.all(np.isfinite(weights)):raise ValueError('Graph requires one finite real weight per edge')
        labels=options.get('labels',SimpleNamespace());labelmap=p_options(labels)
        if any(not key.startswith('n') or not key[1:].isdigit() or str(int(key[1:]))!=key[1:] or not 1<=int(key[1:])<=n or not isinstance(value,str) for key,value in labelmap.items()):raise ValueError('Graph labels must be strings keyed n1,n2,...')
        directed=name=='digraph';seen=set()
        for a,b in edges.astype(int):
            pair=(a,b) if directed else (min(a,b),max(a,b))
            if not multiple and pair in seen:raise ValueError('Duplicate graph edges require multigraph=1')
            seen.add(pair)
        return SimpleNamespace(format='PentarnionGraph-v1',nodes=int(n),edge_count=m,directed=int(directed),multigraph=int(multiple),edges=edges.copy(),weights=weights[:,None].copy(),labels=labels)
    if not args or not isinstance(args[0],SimpleNamespace) or set(vars(args[0]))!={'format','nodes','edge_count','directed','multigraph','edges','weights','labels'} or args[0].format!='PentarnionGraph-v1':raise ValueError('Expected a checked graph record')
    original=args[0]
    if original.directed not in (0,1) or original.edge_count!=len(np.asarray(original.edges)):raise ValueError('Graph record has inconsistent flags/edge_count')
    G=p_graph('digraph' if original.directed else 'graph',original.edges,SimpleNamespace(n=original.nodes,weights=original.weights,multigraph=original.multigraph,labels=original.labels));n=G.nodes;m=G.edge_count;edges=G.edges.astype(int)-1;weights=G.weights[:,0];directed=bool(G.directed)
    adjacent=[[] for _ in range(n)];indegree=np.zeros(n);outdegree=np.zeros(n);strength=np.zeros(n);work=0
    for identifier,((a,b),weight) in enumerate(zip(edges,weights),1):
        adjacent[a].append((b,float(weight),identifier));outdegree[a]+=1;indegree[b]+=1;strength[a]+=weight;strength[b]+=weight
        if not directed:
            outdegree[b]+=1;indegree[a]+=1
            if a!=b:adjacent[b].append((a,float(weight),identifier))
    if not np.all(np.isfinite(strength)) or not np.isfinite(weights.sum()):raise ValueError('Graph sums exceed finite target range')
    def spend(count=1):
        nonlocal work
        work+=count
        if work>20000000:raise ValueError('Portable graph work allowance20million exceeded')
    def settings(required,allowed):
        if len(args) not in (required,required+1):raise ValueError('Incorrect graph argument count')
        values=p_options(args[-1]) if len(args)>required else {};p_check_options(values,allowed);return values
    def components(strong=False):
        if not n:return np.empty((0,1)),0
        spend(n+2*m);rows=[];cols=[]
        for a,entries in enumerate(adjacent):
            for b,_,_ in entries:rows.append(a);cols.append(b)
        count,raw=connected_components(coo_matrix((np.ones(len(rows)),(rows,cols)),shape=(n,n)).tocsr(),directed=directed,connection='strong' if strong else 'weak');mapping={};labels=[]
        for value in raw:
            if value not in mapping:mapping[value]=len(mapping)+1
            labels.append(mapping[value])
        return np.asarray(labels,dtype=float)[:,None],int(count)
    def paths(source,bellman=False,weighted=True):
        distance=np.full(n,np.inf);previous=np.full(n,-1,dtype=int);edge_ids=np.zeros(n,dtype=int);distance[source]=0.;previous[source]=source
        def relax(a,b,weight,identifier):
            candidate=distance[a]+(weight if weighted else 1.)
            if not np.isfinite(candidate):raise ValueError('Graph cost exceeds finite binary64 range')
            if candidate<distance[b]:distance[b]=candidate;previous[b]=a;edge_ids[b]=identifier;return True
            return False
        if bellman:
            for _ in range(max(0,n-1)):
                changed=False
                for a,entries in enumerate(adjacent):
                    if np.isfinite(distance[a]):
                        for b,w,i in entries:spend();changed=relax(a,b,w,i) or changed
                if not changed:break
            for a,entries in enumerate(adjacent):
                if np.isfinite(distance[a]):
                    for b,w,_ in entries:
                        spend()
                        if distance[a]+w<distance[b]:return distance,previous,edge_ids,True
        else:
            queue=[(0.,source)]
            while queue:
                d,a=heapq.heappop(queue);spend()
                if d!=distance[a]:continue
                for b,w,i in adjacent[a]:
                    spend()
                    if relax(a,b,w,i):heapq.heappush(queue,(distance[b],b))
        return distance,previous,edge_ids,False
    if name=='graphinfo':
        settings(1,set());_,count=components();return SimpleNamespace(nodes=n,edge_count=m,directed=int(directed),multigraph=G.multigraph,self_loops=int(np.sum(edges[:,0]==edges[:,1])),negative_edges=int(np.sum(weights<0)),component_count=count,total_weight=float(np.sum(weights)),degree=(indegree+outdegree if directed else outdegree)[:,None],in_degree=indegree[:,None],out_degree=outdegree[:,None],strength=strength[:,None],threads_used=1,work_units=work,status='complete')
    if name in ('components','graphcomponents'):
        options=settings(1,{'kind'});kind=options.get('kind','weak')
        if kind not in ('weak','strong'):raise ValueError('Unknown components kind')
        labels,count=components(kind=='strong');return SimpleNamespace(labels=labels,count=count,kind=kind,status='complete')
    if name in ('graphadjacency','graphlaplacian'):
        settings(1,set())
        if n*n>1000000:raise ValueError('Portable dense graph matrix exceeds1million entries')
        A=np.zeros((n,n))
        for (a,b),w in zip(edges,weights):
            spend();A[a,b]+=w
            if not directed and a!=b:A[b,a]+=w
        if name=='graphlaplacian':np.fill_diagonal(A,0);A=np.diag(A.sum(axis=1))-A
        return A
    if name=='shortestpath':
        options=settings(3,{'method'});method=options.get('method','auto');source=int(args[1])-1;target=int(args[2])-1
        if source+1!=args[1] or target+1!=args[2] or not 0<=source<n or not 0<=target<n or method not in ('auto','dijkstra','bellman_ford'):raise ValueError('Invalid path endpoints/method')
        bellman=method=='bellman_ford' or (method=='auto' and np.any(weights<0))
        if not bellman and np.any(weights<0):raise ValueError('Dijkstra requires nonnegative weights')
        distance,previous,identifiers,negative=paths(source,bellman);reachable=not negative and np.isfinite(distance[target]);nodes=[];used=[]
        if reachable:
            cursor=target
            while cursor!=source:
                if len(nodes)>=n:raise ValueError('Invalid predecessor cycle')
                nodes.append(cursor+1);used.append(identifiers[cursor]);cursor=previous[cursor]
            nodes.append(source+1);nodes.reverse();used.reverse()
        return SimpleNamespace(path=np.asarray(nodes,dtype=float)[None,:],edge_ids=np.asarray(used,dtype=float)[None,:],distance=float(distance[target]) if reachable else 0.,reachable=int(reachable),distance_available=int(reachable),negative_cycle=int(negative),status='negative_cycle' if negative else 'complete' if reachable else 'unreachable',method='bellman_ford' if bellman else 'dijkstra')
    if name=='spanningtree':
        settings(1,set())
        if directed:raise ValueError('Spanning tree requires an undirected graph')
        parent=list(range(n));count=n;used=[]
        def root(a):
            while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
            return a
        for index in sorted(range(m),key=lambda i:(weights[i],i)):
            spend();a,b=map(root,edges[index])
            if a!=b:parent[b]=a;count-=1;used.append(index)
        return SimpleNamespace(edge_ids=(np.asarray(used,dtype=float)+1)[None,:],edges=G.edges[used,:],weights=G.weights[used,:],total_weight=float(weights[used].sum()),component_count=count,spanning=int(count==1),status='complete' if count==1 else 'forest')
    if name=='toposort':
        settings(1,set())
        if not directed:raise ValueError('Topological sort requires a directed graph')
        incoming=indegree.copy();queue=[a for a in range(n) if incoming[a]==0];heapq.heapify(queue);order=[]
        while queue:
            a=heapq.heappop(queue);order.append(a+1)
            for b,_,_ in adjacent[a]:
                spend();incoming[b]-=1
                if incoming[b]==0:heapq.heappush(queue,b)
        valid=len(order)==n
        return SimpleNamespace(order=np.asarray(order if valid else [],dtype=float)[None,:],acyclic=int(valid),status='complete' if valid else 'cycle')
    if name=='graphcycle':
        settings(1,set());color=np.zeros(n,dtype=int);parent=np.full(n,-1,dtype=int);incoming=np.zeros(n,dtype=int)
        for start in range(n):
            if color[start]:continue
            color[start]=1;stack=[(start,0)]
            while stack:
                a,index=stack[-1]
                if index==len(adjacent[a]):color[a]=2;stack.pop();continue
                stack[-1]=(a,index+1);b,_,identifier=adjacent[a][index];spend()
                if not directed and identifier==incoming[a]:continue
                if color[b]==0:parent[b]=a;incoming[b]=identifier;color[b]=1;stack.append((b,0))
                elif color[b]==1:
                    nodes=[a];used=[identifier];cursor=a
                    while cursor!=b:used.append(incoming[cursor]);cursor=parent[cursor];nodes.append(cursor)
                    nodes.reverse();nodes.append(b);used=used[1:][::-1]+used[:1]
                    return SimpleNamespace(nodes=(np.asarray(nodes,dtype=float)+1)[None,:],edge_ids=np.asarray(used,dtype=float)[None,:],found=1,status='complete')
        return SimpleNamespace(nodes=np.empty((1,0)),edge_ids=np.empty((1,0)),found=0,status='complete')
    if name in ('graphlayout','plotgraph'):
        options=settings(1,{'dimensions','radius'}) if name=='graphlayout' else {};dimensions=options.get('dimensions',2);radius=options.get('radius',1.)
        if dimensions not in (2,3) or not np.isfinite(radius) or radius<=0:raise ValueError('Layout needs2/3 dimensions and positive finite radius')
        theta=np.arange(n)*2*np.pi/max(1,n);positions=np.zeros((n,dimensions));positions[:,0]=radius*np.cos(theta);positions[:,1]=radius*np.sin(theta)
        if n==1:positions[:]=0
        if name=='graphlayout':return positions
        if len(args)>2:raise ValueError('Use plotgraph(G[,positions])')
        if len(args)==2:positions=np.asarray(args[1],dtype=float)
        if positions.ndim!=2 or positions.shape[0]!=n or positions.shape[1] not in (2,3) or not np.all(np.isfinite(positions)) or not n:raise ValueError('Graph positions require nodes rows and2/3 real columns')
        import matplotlib.pyplot as plt
        dimensions=positions.shape[1];axes=plt.gca()
        if dimensions==3 and not hasattr(axes,'get_zlim'):axes.remove();axes=plt.gcf().add_subplot(111,projection='3d')
        axes.scatter(*positions.T)
        for (a,b),weight in zip(edges,weights):
            axes.plot(*positions[[a,b]].T,color='C0')
            if directed and a!=b:axes.quiver(*positions[a],*(positions[b]-positions[a]),**({'angles':'xy','scale_units':'xy','scale':1} if dimensions==2 else {}))
        for index,p in enumerate(positions):axes.text(*p,vars(G.labels).get('n'+str(index+1),str(index+1)))
        return 0
    if name=='centrality':
        options=settings(1,{'kind','normalized','weighted'});kind=options.get('kind','degree');normalized=options.get('normalized',0);weighted=options.get('weighted',1)
        if normalized not in (0,1) or weighted not in (0,1):raise ValueError('Centrality flags must be0/1')
        simple={'degree':indegree+outdegree if directed else outdegree,'in_degree':indegree,'out_degree':outdegree,'strength':strength}
        if kind in simple:scores=simple[kind].copy();scores/=n-1 if normalized and n>1 else 1
        elif kind in ('closeness','harmonic','betweenness'):
            if weighted and np.any(weights<=0):raise ValueError('Weighted path centralities require strictly positive weights')
            scores=np.zeros(n)
            for source in range(n):
                distance,_,_,_=paths(source,False,bool(weighted))
                if kind!='betweenness':
                    d=distance[np.isfinite(distance)&(np.arange(n)!=source)];scores[source]=(len(d)/d.sum()*(len(d)/(n-1) if normalized and n>1 else 1)) if kind=='closeness' and len(d) else (np.sum(1/d)/(n-1 if normalized and n>1 else 1) if kind=='harmonic' else 0.);continue
                order=sorted((a for a in range(n) if np.isfinite(distance[a])),key=lambda a:(distance[a],a));sigma=np.zeros(n);sigma[source]=1.;predecessors=[[] for _ in range(n)]
                for a in order:
                    for b,w,_ in adjacent[a]:
                        spend()
                        if distance[a]+(w if weighted else 1)==distance[b]:sigma[b]+=sigma[a];predecessors[b].append(a)
                dependency=np.zeros(n)
                for b in reversed(order):
                    for a in predecessors[b]:spend();dependency[a]+=sigma[a]/sigma[b]*(1+dependency[b])
                    if b!=source:scores[b]+=dependency[b]
            if kind=='betweenness':
                if not directed:scores/=2
                if normalized and n>2:scores*= (1 if directed else 2)/((n-1)*(n-2))
        else:raise ValueError('Unknown centrality kind')
        if not np.all(np.isfinite(scores)):raise ValueError('Centrality exceeds finite target range')
        return SimpleNamespace(scores=scores[:,None],kind=kind,normalized=int(normalized),weighted=int(weighted),status='complete')
    raise ValueError('Unsupported graph target command')
)PENTA_GRAPH_PY"
