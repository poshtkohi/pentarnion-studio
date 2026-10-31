% An isometric sphere embedding using all five Pentarnion coefficients.
use_algebra("A_gamma",gamma=1);clc;clear;
native function pentarnion embedded_sphere(real u,real v){
    return .6*sin(u)*cos(v)+.6*sin(u)*sin(v)*i+cos(u)*j+.8*sin(u)*cos(v)*k+.8*sin(u)*sin(v)*ell;
}
record geometry=chartgeometry(embedded_sphere,[.8,.4],options(metric="euclidean"));
display("Five-coordinate point:",geometry.point);
display("Gaussian/scalar curvature:",geometry.gaussian_curvature,geometry.scalar_curvature);
display("Normal-space projector (rank three, not a single normal):",geometry.normal_projector);
assert(geometry.embedding_dimension==5&&abs(geometry.gaussian_curvature-1)<1e-12);
real normal_rank=0;
for(a=1:5){normal_rank=normal_rank+geometry.normal_projector(a,a);}
assert(abs(normal_rank-3)<1e-12);
record path=geodesic(embedded_sphere,[1.5707963267948966,.1],[0,1],options(metric="euclidean",t1=.2,time_step=.01,sample_step=.1));
display("Embedded geodesic points retain every coefficient:",path.positions);
