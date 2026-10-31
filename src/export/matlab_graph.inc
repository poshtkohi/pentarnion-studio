R"PENTA_GRAPH_M(
function out=pentagraph(name,varargin)
if any(strcmp(name,{'graph','digraph'}))
    options=struct;if numel(varargin)>1,options=varargin{2};end;checkoptions(options,{'n','weights','multigraph','labels'});
    edges=zeros(0,2);if ~isempty(varargin),edges=varargin{1};end;if isempty(edges),edges=zeros(0,2);end
    if size(edges,2)~=2||any(~isfinite(edges(:)))||any(edges(:)~=floor(edges(:)))||any(edges(:)<1),error('Graph endpoints need two positive integral columns');end
    inferred=max([0;edges(:)]);n=opt(options,'n',inferred);m=size(edges,1);multiple=opt(options,'multigraph',0);directed=strcmp(name,'digraph');
    if n~=floor(n)||n<inferred||n>100000||n<0||m>500000||~ismember(multiple,[0,1]),error('Graph size/multigraph bounds exceeded');end
    weights=opt(options,'weights',1);weights=weights(:);if isscalar(weights),weights=repmat(weights,m,1);end;if numel(weights)~=m||any(~isfinite(weights)),error('Graph weights need one finite real per edge');end
    keys=edges;if ~directed,keys=sort(keys,2);end;if ~multiple&&size(unique(keys,'rows'),1)~=m,error('Duplicate graph edges require multigraph=1');end
    labels=opt(options,'labels',struct);fields=fieldnames(labels);for k=1:numel(fields),key=fields{k};id=sscanf(key,'n%d');if isempty(regexp(key,'^n[1-9][0-9]*$','once'))||numel(id)~=1||id>n||~ischar(labels.(key)),error('Graph labels need strings keyed n1,n2,...');end;end
    out=struct('format','PentarnionGraph-v1','nodes',n,'edge_count',m,'directed',double(directed),'multigraph',multiple,'edges',edges,'weights',weights,'labels',labels);return
end
if isempty(varargin)||~isstruct(varargin{1})||~isequal(sort(fieldnames(varargin{1})),sort({'format';'nodes';'edge_count';'directed';'multigraph';'edges';'weights';'labels'}))||~strcmp(varargin{1}.format,'PentarnionGraph-v1'),error('Expected checked graph record');end
original=varargin{1};if ~ismember(original.directed,[0,1])||original.edge_count~=size(original.edges,1),error('Graph record has inconsistent flags/edge_count');end;kind='graph';if original.directed,kind='digraph';end;G=pentagraph(kind,original.edges,struct('n',original.nodes,'weights',original.weights,'multigraph',original.multigraph,'labels',original.labels));n=G.nodes;m=G.edge_count;edges=G.edges;weights=G.weights;directed=logical(G.directed);work=0;
adjacent=cell(n,1);indegree=zeros(n,1);outdegree=indegree;strength=indegree;
for i=1:m,a=edges(i,1);b=edges(i,2);adjacent{a}(end+1,:)=[b,weights(i),i];outdegree(a)=outdegree(a)+1;indegree(b)=indegree(b)+1;strength(a)=strength(a)+weights(i);strength(b)=strength(b)+weights(i);if ~directed,outdegree(b)=outdegree(b)+1;indegree(a)=indegree(a)+1;if a~=b,adjacent{b}(end+1,:)=[a,weights(i),i];end;end;end
if any(~isfinite(strength))||~isfinite(sum(weights)),error('Graph sums exceed finite target range');end
switch name
case 'graphinfo'
    settings(1,{});[~,count]=components(false);degree=outdegree;if directed,degree=degree+indegree;end
    out=struct('nodes',n,'edge_count',m,'directed',double(directed),'multigraph',G.multigraph,'self_loops',sum(edges(:,1)==edges(:,2)),'negative_edges',sum(weights<0),'component_count',count,'total_weight',sum(weights),'degree',degree,'in_degree',indegree,'out_degree',outdegree,'strength',strength,'threads_used',1,'work_units',work,'status','complete');
