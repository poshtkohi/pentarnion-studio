Native signal workflows (extension19)

prime-fft-and-time-frequency.p exercises arbitrary-length/axis FFT, Welch,
centered short-time Fourier analysis and inverse weighted overlap-add. With the
default detrend="none" STFT, every original real sample is reconstructed.

butterworth-filtering-and-peak-properties.p designs a stable Butterworth lowpass,
uses forward/reverse odd-reflection filtering, and reports topographic prominence,
half-prominence widths, plateau metadata and parabolic refined positions.

Operations run in the lazy native science DLL/SO. Real coefficients/signals are
required by filtering; split real/imag records support complex FFT/spectral input.
Explicit options document sampling frequency and window/detrending assumptions.
