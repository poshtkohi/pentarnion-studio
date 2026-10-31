R"PENTA_DUAL(classdef penta_dual
% Forward real-coordinate AD for the explicit exported ode15s adapter.
% One value may contain a numerical vector/matrix; tangent rows use MATLAB
% column-major coordinates. This class is bundled, never installed globally.
properties
    value
    tangent
    second
end
methods
    function a=penta_dual(value,tangent,second)
        if nargin==0,value=0;tangent=zeros(1,0);end
        if nargin<3,second=[];end
        a.value=value;a.tangent=tangent;a.second=second;
        if size(tangent,1)~=numel(value),error('AD value/tangent shape mismatch');end
        if ~isempty(second)&&(size(second,1)~=numel(value)||size(second,2)~=size(tangent,2)||size(second,3)~=size(tangent,2)),error('AD Hessian shape mismatch');end
    end
    function c=plus(a,b),[a,b]=penta_dual.align(a,b);h=[];if ~isempty(a.second)||~isempty(b.second),h=penta_dual.hessian(a)+penta_dual.hessian(b);end;c=penta_dual(a.value+b.value,a.tangent+b.tangent,h);end
    function c=minus(a,b),c=plus(a,uminus(b));end
    function c=uminus(a),c=penta_dual(-a.value,-a.tangent,-a.second);end
    function c=uplus(a),c=a;end
    function c=times(a,b)
        [a,b]=penta_dual.align(a,b);h=[];if ~isempty(a.second)||~isempty(b.second),h=penta_dual.hessian(a).*b.value(:)+penta_dual.hessian(b).*a.value(:);for i=1:size(a.tangent,2),for j=1:size(a.tangent,2),h(:,i,j)=h(:,i,j)+a.tangent(:,i).*b.tangent(:,j)+b.tangent(:,i).*a.tangent(:,j);end;end;end
        c=penta_dual(a.value.*b.value,a.tangent.*b.value(:)+b.tangent.*a.value(:),h);
    end
    function c=rdivide(a,b),[a,b]=penta_dual.align(a,b);c=times(a,power(b,-1));end
    function c=mrdivide(a,b)
        if ~isa(b,'penta_dual')&&isscalar(b),c=rdivide(a,b);elseif isa(b,'penta_dual')&&isscalar(b.value),c=rdivide(a,b);else,error('AD equation division is unsupported');end
    end
    function c=mtimes(a,b)
        if ~isa(a,'penta_dual'),a=penta_dual(a,zeros(numel(a),size(b.tangent,2)));end
        if ~isa(b,'penta_dual'),b=penta_dual(b,zeros(numel(b),size(a.tangent,2)));end
        if isscalar(a.value)||isscalar(b.value),c=times(a,b);return;end
        value=a.value*b.value;t=zeros(numel(value),size(a.tangent,2));
        for k=1:size(t,2),d=reshape(a.tangent(:,k),size(a.value))*b.value+a.value*reshape(b.tangent(:,k),size(b.value));t(:,k)=d(:);end
        h=[];if ~isempty(a.second)||~isempty(b.second),ha=penta_dual.hessian(a);hb=penta_dual.hessian(b);directions=size(t,2);h=zeros(numel(value),directions,directions);for i=1:directions,for j=1:directions,d=reshape(ha(:,i,j),size(a.value))*b.value+a.value*reshape(hb(:,i,j),size(b.value))+reshape(a.tangent(:,i),size(a.value))*reshape(b.tangent(:,j),size(b.value))+reshape(a.tangent(:,j),size(a.value))*reshape(b.tangent(:,i),size(b.value));h(:,i,j)=d(:);end;end;end
        c=penta_dual(value,t,h);
    end
    function c=power(a,b)
        if isa(b,'penta_dual'),c=exp(times(b,log(a)));return;end
        value=a.value.^b;factor=zeros(size(value));curvature=zeros(size(value));if ~isscalar(b)||b~=0,factor=b.*a.value.^(b-1);end;if ~isscalar(b)||~ismember(b,[0,1]),curvature=b.*(b-1).*a.value.^(b-2);end;c=penta_dual.chain(a,value,factor,curvature);
    end
    function c=mpower(a,b),if ~isscalar(a.value),error('AD matrix power is unsupported');end;c=power(a,b);end
    function c=sin(a),c=penta_dual.chain(a,sin(a.value),cos(a.value),-sin(a.value));end
    function c=cos(a),c=penta_dual.chain(a,cos(a.value),-sin(a.value),-cos(a.value));end
    function c=exp(a),v=exp(a.value);c=penta_dual.chain(a,v,v,v);end
    function c=sinh(a),c=penta_dual.chain(a,sinh(a.value),cosh(a.value),sinh(a.value));end
    function c=cosh(a),c=penta_dual.chain(a,cosh(a.value),sinh(a.value),cosh(a.value));end
    function c=log(a),c=penta_dual.chain(a,log(a.value),1./a.value,-1./a.value.^2);end
    function c=sqrt(a),v=sqrt(a.value);c=penta_dual.chain(a,v,.5./v,-.25./a.value.^1.5);end
    function c=tan(a),v=tan(a.value);factor=1./cos(a.value).^2;c=penta_dual.chain(a,v,factor,2*v.*factor);end
    function c=tanh(a),v=tanh(a.value);factor=1./cosh(a.value).^2;c=penta_dual.chain(a,v,factor,-2*v.*factor);end
    function c=abs(a),if any(a.value(:)==0),error('abs is not differentiable at zero');end;c=penta_dual.chain(a,abs(a.value),sign(a.value),zeros(size(a.value)));end
    function c=floor(a),if any(a.value(:)==floor(a.value(:))),error('floor is not differentiable at an integer');end;c=penta_dual.chain(a,floor(a.value),zeros(size(a.value)),zeros(size(a.value)));end
    function c=ceil(a),if any(a.value(:)==floor(a.value(:))),error('ceil is not differentiable at an integer');end;c=penta_dual.chain(a,ceil(a.value),zeros(size(a.value)),zeros(size(a.value)));end
    function c=conj(a),c=penta_dual(conj(a.value),conj(a.tangent),conj(a.second));end
    function c=real(a),c=penta_dual(real(a.value),real(a.tangent),real(a.second));end
    function c=imag(a),c=penta_dual(imag(a.value),imag(a.tangent),imag(a.second));end
    function c=sum(a,varargin)
        if isempty(varargin),dim=find(size(a.value)~=1,1);if isempty(dim),dim=1;end;else,dim=varargin{1};end
        value=sum(a.value,dim);t=zeros(numel(value),size(a.tangent,2));for k=1:size(t,2),v=sum(reshape(a.tangent(:,k),size(a.value)),dim);t(:,k)=v(:);end;h=[];if ~isempty(a.second),h=zeros(numel(value),size(t,2),size(t,2));for i=1:size(t,2),for j=1:size(t,2),v=sum(reshape(a.second(:,i,j),size(a.value)),dim);h(:,i,j)=v(:);end;end;end;c=penta_dual(value,t,h);
    end
    function c=mean(a,varargin)
        if isempty(varargin),dim=find(size(a.value)~=1,1);if isempty(dim),dim=1;end;else,dim=varargin{1};end;c=sum(a,dim)./size(a.value,dim);
    end
    function c=diag(a,varargin)
        value=diag(a.value,varargin{:});directions=size(a.tangent,2);t=zeros(numel(value),directions);for i=1:directions,v=diag(reshape(a.tangent(:,i),size(a.value)),varargin{:});t(:,i)=v(:);end
        h=[];if ~isempty(a.second),h=zeros(numel(value),directions,directions);for i=1:directions,for j=1:directions,v=diag(reshape(a.second(:,i,j),size(a.value)),varargin{:});h(:,i,j)=v(:);end;end;end;c=penta_dual(value,t,h);
    end
    function c=norm(a,varargin)
        if ~isempty(varargin)&&~isequal(varargin{1},2)&&~strcmp(varargin{1},'fro'),error('Unsupported AD norm');end
        if ~isvector(a.value)&&(isempty(varargin)||~strcmp(varargin{1},'fro')),error('AD matrix norm requires fro');end
        value=norm(a.value);if value==0,error('Norm is not differentiable at zero; use norm2');end;c=sqrt(sum(times(conj(reshape(a,[],1)),reshape(a,[],1)),1));
    end
    function out=eq(a,b),[a,b]=penta_dual.align(a,b);out=a.value==b.value;end
    function out=ne(a,b),out=~eq(a,b);end
    function out=lt(a,b),[a,b]=penta_dual.align(a,b);out=a.value<b.value;end
    function out=le(a,b),[a,b]=penta_dual.align(a,b);out=a.value<=b.value;end
    function out=gt(a,b),out=lt(b,a);end
    function out=ge(a,b),out=le(b,a);end
    function out=logical(a),out=logical(a.value);end
    function c=reshape(a,varargin),c=penta_dual(reshape(a.value,varargin{:}),a.tangent,a.second);end
    function c=transpose(a),indices=reshape(1:numel(a.value),size(a.value)).';h=[];if ~isempty(a.second),h=a.second(indices(:),:,:);end;c=penta_dual(a.value.',a.tangent(indices(:),:),h);end
    function c=ctranspose(a),c=conj(transpose(a));end
    function c=horzcat(varargin),c=penta_dual.cat(2,varargin{:});end
    function c=vertcat(varargin),c=penta_dual.cat(1,varargin{:});end
    function out=subsref(a,s)
        if strcmp(s(1).type,'()'),indices=reshape(1:numel(a.value),size(a.value));selected=builtin('subsref',indices,s(1));h=[];if ~isempty(a.second),h=a.second(selected(:),:,:);end;out=penta_dual(builtin('subsref',a.value,s(1)),a.tangent(selected(:),:),h);if numel(s)>1,out=subsref(out,s(2:end));end
        else,out=builtin('subsref',a,s);end
    end
    function a=subsasgn(a,s,b)
        if strcmp(s(1).type,'()')
            if numel(s)~=1,error('Nested AD indexed assignment is unsupported');end
            indices=reshape(1:numel(a.value),size(a.value));selected=builtin('subsref',indices,s(1));if ~isa(b,'penta_dual'),b=penta_dual(b,zeros(numel(b),size(a.tangent,2)));end
            if isscalar(b.value)&&numel(selected)>1,h=[];if ~isempty(b.second),h=repmat(b.second,numel(selected),1,1);end;b=penta_dual(repmat(b.value,size(selected)),repmat(b.tangent,numel(selected),1),h);end
            if ~isempty(a.second)||~isempty(b.second),h=penta_dual.hessian(a);h(selected(:),:,:)=penta_dual.hessian(b);a.second=h;end
            a.value=builtin('subsasgn',a.value,s(1),b.value);a.tangent(selected(:),:)=b.tangent;
        else,a=builtin('subsasgn',a,s,b);end
    end
    function out=end(a,k,n),if n==1,out=numel(a.value);else,out=size(a.value,k);end;end
