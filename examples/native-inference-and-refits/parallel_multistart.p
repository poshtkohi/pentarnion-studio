% Independent Ceres fits use outer workers; each inner fit receives one CPU slot.
use_algebra("A_gamma",gamma=1);clc;clear;
rng(42);
matrix x=linspace(0,10,41);
matrix y=2+3*x+.1*randn(1,41);
native function real line(real intercept,real slope,real x){return intercept+slope*x;}
record fits=multistartfit(line,[-5,1;0,0;1,4;5,2],x,y,options(threads=4,max_iterations=100));
display("Task status / costs / parameter coordinates:",fits.task_status,fits.costs,fits.parameters);
display("Best task and parameter coordinates:",fits.best_task,fits.best_parameters);
display("Requested / granted / actually used / inner threads:",fits.threads_requested,fits.threads_granted,fits.threads_used,fits.inner_threads);
