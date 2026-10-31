R"PENTA_M(function out=penta_runtime(name,varargin)
% Portable checked-export adapter. Pentarnions are scalar structs with five
% coefficients; Pentarnion arrays are cell matrices, never complex numbers.
persistent gamma timer
if isempty(gamma),gamma=1;end
out=[];
switch name
case 'init', gamma=varargin{1};
case 'gamma',out=gamma;
case 'pent'
    a=varargin{1};if isstruct(a),out=a;else,out=struct('c',[a,0,0,0,0]);end
case 'coefficients',out=struct('c',[varargin{:}]);
case 'member',value=varargin{1};if isstruct(value)&&isfield(value,'penta_solution_format'),out=value.fields.(varargin{2});else,out=value.(varargin{2});end
case 'record',out=struct;names=varargin{1};values=varargin{2};for k=1:numel(names),out.(names{k})=values{k};end
case 'closure',out=typedclosure(varargin{:});
case 'invoke'
    fn=varargin{1};parameters=varargin{2};kinds=varargin{3};names=varargin{4};values=varargin{5};args=varargin{6};used=false(size(args));position=1;
    for k=1:numel(values),if isempty(names{k}),index=position;position=position+1;else,index=find(strcmp(parameters,names{k}),1);end;if isempty(index)||index>numel(args)||used(index),error('Duplicate/outside exported parameter');end;args{index}=values{k};used(index)=true;end
    for k=1:numel(args),args{k}=penta_runtime('cast',args{k},kinds{k});end;out=fn(args{:});