case {'components','graphcomponents'}
    options=settings(1,{'kind'});kind=opt(options,'kind','weak');if ~any(strcmp(kind,{'weak','strong'})),error('Unknown component kind');end;[labels,count]=components(strcmp(kind,'strong'));out=struct('labels',labels,'count',count,'kind',kind,'status','complete');
case {'graphadjacency','graphlaplacian'}
    settings(1,{});if n*n>1000000,error('Portable graph dense matrix exceeds1million entries');end
    out=zeros(n);for i=1:m,spend(1);a=edges(i,1);b=edges(i,2);out(a,b)=out(a,b)+weights(i);if ~directed&&a~=b,out(b,a)=out(b,a)+weights(i);end;end
    if strcmp(name,'graphlaplacian'),out(1:n+1:end)=0;out=diag(sum(out,2))-out;end
case 'shortestpath'
    options=settings(3,{'method'});method=opt(options,'method','auto');source=varargin{2};target=varargin{3};if source~=floor(source)||target~=floor(target)||source<1||source>n||target<1||target>n||~any(strcmp(method,{'auto','dijkstra','bellman_ford'})),error('Invalid path endpoints/method');end
    bellman=strcmp(method,'bellman_ford')||(strcmp(method,'auto')&&any(weights<0));if ~bellman&&any(weights<0),error('Dijkstra requires nonnegative weights');end
    [distance,previous,identifiers,negative]=paths(source,bellman,true);reachable=~negative&&isfinite(distance(target));nodes=[];used=[];
    if reachable,cursor=target;while cursor~=source,if numel(nodes)>=n,error('Predecessor cycle');end;nodes(end+1)=cursor;used(end+1)=identifiers(cursor);cursor=previous(cursor);end;nodes=fliplr([nodes,source]);used=fliplr(used);end
    value=0;label='unreachable';if negative,label='negative_cycle';elseif reachable,label='complete';value=distance(target);end;method='dijkstra';if bellman,method='bellman_ford';end
    out=struct('path',reshape(nodes,1,[]),'edge_ids',reshape(used,1,[]),'distance',value,'reachable',double(reachable),'distance_available',double(reachable),'negative_cycle',double(negative),'status',label,'method',method);
