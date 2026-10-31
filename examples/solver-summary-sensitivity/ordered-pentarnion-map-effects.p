use_algebra("A_gamma",gamma=1);
clc;clear;
native function pentarnion ordered_step(pentarnion p,real alpha) {
    return p+alpha*(j*p);
}
% Initial values retain(1,i,j,k,ell). coefficient4 selects the real k output.
% Every native iteration still updates all five coefficients with j*p grouping.
record effects=modelsensitivity(@ordered_step,[0],[.5],
    options(kind="map",initial=[1,.2,-.3,.4,-.5],
        parameters=options(alpha=0.0),summary="final",coefficient=4,
        method="morris",samples=8,threads=4,solver=options(iterations=3)));
display("Ordered map k-coefficient effects: ",effects.mu_star);
display("All retained initial coefficients: ",effects.initial);
display("Declared state widths: ",effects.state_widths);
