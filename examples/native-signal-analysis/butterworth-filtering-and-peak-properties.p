real pi_value=acos(-1);
vector t=0:0.01:5.11;
vector clean=sin(2*pi_value*3*t);
record noise=distrand("normal",1,numel(t),options(seed=42));
vector observed=clean+0.2*noise.values;
record design=butter(4,8,options(fs=100,type="low"));
matrix smoothed=filtfilt(design.b,design.a,observed);
record peaks=findpeaks(smoothed,options(fs=100,min_prominence=0.8,min_distance=20));
display("Peak positions / seconds: ",peaks.refined_positions);
display("Topographic prominences: ",peaks.prominences);
display("Half-prominence widths / seconds: ",peaks.widths);
figure(1,"Name","Filtering and refined peaks","NumberTitle","off");
plot(t,smoothed);xlabel("Time / s");ylabel("Amplitude");title("Butterworth forward/reverse filtering");
% For streaming data use filterstate(b,a,block), then pass its final_state as
% options(initial_state=...) on the following block. filter(b,a,data) returns y.
