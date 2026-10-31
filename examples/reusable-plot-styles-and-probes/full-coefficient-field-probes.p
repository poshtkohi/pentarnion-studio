clc; clear; close("all");
% Each spatial node retains16 real coefficients; rows retain two stored times.
matrix retained=zeros(2,64);
for(node_index=0:3){for(component_index=0:15){
    integer column_index=node_index*16+component_index+1;
    retained(1,column_index)=component_index+(node_index-2*floor(node_index/2))+2*floor(node_index/2);
    retained(2,column_index)=retained(1,column_index)+6;
}}
record field=options(dimension=2,x=[0,1],y=[0,1],t=[0,2],components=16,
    value_layout="node_coefficients",algebra_id="sedenion_coordinate_field",values=retained);
record sampled=fieldprobe(field,[0.25,0.5]);
assert(abs(sampled.values(2,16)-22.25)<1e-12);
display("All16 coefficients at the selected spatial point:",sampled.values);
record slice=fieldslice(field,"y",0.5);
figure(1,"Name","Full-coefficient retained field slice","NumberTitle","off");
animatefield(slice,options(component=15));animationframe(2);
title("Coefficient15; every other coefficient remains selectable");