case 'spanningtree'
    settings(1,{});if directed,error('Spanning tree requires an undirected graph');end
    parent=1:n;count=n;used=[];[~,order]=sortrows([weights,(1:m).'],[1,2]);for i=reshape(order,1,[]),spend(1);a=root(edges(i,1));b=root(edges(i,2));if a~=b,parent(b)=a;count=count-1;used(end+1)=i;end;end
    label='forest';if count==1,label='complete';end;out=struct('edge_ids',reshape(used,1,[]),'edges',edges(used,:),'weights',weights(used),'total_weight',sum(weights(used)),'component_count',count,'spanning',double(count==1),'status',label);
case 'toposort'
    settings(1,{});if ~directed,error('Topological sort requires a directed graph');end
    incoming=indegree;order=[];available=find(incoming==0).';while ~isempty(available),a=min(available);available(available==a)=[];order(end+1)=a;for row=adjacent{a}.',spend(1);b=row(1);incoming(b)=incoming(b)-1;if incoming(b)==0,available(end+1)=b;end;end;end
    valid=numel(order)==n;label='complete';if ~valid,order=[];label='cycle';end;out=struct('order',reshape(order,1,[]),'acyclic',double(valid),'status',label);
case 'graphcycle'
    settings(1,{});color=zeros(n,1);predecessor=zeros(n,1);incoming=zeros(n,1);nodes=[];used=[];
    for start=1:n
        if color(start),continue;end;color(start)=1;stack=[start,1];
        while ~isempty(stack)
            a=stack(end,1);index=stack(end,2);if index>size(adjacent{a},1),color(a)=2;stack(end,:)=[];continue;end
            stack(end,2)=index+1;b=adjacent{a}(index,1);id=adjacent{a}(index,3);spend(1);if ~directed&&id==incoming(a),continue;end
            if color(b)==0,predecessor(b)=a;incoming(b)=id;color(b)=1;stack(end+1,:)=[b,1];elseif color(b)==1
                nodes=a;used=id;cursor=a;while cursor~=b,used(end+1)=incoming(cursor);cursor=predecessor(cursor);nodes(end+1)=cursor;end;nodes=[fliplr(nodes),b];used=[fliplr(used(2:end)),used(1)];break
            end
        end
        if ~isempty(nodes),break;end
    end
    out=struct('nodes',reshape(nodes,1,[]),'edge_ids',reshape(used,1,[]),'found',double(~isempty(nodes)),'status','complete');
case {'graphlayout','plotgraph'}
    options=struct;if strcmp(name,'graphlayout'),options=settings(1,{'dimensions','radius'});end;dimensions=opt(options,'dimensions',2);radius=opt(options,'radius',1);if ~ismember(dimensions,[2,3])||~isfinite(radius)||radius<=0,error('Layout needs2/3 dimensions and positive radius');end
    theta=(0:n-1).'*2*pi/max(1,n);positions=zeros(n,dimensions);positions(:,1:2)=radius*[cos(theta),sin(theta)];if n==1,positions(:)=0;end
    if strcmp(name,'graphlayout'),out=positions;return;end;if numel(varargin)>2,error('Use plotgraph(G[,positions])');end;if numel(varargin)==2,positions=varargin{2};end
    if size(positions,1)~=n||~ismember(size(positions,2),[2,3])||any(~isfinite(positions(:)))||n==0,error('Invalid graph positions');end
    dimensions=size(positions,2);held=ishold;hold on;if dimensions==2,plot(positions(:,1),positions(:,2),'o');else,plot3(positions(:,1),positions(:,2),positions(:,3),'o');end
    for i=1:m,a=positions(edges(i,1),:);b=positions(edges(i,2),:);if dimensions==2,plot([a(1),b(1)],[a(2),b(2)],'b-');if directed&&edges(i,1)~=edges(i,2),quiver(a(1),a(2),b(1)-a(1),b(2)-a(2),0);end;else,plot3([a(1),b(1)],[a(2),b(2)],[a(3),b(3)],'b-');if directed&&edges(i,1)~=edges(i,2),quiver3(a(1),a(2),a(3),b(1)-a(1),b(2)-a(2),b(3)-a(3),0);end;end;end
    for i=1:n,label=num2str(i);key=['n',num2str(i)];if isfield(G.labels,key),label=G.labels.(key);end;if dimensions==2,text(positions(i,1),positions(i,2),label);else,text(positions(i,1),positions(i,2),positions(i,3),label);end;end;if ~held,hold off;end;out=0;
case 'centrality'
    options=settings(1,{'kind','normalized','weighted'});kind=opt(options,'kind','degree');normalized=opt(options,'normalized',0);weighted=opt(options,'weighted',1);if ~ismember(normalized,[0,1])||~ismember(weighted,[0,1]),error('Centrality flags need0/1');end
    if any(strcmp(kind,{'degree','in_degree','out_degree','strength'}))
        switch kind,case 'degree',scores=outdegree;if directed,scores=scores+indegree;end;case 'in_degree',scores=indegree;case 'out_degree',scores=outdegree;otherwise,scores=strength;end;if normalized&&n>1,scores=scores/(n-1);end
    elseif any(strcmp(kind,{'closeness','harmonic','betweenness'}))
        if weighted&&any(weights<=0),error('Weighted path centralities need strictly positive weights');end;scores=zeros(n,1);
        for source=1:n
            distance=paths(source,false,weighted);if ~strcmp(kind,'betweenness'),d=distance(isfinite(distance)&(1:n).'~=source);if strcmp(kind,'closeness'),if ~isempty(d),scores(source)=numel(d)/sum(d);if normalized&&n>1,scores(source)=scores(source)*numel(d)/(n-1);end;end;else,scores(source)=sum(1./d);if normalized&&n>1,scores(source)=scores(source)/(n-1);end;end;continue;end
            [~,order]=sortrows([distance,(1:n).'],[1,2]);order=order(isfinite(distance(order)));sigma=zeros(n,1);sigma(source)=1;predecessors=cell(n,1);
            for a=reshape(order,1,[]),for row=adjacent{a}.',spend(1);b=row(1);w=1;if weighted,w=row(2);end;if distance(a)+w==distance(b),sigma(b)=sigma(b)+sigma(a);predecessors{b}(end+1)=a;end;end;end
            dependency=zeros(n,1);for b=reshape(flipud(order),1,[]),for a=predecessors{b},spend(1);dependency(a)=dependency(a)+sigma(a)/sigma(b)*(1+dependency(b));end;if b~=source,scores(b)=scores(b)+dependency(b);end;end
        end
        if strcmp(kind,'betweenness'),if ~directed,scores=scores/2;end;if normalized&&n>2,factor=1;if ~directed,factor=2;end;scores=scores*factor/((n-1)*(n-2));end;end
    else,error('Unknown centrality kind');end
    if any(~isfinite(scores)),error('Centrality exceeds finite target range');end;out=struct('scores',scores,'kind',kind,'normalized',normalized,'weighted',weighted,'status','complete');
otherwise,error('Unsupported graph target command');
end
    function spend(count),work=work+count;if work>20000000,error('Portable graph work allowance20million exceeded');end;end
    function options=settings(required,allowed),if numel(varargin)~=required&&numel(varargin)~=required+1,error('Incorrect graph argument count');end;options=struct;if numel(varargin)>required,options=varargin{end};end;checkoptions(options,allowed);end
    function [labels,count]=components(strong)
        spend(n+2*m);if n==0,labels=zeros(0,1);count=0;return;end
        if directed,graphValue=digraph(edges(:,1),edges(:,2),[],n);connection='weak';if strong,connection='strong';end;raw=conncomp(graphValue,'Type',connection);else,graphValue=graph(edges(:,1),edges(:,2),[],n);raw=conncomp(graphValue);end
        mapping=zeros(n,1);labels=zeros(n,1);count=0;for vertex=1:n,if mapping(raw(vertex))==0,count=count+1;mapping(raw(vertex))=count;end;labels(vertex)=mapping(raw(vertex));end
    end
    function [distance,previous,identifiers,negative]=paths(source,bellman,weighted)
        distance=inf(n,1);previous=zeros(n,1);identifiers=zeros(n,1);distance(source)=0;previous(source)=source;negative=false;
        if bellman
            for pass=1:n-1,changed=false;for a=1:n,if isfinite(distance(a)),for row=adjacent{a}.',spend(1);changed=relax(a,row(1),row(2),row(3))||changed;end;end;end;if ~changed,break;end;end
            for a=1:n,if isfinite(distance(a)),for row=adjacent{a}.',spend(1);if distance(a)+row(2)<distance(row(1)),negative=true;return;end;end;end;end
        else
            settled=false(n,1);for pass=1:n,spend(n);candidates=distance;candidates(settled)=inf;[d,a]=min(candidates);if ~isfinite(d),break;end;settled(a)=true;for row=adjacent{a}.',spend(1);relax(a,row(1),row(2),row(3));end;end
        end
        function changed=relax(a,b,w,id)
            if ~weighted,w=1;end;candidate=distance(a)+w;if ~isfinite(candidate),error('Path cost exceeds finite binary64 range');end;changed=candidate<distance(b);if changed,distance(b)=candidate;previous(b)=a;identifiers(b)=id;end
        end
    end
    function value=root(value),while parent(value)~=value,parent(value)=parent(parent(value));value=parent(value);end;end
end
)PENTA_GRAPH_M"
