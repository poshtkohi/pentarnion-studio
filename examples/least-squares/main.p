use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");

% Native model body is cached. Data, guesses, bounds, weights and solver
% settings are runtime inputs and do not change the shared-library cache key.
native function real line_model(real intercept, real slope, real x) {
    return intercept+slope*x;
}
vector xdata = 0:4;
vector ydata = [1.1,2.8,5.1,7.2,8.8];

tic;
record fitted = curvefit(line_model, [0,0], xdata, ydata,
    options(method="lm", jacobian="analytic", covariance=1));
toc;
assert(fitted.converged);
assert(norm(fitted.x-[1.04,1.98])<1e-7);
assert(fitted.covariance_valid);
display("Ceres fit: expected intercept 1.04 and slope 1.98:", fitted.x);
display("Weighted residual norm:", fitted.weighted_residual_norm);
display("Local covariance assumptions:", fitted.covariance_assumption);
display("Local covariance (does not establish parameter uniqueness):", fitted.covariance);

record changed_data = curvefit(line_model, [0,0], xdata, [2,5,8,11,14],
    options(method="dogleg"));
assert(changed_data.cache_hit);
assert(norm(changed_data.x-[2,3])<1e-7);
display("Changed observations reuse the model DLL/SO:", changed_data.cache_hit);

% Robust losses apply independently to each weighted residual coordinate.
native function real location_model(real mu, real x) {
    return mu;
}
record robust = curvefit(location_model, 0.0, xdata, [1,1,1,1,100],
    options(loss="huber", loss_scale=1.0));
assert(robust.converged);
assert(abs(robust.x-1.25)<1e-5);
display("Huber robust location (expected 1.25):", robust.x);

figure(9,"Name","Ceres: native model fitting","NumberTitle","off");
subplot(2,2,1);
plot(xdata,ydata);
title("Observed samples"); xlabel("$x$"); ylabel("$y$");
subplot(2,2,2);
vector dense_x = linspace(0,4,201);
vector dense_y = fitted.x(1)+fitted.x(2)*dense_x;
plot(dense_x,dense_y);
title("Fitted native model"); xlabel("$x$"); ylabel("$y_{fit}$");
subplot(2,2,3);
plot(fitted.xdata,fitted.residuals);
title("Residuals"); xlabel("$x$"); ylabel("$r$");
subplot(2,2,4);
plot(fitted.history(:,2),fitted.history(:,3));
title("Ceres cost history"); xlabel("Iteration"); ylabel("Cost");
