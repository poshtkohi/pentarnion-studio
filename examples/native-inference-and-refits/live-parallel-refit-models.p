% Every iterate comes from native Ceres; only observation/plotting runs in Studio.
native function real line(real intercept,real slope,real x) { return intercept+slope*x; }
function integer fitted_model(matrix theta,record optimValues=iterationinfo(),string state="iter") {
    matrix grid=linspace(0,9,40);
    if(state=="init") { figure(1,"Name","Live native refits","NumberTitle","off"); }
    plot(grid,theta(1)+theta(2)*grid);
    xlabel("$x$"); ylabel("Fitted mean");
    display("Task, iteration, state: ",optimValues.start_index,optimValues.iteration,state);
    return 0;
}
matrix x=0:9; matrix y=2+3*x+.1*sin(x);
record fits=multistartfit(line,[-2,1;0,0;4,2;3,5],x,y,options(threads=4,OutputFcn=fitted_model,OutputEvery=1));
display("Best complete parameters: ",fits.best_parameters);
