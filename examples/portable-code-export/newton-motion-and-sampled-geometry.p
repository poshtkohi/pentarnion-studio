native function real spring_force(real t,real q) { return -q; }
record motion=mechanics(spring_force,1.0,0.0,options(method="verlet",t1=1.0,time_step=.001,sample_step=.02));
display("Expected cos(1), -sin(1):",motion.q(end),motion.v(end));
display("Retained-sample work/energy balance:",motion.energy_balance(end));
figure(1);plot(motion.t,motion.q);title("Velocity Verlet harmonic motion");
matrix theta=linspace(0,3.141592653589793,101);
matrix semicircle=[cos(theta).',sin(theta).'];
record curve=curvegeometry(semicircle);
display("Semicircle arc length and central curvature:",curve.length,curve.curvature(51));
figure(2);surf([-1,1],[-1,1],[1,0;0,1]);title("Sampled real surface");
figure(3);contour([-1,1],[-1,1],[1,0;0,1],[.25,.5,.75]);title("Explicit contour levels");
figure(4);isosurface([-1,1],[-1,1],[-1,1],[-1,1,-1,1,-1,1,-1,1],0.0);title("Real level-set plane");
