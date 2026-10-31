use_algebra("A_gamma", gamma=1);
clc;
clear;
pentarnion p = 1+i+j+ell;
pentarnion q = 1-i+j-ell;
pentarnion p_plus_q = p+q;
pentarnion p_times_q = p*q;

figure(1,"Name","Vector addition","NumberTitle","off");
plot2("i","j",p,q,p_plus_q);
title("$p+q$");
xlabel("$i$"); ylabel("$j$");
caption("$p+q=2+2j$");

figure(2,"Name","Ordered product","NumberTitle","off");
plot3("i","j","k",p,q,p_times_q);
title("$p\\star q$");
xlabel("$i$"); ylabel("$j$"); zlabel("$k$");
caption("$(p\\star q)_k=2\\sqrt{2}$");

tilefigures("studio",1,2);
% To arrange separate figure windows instead, enter:
% tilefigures("desktop",1,2);
% dock(1); undock(2);
