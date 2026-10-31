% A declared residual block length retains local dependence within sampled blocks.
use_algebra("A_gamma",gamma=1);clc;clear;
rng(2026);
matrix x=linspace(0,10,81);matrix noise=zeros(1,81);matrix shocks=.1*randn(1,81);
noise(1)=shocks(1);for(a=2:81){noise(a)=.7*noise(a-1)+shocks(a);}
matrix y=2+3*x+noise;
native function real line(real intercept,real slope,real x){return intercept+slope*x;}
record refits=fitbootstrap(line,[0,0],x,y,options(samples=64,sample_method="moving_block",block_length=8,seed=42,threads=4));
display("Circular moving-block residual bootstrap parameter spread:",refits.parameter_std);
display("Sampling convention / block length:",refits.sample_method,refits.block_length);
% Block choice and stationarity assumptions need domain-specific justification.
