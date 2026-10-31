% Five state coefficients, ordered native reaction, real spatial diffusivity.
use_algebra("A_gamma",gamma=1);
clc;clear;close("all");
native function pentarnion initial_pattern(real x,real y,real z) {
    real wave_number=3.141592653589793;
    return 0.4 + 0.2*sin(wave_number*x)*i + 0.2*cos(wave_number*y)*j + 0.1*x*y*ell;
}
native function real spatial_diffusion(real x,real y,real z) {
    return 0.05 + 0.02*x*x;
}
native function pentarnion reaction(pentarnion p,real t,real x,real y,real z,real rate) {
    return -rate*p + 0.1*(i*p-p*i);
}
record plane_mesh=meshplane([0,1],[0,1],options(nx=21,ny=21));
record field=surfaceheat(plane_mesh,@initial_pattern,
    options(t1=0.2,time_step=0.002,sample_step=0.02,
      diffusion=@spatial_diffusion,reaction=@reaction,
      parameters=options(rate=0.5),threads=0));
display("Native cached reaction: ",field.reaction_cache_key);
display("All five coefficient means: ",field.means);
figure(1,"Name","Pentarnion surface reaction-diffusion","NumberTitle","off");
subplot(1,2,1);meshplot(plane_mesh,options(values=surfacecoeff(field,1,1)));title("Initial $i$ coefficient");
subplot(1,2,2);meshplot(plane_mesh,options(values=surfacecoeff(field,1,length(field.t))));title("Final $i$ coefficient");
