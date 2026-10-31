Map Explorer is a docked Studio panel. Choose Real/Pentarnion map, enter the next
state expression in x/p, and fill the typed runtime parameter table. Run submits
the checked source to the existing worker/Command Window; the map body compiles
as a cached native callback. Parameter values, initials and sampling options reuse
the unchanged body. Generate Script previews it; Save Script writes a .p file.
Export Data saves the last completed Explorer result as iteration followed by all
original real coordinates, including all five Pentarnion coefficients.

Default real map: x_next=r*x*(1-x),r=3.7,x0=0.2,500 iterations,100 discarded,
stride1;401 retained samples. main.p is its runnable scripted equivalent.
Default Pentarnion map: p_next=a*p+b, a=0.8+0.1*i+0.05*j,
b=0.05*k+0.1*ell,p0=1+i+j+k+ell. Timeline and selected3D coefficient projection.
Use three distinct projection coordinates. This panel does not claim that a finite
trajectory is bounded/periodic/chaotic and does not implement PDEs.
