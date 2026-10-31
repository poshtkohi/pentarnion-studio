clc;
clear;
close("all");
algebra H = builtin_algebra("quaternion");

ode paired_rotations(t,matrix<H,1,2> states;real speed) {
    states' = speed*(states*H.k);
}

solution result = odesolve(paired_rotations,states=[H.one,H.i],speed=1.0,
    t0=0,t1=6.283185307179586,sample_step=0.025,
    method="bdf",jacobian="analytic",rtol=1e-10,atol=1e-12);
matrix<H,1,2> final = result.states(length(result.t));
display("Both complete quaternion states at the final time:",final);

figure(1,"Name","Two quaternion states in one native array","NumberTitle","off");
subplot(2,1,1);
plot(result.t,statecoord(result.states,0));hold("on");
plot(result.t,statecoord(result.states,3));hold("off");
title("First quaternion: scalar and $k$ coordinates");xlabel("$t$");
subplot(2,1,2);
plot(result.t,statecoord(result.states,5));hold("on");
plot(result.t,statecoord(result.states,6));hold("off");
title("Second quaternion: $i$ and $j$ coordinates");xlabel("$t$");
