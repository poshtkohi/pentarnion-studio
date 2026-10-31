clc;
record room = quantity(20,"degC");
record warmed = unitadd(room,quantity(18,"delta_degF"));
display("20 Celsius plus an 18 Fahrenheit delta, Celsius: ",unitvalues(warmed));
display("Absolute Kelvin: ",unitvalues(unitconvert(warmed,"K_abs")));
display("0 and 100 Celsius, Fahrenheit: ",
        unitvalues(unitconvert(quantity([0,100],"degC"),"degF")));
record difference = unitsub(quantity(68,"degF"),quantity(10,"degC"));
display("Temperature difference in Fahrenheit degrees: ",unitvalues(difference));
display("0 and 30 dBm, watts: ",
        unitvalues(unitconvert(quantity([0,30],"dBm"),"W")));
display("Two volts, dBV: ",unitvalues(unitconvert(quantity(2,"V"),"dBV")));
% Absolute+absolute and direct arithmetic on log levels intentionally reject.
