% Small interactive desktop demo; no solver or native compilation is needed.
use_algebra("A_gamma", gamma=1);
clc;
clear;
close("all");
pentarnion p = 1 + i + j + ell;
pentarnion q = 1 - i + j - ell;
pentarnion p_plus_q = p + q;
pentarnion p_times_q = p*q;
display("p + q =", p_plus_q);
display("p*q =", p_times_q);
figure(1, "Name", "Addition in the plane", "NumberTitle", "off");
plot2("i", "j", p, q, p_plus_q);
title("Addition in the $(i,j)$ plane");
xlabel("$i$"); ylabel("$j$");
figure(2, "Name", "Ordered product in space", "NumberTitle", "off");
plot3("i", "j", "k", p, q, p_times_q);
title("The ordered product $pq$");
xlabel("$i$"); ylabel("$j$"); zlabel("$k$");
% Use Arrange > Split right/Tab with, or Tile in Studio in the figure toolbar.
