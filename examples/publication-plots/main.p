% A reusable plotting style, held curves and linked subplot axes.
clc;
clear;
close("all");
record publication = options(colors=[0.12,0.36,0.78;0.88,0.30,0.08],
    line_width=2.5, line_style="solid", font_size=12.0,
    font_family="serif", legend_location="northwest");
matrix t = linspace(0, 4*acos(-1), 401);
matrix observed = sin(t);
matrix fitted = 0.96*sin(t+0.05);

figure(1, "Name", "Publication curves and residuals", "NumberTitle", "off");
subplot(2,1,1);
plotstyle(publication);
plot(t, observed);
hold("on");
plot(t, fitted);
hold("off");
legend("Observed model", "Fitted model", options(location="northwest", box=1));
title("Two curves with original samples retained");
xlabel("$t$"); ylabel("Signal");
ylim([-1.2,1.2]);

subplot(2,1,2);
plotstyle(publication);
plot(t, observed-fitted);
legend("Residual");
title("Fit residual"); xlabel("$t$"); ylabel("Observed minus fitted");
linkaxes([1,2], "x");
% Zoom or pan either plot: the linked time axis follows in the other plot.

% Export is explicit. These deterministic filenames are intentionally replaced
% on rerun only because overwrite=1 is supplied here.
exportgraphics("publication-curves.png", options(width_cm=16.0,height_cm=12.0,dpi=150,overwrite=1));
% In Studio, vector formats also preserve mathematical labels:
% exportgraphics("publication-curves.pdf", options(width_cm=16.0,height_cm=12.0,overwrite=1));
% exportgraphics("residual-only.svg", options(subplot=2,width_cm=16.0,height_cm=6.0,overwrite=1));
