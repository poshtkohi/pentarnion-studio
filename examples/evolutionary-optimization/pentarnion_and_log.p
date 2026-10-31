use_algebra("A_gamma", gamma=1);
clc;
clear;

native function real energy(pentarnion p) {
    return norm2(p-1.0-i);
}
record pfit = evolve(energy,0.0,options(algorithm="jde",lower=-2.0,
    upper=2.0,population=80,seed=42,max_generations=180));
assert(pfit.objective<1e-4);
display("Pentarnion target 1+i:",pfit.x);

native function real positive_scale(real rate) {
    return (log(rate)-log(100.0))^2;
}
record ratefit = evolve(positive_scale,1.0,options(algorithm="jde",
    lower=1e-6,upper=1e6,log_coordinates=[1],population=32,
    seed=42,max_generations=120));
assert(abs(log(ratefit.x/100.0))<1e-4);
display("Positive rate target 100:",ratefit.x);
