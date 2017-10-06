%% INITIALIZE
clear all ; close all ; clc ;
d = 0.02 ; % Distance between the mics
N = 4 ; % Amount of mics
% Physical  constants
c = 343; % Speed of sound in  air
% Properties of the signal
freq = 1500; % The frequency of the source signal
freq2 = 2500; % The frequency of the noise signal
theta = pi ; % Angle of the source signal
theta2 = pi ; % Angle of the noise signal
% Our chosen properties
fs=24000; % Sample frequency
time_frame = 0.02; % Duration of 1 time frame
sptf=fs*time_frame ; % The amount of samples per time frame
noise_amp= 0.1; % The noise amplitude
L=2^( ceil(log2(sptf) ) ) ; % The power of 2 for the FFT
totaltime =1 ; % The total time of the signal
%% SIGNAL
tt = linspace(0,totaltime,fs*totaltime).';
tau1 = d/c*sin(theta);
tau2 = d/c*sin(theta2);
noise1 = sin(2*pi*2500*tt) + noise_amp* randn(fs*totaltime,1);
noise2 = sin(2*pi*2500*(tt-tau2))+noise_amp*randn(fs*totaltime,1) ;
signal1 = sin(2*pi*freq*tt)+noise1;
signal2 = sin(2*pi*freq*(tt-tau1))+noise2 ;
noise = [noise1,noise2] ;
signal = [signal1,signal2];
estimate_signal = zeros(L,1);
nr_frames = floor((length(signal1)-sptf)/(sptf/2));
rec_signal=zeros(size(signal(:,1)));
%% CALCULATING THE NOISE CORRELATION MATRIX
for J = 1:10+1
win = [sqrt(hanning(sptf)),sqrt(hanning(sptf))];
frame_noise=noise((sptf/2)*(J-1)+1:(sptf/2)*(J-1)+sptf,:).*(win);
fft_noise = fft(frame_noise,L);
fft_noise([1,L/2+1],:) = real(fft_noise([1,L/2+1],:));
part1 = fft_noise(1:L/2+1,:);
for I = 1:L/2+1
noise_cor(1,1,I,J)=abs(part1(I,1)).^2;
noise_cor(2,2,I,J)=abs(part1(I,2)).^2;
noise_cor(1,2,I,J) = part1(I,1)*conj(part1(I,2));
noise_cor(2,1,I,J) = part1(I,2)*conj(part1(I,1));
end
end
noise_cor = mean(noise_cor,4);
%% RECONSTRUCTING THE ORIGINAL SIGNAL
for J=1:nr_frames+1
win = [sqrt(hanning(sptf)),sqrt(hanning(sptf))];
frame_signal = signal((sptf/2)*(J-1)+1:(sptf/2)*(J-1)+sptf,:).*(win);
fft_signal = fft(frame_signal,L);
fft_signal([1,L/2+1],:) = real(fft_signal([1,L/2+1],:));
part1=fft_signal(1:L/2+1,:);
fft_signal = [part1;conj(flipud(part1(2:end-1,:)))];
fft_signal_mat(:,J) = fft_signal(:,1);
for I = 0:L/2
f_center = I*fs/L ;
zeta = -1i*2*pi*f_center*d*sin(theta)/c;
a_n = 1/N;
c_ = [1;exp(zeta)];
w = (noise_cor(:,:,I+1)\c_)/((c_'/noise_cor(:,:,I+1))*c_);
w = conj(w);
testsignal(:,I+1)=w ;
estimate_signal(I+1)=w.'*fft_signal(I+1,:).';
end
fft_estimate_signal_mat(:,J)=estimate_signal;
estimate_signal=estimate_signal(1:L/2+1);
estimate_signal([1,L/2+1],:) = real(estimate_signal([1,L/2+1],:));
estimate_signal = [estimate_signal;flipud(conj(estimate_signal(2:end-1)))];
td_estimate_signal = real(ifft(estimate_signal(1:L)));
rec_signal((sptf/2)*(J-1)+1:(sptf/2)*(J-1)+sptf)=rec_signal((sptf/2)*(J-1)+1:(sptf/2)*(J-1)+sptf)+td_estimate_signal(1:sptf).*(sqrt(hanning(sptf)));
end
%% PLOTS
% Time plot
figure;
subplot(2,1,2)
plot(linspace(0,totaltime,length(signal1)),signal1,'b');
hold on
plot(linspace(0,totaltime,length(rec_signal)),rec_signal,'--r');
plot(linspace(0,totaltime,length(signal1)),sin(2*pi*freq*tt),'--k')
legend('noisy in','beamformed','desired in' )
title('Incoming,beamformed and desired signals')
axis([0.200 0.204 -2 2]);
xlabel('Time(in seconds)') ;
ylabel('Amplitude');
%%
% Frequency plot
subplot(2,1,1)
plot(linspace(1,fs,L),10*log10(mean(abs(fft_signal_mat).^2,2)));
hold on
plot(linspace(1,fs,L),10*log10(mean(abs(fft_estimate_signal_mat).^2,2)),'r');
title('Spectrum plots of insignal and signal after beamforming in dB' )
legend('in signal' ,'beamformed signal' )
axis([0 4000 -10 60]);
xlabel('Frequency(in Hertz)');
ylabel('Magnitude(in dB)');
%%
% Polar plot
figure ;
f_center = linspace(0,fs-fs/(2*L),L).';
polarfreq = [1500,2500];
polarbin = zeros(1,length(polarfreq));
for I = 1 : length(polarfreq);
polarbin ( I ) = floor(polarfreq(I)/fs*L);
delta=((c/f_center(polarbin(I)))/d)^-1;
[w_dakje,polarhoek] = beam_resp(testsignal(:,polarbin(I)),L,delta);
subplot(ceil(length(polarfreq)/2),2,I)
polar90(polarhoek,abs(w_dakje));
title(['Frequency=', num2str(polarfreq(I))])
end