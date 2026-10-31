% Five-coefficient Pentarnion evolution; multiplication order is explicit.
use_algebra("A_gamma",gamma=1);clc;clear;close("all");
real pi=3.141592653589793;
matrix x=(0:31)/32;matrix y=(0:23)/24;
[matrix X,matrix Y]=meshgrid(x,y);
matrix initial=.1*sin(2*pi*X)+.2*cos(2*pi*Y)*i+.15*sin(2*pi*(X+Y))*j+.1*cos(2*pi*X)*k+.12*sin(2*pi*Y)*ell;
pentarnion A=1+i+j+ell;
native function pentarnion reaction(pentarnion p,real t,real x,real y,pentarnion A){return -.2*p+.05*(A*p-p*A);}
record field=pdesolve2(reaction,x,y,initial,options(parameters=options(A=A),integrator="imex_euler",boundary="periodic",length_x=1.0,length_y=1.0,diffusion=.01,t1=.5,time_step=.005,sample_step=.1,threads=4));
display("Field columns use x-fastest spatial order; all five coefficients retained.");
display("Native body cache / used workers:",field.cache_hit,field.threads_used);
figure(43,"Name","Pentarnion PDE: final coefficient fields","NumberTitle","off");
subplot(3,2,1);imagesc(x,y,pdeslice(field,size(field.t,1),0));title("Scalar component");
subplot(3,2,2);imagesc(x,y,pdeslice(field,size(field.t,1),1));title("$i$ component");
subplot(3,2,3);imagesc(x,y,pdeslice(field,size(field.t,1),2));title("$j$ component");
subplot(3,2,4);imagesc(x,y,pdeslice(field,size(field.t,1),3));title("$k$ component");
subplot(3,2,5);imagesc(x,y,pdeslice(field,size(field.t,1),4));title("$\\ell$ component");
