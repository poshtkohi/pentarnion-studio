% Scalar fields, implicit geometry and sampled real curve geometry.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
matrix x=linspace(-2,2,41);matrix y=linspace(-2,2,41);
[matrix X,matrix Y]=meshgrid(x,y);
matrix Z=X.*X+Y.*Y;
figure(15,"Name","Real scalar geometry","NumberTitle","off");
subplot(2,2,1);imagesc(x,y,Z);title("Scalar field");xlabel("x");ylabel("y");
subplot(2,2,2);surf(x,y,Z);title("Paraboloid surface");xlabel("x");ylabel("y");zlabel("z");
subplot(2,2,3);contour(x,y,Z,[.5,1,2,3]);title("Level sets");xlabel("x");ylabel("y");
matrix a=linspace(-1.5,1.5,17);matrix volume=zeros(289,17);
for(iz=1:17){for(iy=1:17){for(ix=1:17){volume((iz-1)*17+iy,ix)=a(ix)^2+a(iy)^2+a(iz)^2;}}}
subplot(2,2,4);isosurface(a,a,a,volume,1.0);title("Implicit unit sphere");xlabel("x");ylabel("y");zlabel("z");
matrix t=linspace(0,6.283185307179586,101);matrix circle=[cos(t)',sin(t)'];
record curve=curvegeometry(circle);display("Sampled circle length and curvature:",curve.length,curve.curvature(50));