end
methods(Static)
    function h=hessian(a),h=a.second;if isempty(h),h=zeros(numel(a.value),size(a.tangent,2),size(a.tangent,2));end;end
    function c=chain(a,value,slope,curvature)
        h=[];if ~isempty(a.second),h=a.second.*slope(:);for i=1:size(a.tangent,2),for j=1:size(a.tangent,2),h(:,i,j)=h(:,i,j)+curvature(:).*a.tangent(:,i).*a.tangent(:,j);end;end;end;c=penta_dual(value,a.tangent.*slope(:),h);
    end
    function [a,b]=align(a,b)
        if ~isa(a,'penta_dual'),a=penta_dual(a,zeros(numel(a),size(b.tangent,2)));end
        if ~isa(b,'penta_dual'),b=penta_dual(b,zeros(numel(b),size(a.tangent,2)));end
        if size(a.tangent,2)~=size(b.tangent,2),error('AD direction mismatch');end
        if isscalar(a.value)&&~isscalar(b.value),h=[];if ~isempty(a.second),h=repmat(a.second,numel(b.value),1,1);end;a=penta_dual(repmat(a.value,size(b.value)),repmat(a.tangent,numel(b.value),1),h);elseif isscalar(b.value)&&~isscalar(a.value),h=[];if ~isempty(b.second),h=repmat(b.second,numel(a.value),1,1);end;b=penta_dual(repmat(b.value,size(a.value)),repmat(b.tangent,numel(a.value),1),h);elseif ~isequal(size(a.value),size(b.value)),error('AD array shape mismatch');end
    end
    function c=cat(dim,varargin)
        directions=0;for k=1:numel(varargin),if isa(varargin{k},'penta_dual'),directions=size(varargin{k}.tangent,2);break;end;end
        values=cell(size(varargin));tangents=cell(size(varargin));second=false;for k=1:numel(varargin),if isa(varargin{k},'penta_dual')&&~isempty(varargin{k}.second),second=true;end;end
        for k=1:numel(varargin),a=varargin{k};if ~isa(a,'penta_dual'),a=penta_dual(a,zeros(numel(a),directions));end;values{k}=a.value;tangents{k}=reshape(a.tangent,[size(a.value),directions]);end
        value=cat(dim,values{:});if directions,t=cat(dim,tangents{:});t=reshape(t,numel(value),directions);else,t=zeros(numel(value),0);end;h=[];if second,h=zeros(numel(value),directions,directions);for i=1:directions,for j=1:directions,parts=cell(size(varargin));for k=1:numel(varargin),a=varargin{k};if isa(a,'penta_dual'),ha=penta_dual.hessian(a);parts{k}=reshape(ha(:,i,j),size(a.value));else,parts{k}=zeros(size(a));end;end;v=cat(dim,parts{:});h(:,i,j)=v(:);end;end;end;c=penta_dual(value,t,h);
    end
end
end
)PENTA_DUAL"