case 'pack'
    values=varargin{1};parts=cell(size(values));for k=1:numel(values),a=values{k};if isstruct(a),parts{k}=reshape(a.c,[],1);elseif iscell(a),parts{k}=penta_runtime('pack',reshape(a.',1,[]));else,parts{k}=reshape(a.',[],1);end;end;out=vertcat(parts{:});
case 'ode',out=odeadapter(varargin{:});
case 'ode_query',out=pentaodequery(varargin{:});
case 'masssolve',out=pentamasssolve(varargin{:});
case 'hybrid',out=pentahybrid(varargin{:});
case 'hybridval',out=pentahybridval(varargin{:});
case 'ode_adjoint',out=pentaodeadjoint(varargin{:});
case 'hybrid_adjoint',out=pentahybridadjoint(varargin{:});
case 'scientific',out=penta_scientific(varargin{:});
case 'graph',out=pentagraph(varargin{:});
case 'pde',out=pentapde(varargin{:});
case 'geometry',out=pentageometry(varargin{:});
case 'mechanics',out=pentamechanics(varargin{:});
case 'basis',c=zeros(1,5);c(varargin{1}+1)=1;out=struct('c',c);
case 'cast'
    a=varargin{1};kind=varargin{2};
    if strcmp(kind,'pentarnion'),out=penta_runtime('pent',a);
    elseif strcmp(kind,'pentarnionmatrix'),out=cell(size(a));for k=1:numel(a),out{k}=penta_runtime('pent',entry(a,k));end
    else,out=a;end
case 'mat'
    rows=varargin{1};if isempty(rows),out=zeros(0,0);return;end
    blocks=cell(size(rows));pent=false;
    for r=1:numel(rows),parts=rows{r};for c=1:numel(parts),pent=pent||isstruct(parts{c})||iscell(parts{c});end,end
    for r=1:numel(rows)
        parts=rows{r};if pent,for c=1:numel(parts),if ~iscell(parts{c}),if isstruct(parts{c}),parts{c}={parts{c}};else,parts{c}=num2cell(parts{c});end,end,end,end
        blocks{r}=[parts{:}];
    end
    out=vertcat(blocks{:});
case 'bin'
    op=varargin{1};a=varargin{2};b=varargin{3};am=varargin{4};bm=varargin{5};
    if strcmp(op,'==')||strcmp(op,'!='),if isa(a,'penta_dual'),a=a.value;end;if isa(b,'penta_dual'),b=b.value;end;out=isequal(a,b);if strcmp(op,'!='),out=~out;end;return;end
    if (isa(a,'penta_dual')||isa(b,'penta_dual'))&&~isstruct(a)&&~isstruct(b)&&~iscell(a)&&~iscell(b)
        switch op,case '+',out=a+b;case '-',out=a-b;case '*',if am&&bm,out=a*b;else,out=a.*b;end;case '.*',out=a.*b;case {'/','./'},out=a./b;case {'^','.^'},out=a.^b;case '<',out=a<b;case '<=',out=a<=b;case '>',out=a>b;case '>=',out=a>=b;otherwise,error('Unsupported analytic array operator');end;return
    end
    if strcmp(op,'*')&&am&&bm
        if size(a,2)~=size(b,1),error('Matrix dimensions do not match');end
        out=cell(size(a,1),size(b,2));pent=iscell(a)||iscell(b);
        for r=1:size(a,1),for c=1:size(b,2),v=0;for k=1:size(a,2),q=scalarbin('*',entry(a,r+(k-1)*size(a,1)),entry(b,k+(c-1)*size(b,1)),gamma);v=scalarbin('+',v,q,gamma);end;out{r,c}=v;end,end
        if ~pent,out=cell2mat(out);end;return
    end
    if am||bm
        if am&&bm&&~isequal(size(a),size(b)),error('Array dimensions must match');end
        if am,shape=size(a);else,shape=size(b);end
        out=cell(shape);pent=iscell(a)||iscell(b)||isstruct(a)||isstruct(b);
        for k=1:prod(shape),if am,x=entry(a,k);else,x=a;end;if bm,y=entry(b,k);else,y=b;end;out{k}=scalarbin(op,x,y,gamma);end
        if ~pent,out=cell2mat(out);end
    else,out=scalarbin(op,a,b,gamma);end
case 'unary'
    op=varargin{1};a=varargin{2};
    if strcmp(op,'-'),out=penta_runtime('bin','*',-1,a,false,~isstruct(a)&&numel(a)~=1);
    elseif strcmp(op,'+'),out=a;
    else,out=~a;end
case 'range',a=varargin{1};h=varargin{2};b=varargin{3};if h==0,error('Range step cannot be zero');end;out=a:h:b;
case 'end'
    a=varargin{1};if isa(a,'penta_dual'),a=a.value;end;if varargin{3}==1,out=numel(a);else,out=size(a,varargin{2}+1);end
case 'get'
    a=varargin{1};s=varargin{2};for k=1:numel(s),if ischar(s{k})&&strcmp(s{k},'all'),s{k}=':';end,end
    out=a(s{:});if iscell(out)&&numel(out)==1&&all(cellfun(@(x)isnumeric(x)&&isscalar(x),s)),out=out{1};end
case 'set'
    a=varargin{1};s=varargin{2};b=varargin{3};for k=1:numel(s),if ischar(s{k})&&strcmp(s{k},'all'),s{k}=':';end,end
    if iscell(a)&&~iscell(b),target=a(s{:});b=repmat({b},size(target));end;a(s{:})=b;out=a;
case 'transpose'
    a=varargin{1};conjugate=varargin{2};
    if isstruct(a),out=a;if conjugate,out.c(2:5)=-out.c(2:5);end
    elseif iscell(a),out=a.';if conjugate,for k=1:numel(out),x=penta_runtime('pent',out{k});x.c(2:5)=-x.c(2:5);out{k}=x;end,end
    elseif conjugate,out=a';else,out=a.';end
case 'truth',a=varargin{1};if ~isscalar(a),error('Condition requires one scalar');end;out=logical(a);
case 'call'
    f=varargin{1};args=varargin(2:end);if isempty(args),a=[];else,a=args{1};end
    switch f
    case 'display',for k=1:numel(args),disp(args{k});end
    case {'complex','real','imag'},out=feval(f,args{:});
    case {'linsolve','solveleft','solveright'},if numel(args)~=2,error('Portable solves use default numerical policy');end;out=orderedsolve(args{1},args{2},strcmp(f,'solveright'),gamma);
    case {'leftop','rightop'},out=orderedoperator(a,strcmp(f,'rightop'),gamma);
    case 'det',out=det(a);
    case 'inv',out=densesolve(a,eye(size(a,1)));
    case 'assert',assert(args{:});out=0;
    case 'coeff'
        index=args{2}+1;if iscell(a),parts=cell(size(a));for k=1:numel(a),v=penta_runtime('pent',a{k});parts{k}=v.c(index);end;out=realcells(parts);else,v=penta_runtime('pent',a);out=v.c(index);end
    case 'scalarpart',out=penta_runtime('call','coeff',a,0);
    case 'purepart',if iscell(a),out=cell(size(a));for k=1:numel(a),out{k}=penta_runtime('call','purepart',a{k});end;else,out=penta_runtime('pent',a);out.c(1)=0;end
    case 'norm2',if iscell(a),parts=cell(size(a));for k=1:numel(a),parts{k}=penta_runtime('call','norm2',a{k});end;out=realcells(parts);elseif ~isstruct(a)&&~isreal(a),out=abs(a)^2;else,a=penta_runtime('pent',a);out=sum(a.c.^2);end
    case 'commutator',out=scalarbin('-',scalarbin('*',args{1},args{2},gamma),scalarbin('*',args{2},args{1},gamma),gamma);
    case 'associator',out=scalarbin('-',scalarbin('*',scalarbin('*',args{1},args{2},gamma),args{3},gamma),scalarbin('*',args{1},scalarbin('*',args{2},args{3},gamma),gamma),gamma);
    case 'norm',if isstruct(a),out=norm(a.c);elseif iscell(a),out=norm(penta_runtime('pack',{a}),args{2:end});else,out=norm(args{:});end
    case 'conj',out=penta_runtime('transpose',penta_runtime('transpose',a,false),true);
    case 'tic',timer=tic;out=0;
    case 'toc',if isempty(timer),error('toc requires tic');end;out=toc(timer);
    case {'sin','cos','exp','sinh','cosh','sqrt','log','tan','tanh','abs','floor','ceil'}
        if iscell(a),out=cell(size(a));for k=1:numel(a),out{k}=one(f,a{k});end;else,out=one(f,a);end
    case 'find',out=find(a);out=out(:);
    case {'reshape','zeros','ones','eye','linspace','size','numel','length','logical','diag','sum','mean'}
        if iscell(a)&&any(strcmp(f,{'sum','mean'})),error('Pentarnion array reduction adapter is not supported');end
        if isa(a,'penta_dual')&&any(strcmp(f,{'size','numel','length'})),args{1}=a.value;end
        out=feval(f,args{:});
    case 'transpose',out=penta_runtime('transpose',a,false);
    otherwise,error(['Unsupported exported call ',f]);
    end
otherwise,error(['Unsupported export runtime operation ',name]);
end
end
function x=entry(a,k)
if iscell(a),x=a{k};else,x=a(k);end
end
function out=realcells(parts)
blocks=cell(size(parts,1),1);for row=1:size(parts,1),cells=parts(row,:);blocks{row}=[cells{:}];end;out=vertcat(blocks{:});
end
function out=scalarbin(op,a,b,gamma)
if any(strcmp(op,{'.*','./','.^'})),op=op(2:end);end
if ~isstruct(a)&&~isstruct(b)
    switch op
    case '+',out=a+b;case '-',out=a-b;case '*',out=a*b;case '/',out=a/b;case '^',out=a^b;
    case '<',out=a<b;case '<=',out=a<=b;case '>',out=a>b;case '>=',out=a>=b;case '&',out=logical(a)&&logical(b);case '|',out=logical(a)||logical(b);
    otherwise,error(['Unsupported operator ',op]);end;return
end
if strcmp(op,'/')&&~isstruct(b),out=a;out.c=a.c/b;return;end
a=penta_runtime('pent',a);b=penta_runtime('pent',b);
switch op
case '+',out=struct('c',a.c+b.c);
case '-',out=struct('c',a.c-b.c);
case '*'
    k=1/sqrt(1+gamma^2);s=gamma*k;h=1/sqrt(2);T=zeros(5,5,5);
    for n=1:5,T(1,n,n)=1;T(n,1,n)=1;end;for n=2:5,T(n,n,1)=-1;end
    T(2,3,:)=[0,0,0,k,s];T(2,4,:)=[0,0,-k,0,-s];T(2,5,:)=[0,0,-h,-h,0];
    T(3,2,:)=[0,0,0,-k,-s];T(3,4,:)=[0,k,0,0,s];T(3,5,:)=[0,-h,0,-h,0];
    T(4,2,:)=[0,0,k,0,s];T(4,3,:)=[0,-k,0,0,-s];T(4,5,:)=[0,-h,-h,0,0];
    T(5,2,:)=[0,0,h,h,0];T(5,3,:)=[0,h,0,h,0];T(5,4,:)=[0,h,h,0,0];
    c=zeros(1,5);for i=1:5,for j=1:5,c=c+a.c(i)*b.c(j)*reshape(T(i,j,:),1,5);end,end;out=struct('c',c);
case '^'
    if any(b.c(2:5))||b.c(1)<0||b.c(1)~=fix(b.c(1)),error('Power requires nonnegative integer');end
    out=penta_runtime('pent',1);for n=1:b.c(1),out=scalarbin('*',out,a,gamma);end
otherwise,error('Unsupported Pentarnion operator');
end
end
function out=one(f,a)
if ~isstruct(a),out=feval(f,a);return;end
x=a.c(1);r2=sum(a.c(2:5).^2);if isa(r2,'penta_dual')&&r2.value==0,r=0;else,r=sqrt(r2);end
if (isa(r,'penta_dual')&&r.value==0)||(~isa(r,'penta_dual')&&r==0),sinc=1-r2/6+r2*r2/120;sinhc=1+r2/6+r2*r2/120;circular=1-r2/2+r2*r2/24;hyperbolic=1+r2/2+r2*r2/24;else,sinc=sin(r)/r;sinhc=sinh(r)/r;circular=cos(r);hyperbolic=cosh(r);end
switch f
case 'exp',v=exp(x)*circular;factor=exp(x)*sinc;
case 'sin',v=sin(x)*hyperbolic;factor=cos(x)*sinhc;
case 'cos',v=cos(x)*hyperbolic;factor=-sin(x)*sinhc;
case 'sinh',v=sinh(x)*circular;factor=cosh(x)*sinc;
case 'cosh',v=cosh(x)*circular;factor=sinh(x)*sinc;
otherwise,error('Unsupported Pentarnion elementary function');end
out=struct('c',[v,factor*a.c(2:5)]);
end
function out=odeadapter(fn,names,widths,initial,parameters,settings)
out=pentaodetarget(fn,names,widths,initial,parameters,settings);
end
function value=option(settings,name,fallback),if isfield(settings,name),value=settings.(name);else,value=fallback;end;end
function out=densesolve(a,b)
if ~ismatrix(a)||size(a,1)~=size(b,1),error('Equation dimensions do not agree');end
if any(~isfinite(a(:)))||any(~isfinite(b(:))),error('Equation inputs must be finite');end
scale=max(abs(a(:)));if isempty(scale)||scale==0,error('No accepted unique linear solution');end
normalized=a/scale;rhs=b/scale;if any(~isfinite(rhs(:))),error('Scaled equation exceeds finite target range');end
[U,S,V]=svd(normalized,'econ');s=diag(S);if numel(s)<size(a,2)||isempty(s)||s(end)<=1e-12*s(1),error('No reliable unique linear solution');end
out=V*((U'*rhs)./s);if any(~isfinite(out(:))),error('Nonfinite equation solution');end
common=max([1;abs(out(:));abs(rhs(:))]);denominator=norm(normalized,inf)*norm(out/common,inf)+norm(rhs/common,inf);if norm(normalized*(out/common)-rhs/common,inf)>1e-10*max(denominator,realmin),error('Equation residual failed');end
end
function out=orderedoperator(a,right,gamma)
if isstruct(a),a={a};elseif ~iscell(a),a=num2cell(a);end
[rows,columns]=size(a);if right,out=zeros(5*columns,5*rows);else,out=zeros(5*rows,5*columns);end
for r=1:rows,for c=1:columns,for k=1:5,basis=penta_runtime('basis',k-1);if right,v=scalarbin('*',basis,a{r,c},gamma);out(5*(c-1)+(1:5),5*(r-1)+k)=v.c(:);else,v=scalarbin('*',a{r,c},basis,gamma);out(5*(r-1)+(1:5),5*(c-1)+k)=v.c(:);end;end;end;end
end
function out=orderedsolve(a,b,right,gamma)
scalar=~iscell(a)&&~iscell(b)&&numel(a)==1&&numel(b)==1;pent=isstruct(a)||isstruct(b)||iscell(a)||iscell(b);
if ~pent,if right,out=densesolve(a.',b.').';else,out=densesolve(a,b);end;return;end
if isstruct(a),a={a};elseif ~iscell(a),a=num2cell(a);end;if isstruct(b),b={b};elseif ~iscell(b),b=num2cell(b);end
if right,packed=zeros(5*size(b,2),size(b,1));else,packed=zeros(5*size(b,1),size(b,2));end
for r=1:size(b,1),for c=1:size(b,2),v=penta_runtime('pent',b{r,c});if right,packed(5*(c-1)+(1:5),r)=v.c(:);else,packed(5*(r-1)+(1:5),c)=v.c(:);end;end;end
solved=densesolve(orderedoperator(a,right,gamma),packed);
if right,out=cell(size(b,1),size(a,1));else,out=cell(size(a,2),size(b,2));end
for r=1:size(out,1),for c=1:size(out,2),if right,coefficients=solved(5*(c-1)+(1:5),r).';else,coefficients=solved(5*(r-1)+(1:5),c).';end;out{r,c}=struct('c',coefficients);end;end
if scalar,out=out{1};end
end
function out=typedclosure(fn,kinds,captures)
out=@invoke;
    function result=invoke(varargin)
        if numel(varargin)~=numel(kinds),error('Anonymous callback arity differs from its checked signature');end
        args=varargin;for k=1:numel(args),args{k}=penta_runtime('cast',args{k},kinds{k});end;result=fn(args{:},captures{:});
    end
end
)PENTA_M"
