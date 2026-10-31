real pi_value=acos(-1);
vector t=0:0.01:5.11;
vector signal=sin(2*pi_value*5*t)+0.3*sin(2*pi_value*17*t);
% Prime FFT lengths and explicit matrix axes are accepted without manual padding.
record spectrum=fft(signal,521);
display("Prime FFT bins: ",spectrum.samples);
record spectral=pwelch(signal,options(fs=100,segment=101,nfft=127,window="hamming"));
record local=spectrogram(signal,options(fs=100,segment=63,overlap=31,nfft=67));
matrix reconstructed=istft(local);
display("STFT reconstruction error: ",norm(reconstructed-signal'));
figure(1,"Name","Native spectral workflows","NumberTitle","off");
subplot(2,1,1);plot(spectral.frequency,spectral.power);title("Welch power density with prime FFT length");xlabel("Frequency / Hz");ylabel("Power / Hz");
subplot(2,1,2);imagesc(local.time,local.frequency,local.power);title("Native STFT power density");xlabel("Time / s");ylabel("Frequency / Hz");
