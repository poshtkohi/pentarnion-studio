R"PENTA_GEOM_M(
function out=pentageometry(name,varargin)
if strcmp(name,'curvegeometry')
    points=varargin{1};n=size(points,1);if n<3||~ismember(size(points,2),[2,3])||any(~isfinite(points(:))),error('Curve needs at least3 finite2D/3D points');end
    delta=sqrt(sum(diff(points).^2,2));if any(delta==0),error('Consecutive duplicate curve points');end
    arc=[0;cumsum(delta)];first=max((1:n)-1,1);last=min((1:n)+1,n);tangent=(points(last,:)-points(first,:))./(arc(last)-arc(first));speed=sqrt(sum(tangent.^2,2));if any(speed==0),error('Curve tangent undefined at reversing cusp');end
    tangent=tangent./speed;normal=(tangent(last,:)-tangent(first,:))./(arc(last)-arc(first));curvature=sqrt(sum(normal.^2,2));positive=curvature>0;normal(positive,:)=normal(positive,:)./curvature(positive);normal(~positive,:)=0;
    out=struct('arc',arc,'length',arc(end),'tangent',tangent,'normal',normal,'curvature',curvature,'status','sampled_estimate');return
end
x=varargin{1};y=varargin{2};x=x(:);y=y(:);volume=strcmp(name,'isosurface');axesValues={x,y};if volume,z=varargin{3};z=z(:);axesValues{3}=z;values=varargin{4};else,values=varargin{3};end
for k=1:numel(axesValues),axisValue=axesValues{k};if numel(axisValue)<2||any(~isfinite(axisValue))||any(diff(axisValue)<=0),error('Geometry axes must be finite and strictly increasing');end;end
total=numel(x)*numel(y);if volume,total=total*numel(z);end;if total>1000000||numel(values)~=total||any(~isfinite(values(:)))||(~volume&&~isequal(size(values),[numel(y),numel(x)])),error('Geometry shape or1million sample bound exceeded');end
[X,Y]=meshgrid(x,y);out=0;
switch name
case 'surf',surf(X,Y,values);
case 'imagesc',surf(X,Y,zeros(size(values)),values,'EdgeColor','none');view(2);
case 'contour'
    if numel(varargin)>3,levels=varargin{4};else,levels=linspace(min(values(:)),max(values(:)),10);levels=levels(2:end-1);end
    levels=unique(levels(:));if isempty(levels)||numel(levels)>32||any(~isfinite(levels)),error('Contour needs1..32 finite levels');end;if isscalar(levels),levels=[levels,levels];end;contour(X,Y,values,levels);
case 'isosurface'
    level=varargin{5};if ~isscalar(level)||~isfinite(level),error('Isosurface level must be finite');end
    data=permute(reshape(reshape(values.',[],1),numel(x),numel(y),numel(z)),[2,1,3]);[X,Y,Z]=meshgrid(x,y,z);[faces,vertices]=isosurface(X,Y,Z,data,level);if size(faces,1)>333333,error('Isosurface portable triangle allowance exceeded');end;patch('Faces',faces,'Vertices',vertices,'FaceColor',[0,.447,.741],'EdgeColor','none');view(3);axis tight;
otherwise,error('Unsupported geometry target');
end
end
)PENTA_GEOM_M"
